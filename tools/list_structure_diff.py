#!/usr/bin/env python3
"""Compare list and block structure between two boo2git corpus export trees.

The project's standing acceptance bar for a rendering change is that no word
may be gained or lost, and that every structural change is enumerated and
justified.  This tool turns that bar into a repeatable check.

An export tree is a directory containing one directory per book, each holding
the topic ``.md`` files and (optionally) ``render-diagnostics.tsv``.  A single
book directory works too: topics are located by recursive glob and the
diagnostics table is looked up next to each topic.

For every topic present in both trees the tool reports

  * words gained / lost, as a multiset difference over word tokens with
    Markdown syntax, link targets, HTML tags, the ``<!-- geist-render: -->``
    diagnostic comment and backslash escapes removed.  This is the primary
    gate and should normally be zero in both directions.
  * unordered list items (``- `` / ``* ``) before and after
  * ordered list items (``1. `` style) before and after
  * escaped-ordinal paragraphs (``1\\. text``) before and after -- a numbered
    procedure that lost its list structure and is now plain prose
  * bold-ordinal paragraphs (``**1\\.** text``) before and after -- the other
    shape a numbered procedure degrades into
  * fenced blocks before and after, so a topic cannot silently fall into or
    out of verbatim rendering
  * anchors (``<a id="...">``) gained / lost, so link targets are not dropped
  * the per-topic render severity and route on each side, when
    ``render-diagnostics.tsv`` is present, so a structural change can be
    attributed to a route change

One deliberate choice about the word gate: the digits of a list ordinal are
kept in the word stream rather than stripped as syntax.  Every shape issue #51
converts already carries the ordinal as visible text -- ``**1\\.**`` and
``1\\.`` both tokenise to ``1`` -- so promoting either one to a real ``1. ``
item cancels out and the gate stays at zero, which is what it must do for a
pure restructuring.  Stripping the marker instead would report 133 topics as
having lost a word.  The side effect is that turning a plain bullet into a
numbered item does show up as a gained digit; that is a genuine structural
change, and the unordered/ordered counters name it on the same line.

Everything is counted with fenced blocks masked out for structure purposes:
a ``- `` inside a code fence is verbatim text, not a bullet.  Words inside a
fence *are* counted, because verbatim text is book content.

Exit status is 1 when any topic gained or lost a word (the gate), 0 otherwise.
Use --no-gate to always exit 0.

Standard library only.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import sys
from collections import Counter


# --------------------------------------------------------------------------
# Markdown scanning
# --------------------------------------------------------------------------

COMMENT_RE = re.compile(r"<!--.*?-->", re.DOTALL)
FENCE_RE = re.compile(r"^\s{0,3}(`{3,}|~{3,})")
IMAGE_RE = re.compile(r"!\[([^\]]*)\]\(<?[^)]*>?\)")
LINK_RE = re.compile(r"\[([^\]]*)\]\(<?[^)]*>?\)")
TAG_RE = re.compile(r"<[^<>]*>")
ESCAPE_RE = re.compile(r"\\(.)")
WORD_RE = re.compile(r"\w+")

ANCHOR_RE = re.compile(r"""<a\s+id=["']([^"']*)["']""", re.IGNORECASE)

# Structure markers, all anchored at the start of a (possibly indented) line.
UNORDERED_RE = re.compile(r"^\s*[-*+](?:\s+|$)")
# A real ordered list item: the dot is *not* backslash-escaped.
ORDERED_RE = re.compile(r"^\s*\d+[.)](?:\s+|$)")
# The same ordinal with the dot escaped renders as plain prose, not a list.
ESCAPED_ORDINAL_RE = re.compile(r"^\s*\d+\\\.(?:\s+|$)")
# The bold-ordinal paragraph shape issue #51 is replacing.
BOLD_ORDINAL_RE = re.compile(r"^\s*(?:[-*+]\s+)?\*\*\d+\\?\.\*\*")

METRIC_KEYS = (
    "unordered_items",
    "ordered_items",
    "escaped_ordinal_paragraphs",
    "bold_ordinal_paragraphs",
    "fenced_blocks",
)


class TopicScan:
    """Structure counters, word multiset and anchor multiset for one topic."""

    __slots__ = ("counts", "words", "anchors")

    def __init__(self) -> None:
        self.counts = dict.fromkeys(METRIC_KEYS, 0)
        self.words: Counter[str] = Counter()
        self.anchors: Counter[str] = Counter()


def scan_markdown(source: str) -> TopicScan:
    """Count structure and collect the word multiset for one topic."""
    scan = TopicScan()
    source = COMMENT_RE.sub(" ", source)

    fence: str | None = None
    for line in source.splitlines():
        marker = FENCE_RE.match(line)
        if marker is not None:
            token = marker.group(1)[0] * 3
            if fence is None:
                fence = token
                scan.counts["fenced_blocks"] += 1
            elif token == fence:
                fence = None
            # A fence line carries only its info string; nothing to count.
            continue

        if fence is not None:
            # Verbatim content: words count, Markdown syntax does not exist.
            scan.words.update(WORD_RE.findall(line))
            continue

        if BOLD_ORDINAL_RE.match(line):
            scan.counts["bold_ordinal_paragraphs"] += 1
        elif ESCAPED_ORDINAL_RE.match(line):
            scan.counts["escaped_ordinal_paragraphs"] += 1
        elif ORDERED_RE.match(line):
            scan.counts["ordered_items"] += 1
        if UNORDERED_RE.match(line):
            scan.counts["unordered_items"] += 1

        for anchor in ANCHOR_RE.findall(line):
            scan.anchors[anchor] += 1

        text = IMAGE_RE.sub(r"\1", line)
        text = LINK_RE.sub(r"\1", text)
        text = TAG_RE.sub(" ", text)
        # The renderer escapes Markdown punctuation, so ordinals arrive as
        # `**1\.**` and sentence dots as `\.`.  Unescape before tokenising.
        text = ESCAPE_RE.sub(r"\1", text)
        scan.words.update(WORD_RE.findall(text))

    return scan


# --------------------------------------------------------------------------
# Export trees
# --------------------------------------------------------------------------

DIAGNOSTICS_NAME = "render-diagnostics.tsv"


def collect_topics(root: str) -> dict[str, str]:
    """Map export-relative topic path -> absolute path, for every ``.md``."""
    topics: dict[str, str] = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [name for name in dirnames if name != ".git"]
        for name in filenames:
            if name.endswith(".md"):
                full = os.path.join(dirpath, name)
                topics[os.path.relpath(full, root).replace(os.sep, "/")] = full
    return topics


class DiagnosticsIndex:
    """Lazily read ``render-diagnostics.tsv`` next to each topic."""

    def __init__(self) -> None:
        self._cache: dict[str, dict[str, dict[str, str]]] = {}

    def _table(self, directory: str) -> dict[str, dict[str, str]]:
        table = self._cache.get(directory)
        if table is not None:
            return table
        table = {}
        path = os.path.join(directory, DIAGNOSTICS_NAME)
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8", errors="replace",
                      newline="") as handle:
                for row in csv.DictReader(handle, delimiter="\t"):
                    key = (row.get("file") or "").strip()
                    if key:
                        table[key] = row
        self._cache[directory] = table
        return table

    def lookup(self, topic_path: str) -> dict[str, str]:
        row = self._table(os.path.dirname(topic_path)).get(
            os.path.basename(topic_path)
        )
        if not row:
            return {}
        return {
            "severity": (row.get("severity") or "").strip(),
            "route": (row.get("route") or "").strip(),
            "family": (row.get("family") or "").strip(),
            "reason": (row.get("reason") or "").strip(),
        }


def read_text(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def load_topic_filter(path: str) -> set[str]:
    wanted: set[str] = set()
    for raw in read_text(path).splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        wanted.add(line.replace(os.sep, "/").lstrip("./"))
    return wanted


def topic_selected(topic: str, wanted: set[str]) -> bool:
    if topic in wanted or os.path.basename(topic) in wanted:
        return True
    # Allow a book directory to stand for every topic inside it.
    head = topic.split("/", 1)[0]
    return head in wanted or head + "/" in wanted


# --------------------------------------------------------------------------
# Comparison
# --------------------------------------------------------------------------

def multiset_delta(before: Counter, after: Counter) -> tuple[list, list]:
    gained = after - before
    lost = before - after
    return (
        sorted(gained.items(), key=lambda item: (-item[1], item[0])),
        sorted(lost.items(), key=lambda item: (-item[1], item[0])),
    )


def compare_topic(topic: str, before_path: str, after_path: str,
                  diagnostics: DiagnosticsIndex) -> dict:
    before = scan_markdown(read_text(before_path))
    after = scan_markdown(read_text(after_path))

    words_gained, words_lost = multiset_delta(before.words, after.words)
    anchors_gained, anchors_lost = multiset_delta(before.anchors, after.anchors)

    metrics = {
        key: {
            "before": before.counts[key],
            "after": after.counts[key],
            "delta": after.counts[key] - before.counts[key],
        }
        for key in METRIC_KEYS
    }

    before_diag = diagnostics.lookup(before_path)
    after_diag = diagnostics.lookup(after_path)

    changed = bool(
        words_gained
        or words_lost
        or anchors_gained
        or anchors_lost
        or any(entry["delta"] for entry in metrics.values())
        or before_diag != after_diag
    )

    return {
        "topic": topic,
        "changed": changed,
        "words_gained": [[word, n] for word, n in words_gained],
        "words_lost": [[word, n] for word, n in words_lost],
        "words_gained_total": sum(n for _, n in words_gained),
        "words_lost_total": sum(n for _, n in words_lost),
        "anchors_gained": [[name, n] for name, n in anchors_gained],
        "anchors_lost": [[name, n] for name, n in anchors_lost],
        "metrics": metrics,
        "diagnostics": {"before": before_diag, "after": after_diag},
    }


def build_report(before_root: str, after_root: str,
                 topic_filter: set[str] | None) -> dict:
    before_topics = collect_topics(before_root)
    after_topics = collect_topics(after_root)

    common = sorted(set(before_topics) & set(after_topics))
    only_before = sorted(set(before_topics) - set(after_topics))
    only_after = sorted(set(after_topics) - set(before_topics))

    if topic_filter is not None:
        common = [t for t in common if topic_selected(t, topic_filter)]
        only_before = [t for t in only_before if topic_selected(t, topic_filter)]
        only_after = [t for t in only_after if topic_selected(t, topic_filter)]

    diagnostics = DiagnosticsIndex()
    topics = [
        compare_topic(topic, before_topics[topic], after_topics[topic],
                      diagnostics)
        for topic in common
    ]

    totals = {
        key: {"before": 0, "after": 0, "delta": 0} for key in METRIC_KEYS
    }
    words_gained: Counter[str] = Counter()
    words_lost: Counter[str] = Counter()
    anchors_gained: Counter[str] = Counter()
    anchors_lost: Counter[str] = Counter()
    severity_moves: Counter[str] = Counter()

    for entry in topics:
        for key in METRIC_KEYS:
            for side in ("before", "after", "delta"):
                totals[key][side] += entry["metrics"][key][side]
        words_gained.update({w: n for w, n in entry["words_gained"]})
        words_lost.update({w: n for w, n in entry["words_lost"]})
        anchors_gained.update({a: n for a, n in entry["anchors_gained"]})
        anchors_lost.update({a: n for a, n in entry["anchors_lost"]})
        before_sev = entry["diagnostics"]["before"].get("severity", "")
        after_sev = entry["diagnostics"]["after"].get("severity", "")
        if before_sev != after_sev:
            severity_moves[f"{before_sev or '-'} -> {after_sev or '-'}"] += 1

    return {
        "before_root": before_root,
        "after_root": after_root,
        "topics_compared": len(topics),
        "topics_changed": sum(1 for entry in topics if entry["changed"]),
        "only_in_before": only_before,
        "only_in_after": only_after,
        "totals": {
            "metrics": totals,
            "words_gained_total": sum(words_gained.values()),
            "words_lost_total": sum(words_lost.values()),
            "words_gained": [
                [w, n] for w, n in words_gained.most_common()
            ],
            "words_lost": [[w, n] for w, n in words_lost.most_common()],
            "anchors_gained_total": sum(anchors_gained.values()),
            "anchors_lost_total": sum(anchors_lost.values()),
            "anchors_gained": [[a, n] for a, n in anchors_gained.most_common()],
            "anchors_lost": [[a, n] for a, n in anchors_lost.most_common()],
            "severity_moves": dict(severity_moves),
        },
        "topics": topics,
    }


# --------------------------------------------------------------------------
# Text rendering
# --------------------------------------------------------------------------

def sample(pairs: list, limit: int) -> str:
    shown = [f"{value}x{n}" if n > 1 else str(value) for value, n in pairs[:limit]]
    if len(pairs) > limit:
        shown.append(f"... (+{len(pairs) - limit} more)")
    return ", ".join(shown)


def print_report(report: dict, limit: int, show_all: bool) -> None:
    out = sys.stdout.write
    out(f"before: {report['before_root']}\n")
    out(f"after:  {report['after_root']}\n")
    out(
        f"topics compared: {report['topics_compared']}"
        f"  changed: {report['topics_changed']}\n"
    )
    for label, key in (("only in before", "only_in_before"),
                       ("only in after", "only_in_after")):
        missing = report[key]
        if missing:
            out(f"{label}: {len(missing)} ({sample([(m, 1) for m in missing], limit)})\n")

    for entry in report["topics"]:
        if not (show_all or entry["changed"]):
            continue
        out(f"\n{entry['topic']}\n")
        if entry["words_gained"] or entry["words_lost"]:
            out(
                f"  WORDS  +{entry['words_gained_total']}"
                f" -{entry['words_lost_total']}\n"
            )
            if entry["words_gained"]:
                out(f"    gained: {sample(entry['words_gained'], limit)}\n")
            if entry["words_lost"]:
                out(f"    lost:   {sample(entry['words_lost'], limit)}\n")
        for key in METRIC_KEYS:
            metric = entry["metrics"][key]
            if metric["delta"]:
                out(
                    f"  {key:<28} {metric['before']} -> {metric['after']}"
                    f" ({metric['delta']:+d})\n"
                )
        if entry["anchors_gained"] or entry["anchors_lost"]:
            out(
                f"  anchors                      "
                f"+{sum(n for _, n in entry['anchors_gained'])}"
                f" -{sum(n for _, n in entry['anchors_lost'])}\n"
            )
            if entry["anchors_gained"]:
                out(f"    gained: {sample(entry['anchors_gained'], limit)}\n")
            if entry["anchors_lost"]:
                out(f"    lost:   {sample(entry['anchors_lost'], limit)}\n")
        before_diag = entry["diagnostics"]["before"]
        after_diag = entry["diagnostics"]["after"]
        if before_diag or after_diag:
            if before_diag != after_diag:
                out(
                    f"  render                       "
                    f"{before_diag.get('severity', '-')}/"
                    f"{before_diag.get('route', '-')} -> "
                    f"{after_diag.get('severity', '-')}/"
                    f"{after_diag.get('route', '-')}\n"
                )
            else:
                out(
                    f"  render                       "
                    f"{after_diag.get('severity', '-')}/"
                    f"{after_diag.get('route', '-')} (unchanged)\n"
                )

    totals = report["totals"]
    out("\n=== totals ===\n")
    out(
        f"words gained: {totals['words_gained_total']}"
        f"   words lost: {totals['words_lost_total']}\n"
    )
    if totals["words_gained"]:
        out(f"  gained: {sample(totals['words_gained'], limit)}\n")
    if totals["words_lost"]:
        out(f"  lost:   {sample(totals['words_lost'], limit)}\n")
    for key in METRIC_KEYS:
        metric = totals["metrics"][key]
        out(
            f"{key:<28} {metric['before']} -> {metric['after']}"
            f" ({metric['delta']:+d})\n"
        )
    out(
        f"{'anchors':<28} +{totals['anchors_gained_total']}"
        f" -{totals['anchors_lost_total']}\n"
    )
    if totals["anchors_lost"]:
        out(f"  lost:   {sample(totals['anchors_lost'], limit)}\n")
    if totals["anchors_gained"]:
        out(f"  gained: {sample(totals['anchors_gained'], limit)}\n")
    if totals["severity_moves"]:
        out("render severity moves:\n")
        for move, count in sorted(totals["severity_moves"].items()):
            out(f"  {move}: {count}\n")

    verdict = (
        "PASS: no word gained or lost"
        if not (totals["words_gained_total"] or totals["words_lost_total"])
        else "FAIL: word count changed"
    )
    out(f"\n{verdict}\n")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Report list and block structure differences between two "
        "boo2git corpus export trees."
    )
    parser.add_argument("before", help="export tree rendered before the change")
    parser.add_argument("after", help="export tree rendered after the change")
    parser.add_argument(
        "--topics",
        metavar="FILE",
        help="limit the comparison to the topic paths, topic basenames or "
        "book directories listed one per line in FILE",
    )
    parser.add_argument(
        "--json", action="store_true", help="emit the full report as JSON"
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="list unchanged topics too (text output only)",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=12,
        help="how many sample words/anchors to show per list (default 12)",
    )
    parser.add_argument(
        "--no-gate",
        action="store_true",
        help="exit 0 even when words were gained or lost",
    )
    args = parser.parse_args(argv)

    for root in (args.before, args.after):
        if not os.path.isdir(root):
            parser.error(f"not a directory: {root}")

    topic_filter = load_topic_filter(args.topics) if args.topics else None
    report = build_report(args.before, args.after, topic_filter)

    if args.json:
        json.dump(report, sys.stdout, indent=2, sort_keys=False)
        sys.stdout.write("\n")
    else:
        print_report(report, args.limit, args.all)

    totals = report["totals"]
    if args.no_gate:
        return 0
    return 1 if (totals["words_gained_total"] or totals["words_lost_total"]) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
