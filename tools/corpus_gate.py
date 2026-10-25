#!/usr/bin/env python3
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0
"""Identity-pinned maintainer corpus gate; see doc/corpus-regression.md."""

import argparse
import collections
import concurrent.futures
import difflib
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import subprocess
import sys


SEVERITIES = {"failed": 0, "best-effort": 1, "typed-degraded": 2, "typed": 3}
# Schema 2 pins routes and word conservation. Schema 3 adds per-topic rendered
# fingerprints; a schema 2 baseline is still checked, without them. Schema 4
# adds per-topic punctuation deficits; a schema 3 baseline is still checked,
# without them.
SCHEMA = 4
SCHEMAS = (2, 3, 4)


def summary_fields(line):
    fields = {}
    for field in line.split("\t")[1:]:
        key, value = field.split("=", 1)
        if key in fields or not value.isdecimal():
            raise ValueError("invalid summary field: " + field)
        fields[key] = int(value)
    return fields


def parse_coverage(output):
    lines = output.splitlines()
    expected = ["id", "level", "route", "family", "reason", "class",
                "signature", "severity", "degraded", "declined",
                "source_kind", "source_index", "source_start", "source_end"]
    if not lines or lines[0].split("\t") != expected:
        raise ValueError("unexpected coverage header")
    topics = {}
    summary = None
    severity = None
    for line in lines[1:]:
        # A book with no topics prints its summaries with no fields after
        # the label: the severity summary then has nothing to count.
        if line == "# summary" or line.startswith("# summary\t"):
            if summary is not None:
                raise ValueError("duplicate coverage summary")
            summary = summary_fields(line)
        elif line == "# severity" or line.startswith("# severity\t"):
            if severity is not None:
                raise ValueError("duplicate severity summary")
            severity = summary_fields(line)
        else:
            fields = line.split("\t")
            if len(fields) != len(expected):
                raise ValueError("invalid coverage row")
            row = dict(zip(expected, fields))
            topic = row["id"]
            rank = row["severity"]
            route = row["route"]
            if not topic or topic in topics or rank not in SEVERITIES:
                raise ValueError("duplicate/invalid coverage topic: " + topic)
            if route not in ("typed", "legacy") or (
                    (route == "typed") != rank.startswith("typed")):
                raise ValueError("inconsistent route/severity: " + topic)
            if (row["source_kind"] not in ("topic-index", "toc-only") or
                    not all(row[key].isdecimal() for key in
                            ("source_index", "source_start", "source_end")) or
                    int(row["source_end"]) < int(row["source_start"])):
                raise ValueError("invalid coverage source identity: " + topic)
            topics[topic] = {key: row[key] for key in
                             ("route", "severity", "family", "degraded",
                              "source_kind", "source_index", "source_start",
                              "source_end")}
    counts = {"typed": 0, "legacy": 0, "total": len(topics)}
    ranks = {}
    sources = set()
    for topic in topics.values():
        source = tuple(topic[key] for key in
                       ("source_kind", "source_index", "source_start", "source_end"))
        if source in sources:
            raise ValueError("duplicate coverage source identity")
        sources.add(source)
        counts[topic["route"]] += 1
        rank = topic["severity"]
        ranks[rank] = ranks.get(rank, 0) + 1
    if summary != counts or severity != ranks:
        raise ValueError("coverage summary does not match topic rows")
    return topics


