# Gap-column fixed-table admission sweep, 2026-08-28

Scope: the rule-less (gap-column) geometry added to the typed fixed-table
block (`libgeist/src/fixed_table_block_ir.cpp`,
`libgeist/src/geist/detail/fixed_table_block_ir.hpp`). It extends the
box-drawn geometry recorded in `fixed-table-block-sweep-2026-08-28.md`,
which admitted 677 of 1,896 SRTBL envelopes and named the 817 rule-less
envelopes as the top decline. The block is still not wired into the
dispatcher; the rendered corpus is unchanged (26 `boorender --md` spot
checks byte-identical against a build of the base commit `afc62c0`).
Tracker: issue #58.

## Gap geometry model

An SRTBL envelope that draws no box (no U+250C top rule) is admitted only
when positioned evidence fixes its columns. Nothing here inspects words,
spelling, or dictionaries.

- **Line assembly.** A display line is `[hidden one-byte marker slot | fill
  run | control-only prefix token]* <exact-space origin> <content>`. Every
  column is the assembled record's output index relative to the origin's
  first space, i.e. a page column. A one-byte non-space token directly
  before a control or the envelope end is a hidden terminal slot (`,`
  before SRETBL in QSYSINFO APPENDIX1.4.1.1, `?` before CFONT in
  SC24-5527-02) and is never content.
- **Line boundaries** are certain when a control lies between the previous
  content and the origin, when two space runs meet (line fill plus origin),
  when the marker slots are box glyphs, or when a control-only prefix token
  precedes a glyph marker (a paragraph break; the hosted page shows a blank
  line). A one-byte token followed by a single space run is ambiguous and
  opens a new line only when the run's length is an already-established cell
  start column and the in-line reading is not.
- **Cell boundaries.** A column is a cell start when some line starts a word
  there and *every* line of the envelope leaves the two preceding columns
  blank (a space run of >= 2 columns, empty cells allowed). A table needs at
  least two cells, and every content word must lie inside exactly one cell.
- **Header** is the first row when typed CFONT provenance sets every visible
  word of every one of its display lines in a bold face (`highlight_2`,
  `highlight_3`, `bold_phrase`). Italic and other known styles no longer
  qualify: SC24-5520-00 3.8.1.10.2/3.8.1.11 render their first rows as
  `<I>VM architected area starts here</I>` on the hosted page, which is
  emphasis in a body row. This tightening also corrects five box tables that
  the previous sweep called headers (TBLUNIQ110-114).
- **Caption** is a leading single-cell row cut off by a paragraph break
  (SC33-033 4.6 `CHAATT      (count, array)`).
- **Rows.** Paragraph breaks separate line groups. Inside a group whose
  first line has first-cell content, every such line starts a row and lines
  with an empty first cell extend it; a group whose first line has an empty
  first cell is one vertically centred row (the SC24-5527-02 `vmfbld`
  command/explanation rows).
- **Fail closed** on ragged gaps (`cell text has an unaligned gap`), a
  single column, a single display line, a line with no origin, a line wider
  than the page, and any word no cell owns.

## Sweep

Same scratch tool and corpus as the box sweep: every TOC topic of every
fixture whose typed structure has an SRTBL control, full row range, then
`verify_fixed_table_blocks_ir` and `verify_fixed_table_document_ir`.

- SRTBL envelopes: 1,896 over 1,268 topics (unchanged).
- Admitted: **677 -> 1,025** (35.7 % -> 54.1 %); declined 1,219 -> 871.
- Topics with every envelope admitted: 464 -> **668**; partial 59 -> 133;
  none 745 -> 467.
- No admitted envelope was lost. 672 of the 677 previously admitted blocks
  are byte-identical; the other five are the SC24-5520-00 header correction
  above.
- 0 block-verifier and 0 lowering failures on admitted blocks. The 92
  `VERIFY_FAIL` topics are unchanged (conflicted Ownership IR, refused by
  design).
- Of the 348 newly admitted envelopes, 267 are two-column, 30
  three-column, 50 four-column, 1 five-column; 95 carry a header and 102 a
  caption.

Per book (admitted / declined envelopes), books that moved:

