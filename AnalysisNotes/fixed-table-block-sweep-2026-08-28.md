# Fixed-table block admission sweep, 2026-08-28

Scope: the typed fixed-table block (`libgeist/src/fixed_table_block_ir.cpp`,
`fixed_table_document_lowering.cpp`) that the prose-topic composer will call
per row range (`extract_fixed_table_blocks_ir(records, layout, ownership,
range)`). It is not wired into the dispatcher; corpus Markdown is unchanged
(24 `boorender` spot checks byte-identical against a build of the base
commit). Tracker: issue #58 ("fixed-table block" slice of the census in
`typed-route-census-2026-08-28.md`).

## Geometry model

Verified against hosted BookServer pages (latin-1, `python3 urllib`):

| Book | Topic | URL | What it proves |
|---|---|---|---|
| SC31-711 | FRONT_1.1 | `/BOOKS/SC31-711/FRONT_1.1?DT=19941010174546` | headerless 2-column box; `RISC System/6000` rejoined across a false Layout IR row split |
| SC31-711 | 4.0 | `/BOOKS/SC31-711/4.0?DT=19941010174546` | bold two-line header (CFONT `5 3 2 9 11 2 29 5 2`), CSELECT cells, top rule missing its corner before a control, record cut at LR 95/96, `3` of `topic 4.3` unpositioned |
| GG24-4302-00 | 10.2 | `/BOOKS/GG24-4302-00/10.2?DT=19950308184737` | caption row under a junction-free top rule, bold header, bullet cells over LR 713-717 |
| SC31-605 | 2.1, 3.5 | `/BOOKS/SC31-605/2.1?DT=19911015203151`, `.../3.5` | one-byte marker slots that decode to box glyphs (`v16` = U+251C), header line cut before its blank third cell, record boundary opening directly with the left border |

Every display line of an SRTBL envelope is `[hidden one-byte marker slot]*
[exact-space origin] <left border> ... <right border>`; columns are the
assembled record's output index relative to the origin token. The top rule
gives the width, rule junctions give the separators, content lines must
carry U+2502 at exactly those columns. Blank line tails and rule corners may
be omitted before a control or a record boundary; a record boundary may drop
the next line's marker/origin. See the header comment in
`libgeist/src/geist/detail/fixed_table_block_ir.hpp`.

## Sweep

Scratch tool: every TOC topic of every fixture whose typed structure has an
SRTBL control (`extract_topic_structure_ir`), full row range, then
`verify_fixed_table_blocks_ir` and `verify_fixed_table_document_ir`.

- Topics with SRTBL: **1,268** (426 census class "contains fixed rows/tables",
  841 mixed, 1 front matter). SRTBL envelopes: **1,896**.
- Envelopes admitted: **677** (35.7 %); declined: 1,219.
- Topics with every envelope admitted: **464** (86 of the 426 fixed-table
  class, 378 of the 841 mixed); partially admitted: 59; none: 745.
- Verification: 0 block-verifier or lowering failures on admitted blocks;
  the 105 `VERIFY_FAIL` rows are topics whose Ownership IR is conflicted
  (the pre-family rejection already counted in the census), where the
  verifier refuses by design.
- 438 topics contain cell words that no physical row positioned (Layout IR
  drops a marker-started row whose payload was trimmed to a placeholder
  run); they are claimed from the opaque source ledger and verified there.

Decline reasons (envelopes):

| Reason | Envelopes | Representative topics |
|---|---:|---|
| visible source between table lines (no box rules: gap-column SRTBL tables such as the SC24-5527-02 command/explanation tables, `vmfview`, `Binder`) | 817 | SC24-5527-02 3.8.4.2, SC33-033 PREFACE.6, QSYSINFO APPENDIX1.4.1.1 |
| source ownership is conflicted | 161 | ACPZMST1 2.1.2, GC23-046 3.2, PRG1SORT FRONT1 |
| rule junctions do not align with the column boundaries (spanning/nested headers) | 76 | DREICMST 2.1.3, GC28-183 2.2 |
| box has a single column (framed text) | 48 | ACPZMST1 2.1.1 |
| table line does not end with a matching border | 31 | ACPZMST1 1.2.4.4, GC28-183 2.3 |
| box glyph inside a table cell (nested boxes/diagrams) | 23 | SC09-138 3.5, SC24-5520-00 3.8.1.1 |
| table line has no display position | 18 | ACPZMST1 4.0, IEAC6MST 2.2 |
| content line separators do not align | 16 | GG24-4302-00 6.7, IEAC6MST PREFACE.2 |
| others (no top rule, unclosed envelope, no rows, width overflow) | 29 | ACPZMST1 FRONT_1.2, GG24-4302-00 FRONT_1 |

Per book (admitted / declined envelopes): SC24-5520-00 161/113, SC31-605
80/3, SC09-138 78/47, SC26-457 62/5, SH12-565 46/9, PRG1SORT 27/49,
FA1PLMM0 26/2, SC24-5527-02 25/467, SC09-2417-00 25/14, IEAC6MST 18/12,
GC23-046 16/12, ACPZMST1 15/83, SC33-033 5/154, QSYSINFO 1/61.

The next slice for this block is a gap-column geometry (CFONT header spans
plus consistent inter-cell gaps) for the 817 rule-less envelopes; it needs
hosted truth for SC24-5527-02/SC33-033/QSYSINFO, which are not on the
hosted server.
