#!/usr/bin/env python3
"""Source-side word conservation over the full BookManager corpus (issue #85).

Coverage measures *which route rendered a topic*, not *whether the output is
complete*.  A topic can render, count as fully ``typed`` in
``tools/typed_route_ratchet.py``, and silently drop words.  That is not
hypothetical: while fixing #84 the same root cause turned out to be damaging
five topics the coverage census reported as fully typed -- ``SC24-546 14.0``,
``SC24-5520-00 1.1.13``, ``SC24-5527-02 6.1.2``/``6.3.7``/``6.4.2`` and
``SH12-565 4.7.2`` -- each missing a word, with no symptom.  They were found by
accident.

None of the standing gates could have found them:

* the **ratchet** counts routes, and all five were ``typed`` before and after;
* ``tools/list_structure_diff.py`` is a *differential* -- it needs a second
  export to compare against, so it says nothing about an export in isolation,
  and it normalises whitespace on top of that;
* the **ownership ledger** conserved the token, as the phantom control's
  opcode, and attribution to a control is still attribution;
* **hosted comparison** is only ever run against topics someone is already
  investigating.

This check answers the question none of them asks: *does this topic's output
contain every word its source draws?*  It needs no prior export and no network.

How it decides what is drawn
----------------------------

A record payload tiles into ``<length byte><that many bytes>`` display lines.
The length byte is the row-control slot, always and only -- but it is a raw
byte, and the token reader resolves any low byte through the dictionary into an
ordinary word, so a length byte routinely spells ``adapter``, ``and`` or
``The``.  Only position separates the two roles, and the record decoder has
already decided it; the check reads that framing rather than re-deriving it.

Everything else on a line is drawn, minus the cells a control consumes.  A
control's opcode is the first token of its own display line, so a control the
framing does not put there is not proven, and its cells stay drawn text.  That
is what makes this check able to see the #84 class at all: a phantom control's
word is conserved by the ledger but is not conserved by the render, and only
the framing can tell the two apart.

Reading the report
------------------

Every row names a topic, a word, and up to three of the display lines the word
stands on.  Enumeration is the point: a count alone repeats the failure this
check exists to fix.

The baseline
------------

``BASELINE`` records what today's corpus reports, per topic, exactly as the
ratchet's floors do: **89 unaccounted words in 19 of 7,362 topics**.  It is not
a list of expected behaviour.  Every entry is a real drop, enumerated word by
word in **issue #88** and summarised in ``BASELINE_TICKETS``.  A topic absent
from ``BASELINE`` must drop nothing; a topic present in it must not drop more
than it did.  Dropping fewer is an improvement the script reports and does not
fail, so the baseline can be lowered in the same commit that earns it.

``libgeist/tests/fixtures/packet.boo`` -- the one redistributable fixture, and
the only book the library's own tests may open -- is clean, so the synthetic
test in ``libgeist/tests/drawn_word_conservation_synthetic.cpp`` enforces zero
there with no baseline at all.

Usage:
    tools/drawn_word_conservation.py [--corpus DIR ...] [--bootrace PATH]
                                     [--jobs N] [--enumerate]
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

# What today's corpus drops: ``book -> {topic: words}``.  Every entry is a
# defect, not an exemption -- see BASELINE_TICKETS.
BASELINE: dict[str, dict[str, int]] = {
    "FA1PLMM0.boo": {"FIGURES": 2},
    "GC23-046.boo": {"FIGURES": 1},
    "GC28-183.boo": {"FIGURES": 6},
    "GG24-395.boo": {"FIGURES": 1},
    "IEAC6MST.BOO": {"FIGURES": 2},
    "ITPPIBOK.BOO": {"FIGURES": 1},
    "SC09-138.boo": {"8.5.4.5": 1, "8.5.7.1": 2},
    "SC09-2417-00.boo": {"1.3.3.4": 20, "3.1.6.1": 10},
    "SC24-546.boo": {"B.2": 15},
    "SC24-5527-02.boo": {"4.2.3": 1, "TABLES": 2},
    "SC31-711.boo": {"BACK_1.9": 1},
    "SG24-204.boo": {"BACK_1.2": 4, "FIGURES": 1},
}
BASELINE_TOTAL = 70
BASELINE_TOPICS = 16

# Why each baseline topic drops words.  Grouped by cause, because the nineteen
# topics are four defects, not sixteen.  All four are tracked by issue #88,
# which carries the full per-word enumeration.
BASELINE_TICKETS = """\
  Tracked by issue #88 -- four defects across sixteen topics:
    FIGURES in seven books and TABLES in SC24-5527-02 (16 words)
        A generated figure- or table-list entry whose caption wraps loses the
        continuation line's words.
    SC09-2417-00 1.3.3.4 (20 words)
        A whole figure caption block and the prose that names it leave the
        render.  The largest single drop in the corpus.
    SC24-546 B.2, SC31-711 BACK_1.9, SC24-5527-02 4.2.3,
    SC09-138 8.5.7.1 / 8.5.4.5 (20 words)
        Individual display lines, or the tail of one, that do not reach the
        render.  Several are code samples whose row the model cuts short.
    SC09-2417-00 3.1.6.1, SG24-204 BACK_1.2 (14 words)
        Topics on the best-effort route, where whole rows are dropped rather
        than reproduced.
