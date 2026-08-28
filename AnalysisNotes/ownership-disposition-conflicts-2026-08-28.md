# Ownership disposition conflicts (typed-route census follow-up)

Date: 2026-08-28. Tracker: issue #58. Input: the census in
`typed-route-census-2026-08-28.md`, which listed 96 topics rejected before any
typed family with `topic ownership rejected: source cell received incompatible
ownership dispositions` (the only pre-family rejection), plus the two topics
whose `bootrace --ir` trace threw `invalid source IR trace` for the same reason
(SC34-425 1.7.1, SH20-918 FRONT_1.3).

## Method

`bootrace <book> --coverage` over every fixture (`grep incompatible`), with the
conflict message temporarily extended to carry the cell coordinates and both
dispositions; then a scratch dump of control segments, tokens and physical rows
around each conflicting cell (all 96 cases, 13 books).

## Finding

Every one of the 96 conflicts is the same pair on one cell:
`existing=control_operand requested=marker_slot`, word 0 of the first row of a
display run, and the cell value is always a box-drawing code point
(U+2500 `─`, U+2502 `│`, U+2514, U+2518, U+251C, U+2524, U+252C, U+2534).

The control IR (`decode_control_segments`) derived opcode and operand ranges
by whitespace-splitting the ASCII projection of the record. Box-drawing glyphs
project to `?`, and the decoder inserts no space between a control token and an
immediately following glyph token, so the glyph was swallowed into the opcode
or operand range and its cells marked `control_operand`. The layout IR
independently (and correctly) took the same glyph token as the marker slot of
the first physical row, so the ledger saw two dispositions on one cell.

### Class 1: `SRETBL` opcode absorbs the horizontal rule (90 topics)

Shape: `SRETBL` dictionary token, a zero-width control token, then a run of
U+2500 as the table's top/bottom rule. Decoded spelling `SRETBL────────────`
became the opcode word; operand range empty; payload started after the rule.

| Book / topic | LR | Token | Value | Token bytes |
| --- | ---: | ---: | --- | --- |
| GC23-046 6.9.5.1 | 168 | 14 (12 words) | U+2500 | `[0x125e6,0x125e7)`, opcode token 12 `SRETBL` `[0x125e3,0x125e5)`, control token 13 `[0x125e5,0x125e6)` |
| ACPZMST1 2.1.2 | 70 | 170 | U+2500 | `[0xccd0,0xccd1)`, opcode token 168 `[0xccce,0xcccf)` |
| ACPZMST1 4.3 | 222 | 163 | U+2500 | `[0x17c6b,0x17c6c)` |
| GC23-046 3.2 | 53 | 14 | U+2500 | opcode token 12 `[0xa7e9,0xa7eb)`, control 13 `[0xa7eb,0xa7ec)` |
| SC24-5520-00 1.1.5 | 54 | 266 | U+2500 | class 1 |
| PRG1SORT 1.2.1.1.1 | 144 | 184 | U+2500 | class 1 |
| SC34-425 1.6.3 | 348 | 214 | U+2524 | class 1 (junction glyph after `SRETBL`) |
| SH20-918 FRONT_1.3 | 25 | 300 | U+2524 | class 1 |

Books: ACPZMST1 2, GC23-046 2, GG24-395 2, GG24-4302-00 1, PRG1SORT 12,
SC09-138 3, SC24-546 1, SC24-5520-00 24, SC24-5527-02 39, SC28-1881-05 1,
SC34-425 2, SH20-918 1.

### Class 2: fixed-operand control absorbs a vertical rail / junction (6 topics)

Shape: `cmitem` (or record-leading `CMITEM`) inside a fixed figure or syntax
diagram whose next whitespace word is a single glyph. The one fixed operand of
a menu item was taken to be that glyph. Three of the six have a zero-width
control token between a real operand and the glyph (`??? ┤`), three have only
spaces.

| Book / topic | LR | Token | Value | Token bytes |
| --- | ---: | ---: | --- | --- |
| SC28-1881-05 1.45 | 1060 | 2 | U+2502 | `[0x80a46,0x80a47)`, opcode token 0 `cmitem` `[0x80a44,0x80a45)` |
| GG24-4302-00 3.2.14 | 206 | 154 | U+2502 | `[0x4d6a9,0x4d6aa)`, opcode token 152 `[0x4d6a7,0x4d6a8)` |
| SC28-1881-05 1.6 | 204 | 370 | U+2502 | `[0x3ebbd,0x3ebbe)` |
| SC28-1881-05 1.17 | 524 | 61 | U+2524 | `[0x57588,0x57589)`, control token 60 `[0x57587,0x57588)` |
| SC28-1881-05 1.24 | 594 | 182 | U+2524 | `[0x5ce6b,0x5ce6c)` |
| OFCUSEOV 1.1 | 56 | 259 | U+2502 | `[0xdf85,0xdf86)` |

Hosted BookServer renders these glyphs as visible table rails (for example
GG24-4302-00 3.2.14 lines `|     5 DBF#FPU0 ...` / `|       BUFFER - NBA=`),
confirming they are display content, not operands.

## Fix

