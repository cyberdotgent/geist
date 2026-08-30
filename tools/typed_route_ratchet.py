#!/usr/bin/env python3
"""Typed-route coverage ratchet over the full BookManager corpus (issue #58).

This used to live in libgeist/tests/typed_route_inventory.cpp.  It moved out of
the library because only ``libgeist/`` is published and the ratchet needs the 34
``.boo`` fixtures, none of which can be redistributed except ``packet.boo``.
It is maintainer tooling now: it consumes libgeist through the ``bootrace``
example binary and needs no library headers.

For each book it runs ``bootrace <book> --coverage`` and reads the trailing
``# summary`` line, whose ``typed=`` field is exactly the count that
``BooDocument::typed_route_inventory()`` used to report.  Coverage may only ever
rise: the per-book baseline and the corpus total below are the committed floor.

Updating the baseline: when a lowering slice raises coverage, run this script
and copy the printed ``book<TAB>typed`` pairs into BASELINE (and the new total
into BASELINE_TOTAL).  Lowering a number is a regression and needs an explicit
explanation in the commit message.

Runtime is about 11 minutes uncontended, most of it in N2AH1MST.BOO whose SRMSG
recognizers are slow.  Use ``--jobs`` to spread the books over several cores.

Usage:
    tools/typed_route_ratchet.py [--corpus DIR ...] [--bootrace PATH] [--jobs N]
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

# Committed floor, carried across from libgeist/tests/typed_route_inventory.cpp.
# Typed topics per book; these may only ever rise.
BASELINE = {
    "ACPZMST1.boo": 186,
    "DREICMST.boo": 370,
    "FA1PLMM0.boo": 414,
    "GC23-046.boo": 99,
    "GC28-183.boo": 145,
    "GG24-395.boo": 223,
    "GG24-4302-00.boo": 228,
    "GX27-3999-00.boo": 29,
    "IBMMMSTR.boo": 52,
    "IEAC6MST.BOO": 201,
    "ITPPIBOK.BOO": 254,
    "N2AH1MST.BOO": 40,
    "OFCUSEOV.BOO": 180,
    "PRG1SORT.boo": 205,
    "QS3X36CM.BOO": 10,
    "QSYSINFO.BOO": 404,
    "QSYSNEWG.BOO": 153,
    "SC09-138.boo": 522,
    "SC09-2417-00.boo": 322,
    "SC24-546.boo": 298,
    "SC24-5520-00.boo": 644,
    "SC24-5527-02.boo": 297,
    "SC26-457.boo": 353,
    "SC28-1881-05.boo": 88,
    "SC31-605.boo": 109,
    "SC31-711.boo": 76,
    "SC33-033.boo": 220,
    "SC34-425.boo": 244,
    "SC41-485.boo": 29,
    "SG24-204.boo": 87,
    "SH12-565.boo": 281,
    "SH20-918.boo": 196,
    "XWEBDEMO.boo": 9,
    # packet.boo is the one redistributable fixture; it now lives inside the
    # published library at libgeist/tests/fixtures/.
    "packet.boo": 120,
}
BASELINE_TOTAL = 7088

SUMMARY = re.compile(r"^# summary\ttyped=(\d+)\tlegacy=(\d+)\ttotal=(\d+)")

REPO_ROOT = Path(__file__).resolve().parent.parent

# Searched in order when --corpus is not given.  packet.boo lives with the
# library so a libgeist-only checkout still has it.
DEFAULT_CORPUS = (REPO_ROOT / "BOO", REPO_ROOT / "libgeist" / "tests" / "fixtures")

BOOTRACE_CANDIDATES = (
    REPO_ROOT / "build_rls" / "bootrace",
    REPO_ROOT / "build" / "bootrace",
)


def find_bootrace(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit)
        if not path.is_file():
            sys.exit(f"typed_route_ratchet: no bootrace binary at {path}")
        return path
    for candidate in BOOTRACE_CANDIDATES:
        if candidate.is_file():
            return candidate
    sys.exit(
        "typed_route_ratchet: no bootrace binary found in "
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


def typed_count(bootrace: Path, path: Path) -> tuple[int, int, int]:
    """Return (typed, legacy, total) for one book."""
    result = subprocess.run(
        [str(bootrace), str(path), "--coverage"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"bootrace failed on {path.name} ({result.returncode}): "
            f"{result.stderr.strip()}"
        )
    for line in result.stdout.splitlines():
        match = SUMMARY.match(line)
        if match:
            return int(match.group(1)), int(match.group(2)), int(match.group(3))
    raise RuntimeError(f"bootrace printed no coverage summary for {path.name}")


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
    args = parser.parse_args()

    directories = [Path(d) for d in args.corpus] if args.corpus else list(DEFAULT_CORPUS)
    books = collect_books(directories)

    missing = sorted(set(BASELINE) - set(books))
    if len(missing) == len(BASELINE):
        sys.stderr.write(
            "typed_route_ratchet: corpus not available.\n"
            "  Looked in: " + ", ".join(str(d) for d in directories) + "\n"
            "  The ratchet needs the full 34-book BookManager corpus, which is\n"
            "  not redistributable. Only packet.boo ships with the library.\n"
            "  Point --corpus at a directory holding the books to run it.\n"
        )
        return 2

    bootrace = find_bootrace(args.bootrace)

    print("book\ttyped\tlegacy\ttotal\tbaseline")
    failures: list[str] = []
    typed_total = 0
    legacy_total = 0

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        counted = {
            name: pool.submit(typed_count, bootrace, path)
            for name, path in sorted(books.items())
        }
        for name, future in counted.items():
            try:
                typed, legacy, total = future.result()
            except RuntimeError as error:
                failures.append(str(error))
                continue
            typed_total += typed
            legacy_total += legacy
            expected = BASELINE.get(name)
            print(
                f"{name}\t{typed}\t{legacy}\t{total}\t"
                f"{'(none)' if expected is None else expected}"
            )
            if expected is None:
                failures.append(f"{name} has no committed baseline; add it to BASELINE")
            elif typed < expected:
                failures.append(
                    f"{name} typed coverage regressed: {typed} < baseline {expected}"
                )
            if typed + legacy != total:
                failures.append(f"{name} inventory counts do not sum to its topic count")

    if missing:
        # A partial corpus can still ratchet the books it has, but it can never
        # certify the total, so say so and do not compare against it.
        sys.stderr.write(
            "typed_route_ratchet: corpus is incomplete, "
            f"{len(missing)} book(s) absent: {', '.join(missing)}\n"
            "  Per-book baselines were checked for the books present; the "
            "corpus total was not.\n"
        )
    else:
        print(
            f"# summary\ttyped={typed_total}\tlegacy={legacy_total}"
            f"\ttotal={typed_total + legacy_total}\tbaseline={BASELINE_TOTAL}"
        )
        if typed_total < BASELINE_TOTAL:
            failures.append(
                f"corpus typed coverage regressed: {typed_total} < "
                f"baseline {BASELINE_TOTAL}"
            )

    for failure in failures:
        sys.stderr.write(f"typed_route_ratchet: {failure}\n")
    if failures:
        return 1
    print("typed_route_ratchet: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
