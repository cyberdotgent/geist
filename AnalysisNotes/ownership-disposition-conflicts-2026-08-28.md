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

`bootrace --coverage` over all 35 fixtures: 0 topics with the ownership
rejection (was 96); all 96 now reach the typed families and are declined by
them (93 with the ordinary prose/mixed reason chain, 3 with the comment
delivery shape reason); no topic changed route or family anywhere in the
corpus (71 typed before and after). `boo2git --force` over the 13 affected
books before and after the change is byte-identical. Fast tier 46/46, slow
gate 15/15.