- `libgeist/src/control_ir.cpp`: opcode/operand words are split on the
  assembled code points, not the projected spelling. Any transition into or
  out of a display-geometry code point (U+2500-U+257F) ends a word, and
  operand parsing stops at the first geometry word (a fixed-operand control
  with no ASCII operand is `malformed` with an empty operand range; the
  geometry belongs to the payload).
- `libgeist/src/ownership_ir.cpp`: display runs are owned atomically. Any
  residual disagreement (incompatible disposition, duplicate row assignment,
  missing cell, no display column) is recorded as a typed
  `OwnershipRunConflictIR` on that run only; the run owns no cells, every
  other run keeps its ownership, and `verify_ownership_ir` still passes. The
  formatter prints `run_conflict run=… kind=… existing=… requested=…`.
  Consumers must treat runs listed in `run_conflicts`
  (`ownership_run_conflicted`) as unowned.
- `libgeist/tests/ownership_ir_synthetic.cpp`: fixtures for both classes and
  for the run-scoped conflict.

## Result

Measured on the merged library state (this branch) against `main` `afc62c0`,
`bootrace <book> --coverage` over all 34 fixtures (7,396 TOC topics):

| | topics | typed route | pre-family ownership rejections |
| --- | ---: | ---: | ---: |
| `afc62c0` (before) | 7,396 | 2,665 | 96 |
| this branch (after) | 7,396 | 2,665 | 0 |

All 96 topics now pass ownership and are offered to every typed family. None is
admitted: all 96 are declined by the prose family that landed in `afc62c0`, for
reasons that belong to families still being built:

| prose-family decline | topics |
| --- | ---: |
| body control `SRTBL…` outside the prose model | 59 |
| structural control `SRFIG…` is not a bare anchor | 16 |
| `SRV*` control carries visible payload (`SRVREQT`, `SRVBLDS`, `SRVRECS`, `SRVAPPS`, `SRVREQ`, `SRVDESCT`, `SRVDEP`) | 14 |
| selector targets a picture or external link | 3 |
| `c.cp` control carries visible payload | 2 |
| body control `CMITEM` outside the prose model | 1 |
| first record lacks the topic metadata envelope | 1 |

The two largest groups are exactly the fixed-table (677 admitted envelopes) and
figure (306 admitted regions) block families already built and waiting on the
prose composer, so these 96 are now reachable by that work. Typed coverage is
therefore unchanged at 2,665, and the ratchet baseline in
`typed_route_inventory.cpp` is not raised: no book gained a typed topic. What
changed is that the 96 stop being a pre-family wall and become ordinary
family-level declines.

Route and family are identical for every one of the 7,396 topics before and
after. `bootrace --ir` over the 96 reports zero `run_conflict` rows and no
`invalid source IR trace`; SC34-425 1.7.1 and SH20-918 FRONT_1.3, which
previously threw, now emit a complete trace. The run-scoped
`OwnershipRunConflictIR` path is therefore unexercised by the corpus and is
held open by the synthetic fixtures only, as the fail-closed reserve for a
genuinely ambiguous shape.

`boo2git --force` over all 34 books before (`afc62c0` build) and after: the
rendered corpus is byte-identical - 7,830 files per side across the 33
fast books with no differing, added or removed file, and N2AH1MST identical
separately. No file changed, as required: no topic moved to a typed route.

Hosted confirmation that the absorbed glyphs are display content, not operands
(`http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/<BOOK>/<TOPIC>?DT=<dt>`):

- SC24-5527-02 1.1.5 (`DT=19920529132045`, class 1): the run absorbed into the
  `SRETBL` opcode is the table's visible horizontal rule
  (`_______________________ ________________________ …`), and the topic renders
  as an 8-rule bordered table.
- SC28-1881-05 1.45 (`DT=19920313000100`, class 2): the glyph taken as the
  `cmitem` operand is the syntax diagram's left rail; every diagram row on the
  hosted page opens with a visible `|`.
- GG24-4302-00 3.2.14 (class 2): hosted rows `|     5 DBF#FPU0 …` /
  `|       BUFFER - NBA=` as recorded above.

## Follow-up for the family owners

`ownership.conflicts` is now empty for all 7,396 topics, so the 161 SRTBL
envelopes the fixed-table sweep declined with "source ownership is conflicted"
(`fixed_table_block_ir.cpp:1086`, `:1119`) no longer decline for that reason;
the same holds for the `ownership.conflicts` gates in `glossary_ir.cpp:217`,
`message_ir.cpp:1141`, `message_section_blocks_ir.cpp:500`/`:535` and
`trap_catalog_ir.cpp:506`.

None of those gates yet inspects `run_conflicts`. A conflicted run owns no
cells, so its content stays `opaque` rather than being reported as missing. The
corpus contains no run conflict today, and each family's canonical
re-extraction verifier is expected to catch the shortfall as a conservation
failure, but a family that wants the ledger to cover the whole topic should
test `ownership.run_conflicts.empty()` (or `ownership_run_conflicted(run)`)
alongside `conflicts`. Left to the owners of those files rather than changed
here.

Tests: fast tier 47/47 (46 on `main` plus `ownership_ir_synthetic_test`), slow
gate 15/15 including `typed_route_inventory_test`.