| Book | Before | After |
|---|---|---|
| SC33-033 | 5/154 | 136/23 |
| SC24-5527-02 | 25/467 | 149/343 |
| QSYSINFO | 1/61 | 58/4 |
| GG24-395 | 3/32 | 14/21 |
| ACPZMST1 | 15/83 | 21/77 |
| SC28-1881-05 | 6/9 | 10/5 |
| SG24-204 | 1/8 | 4/5 |
| SC09-138 | 78/47 | 80/45 |
| N2AH1MST | 5/13 | 7/11 |
| GG24-4302-00 | 6/6 | 8/4 |
| IEAC6MST | 18/12 | 19/11 |
| SC31-711 | 4/5 | 5/4 |
| SC34-425 | 8/6 | 9/5 |
| SC26-457 | 62/5 | 63/4 |
| SC24-5520-00 | 161/113 | 162/112 |
| SC24-546 | 9/10 | 10/9 |

Remaining decline reasons (envelopes):

| Reason | Envelopes | Representative topics |
|---|---:|---|
| visible source between table lines | 193 | ACPZMST1 PREFACE.3, ACPZMST1 2.1.3 |
| gap table has a single display line | 175 | SC24-5527-02 2.3.2, SC24-5527-02 3.4 |
| source ownership is conflicted | 161 | ACPZMST1 2.1.2, GC23-046 3.2 |
| rule junctions do not align with the column boundaries | 76 | DREICMST 2.1.3, GC28-183 2.2 |
| gap table has a single column | 64 | ACPZMST1 7.2, GG24-395 2.3.2.1 |
| box has a single column | 48 | ACPZMST1 2.1.1 |
| cell text has an unaligned gap | 40 | GC28-183 1.3.1, GG24-395 3.3.3 |
| table line does not end with a matching border | 31 | ACPZMST1 1.2.4.4 |
| box glyph inside a table cell | 23 | SC09-138 3.5 |
| table line has no display position | 18 | ACPZMST1 4.0, IEAC6MST 2.2 |
| content line separators do not align | 16 | GG24-4302-00 6.7 |
| gap table line exceeds the page width | 11 | GG24-395 3.2.2, GG24-395 3.3.1 |
| others (no border, no origin, unclosed, no rows, width) | 15 | GG24-395 3.2.3, SC24-5527-02 4.2.3 |

The `single display line` and `single column` declines are mostly genuine
non-tables: SC24-5527-02 wraps single command lines and message listings in
SRTBL envelopes too, and those have no second cell to prove. The 193
remaining `visible source between table lines` envelopes are box tables
whose rules were interrupted, not gap tables.

## Hosted comparison

Cell for cell against the hosted BookServer pages (latin-1, `python3
urllib`), 10 admitted tables over 4 books:

| Book | Topic | Object | URL DT | Result |
|---|---|---|---|---|
| QSYSINFO | APPENDIX1.4.1.1 | TBLUNIQ4 | 19910524120827 | exact |
| QSYSINFO | APPENDIX1.4 | TBLUNIQ3 | 19910524120827 | exact (incl. the wrapped `Release 1` line) |
| QSYSINFO | APPENDIX1.4.1.2 | TBLUNIQ5 | 19910524120827 | exact |
| SC31-711 | GLOSSARY | TBLUNIQ7 | 19941010174546 | exact |
| SC33-033 | PREFACE.6 | TBLUNIQ1 | 19930422134757 | 2 of 27 lines drop a trailing comma (see below) |
| SC33-033 | 4.6 | TBLUNIQ4 | 19930422134757 | exact, caption included |
| SC33-033 | 4.6 | TBLUNIQ5 | 19930422134757 | exact |
| SC33-033 | 4.7 | TBLUNIQ6 | 19930422134757 | exact, caption included |
| SC24-5527-02 | 3.8.4.2 | TBLUNIQ98 | 19921218151459 | exact (vertically centred command cell) |
| SC24-5527-02 | 2.2 | TBLUNIQ24 | 19921218151459 | exact (revision bars structural, empty cell kept) |

Known difference, pre-existing and not specific to the gap geometry: a
source cell that the Layout IR never positioned does not appear in the
cell's `line.text`, though the block still claims it in
`unpositioned_cells` and the verifier checks it against the opaque source
ledger. In SC33-033 PREFACE.6 this drops the trailing `,` of `GDDM Base
Application Programming Reference,` and `GDDM System Customization and
Administration,`. 85 of the 348 newly admitted envelopes carry such words.

Against the legacy Markdown (`boorender <boo> <topic> --md`) for the same
ten tables: eight render only a `[Table: TBLUNIQ*]` placeholder, so the
typed block recovers content the legacy path drops entirely. SC31-711
GLOSSARY is identical in both. SC24-5527-02 2.2 is where the legacy path is
actively wrong: it pulls revision bars into a first column (`| \| | VMSES |
6VMVMK20 |`), shifts the SFS rows one column left, and flattens the
`VMFMRDSK` gap table into a single bogus row; the typed block gets all
three right.
