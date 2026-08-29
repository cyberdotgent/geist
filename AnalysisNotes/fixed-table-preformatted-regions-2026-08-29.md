# Preformatted SRTBL regions, 2026-08-29

Scope: the fixed-table block (`libgeist/src/fixed_table_block_ir.cpp`,
`fixed_table_document_lowering.cpp`) and the ~600 prose topics the prose
composer refused because one of their `SRTBL` envelopes declined. Tracker:
issue #58; predecessors `fixed-table-block-sweep-2026-08-28.md` (box
geometry, 677 envelopes) and `gap-table-block-sweep-2026-08-28.md` (gap
geometry, 1,025 envelopes).

## What the remaining declines turned out to be

The five decline classes named in the census (`visible source between table
lines` 153 topics, `gap table has a single display line` 75, `rule junctions
do not align` 69, `gap table has a single column` 65, `table line does not
end with a matching border` 32, plus `visible token inside the table region
is claimed by no block` 41) are **not one shape**. Sampling them with
`bootrace <book> <topic> --ir` and a scratch line dump showed at least five:

| Shape | Example | Evidence |
|---|---|---|
| a caption line inside the envelope, after the bottom rule | SC09-138 7.5.2 `TBLUNIQ116` | record 1178 tokens 0-15 `Data Types Supported between C and PL/I without Using Pointers`, hosted DT=19910321130500 prints it under the box |
| a single command line wrapped in `SRTBL` | SC24-5527-02 3.6.2 `TBLUNIQ47` | hosted DT=19921218151459 serves `<a name="TBLTBLUNIQ47">   <B>vmfrec</B> <B>ppf</B> <B>esa</B> <B>vmses</B></a>` -- one line, no second cell |
| a console message listing | SC24-5527-02 3.8.4.2 `TBLUNIQ99` | 26 hosted `<pre>` lines including blank ones, no column ever blank in every line |
| a form whose answer cells are ruled off with horizontal box words | SC31-711 2.4.1-2.4.4 | hosted DT=19941010174546 prints `\|     ______________________ \|` inside the cell, and keeps the unresolved `&ballot.` macro of 2.4.4 |
| framed prose (`box has a single column`) | ACPZMST1 2.1.1 `TBLUNIQ3` | hosted DT=19920319123146 prints the one-cell box around `cscmd` |

The single fact all of them share is the one the drawn-figure slice
established for `SRFIG`: **a record payload is a sequence of `<length
byte><that many bytes of tokens>` display lines** (`display_lines.hpp`,
`Format/logical-controls.md`). Parsed that way, every one of these envelopes
resolves into exactly the lines the hosted reader prints -- including the
trailing caption, the `cfont ...` operand lines that display nothing, and the
`SRETBL` line. The `.` that the token reader shows after `vmses` in
SC24-5527-02 3.6.2, previously modelled as a "hidden terminal slot", is
simply the arbitrary dictionary spelling of the next line's length byte.

## Preformatted geometry

`FixedTableGeometryIR::preformatted` is a third geometry, tried only after
both column models decline. It is documented in
`libgeist/src/geist/detail/fixed_table_block_ir.hpp`. Admission requires:

1. every record from the `SRTBL` record to the `SRETBL` record parses into
   display lines;
2. the `SRTBL` opcode is the last token of its line and `SRETBL` opens its
   own line (only blank/`,` tokens may precede it), so the region is a whole
   number of lines -- the control *segment* reaches to the next control and
   for a box table swallows the entire envelope, so only the opcode token
   proves the boundary;
3. every interior line that opens a non-text control segment carries none of
   that segment's payload (`cfont 5 1 2 7 4 2 ...` is a whole line and
   displays nothing); a control segment that starts mid-line declines;
4. a `CSELECT` anywhere in the region declines -- its link would have no cell
   to attach to;
5. at least one non-blank line survives after leading/trailing blank lines
   are trimmed;
6. `body`, `caption` and `separator_columns` stay empty; every positioned
   cell of the envelope's layout rows is claimed as `structural_cells`, which
   is what the composer's region check and the block verifier consume.

It lowers to `AnchorBlockIR{"TBL<object>"} + PreformattedBlockIR`, i.e. a
fenced block of the hosted display lines with trailing blanks removed.