def parse_conservation(output):
    lines = output.splitlines()
    header = ["topic", "route", "word", "drawn_opening", "drawn_inline",
              "emitted", "unaccounted", "evidence", "emitted_markdown",
              "unaccounted_markdown", "emitted_html", "unaccounted_html"]
    if not lines or lines[0].split("\t") != header:
        raise ValueError("unexpected conservation header")
    deficits = {}
    format_deficits = {"markdown": {}, "html": {}}
    unframed = {}
    topics = {}
    summary = None
    seen = set()
    forgiven = 0
    for line in lines[1:]:
        fields = line.split("\t")
        if fields[0] == "# summary":
            if summary is not None:
                raise ValueError("duplicate conservation summary")
            summary = summary_fields(line)
        elif fields[0] == "# unframed":
            if len(fields) != 4 or fields[1] in unframed:
                raise ValueError("invalid unframed row")
            count, records = map(int, fields[2:])
            if count <= 0 or records < count:
                raise ValueError("invalid unframed counts")
            unframed[fields[1]] = {"unframed": count, "records": records}
        elif fields[0] == "# topic":
            if (len(fields) != 7 or not fields[1] or fields[1] in topics or
                    fields[2] not in ("topic-index", "toc-only") or
                    not all(value.isdecimal() for value in fields[3:6]) or
                    int(fields[5]) < int(fields[4]) or
                    fields[6] not in ("typed", "legacy", "best-effort", "none")):
                raise ValueError("invalid conservation topic identity")
            topics[fields[1]] = {
                "source_kind": fields[2], "source_index": fields[3],
                "source_start": fields[4], "source_end": fields[5],
                "route": fields[6]}
        else:
            if len(fields) != len(header) or fields[1] not in (
                    "typed", "legacy", "best-effort", "none"):
                raise ValueError("invalid conservation row")
            topic, _, word = fields[:3]
            counts = list(map(int, fields[3:7]))
            if not topic or not word or min(counts) < 0 or (topic, word) in seen:
                raise ValueError("duplicate/invalid conservation word")
            opening, inside, emitted, unaccounted = counts
            shortfall = opening + inside - emitted
            if shortfall <= 0 or unaccounted != max(0, inside - emitted):
                raise ValueError("inconsistent conservation deficit counts")
            forgiven += min(shortfall, opening)
            seen.add((topic, word))
            if counts[3]:
                deficits.setdefault(topic, {})[word] = counts[3]
            for name, emitted_at, unaccounted_at in (
                    ("markdown", 8, 9), ("html", 10, 11)):
                emitted_format = int(fields[emitted_at])
                unaccounted_format = int(fields[unaccounted_at])
                if (emitted_format < 0 or unaccounted_format !=
                        max(0, inside - emitted_format)):
                    raise ValueError("inconsistent " + name +
                                     " conservation counts")
                if unaccounted_format:
                    format_deficits[name].setdefault(topic, {})[word] = \
                        unaccounted_format
    required = {"checked", "dropping", "unaccounted", "forgiven",
                "unframed-topics", "markdown-dropping",
                "markdown-unaccounted", "html-dropping",
                "html-unaccounted"}
    if summary is None or set(summary) != required:
        raise ValueError("missing/invalid conservation summary")
    sources = {(topic["source_kind"], topic["source_index"],
                topic["source_start"], topic["source_end"])
               for topic in topics.values()}
    if len(sources) != len(topics):
        raise ValueError("duplicate conservation source identity")
    if (summary["checked"] != len(topics) or
            summary["dropping"] != len(deficits) or
            summary["unaccounted"] != sum(sum(words.values()) for words in deficits.values()) or
            summary["unframed-topics"] != len(unframed) or
            summary["markdown-dropping"] != len(format_deficits["markdown"]) or
            summary["markdown-unaccounted"] != sum(
                sum(words.values()) for words in format_deficits["markdown"].values()) or
            summary["html-dropping"] != len(format_deficits["html"]) or
            summary["html-unaccounted"] != sum(
                sum(words.values()) for words in format_deficits["html"].values()) or
            summary["forgiven"] != forgiven):
        raise ValueError("conservation summary does not match rows")
    return {"summary": summary, "topics": topics, "deficits": deficits,
            "format_deficits": format_deficits, "unframed": unframed}


# ---------------------------------------------------------------------------
# Rendered fingerprints (schema 3).
#
# `bootrace --conservation --rendered` adds a `# rendered` row per topic with
# the serializer-trace projection of its Markdown (visible text only, so an
# escaped `\<name\>` is text, never a tag) and its HTML fragment. Characters
# are counted from both; style, structure, anchors and links come from the
# HTML, whose markup is unambiguous.

TSV_ESCAPES = {"\\": "\\", "t": "\t", "r": "\r", "n": "\n"}
TSV_ESCAPE = re.compile(r"\\(.?)", re.DOTALL)


def tsv_unescape(value):
    def replace(match):
        if match.group(1) not in TSV_ESCAPES:
            raise ValueError("invalid TSV escape")
        return TSV_ESCAPES[match.group(1)]
    return TSV_ESCAPE.sub(replace, value)


# Elements that delimit blocks: they end a style run and a word.
BLOCK_TAGS = frozenset((
    "address", "aside", "blockquote", "caption", "dd", "details", "div", "dl",
    "dt", "figcaption", "figure", "h1", "h2", "h3", "h4", "h5", "h6", "hr",
    "li", "nav", "ol", "p", "pre", "section", "summary", "table", "tbody",
    "td", "tfoot", "th", "thead", "tr", "ul"))
