#!/usr/bin/env python3
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0
"""Identity-pinned maintainer corpus gate; see doc/corpus-regression.md."""

import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import subprocess
import sys


SEVERITIES = {"failed": 0, "best-effort": 1, "typed-degraded": 2, "typed": 3}


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
        if line.startswith("# summary\t"):
            if summary is not None:
                raise ValueError("duplicate coverage summary")
            summary = summary_fields(line)
        elif line.startswith("# severity\t"):
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


def measure(book, bootrace, booinfo, raw_dir=None):
    before = identity(book)
    coverage = run_tool(bootrace, book, "--coverage")
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
    return {"identity": before, "edition": edition, "topics": topics,
            "conservation": conserved}


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
        if baseline.get("schema") != 2 or not baseline.get("books"):
            raise ValueError("unsupported/empty baseline")
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
    measured = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        pending = {pool.submit(measure, book, args.bootrace.resolve(),
                               args.booinfo.resolve(), args.raw_dir): name
                   for name, book in books.items()}
        for future in concurrent.futures.as_completed(pending):
            name = pending[future]
            measured[name] = future.result()
            print("Measured " + name, file=sys.stderr, flush=True)
    snapshot = {"schema": 2, "revision": args.revision,
                "scope": "stable book topic catalog: all indexed topics plus TOC-only entries",
                "books": measured}
    if args.capture:
        with args.capture.open("x", encoding="utf-8") as output:
            json.dump(snapshot, output, indent=2, sort_keys=True, ensure_ascii=True)
            output.write("\n")
        print("Created review snapshot " + str(args.capture))
        return 0
    failures, improvements = [], []
    for name in sorted(measured):
        failed, improved = compare_book(name, baseline["books"][name], measured[name])
        failures.extend(failed)
        improvements.extend(improved)
    for message in improvements:
        print("IMPROVEMENT " + message)
    for message in failures:
        print("REGRESSION " + message, file=sys.stderr)
    print(f"Checked {len(measured)} identity-pinned books; {len(failures)} regressions; "
          f"{len(improvements)} improvements. Baseline was not modified.")
    return bool(failures)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("corpus_gate: " + str(error), file=sys.stderr)
        sys.exit(2)
