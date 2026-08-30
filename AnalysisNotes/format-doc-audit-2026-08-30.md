# Format/ Documentation Audit — 2026-08-30

Workflow notes for the corpus-wide re-verification of `Format/*.md`. The
byte-level conclusions live in `Format/`; this note records how to repeat the
measurements. Tracker: GitHub issue #58.

## Why A Corpus Sweep

Four documented claims had already been found wrong and retired mid-flight, all
four with the same cause: a rule derived from the *flattened decoded string*
that is really about the display-line length byte. Spot-checking cannot find the
rest of that family, and several claims turned out to be true of two fixtures
and false of thirty-four. So every counted claim was recomputed over all 34
books at once.

## Build

```sh
cmake -S libgeist -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j3
```

## The `bootrace` Limitation That Forced A Custom Dumper

`bootrace <book> <topic>` resolves the topic through
`BooDocument::find_toc_entry`, so it only reaches topics that appear in the
book's TOC. `IBMMMSTR.BOO` declares 1,677 topics and lists 60, so 1,617 of them
are unreachable from the tool. `bootrace <book> --coverage` has the same limit
and reports `total=60` for that book.

`BooDocument::trace_logical_records` itself looks topics up in `topics_` and
works for every topic. A ~50-line dumper linking `build/libgeist_static.a` and
iterating `doc.topics()` therefore reaches the whole corpus:

```cpp
for (const auto& t : doc.topics())
  for (const auto& r : doc.trace_logical_records(t.id))
    for (const auto& line : r.ir_display_lines)   // or r.ir_tokens
      std::cout << base << "\t" << t.id << "\t" << r.logical_record
                << "\t" << line << "\n";
```

Built with:

```sh
g++ -O2 -DGEIST_STATIC -I libgeist/src -o dumplines dumplines.cpp \
    build/libgeist_static.a -lpng -lgif -lz
```

Running it over `BOO/*.boo BOO/*.BOO` takes about one minute and produces
158 MB of display lines (895,011 rows) and 1.0 GB of tokens (8,972,655 rows).
Both are plain TSV — `book \t topic \t logical_record \t detail` — and every
count in the audit is a `python3` pass over one of them. Keep them out of the
repository; `/tmp` scratch is the right home.

The line detail parses with

```python
re.compile(r"^line=(\d+) prefix_token=(\d+) length=(\d+) tokens=\[(\d+),(\d+)\) "
           r"cols=(\d+) class='(.*)' text='(.*)'$")
```

and the token detail with

```python
re.compile(r"^token=(\d+) value=(\d+) width=(\d+) prefix=(\S+) "
           r"bytes=\[(0x[0-9a-f]+),(0x[0-9a-f]+)\) words='(.*)' values=(.*)$")
```

Joining them on `(book, logical_record, prefix_token)` is what resolves each
display line's length byte back to its source byte and dictionary spelling —
the join behind most of the corrections.

## Corpus Denominators

| Quantity | Value | How |
| --- | ---: | --- |
| Fixtures | 34 | `BOO/*.boo`, `BOO/*.BOO` |
| Declared topics | 10,502 | sum of directory `0x003e` over the 34 |
| Topics `doc.topics()` returns | 10,503 | the extra one is a libgeist defect, below |
| Logical records inside topics | 35,109 | distinct `(book, logical_record)` in the line dump |
| Display lines | 895,011 | rows in the line dump |
| Length bytes resolved to a source byte | 894,877 | the join above |

Directory fields were read straight from the fixtures with a 30-line `struct`
script rather than through `libgeist`, so that the index and header tables in
`Format/` are checked against bytes and not against the parser that was written
from them.

## Hosted BookServer Checks

Base: `http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/<BOOK>/<TOPIC>?DT=<dt>`

Pages fetched for this audit:

| Book | Topic | DT | What it settled |
| --- | --- | --- | --- |
| `PRG1SORT` | `1.1.3.1` | `19900829171904` | The 220-byte display line whose length byte is at or above the token threshold. Hosted prints neither `classification` (the two-byte mis-read) nor `redirecting` (the one-byte dictionary spelling); the line is the ruled bottom row of the header-specifications form. |
| `DREICMST` | `CCONTENTS` | `19911219125856` | `ctoce 0 0 1.0 Part 1. ...` is served as `<strong>` with blank lines around it, refuting "verified displayed entries use nonzero style numbers". |

Fetch with `curl -s` and search with `python3` reading the body as `latin-1`;
`grep` on the raw bytes is unreliable for these pages.

Hosted is not infallible and the audit did not treat it as such. It truncates
`SH20-918`'s `INDEX`, serves some books at a different edition than the bundled
fixture, and does not carry `SC24-5520-00`, `SC24-5527-02`, `SC28-1881-05` or
`packet` at all. Where a corrected claim rests on hosted output, `Format/` says
so and names the DT.

## Defects Found In `libgeist` (Not Fixed Here)

This was a documentation task; no code was changed. Recorded for scheduling, and
repeated in `Format/README.md`:

1. `BooDocument::topics()` yields 10,503 topics where the directories declare
   10,502. The extra entry is `SH12-565.boo` `19-6639`, `topic_number == 0`,
   records 906-907 — exactly the spurious leading-`SH` split that
   `Format/topics.md` warns against. That record is a bibliography order number
   inside `BIBLIOGRAPHY.2`, carries none of the nine envelope controls, and is
   not in the directory `0x003c` topic-start index.
2. `bootrace <book> <topic>` cannot reach non-TOC topics (`find_toc_entry`).
3. `bootrace <book> --coverage` enumerates TOC topics only, so its totals
   understate books with a partial TOC.

## Repeating One Measurement

Example — the `CTOCE` style rule. Collect each topic's `CHDLEVEL` from its
envelope lines and each `CTOCE`'s style from the `CONTENTS` topic's lines, then
cross-tabulate. The result is a clean function with no exceptions in 7,412
entries:

```text
:H0 -> 0    :H1 and all front matter -> 1    :H2 -> 2
:H3 -> 3    :H4 -> 4                         :H5 -> 5
```

Every corrected claim in `Format/` was produced this way: one pass over the line
dump, one cross-tabulation, and the exception list printed rather than assumed
empty.