# `samp`, `kbd`, `cite`, `var` and `dfn` are the phrase elements hosted
# BookServer serves for the `E`, `P`, `C`, `V` and `Q` style codes; each is a
# kind of its own.
STYLE_TAGS = frozenset(("b", "cite", "code", "dfn", "em", "i", "kbd", "s",
                        "samp", "strong", "sub", "sup", "u", "var"))
VOID_TAGS = frozenset(("br", "col", "hr", "img", "input", "link", "meta", "wbr"))
# Inline elements that still separate the text on either side of them.
BREAK_TAGS = frozenset(("br", "img"))
INLINE_TAGS = frozenset(("a", "span"))
# Attributes that are part of a block's identity in the structure fingerprint.
STRUCTURE_ATTRIBUTES = ("type", "value", "data-geist-depth", "data-geist-empty",
                        "start")


def short_hash(values):
    encoded = json.dumps(values, ensure_ascii=False, separators=(",", ":"))
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()[:16]


def char_multiset(text):
    """Letters and digits only, case-sensitive, as `c:n` pairs."""
    counts = collections.Counter(text)
    return " ".join(f"{char}:{count}" for char, count in sorted(counts.items())
                    if char.isalnum())


def parse_multiset(value):
    counts = {}
    for item in value.split():
        char, separator, count = item.rpartition(":")
        if not separator or len(char) != 1 or not count.isdecimal():
            raise ValueError("invalid character multiset: " + item)
        counts[char] = int(count)
    return counts


def kind_counts(values):
    return " ".join(f"{kind}:{count}" for kind, count in
                    sorted(collections.Counter(values).items()))


class FingerprintParser(HTMLParser):
    """Style runs, block structure, ids and links of one Geist HTML fragment.

    A style run is a maximal stretch of text in one block under the same set
    of open style elements; its kind is that set, sorted and joined by `+`.
    A mid-word boundary is two adjacent letters/digits in one block whose
    style sets differ: a run that starts or ends inside a word.
    """

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack = []
        self.key = ""
        self.segment = []
        self.text = []
        self.style = []
        self.structure = []
        self.anchors = []
        self.links = []
        self.midword = []

    def restyle(self):
        self.key = "+".join(sorted({kind for _, kind in self.stack if kind}))

    def flush(self):
        runs = []
        for key, text in self.segment:
            if runs and runs[-1][0] == key:
                runs[-1][1] += text
            else:
                runs.append([key, text])
        self.segment = []
        for index, (key, text) in enumerate(runs):
            if key:
                self.style.append(key + ":" + text)
            if index:
                before_key, before = runs[index - 1]
                if before[-1].isalnum() and text[0].isalnum():
                    self.midword.append(f"[{before_key}]{before[-12:]}|"
                                        f"[{key}]{text[:12]}")

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if "id" in attributes:
            self.anchors.append(attributes["id"] or "")
        if tag == "a" and "href" in attributes:
            self.links.append(attributes["href"] or "")
        if tag == "img":
            self.links.append("img:" + (attributes.get("src") or ""))
            self.text.append(attributes.get("alt") or "")
        if tag in BLOCK_TAGS or tag in BREAK_TAGS:
            self.flush()
        if tag in BLOCK_TAGS:
            token = tag
            classes = (attributes.get("class") or "").split()
            if classes:
                token += "." + classes[0].removeprefix("geist-")
            for name in STRUCTURE_ATTRIBUTES:
                if name in attributes:
                    token += f"[{name}={attributes[name]}]"
            depth = sum(1 for open_tag, _ in self.stack if open_tag in BLOCK_TAGS)
            self.structure.append(f"{depth}:{token}")
        elif tag not in STYLE_TAGS and tag not in VOID_TAGS and \
                tag not in INLINE_TAGS:
            self.structure.append("?" + tag)
        if tag in VOID_TAGS:
            return
        kind = None
        if tag in STYLE_TAGS:
            kind = tag
            if tag == "code" and "geist-opaque-inline" in (
                    attributes.get("class") or "").split():
                kind = "opaque"
        self.stack.append((tag, kind))
        self.restyle()

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VOID_TAGS:
            self.handle_endtag(tag)

    def handle_endtag(self, tag):
        if tag in VOID_TAGS:
            return
        if not self.stack or self.stack[-1][0] != tag:
            raise ValueError("unbalanced HTML end tag: " + tag)
        if tag in BLOCK_TAGS:
            self.flush()
        self.stack.pop()
        self.restyle()

    def handle_data(self, data):
        if data:
            self.text.append(data)
            self.segment.append((self.key, data))

    def finish(self):
        self.close()
        if self.stack:
            raise ValueError("unclosed HTML element: " + self.stack[-1][0])
        self.flush()