Rejected refinement, recorded so it is not retried blindly: allowing a bare
`U+2500` run inside a content line (a fill rule, hosted `_`) makes SC31-711
2.4.1-2.4.4 pass the box model, but the resulting Markdown table is *worse*
than the verbatim region -- it drops the `?` of the questions, drops the
`\xc2\xb0` bullets, and merges eight form rows into one. The verbatim region
is byte-identical to hosted, so the change was reverted.

## Sweep

Same scratch tool and corpus as the two earlier sweeps (every TOC topic of
every fixture whose typed structure has an `SRTBL` control, full row range,
`verify_fixed_table_blocks_ir` + `verify_fixed_table_document_ir`).
Baseline: `main` `f763774`.

- `SRTBL` envelopes: 1,896 over 1,268 topics (unchanged).
- Admitted **1,134 -> 1,803** (59.8 % -> 95.1 %); declined **762 -> 93**.
  1,134 are box/gap tables (byte-identical to the baseline, none lost) and
  669 are the new preformatted regions.
- Topics with every envelope admitted **716 -> 1,183**; partial 144 -> 33;
  none 408 -> 52.
- 0 block-verifier and 0 lowering failures.

Per book (admitted / declined): ACPZMST1 21/77 -> 95/3, DREICMST 8/28 ->
36/0, GC28-183 1/29 -> 30/0, GG24-395 15/20 -> 25/10, PRG1SORT 56/20 ->
74/2, QSYSINFO 58/4 -> 62/0, SC09-138 87/38 -> 119/6, SC24-5520-00 208/66 ->
270/4, SC24-5527-02 162/330 -> 450/42, SC31-711 5/4 -> 9/0, SC33-033 136/23
-> 157/2, SH20-918 7/12 -> 18/1.

Remaining 93 declines, by cause of the *preformatted* attempt:

| Cause | Envelopes |
|---|---:|
| region contains a `CSELECT` | 64 |
| region contains a service-inventory control (`SRVAPPS`, `SRVBLDS`, `SRVREQT`, `SRVRECS`, `SRVDEP`, `SRVDESCT`, `SRLISone`, `SRSPTCHART`) | 23 |
| record does not parse into display lines (length byte at/above the token threshold) | 2 |
| envelope owns no physical rows / is not closed by `SRETBL` | 4 |

## Coverage

`build/bootrace <book.boo> --coverage` over all 34 fixtures:
**4,036 -> 4,103 / 7,362 (54.8 % -> 55.7 %)**. The table declines that block
whole prose topics drop from 602 topics to 65; the other ~535 topics are
released to the *next* rejection in the composer (`font/selector span
exceeds the display line`, `span starts inside a word`, `selector targets a
picture or external link`, `c.cp carries visible payload`, `topic metadata
controls incomplete`), which are the slices other agents hold. Those counts
rise correspondingly, e.g. `font/selector span exceeds the display line`
263 -> 270 and `selector targets a picture or external link` 159 -> 205.

## Verification

- Whole-corpus `boo2git --force`, `f763774` vs this branch: **67 files
  changed, 0 added, 0 removed**, and the 67 are exactly the 67 topics whose
  coverage route changed legacy -> typed. No topic moved the other way.
- Hosted word-level comparison of every moved topic in a book the hosted
  catalog serves (55 topics, 11 books; DTs recovered from the live
  `FINDBOOK` catalog): typed **better on 42, equal on 13, worse on 0**.
- Cell-for-cell: every fenced region of those topics matched against the
  hosted `<pre>` lines -- **55 regions exact** over ACPZMST1, GG24-395,
  N2AH1MST, PRG1SORT, SC09-138, SC24-5527-02, SC31-711 and SG24-204. Seven
  regions could not be compared: the hosted catalog serves a different
  edition of SH20-918, SH12-565, SC24-546 and SG24-204 D.2 under the same
  book id (their topic ids do not resolve and the server returns the title
  page). One further difference is not this slice's: the drawn-box region of
  SC24-5527-02 7.4.3 comes from the prose box family and is emitted with its
  common indent removed.

## Hosted ids used

`ACPZMST1` 19920319123146, `SH20-918` 19910507103243, `PRG1SORT`
19900829171904, `SH12-565` 19921030123419, `SC24-546` 19920529132045,
`N2AH1MST` 19910329000100, `SG24-204` 19971213050822, `GG24-395`
19941215160749, `SC09-138` 19910321130500, `SC31-711` 19941010174546,
`SC24-5527-02` 19921218151459 (recovered from
`bookmgr.exe/FINDBOOK?filter=&SUBMIT=Find`).