"""

SUMMARY = re.compile(
    r"^# summary\tchecked=(\d+)\tdropping=(\d+)\tunaccounted=(\d+)"
    r"\tforgiven=(\d+)\tunframed-topics=(\d+)"
)

REPO_ROOT = Path(__file__).resolve().parent.parent

# Searched in order when --corpus is not given.  packet.boo lives with the
# library so a libgeist-only checkout still has it.  Both spellings of the
# suffix are collected: seven fixtures are uppercase, and a ``*.boo`` glob
# silently covers only 26 of the 34 books.
DEFAULT_CORPUS = (REPO_ROOT / "BOO", REPO_ROOT / "libgeist" / "tests" / "fixtures")

BOOTRACE_CANDIDATES = (
    REPO_ROOT / "build_rls" / "bootrace",
    REPO_ROOT / "build" / "bootrace",
)


class Deficit:
    __slots__ = ("topic", "route", "word", "unaccounted", "evidence")

    def __init__(self, topic: str, route: str, word: str, unaccounted: int,
                 evidence: str) -> None:
        self.topic = topic
        self.route = route
        self.word = word
        self.unaccounted = unaccounted
        self.evidence = evidence


class BookReport:
    __slots__ = ("checked", "dropping", "unaccounted", "forgiven", "unframed",
                 "deficits")

    def __init__(self) -> None:
        self.checked = 0
        self.dropping = 0
        self.unaccounted = 0
        self.forgiven = 0
        self.unframed = 0
        self.deficits: list[Deficit] = []

    def by_topic(self) -> dict[str, int]:
        totals: dict[str, int] = {}
        for deficit in self.deficits:
            totals[deficit.topic] = totals.get(deficit.topic, 0) + deficit.unaccounted
        return totals


def find_bootrace(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit)
        if not path.is_file():
            sys.exit(f"drawn_word_conservation: no bootrace binary at {path}")
        return path
    for candidate in BOOTRACE_CANDIDATES:
        if candidate.is_file():
            return candidate
    sys.exit(
        "drawn_word_conservation: no bootrace binary found in "
        + ", ".join(str(c.parent) for c in BOOTRACE_CANDIDATES)
        + "; build one (cmake --build build_rls --target bootrace) or pass "
        "--bootrace PATH"
    )


def collect_books(directories: list[Path]) -> dict[str, Path]:
    books: dict[str, Path] = {}
    for directory in directories:
        if not directory.is_dir():
            continue
        for entry in sorted(directory.iterdir()):
            if entry.is_file() and entry.suffix.lower() == ".boo":
                books.setdefault(entry.name, entry)
    return books


def trace_book(bootrace: Path, path: Path) -> BookReport:
    result = subprocess.run(
        [str(bootrace), str(path), "--conservation"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"bootrace failed on {path.name} ({result.returncode}): "
            f"{result.stderr.strip()}"
        )
    report = BookReport()
    seen_summary = False
    for line in result.stdout.splitlines():
        match = SUMMARY.match(line)
        if match:
            report.checked = int(match.group(1))
            report.dropping = int(match.group(2))
            report.unaccounted = int(match.group(3))
            report.forgiven = int(match.group(4))
            report.unframed = int(match.group(5))
            seen_summary = True
            continue
        if line.startswith("#") or line.startswith("topic\t"):
            continue
        fields = line.split("\t")
        if len(fields) < 7:
            continue
        unaccounted = int(fields[6])
        if unaccounted == 0:
            continue
        report.deficits.append(
            Deficit(fields[0], fields[1], fields[2], unaccounted,
                    fields[7] if len(fields) > 7 else "")
        )
    if not seen_summary:
        raise RuntimeError(
            f"bootrace printed no conservation summary for {path.name}"
        )
    return report


def print_deficits(book: str, deficits: list[Deficit]) -> None:
    for deficit in sorted(deficits, key=lambda d: (d.topic, d.word)):
        print(
            f"    {book} {deficit.topic} [{deficit.route}] "
            f"{deficit.word} x{deficit.unaccounted}"
        )
        if deficit.evidence:
            print(f"        {deficit.evidence}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--corpus",
        action="append",
        metavar="DIR",
        help="directory of .boo books; repeatable. "
        "Default: BOO/ and libgeist/tests/fixtures/.",
    )
    parser.add_argument("--bootrace", help="path to the bootrace binary")
    parser.add_argument(
        "--jobs",
        type=int,
        default=min(4, os.cpu_count() or 1),
        help="books to trace concurrently (default: %(default)s)",
    )
    parser.add_argument(
        "--enumerate",
        action="store_true",
        help="print every dropped word, baseline or not",
    )
    args = parser.parse_args()

    directories = [Path(d) for d in args.corpus] if args.corpus else list(DEFAULT_CORPUS)
    books = collect_books(directories)

    if not set(BASELINE) & set(books):
        sys.stderr.write(
            "drawn_word_conservation: corpus not available.\n"
            "  Looked in: " + ", ".join(str(d) for d in directories) + "\n"
            "  This check needs the full 34-book BookManager corpus, which is\n"
            "  not redistributable. Only packet.boo ships with the library,\n"
            "  and libgeist/tests/drawn_word_conservation_synthetic.cpp\n"
            "  enforces the same invariant on it.\n"
        )
        return 2

    bootrace = find_bootrace(args.bootrace)

    print("book\tchecked\tdropping\tunaccounted\tforgiven\tbaseline")
    failures: list[str] = []
    improvements: list[str] = []
    checked_total = 0
    unaccounted_total = 0
    dropping_total = 0

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        traced = {
            name: pool.submit(trace_book, bootrace, path)
            for name, path in sorted(books.items())
        }
        for name, future in traced.items():
            try:
                report = future.result()
            except RuntimeError as error:
                failures.append(str(error))
                continue
            checked_total += report.checked
            unaccounted_total += report.unaccounted
            dropping_total += report.dropping
            expected = BASELINE.get(name, {})
            print(
                f"{name}\t{report.checked}\t{report.dropping}\t"
                f"{report.unaccounted}\t{report.forgiven}\t"
                f"{sum(expected.values())}"
            )
            if report.unframed:
                print(
                    f"# {name}: {report.unframed} topic(s) hold a record whose "
                    "payload does not tile into display lines and are not checked"
                )
            actual = report.by_topic()
            regressed = []
            for topic, count in sorted(actual.items()):
                floor = expected.get(topic, 0)
                if count > floor:
                    regressed.append((topic, floor, count))
            if regressed:
                for topic, floor, count in regressed:
                    failures.append(
                        f"{name} {topic}: drops {count} word(s), baseline {floor}"
                    )
                print_deficits(
                    name,
                    [d for d in report.deficits
                     if d.topic in {topic for topic, _, _ in regressed}],
                )
            for topic, floor in sorted(expected.items()):
                count = actual.get(topic, 0)
                if count < floor:
                    improvements.append(
                        f"{name} {topic}: drops {count} word(s), baseline {floor}"
                    )
            if args.enumerate and report.deficits:
                print_deficits(name, report.deficits)

    print(
        f"# summary\tchecked={checked_total}\tdropping={dropping_total}"
        f"\tunaccounted={unaccounted_total}\tbaseline={BASELINE_TOTAL}"
    )

    if improvements:
        print("\n# improved -- lower the baseline in this commit:")
        for line in improvements:
            print(f"    {line}")

    if failures:
        print("\n# FAIL: the render drops display text the source draws.")
        for line in failures:
            print(f"    {line}")
        print(
            "\n  Every word above stands inside a display line, where no control\n"
            "  opcode ever stands, so it is display text and nothing else. Fix\n"
            "  the drop, or -- if it is a defect you are recording rather than\n"
            "  fixing -- add it to BASELINE *and* to BASELINE_TICKETS with the\n"
            "  issue that tracks it."
        )
        return 1

    print("\n# ok: no topic drops more display text than its recorded baseline.")
    print("# recorded baseline (each entry a defect with a ticket, not an "
          "exemption):")
    sys.stdout.write(BASELINE_TICKETS)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