EMPTY_HASH = short_hash([])


def fingerprint(markdown_text, html):
    """Returns (summary for the baseline, detail for review diffs)."""
    parser = FingerprintParser()
    parser.feed(html)
    parser.finish()
    detail = {"style": parser.style, "structure": parser.structure,
              "anchors": parser.anchors, "links": parser.links,
              "midword": parser.midword}
    chars = char_multiset("".join(parser.text))
    markdown_chars = char_multiset(markdown_text)
    summary = {"chars": chars, "markdown_chars": markdown_chars,
               "style": short_hash(parser.style),
               "style_runs": kind_counts(run.split(":", 1)[0] for run in parser.style),
               "structure": short_hash(parser.structure),
               "blocks": kind_counts(token.split(":", 1)[1].split("[", 1)[0]
                                     for token in parser.structure),
               "anchors": short_hash(parser.anchors),
               "anchor_count": len(parser.anchors),
               "links": short_hash(parser.links),
               "link_count": len(parser.links),
               "midword": len(parser.midword)}
    # Compact: Markdown characters are usually the HTML characters, so they
    # are kept only when they differ (even when empty); other empty values
    # are left out. `expand_fingerprint` restores them.
    compact = {key: value for key, value in summary.items()
               if key == "markdown_chars" or value not in ("", 0, EMPTY_HASH)}
    if markdown_chars == chars:
        del compact["markdown_chars"]
    return compact, detail


def expand_fingerprint(summary):
    full = {"chars": "", "style": EMPTY_HASH, "style_runs": "",
            "structure": EMPTY_HASH, "blocks": "", "anchors": EMPTY_HASH,
            "anchor_count": 0, "links": EMPTY_HASH, "link_count": 0,
            "midword": 0}
    unknown = set(summary) - set(full) - {"markdown_chars"}
    if unknown:
        raise ValueError("unknown fingerprint fields: " + repr(sorted(unknown)))
    full.update(summary)
    full.setdefault("markdown_chars", full["chars"])
    return full


def split_rendered(output, punctuation=None):
    """Separates `# rendered` rows from the conservation report proper.

    With a `punctuation` dict, `# punctuation` rows are separated too and
    collected into it as {topic: {unit: unaccounted}}.

    Splits on newline only: the escaped fields may hold other characters that
    `str.splitlines` would treat as line breaks."""
    report, rendered = [], {}
    for line in output.split("\n"):
        if punctuation is not None and line.startswith("# punctuation\t"):
            parse_punctuation_row(line, punctuation)
            continue
        if not line.startswith("# rendered\t"):
            report.append(line)
            continue
        fields = line.split("\t")
        if len(fields) != 4 or not fields[1] or fields[1] in rendered:
            raise ValueError("invalid rendered row")
        rendered[fields[1]] = (tsv_unescape(fields[2]), tsv_unescape(fields[3]))
    return "\n".join(report), rendered


# ---------------------------------------------------------------------------
# Punctuation deficits (schema 4).
#
# Word conservation counts letters and digits, so it cannot see a lost comma
# or a glossary term's delimiter. `bootrace --conservation --rendered
# --punctuation` adds a `# punctuation` row per topic and punctuation unit
# (a word with the mark glued to its end, `cc,`) that the HTML does not
# carry: topic, unit, drawn, emitted, unaccounted.

def parse_punctuation_row(line, punctuation):
    fields = line.split("\t")
    if len(fields) != 6 or not fields[1] or not fields[2] or not all(
            value.isdecimal() for value in fields[3:6]):
        raise ValueError("invalid punctuation row")
    topic, unit = tsv_unescape(fields[1]), tsv_unescape(fields[2])
    drawn, emitted, unaccounted = map(int, fields[3:6])
    if unaccounted <= 0 or drawn != emitted + unaccounted:
        raise ValueError("inconsistent punctuation counts")
    units = punctuation.setdefault(topic, {})
    if unit in units:
        raise ValueError("duplicate punctuation unit")
    units[unit] = unaccounted


def compare_punctuation(name, baseline, current):
    """Returns (failures, improvements): an increase in any topic's
    unaccounted count for a unit is a regression, as for words."""
    failures, improvements = [], []
    for topic, units in sorted(current.items()):
        for unit, count in sorted(units.items()):
            previous = baseline.get(topic, {}).get(unit, 0)
            if count > previous:
                failures.append(f"{name}: {topic}: unaccounted punctuation "
                                f"{unit!r} increased {previous} -> {count}")
    for topic, units in sorted(baseline.items()):
        for unit, count in sorted(units.items()):
            after = current.get(topic, {}).get(unit, 0)
            if after < count:
                improvements.append(f"{name}: {topic}: unaccounted punctuation "
                                    f"{unit!r} decreased {count} -> {after}")
    return failures, improvements


def fingerprint_topics(rendered):
    summaries, details = {}, {}
    for topic, (markdown_text, html) in rendered.items():
        try:
            summaries[topic], details[topic] = fingerprint(markdown_text, html)
        except ValueError as error:
            raise ValueError(topic + ": " + str(error)) from None
    return summaries, details


def sequence_diff(before, after, limit=3):
    """A short, readable account of how one ordered list changed."""
    changes = []
    matcher = difflib.SequenceMatcher(None, before, after, autojunk=False)
    for operation, low, high, new_low, new_high in matcher.get_opcodes():
        if operation == "equal":
            continue
        removed = " ".join(repr(value) for value in before[low:high][:2])
        added = " ".join(repr(value) for value in after[new_low:new_high][:2])
        more = max(high - low, new_high - new_low) > 2
        changes.append(("-" + removed if removed else "") +
                       (" " if removed and added else "") +
                       ("+" + added if added else "") + (" ..." if more else ""))
        if len(changes) == limit:
            break
    return "; ".join(changes)


def count_delta(before, after):
    old, new = parse_counts(before), parse_counts(after)
    return ", ".join(f"{kind} {old.get(kind, 0)}->{new.get(kind, 0)}"
                     for kind in sorted(old.keys() | new.keys())
                     if old.get(kind, 0) != new.get(kind, 0))


def parse_counts(value):
    counts = {}
    for item in value.split():
        kind, _, count = item.rpartition(":")
        counts[kind] = int(count)
    return counts


def char_delta(before, after):
    old, new = parse_multiset(before), parse_multiset(after)
    lost = {char: old[char] - new.get(char, 0) for char in old
            if new.get(char, 0) < old[char]}
    gained = {char: new[char] - old.get(char, 0) for char in new
              if old.get(char, 0) < new[char]}
    return lost, gained


def describe_chars(counts):
    return " ".join(f"{char!r}x{count}" for char, count in sorted(counts.items()))


FINGERPRINT_CATEGORIES = ("characters", "mid-word", "style", "structure",
                          "anchors", "links")


def compare_fingerprints(name, baseline, current, baseline_details=None,
                         current_details=None, strict=False, tally=None):
    """Returns (failures, improvements, reviews) for one book's fingerprints.

    Character loss and any new mid-word style boundary are regressions. Style,
    structure, anchor and link changes, and character gains, need review;
    `strict` makes them failures too. `tally`, if given, collects the topics
    that changed per category."""
    failures, improvements, reviews = [], [], []
    tally = {} if tally is None else tally
    for topic in sorted(baseline.keys() ^ current.keys()):
        failures.append(f"{name}: {topic}: rendered fingerprint "
                        f"{'missing' if topic in baseline else 'unexpected'}")
    for topic in sorted(baseline.keys() & current.keys()):
        old = expand_fingerprint(baseline[topic])
        new = expand_fingerprint(current[topic])
        if old == new:
            continue
        old_detail = (baseline_details or {}).get(topic)
        new_detail = (current_details or {}).get(topic)
        def detail(field):
            if old_detail is None or new_detail is None:
                return ""
            return " {" + sequence_diff(old_detail[field], new_detail[field]) + "}"
        fatal, notes = [], []
        for label, field in (("HTML", "chars"), ("Markdown", "markdown_chars")):
            lost, gained = char_delta(old[field], new[field])
            if lost:
                fatal.append(f"characters: {label} lost {describe_chars(lost)}")
            if gained:
                notes.append(f"characters: {label} gained {describe_chars(gained)}")
        if new["midword"] > old["midword"]:
            fatal.append(f"mid-word: style boundaries inside words "
                         f"{old['midword']} -> {new['midword']}" + detail("midword"))
        elif new["midword"] < old["midword"]:
            improvements.append(f"{name}: {topic}: mid-word style boundaries "
                                f"{old['midword']} -> {new['midword']}")
        if old["style"] != new["style"]:
            notes.append("style: " + (count_delta(old["style_runs"], new["style_runs"])
                                      or "same run kinds, text or order changed")
                         + detail("style"))
        if old["structure"] != new["structure"]:
            notes.append("structure: " + (count_delta(old["blocks"], new["blocks"])
                                          or "same blocks, order or nesting changed")
                         + detail("structure"))
        for category, count in (("anchors", "anchor_count"), ("links", "link_count")):
            if old[category] != new[category]:
                notes.append(f"{category}: " + (
                    f"{old[count]} -> {new[count]}" if old[count] != new[count]
                    else "same count, ids or order changed") + detail(category))
        for message in fatal + notes:
            tally.setdefault(message.split(":", 1)[0], set()).add(topic)
        for message in fatal:
            failures.append(f"{name}: {topic}: {message}")
        for message in notes:
            (failures if strict else reviews).append(f"{name}: {topic}: {message}")
    return failures, improvements, reviews


def identity(path):
    digest = hashlib.sha256()
    with path.open("rb") as book:
        for chunk in iter(lambda: book.read(1024 * 1024), b""):
            digest.update(chunk)
    return {"size": path.stat().st_size, "sha256": digest.hexdigest()}


def select_books(corpus, explicit):
    if corpus is not None:
        books = [path for path in corpus.iterdir()
                 if path.is_file() and path.suffix.lower() == ".boo"]
    else:
        books = explicit
        for path in books:
            if not path.is_file() or path.suffix.lower() != ".boo":
                raise ValueError("explicit book is not a BOO file: " + str(path))
    selected = {}
    for path in books:
        if path.name in selected:
            raise ValueError("duplicate corpus filename: " + path.name)
        selected[path.name] = path
    if not selected:
        raise ValueError("corpus contains no BOO files")
    return selected


def run_tool(executable, book, *args):
    result = subprocess.run([str(executable), str(book), *args],
                            capture_output=True, text=True, encoding="utf-8",
                            errors="strict", timeout=1800, check=True)
    return result.stdout


def measure(book, bootrace, booinfo, raw_dir=None, fingerprints=True):
    """Measures one book. With `fingerprints`, the result also carries
    `fingerprints` (baseline summaries) and `details` (review-only lists that
    are written to the raw directory, never to the baseline)."""
    before = identity(book)
    coverage = run_tool(bootrace, book, "--coverage")
    punctuation = {}
    if fingerprints:
        conservation, rendered = split_rendered(
            run_tool(bootrace, book, "--conservation", "--rendered",
                     "--punctuation"), punctuation)
    else:
        conservation = run_tool(bootrace, book, "--conservation")
    metadata = run_tool(booinfo, book)
    edition = {}
    for line in metadata.splitlines():
        label, separator, value = line.strip().partition(": ")
        if separator and label in ("Timestamp", "Document number", "Title", "Version"):
            edition[label] = value
    if set(edition) != {"Timestamp", "Document number", "Title", "Version"}:
        raise ValueError(book.name + ": missing book edition metadata")
    if identity(book) != before:
        raise ValueError(book.name + ": file changed during measurement")
    if raw_dir is not None:
        for suffix, output in (("coverage", coverage), ("conservation", conservation),
                               ("info", metadata)):
            (raw_dir / (book.name + "." + suffix + ".tsv")).write_text(output, encoding="utf-8")
    topics = parse_coverage(coverage)
    conserved = parse_conservation(conservation)
    if set(conserved["topics"]) != set(topics):
        raise ValueError(book.name + ": conservation topic set differs from coverage")
    for topic, source in conserved["topics"].items():
        for key in ("source_kind", "source_index", "source_start", "source_end"):
            if source[key] != topics[topic][key]:
                raise ValueError(book.name +
                                 ": conservation source identity differs for " + topic)
    if (conserved["deficits"].keys() | conserved["unframed"].keys()) - topics.keys():
        raise ValueError(book.name + ": conservation names an unknown coverage topic")
    result = {"identity": before, "edition": edition, "topics": topics,
              "conservation": conserved}
    if fingerprints:
        if set(rendered) != set(topics):
            raise ValueError(book.name + ": rendered topic set differs from coverage")
        if punctuation.keys() - topics.keys():
            raise ValueError(book.name + ": punctuation names an unknown coverage topic")
        result["punctuation"] = punctuation
        try:
            result["fingerprints"], result["details"] = fingerprint_topics(rendered)
        except ValueError as error:
            raise ValueError(book.name + ": " + str(error)) from None
        if raw_dir is not None:
            with (raw_dir / (book.name + ".fingerprint.json")).open(
                    "x", encoding="utf-8") as output:
                json.dump(result["details"], output, indent=1, sort_keys=True,
                          ensure_ascii=False)
                output.write("\n")
    return result


def load_details(directory, name):
    """Review detail a previous `--raw-dir` kept for one book, if any."""
    if directory is None:
        return None
    path = directory / (name + ".fingerprint.json")
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def compare_book(name, baseline, current):
    failures, improvements = [], []
    def fail(message):
        failures.append(name + ": " + message)
    if baseline["identity"] != current["identity"] or baseline["edition"] != current["edition"]:
        fail("corpus identity/edition mismatch")
        return failures, improvements
    old, new = baseline["topics"], current["topics"]
    for topic in sorted(old.keys() - new.keys()):
        fail(topic + ": missing coverage topic")
    for topic in sorted(new.keys() - old.keys()):
        fail(topic + ": unexpected coverage topic; review scope change")
    for topic in sorted(old.keys() & new.keys()):
        old_source = {key: old[topic][key] for key in
                      ("source_kind", "source_index", "source_start", "source_end")}
        new_source = {key: new[topic][key] for key in old_source}
        if old_source != new_source:
            fail(topic + ": source identity changed")
        before, after = old[topic]["severity"], new[topic]["severity"]
        if SEVERITIES[after] < SEVERITIES[before]:
            fail(topic + ": severity regressed " + before + " -> " + after)
        elif SEVERITIES[after] > SEVERITIES[before]:
            improvements.append(name + ": " + topic + ": " + before + " -> " + after)
        else:
            if old[topic]["family"] != new[topic]["family"]:
                fail(topic + ": lowering family changed; review required")
            if old[topic]["degraded"] != new[topic]["degraded"]:
                fail(topic + ": degradation details changed; review required")
    old, new = baseline["conservation"], current["conservation"]
    if new["summary"]["checked"] < old["summary"]["checked"]:
        fail("conservation checked-topic count decreased")
    for topic, counts in new["unframed"].items():
        previous = old["unframed"].get(topic, {"unframed": 0, "records": counts["records"]})
        if counts["unframed"] > previous["unframed"]:
            fail(topic + ": unframed record count increased")
    # No aggregate allowance: fixing one word never permits losing another.
    for topic, words in new["deficits"].items():
        for word, count in words.items():
            previous = old["deficits"].get(topic, {}).get(word, 0)
            if count > previous:
                fail(f"{topic}: unaccounted word {word!r} increased {previous} -> {count}")
    for topic, words in old["deficits"].items():
        for word, count in words.items():
            after = new["deficits"].get(topic, {}).get(word, 0)
            if after < count:
                improvements.append(f"{name}: {topic}: unaccounted word {word!r} decreased {count} -> {after}")
    for format_name in ("markdown", "html"):
        label = format_name.capitalize()
        old_format = old["format_deficits"][format_name]
        new_format = new["format_deficits"][format_name]
        for topic, words in new_format.items():
            for word, count in words.items():
                previous = old_format.get(topic, {}).get(word, 0)
                if count > previous:
                    fail(f"{topic}: {label} unaccounted word {word!r} "
                         f"increased {previous} -> {count}")
        for topic, words in old_format.items():
            for word, count in words.items():
                after = new_format.get(topic, {}).get(word, 0)
                if after < count:
                    improvements.append(
                        f"{name}: {topic}: {label} unaccounted word {word!r} "
                        f"decreased {count} -> {after}")
    return failures, improvements


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--corpus", type=Path,
                        help="flat directory containing exactly the baseline books")
    source.add_argument("--book", action="append", type=Path, dest="books",
                        help="explicit BOO path; repeat for a separately pinned set")
    parser.add_argument("--bootrace", default=Path("build/bootrace"), type=Path)
    parser.add_argument("--booinfo", default=Path("build/booinfo"), type=Path)
    parser.add_argument("--baseline", default=Path(__file__).with_name("corpus-baseline.json"), type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--capture", type=Path, help="create NEW review snapshot; never overwrite")
    parser.add_argument("--revision", help="source revision for an explicit capture")
    parser.add_argument("--raw-dir", type=Path, help="NEW scratch directory for raw tool outputs")
    parser.add_argument("--baseline-raw", type=Path,
                        help="the --raw-dir kept when the baseline was captured; "
                             "adds exact per-topic diffs to fingerprint reports")
    parser.add_argument("--fail-on-review", action="store_true",
                        help="treat style, structure, anchor, link and character-gain "
                             "fingerprint changes as regressions")
    args = parser.parse_args(argv)
    if args.jobs < 1 or (args.capture is not None and not args.revision):
        parser.error("positive --jobs required; --capture requires --revision")
    books = select_books(args.corpus, args.books)
    baseline = None
    if args.capture:
        if args.capture.exists():
            raise ValueError("capture output already exists; refusing overwrite")
    else:
        baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
        if baseline.get("schema") not in SCHEMAS or not baseline.get("books"):
            raise ValueError("unsupported/empty baseline")
        if baseline["schema"] >= 3 and not all(
                isinstance(book.get("fingerprints"), dict)
                for book in baseline["books"].values()):
            raise ValueError("schema 3 baseline without fingerprints")
        if baseline["schema"] >= 4 and not all(
                isinstance(book.get("punctuation"), dict)
                for book in baseline["books"].values()):
            raise ValueError("schema 4 baseline without punctuation deficits")
        if set(books) != set(baseline["books"]):
            raise ValueError("corpus book set mismatch; missing=" +
                             repr(sorted(set(baseline["books"]) - set(books))) +
                             "; extra=" + repr(sorted(set(books) - set(baseline["books"]))))
        # Identity failures are detected before invoking any decoder.
        for name, book in books.items():
            if identity(book) != baseline["books"][name]["identity"]:
                raise ValueError(name + ": corpus identity mismatch")
    if args.raw_dir:
        args.raw_dir.mkdir(parents=True, exist_ok=False)
    # A schema 2 baseline has nothing to compare fingerprints against, so it
    # is checked exactly as before, without rendering them.
    fingerprints = baseline is None or baseline["schema"] >= 3
    if not fingerprints:
        print("NOTE baseline schema 2 carries no rendered fingerprints; characters, "
              "style, structure, anchors and mid-word spans are not compared. "
              "Capture a schema 4 baseline to enable them.", file=sys.stderr)
    elif baseline is not None and baseline["schema"] < 4:
        print("NOTE baseline schema 3 carries no punctuation deficits; punctuation "
              "is measured but not compared. Capture a schema 4 baseline to "
              "enable it.", file=sys.stderr)
    measured = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        pending = {pool.submit(measure, book, args.bootrace.resolve(),
                               args.booinfo.resolve(), args.raw_dir,
                               fingerprints): name
                   for name, book in books.items()}
        for future in concurrent.futures.as_completed(pending):
            name = pending[future]
            measured[name] = future.result()
            print("Measured " + name, file=sys.stderr, flush=True)
    details = {name: book.pop("details", None) for name, book in measured.items()}
    # A book the decoder finds no topics in is measured like any other, but
    # it says nothing about rendering, so say so rather than let it pass as
    # an unchanged book.
    for name in sorted(measured):
        if not measured[name]["topics"]:
            print("ZERO TOPICS " + name + ": the decoder found no topics in this book")
    snapshot = {"schema": SCHEMA, "revision": args.revision,
                "scope": "stable book topic catalog: all indexed topics plus TOC-only entries",
                "books": measured}
    if args.capture:
        with args.capture.open("x", encoding="utf-8") as output:
            json.dump(snapshot, output, indent=2, sort_keys=True, ensure_ascii=True)
            output.write("\n")
        print("Created review snapshot " + str(args.capture))
        return 0
    failures, improvements, reviews, summaries = [], [], [], []
    for name in sorted(measured):
        failed, improved = compare_book(name, baseline["books"][name], measured[name])
        failures.extend(failed)
        improvements.extend(improved)
        if not fingerprints or failed and "identity/edition" in failed[0]:
            continue
        if baseline["schema"] >= 4:
            failed, improved = compare_punctuation(
                name, baseline["books"][name]["punctuation"],
                measured[name]["punctuation"])
            failures.extend(failed)
            improvements.extend(improved)
        tally = {}
        failed, improved, review = compare_fingerprints(
            name, baseline["books"][name]["fingerprints"],
            measured[name]["fingerprints"], load_details(args.baseline_raw, name),
            details[name], args.fail_on_review, tally)
        failures.extend(failed)
        improvements.extend(improved)
        reviews.extend(review)
        if tally:
            summaries.append(name + ": " + ", ".join(
                f"{category} {len(tally[category])}"
                for category in FINGERPRINT_CATEGORIES if category in tally))
    for message in improvements:
        print("IMPROVEMENT " + message)
    for message in reviews:
        print("REVIEW " + message)
    for message in summaries:
        print("CHANGED TOPICS " + message)
    # Keep whole lines when both streams share one file.
    sys.stdout.flush()
    for message in failures:
        print("REGRESSION " + message, file=sys.stderr)
    print(f"Checked {len(measured)} identity-pinned books; {len(failures)} regressions; "
          f"{len(improvements)} improvements; {len(reviews)} fingerprint changes to "
          f"review. Baseline was not modified.")
    return bool(failures)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("corpus_gate: " + str(error), file=sys.stderr)
        sys.exit(2)
