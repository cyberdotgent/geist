# The font-control and span residual cluster (issue #58) — 2026-08-30

Workflow, evidence and measurement for the last three `CFONT` rejection
classes of the typed prose family.  The normative format facts derived here
are in `Format/markup.md` ("Style-Code Presentation Verified Against Hosted
BookServer", "Font And Highlight Controls", "The three-column left margin")
and `Format/logical-controls.md` ("A Metadata Opcode In The Body Is A
Display-Line Length Byte"); this note keeps the procedure, the measurement and
the residuals.

Follow-up to `prose-font-selector-spans-2026-08-29.md`,
`prose-subtoken-spans-2026-08-29.md` and
`prose-row-left-margin-2026-08-29.md`.

## Starting point

`build/bootrace <book> --coverage` over all 34 fixtures at `origin/main`
`475af08`: **6,986 / 7,362 typed**.  The three classes this slice owns:

| Rejection | Topics |
| --- | ---: |
| `font control rejected: segment is not a well-formed font control` | 18 |
| `font/selector span [a,b) exceeds the display line of N cells` | 13 (+2 `is blank`) |
| `font style code 'X' is not a highlight phrase` | 11 |

## Procedure

1. Coverage sweep over all 34 books, grouping the rejection column with the
   same normalisation as `normalize_rejection_reason`, to size each cluster.
2. Positioned evidence first, never the flattened string: `bootrace <book>
   <topic> --lines` for the record's display-line structure and per-column
   class, `--tokens` for encoded value/width/byte range of every token,
   `--segments` and `--all` for the segment ranges and the `malformed` flag,
   `--fonts` for the book's `CFONTDEF` table and the decoded span text.
3. A **corpus-wide font-span census** was the decisive new tool for the style
   codes: `bootrace <book> <topic> --fonts` over all 7,430 catalog entries
   (317,684 spans) gives every code that occurs anywhere, so "is there a
   second witness?" is answered by measurement rather than by the rejection
   list, which only reports each topic's *first* failure.
4. Every rule hosted-checked on at least two books before adoption.
5. Re-measure with the coverage sweep after each rule; any topic leaving the
   typed route is a defect, not a trade.

## What each class turned out to be

### "segment is not a well-formed font control" (18) — a display-line length byte

Every one of the 18 has a `cfont`/`CFONT` segment whose operand is empty.
The opcode is not an opcode: it is the **length byte of the next display
line**, and the dictionary happens to spell that byte `cfont`.  This is the
documented pattern of `Format/logical-controls.md` §"A Metadata Opcode In The
Body Is A Display-Line Length Byte", which had been applied to the
object-scope opcodes and to the metadata ones but explicitly *not* to `font`,
because a blanket demotion costs seven topics and prints `cfont` inside
SC31-711 5.0's message texts.

The operand makes the distinction exact and costs nothing: a genuine `CFONT`
carries at least one complete `<column> <length> <code>` triple; a length byte
that spells `cfont` is followed by the line's display text.

Byte-level, three books:

* **SC24-546 record 79** spells both readings four display lines apart.
  Token 108 (value 44) is the length byte of display line 3 and token 109
  (value 59) the genuine opcode of `cfont 13 2 X 17 2 X … 67 3 X`; token 149,
  the *same* encoded value 59, is the length of display line 4
  (`             ¬<  ¬=  ¬==  >>  <<  >>=  \<<  ¬<<  \>>  ¬>>  <<= /=  /==`)
  and carries no triple at all.  Bytes `0x16bcb` and `0x16bf7`.
* **N2AH1MST record 89** spells them adjacently: tokens 31 and 32 are both
  encoded value 37 at bytes `0x19460`/`0x19461`, the first the length of
  display line 4 and the second the opcode word of `cfont 7 8 P 16 2 P …`.
* **OFCUSEOV record 276** token 0 (value 59, byte `0x1ed04`) is the length
  byte of the 59-byte first row of a drawn calendar box,
  `       |   10/23/89       10/24/89       10/25/89 …`.

The prose stream already has the machinery for this — the `default:` case
re-emits such a segment's tokens so the display-row pass gives the byte its
row-control slot — so the fix is the same treatment in the `font` case,
gated on the three conditions above (`libgeist/src/prose_topic_stream.cpp`).

**18 -> 0.**  15 topics reach the typed route; 3 fall to another fail-closed
class (OFCUSEOV `1.20` "token belongs to no block", SC33-033 `D.0` a
placeholder run, SH12-565 `APPENDIX1.10` the `W` style code, which this slice
then fixed).

### "font style code X is not a highlight phrase" (11) — two unrelated causes

**`W` and `G` are the GML warning block, and the book names them.**  Every
book's `CFONTDEF` table defines `W` = `WARNING` and `G` = `WARNINGTEXT`, and
the table is byte-identical in all 34 fixtures.  `W` styles the block's
`Warning:` lead, `G` every word of its body.  Hosted renders both `<em>`, and
the block is anchored `<a name="WRN">`.  Four books, read off the hosted page:

| Book / topic | Operand | Hosted |
| --- | --- | --- |
| SC26-457 `1.6.5` | `cfont 3 8 W 13 2 G 16 3 G 20 7 G …` | `<em>Warning:</em>  <em>If</em> <em>the</em> <em>generic</em> …` |
| SC31-711 `3.3` | (record 94) | `<em>Warning:</em>  <em>Do</em> <em>not</em> <em>modify</em> … <em>impaired.</em>` |
| SH12-565 `FRONT_1.1` | (record 20) | `<em>Warning:</em> <em>Do</em> <em>not</em> <em>use</em> <em>this</em> <em>Diagnosis,</em> …` |
| GC23-046 `6.9.3` | (records 141-142) | `<em>Warning:</em>  <em>Do</em> <em>not</em> <em>use</em> <em>the</em> <em>ISPF</em> <em>LIBDEF</em> …` |

The corpus census finds the two codes in exactly those four books: 7 `W`
spans, 363 `G` spans.  They have to be adopted together — `W` opens the block
and `G` carries it, so admitting only `W` moves SC26-457 `1.6.5` from one
rejection to the next.

**`200` and `(results` are not codes at all**: they are the row's own display
text, eaten by an operand walk that only checked the first two words of each
triple for decimal digits.  SC33-033 record 493 segment 12 is
`cfont 17 3 E 26 3 E 35 3 E 44 3 E` over the row
`                      190      195      200      205`, and FA1PLMM0 record
655 segment 4 is the same shape over
`                    3350    30      (results in 1410 entries)`.  Requiring
the third word to be a `CFONTDEF` code stops the walk where the operand ends
(`libgeist/src/control_ir.cpp`, `font_code_word`).

The corpus census also lists every other over-run this repairs, all of them in
topics that were rejected for other reasons: `-bit` (SC24-5520-00 `6.11.9.3`),
`yes` (SC33-033 `4.108`), `----` (SC34-425 `2.5.10`), `/2"` (QSYSINFO
`2.1.138`), `927` / `3.2.4` and six bare numbers in PRG1SORT `1.5.2.1`.

**Left fail-closed for want of a second witness:** `Z` (`PVDEF`) occurs in one
topic of one book — SC26-457 `1.3`, `cfont 3 7 P 11 10 Z 22 4 P 27 1 P 29 10 Z
39 1 P`, whose hosted row is `<kbd>COMMAND</kbd> <dfn>parameters</dfn>
<kbd>....</kbd> <kbd>[</kbd> <dfn>terminator</dfn><kbd>]</kbd>`, so `PVDEF`
presents there exactly as `PKDEF` does — and `_` (`UNDERSCORE`) in one topic
of one book, SC24-5527-02 `COMMENTS`, which the hosted catalog does not serve
at all.  The corpus records repeatedly that identical geometry means different
things in different books, so both stay unknown.  **11 -> 1.**

### "exceeds the display line" (13) — two different row-geometry gaps

**Five title pages: two leading space runs.**  The row model reads a length
byte followed by *one* space run as `<slot> <origin> <text>`, and everything
else as the fill/origin pair that opens the next row.  A title page stores its
line as `<length byte> <63-cell run> <small run> <word>`, so the row keeps only
the small run and the operand's column lands past its end.  The margin is not
inferred, it is read: the display line's own cells put the first word at the
column the `CFONT` names.

| Book / record / line | Stored | Operand | First word at |
| --- | --- | --- | ---: |
| SC24-546 record 3 line 17 | token 83, 63-cell run (84), 3-cell run (85), `Release` (86) | `cfont 66 7 2 74 3 2` | 66 |
| N2AH1MST record 2 line 10 | token 33, 63-cell run (34), 7-cell run (35), `MVS` (36) | `cfont 70 7 2` | 70 |
| IBMMMSTR record 2 line 12 | token 57, 63-cell run (58), 2-cell run (59), `Programming` (60) | `cfont 65 12 2` | 65 |

The rule generalises `change_bar_margin_line`, which already measured exactly
this for a revised row, by dropping its "there is exactly one change bar"
condition and adding three of its own: two or more runs, a pending span
opening at the line's first-word column, and the flattened dialect.  All three
were forced by measurement — the unguarded form re-indents the verbatim rows
of SC09-2417-00 `4.1.9.4`'s `cz OFF XMP` COBOL listing by ten columns.
**Moves the five `TITLE` topics and SC33-033 `A.3.1`.**

**Measured and reverted: the fill/origin pair inside one display line.**  The
other seven were rows cut at a wide in-row gap stored as two space runs.  The
documented rule "a span of the open row that reaches past the cells written so
far proves the row has not ended" (`Format/markup.md`, "A span holds its row
open") does recover them — SC33-033 record 872 display line 18 is the
85-column FORTRAN comment row
`     C*                                   …                     *  02100000`
under `cfont 5 2 E 74 1 E 77 8 E`, cut after `C*` — but exporting all 34
fixtures with and without the guard moves exactly six topics (SC33-033
`A.3.2`..`A.3.7`) and **every one of them comes out worse**: the recovered
rows are all `E`-styled, so the reflow joins the whole listing into a single
inline code span where hosted serves a `<pre>` of 120 lines and the legacy
route keeps one row per paragraph.  It is also a one-book rule.  Reverted,
with the reason recorded in `prose_topic_lines.cpp`.

## Recorded regression

**SC33-033 `A.3.1`** moves to the typed route through the margin rule and
suffers the same reflow: its COBOL listing, every word `E`-styled, becomes a
handful of long inline code spans instead of 345 one-row paragraphs.  No word
is lost — against hosted the only two word differences are the comparator's
tokenisation of `I-1)*10` — and the spurious words the legacy route emits are
gone, but the line structure is.  This is the fixed-layout family's
`typed-degraded` gap, not a span defect; it disappears when `SRTBL`/`XMP`
regions render verbatim.

## Measured

Baseline: `origin/main` `475af08`, built from `git archive origin/main
libgeist` into a scratch tree.  `bootrace --coverage` over every fixture,
N2AH1MST included.

* **6,986 -> 7,015 of 7,362 typed (94.9% -> 95.3%, +29)**; 29 topics moved legacy ->
  typed and **none moved the other way**.  13 of the 34 books gain:
  SC24-546 +4, IBMMMSTR +4, ITPPIBOK +4, SC26-457 +3, N2AH1MST +3,
  SC09-2417-00 +3, SH12-565 +2, FA1PLMM0 +1, GC23-046 +1, OFCUSEOV +1,
  SC24-5520-00 +1, SC31-711 +1, SC33-033 +1.
* Rejection classes: malformed font control **18 -> 0**, font style code
  **11 -> 1**, span exceeds the display line **13 -> 8**, span is blank
  2 -> 2.
* Whole-corpus `boo2git --force` before/after: **36 changed files, 0 added,
  0 removed** — the 29 moved topics plus **7 whose only change is the recorded
  rejection reason in the render-diagnostic comment** (OFCUSEOV `1.20`,
  SC33-033 `4.77`/`4.90`/`4.98`/`A.3.4`/`A.3.5`/`D.0`, each of which now fails
  closed in a different class).  **No already-typed topic's content changed.**

## Hosted verification

Word- and markup-level comparison of hosted body text against the exported
Markdown of both builds, with Markdown syntax/escaping stripped, `<a name>`
anchors and `<Hn>` headings excluded, and hosted inline tags mapped to the
three Markdown emphasis kinds (`<B>`/`<STRONG>` strong; `<I>`/`<EM>`/`<U>`/
`<CITE>`/`<VAR>` emphasis; `<SAMP>`/`<KBD>`/`<TT>`/`<DFN>`/`<CODE>` code).

**28 of the 29 moved topics, across 12 books** (SC24-5520-00 is absent from
the hosted catalog): typed **better on 22**, mixed on 5, worse on 1.  Each
non-better case was read:

| Topic | Metric | Reading |
| --- | --- | --- |
| N2AH1MST `PREFACE` ("worse") | words 89 -> 105 extra | comparator artefact: the topic's own footnote `<hr>` truncates the hosted side.  Typed restores the whole `Book`/`Message Prefixes` table and the footnote that legacy replaced with `[Table: TBLTBLUNIQ3]`, and adds the `*System Messages*` citation. |
| IBMMMSTR `PREFACE.3` | marks 65 -> 162 | the figure now renders as a `fixed-table-verbatim` fence whose rows are **character-identical to hosted**, including hosted's own `Evaluati\|nGeneral` cell overlap; hosted marks each title `<cite>`, the fence marks the block.  Documented class "verbatim block rows hosted marks per word -> one fenced block". |
| OFCUSEOV `1.9` | marks 982 -> 1041 | same class: five `prose-drawn-box-verbatim` fences replace legacy's flattened prose. |
| ITPPIBOK `2.4.3.3` | marks 79 -> 86 | comparator artefact (`*` inside a code span toggles emphasis in the Markdown side).  Typed drops legacy's interpolated `interface`/`application`/`as`/`ABOUT` words entirely. |
| SC24-546 `2.1.3` | words 1859 -> 1876 extra | mid-topic `<hr>` truncation again; marks improve 1016 -> 553 and typed drops legacy's duplicated paragraph and its leaked `, SRSPTREF11`. |
| SC33-033 `A.3.1` | marks 1082 -> 2548 | the recorded regression above. |

The warning-block topics are the sharpest result: on SH12-565 `FRONT_1.1` the
typed route now marks **exactly** the 14 words hosted marks `<em>`
(`Warning: Do not use this Diagnosis, Modification, or Tuning Information as a
programming interface.`) where the baseline marked none; SC26-457 `1.6.5`,
SC26-457 `3.21.1.2`, SC31-711 `3.3`, IBMMMSTR `PREFACE.4`/`PREFACE.6.9`,
FA1PLMM0 `16.1.4`, SC09-2417-00 `3.3.2.7`, SH12-565 `APPENDIX1.10` and
GC23-046 `6.9.3` all end with **zero** markup differences against hosted apart
from the `<cite>© Copyright IBM Corp …</cite>` footer no exporter emits.

Hosted DTs are the table in `prose-display-line-rows-2026-08-29.md`.
SC24-5520-00, SC24-5527-02, SC28-1881-05, GG24-395, XWEBDEMO and packet are
absent from the hosted catalog and excluded from the sample.

## Residual span-family rejections

**8** `font/selector span … exceeds the display line`, **2** `is blank`,
**1** `font style code`:

* SC33-033 `A.3.2`..`A.3.7` (6) — the fill/origin pair inside one display
  line, measured and reverted above.
* SC34-425 `2.5.6` — not a span problem: the SCLM type names `SRC1`/`SRC2`/
  `SRC3` in an example are classified as `SR<id>` structural anchors and their
  rows are dropped, so the surviving row starts at `FLMTYPE` and
  `cfont 22 4 E 29 7 E 37 11 E` no longer fits.  This is the documented
  "text segment begins with control-like word" residual, whose remaining cases
  all *open* their display line.
* IEAC6MST `4.8.2` — the row is 69 cells where the display line is 71: the
  trailing unmapped placeholder glyph the span at column 70 covers carries no
  display cell.
* SC24-5527-02 `6.6` — book absent from the hosted catalog, undiagnosed.
* FA1PLMM0 `3.2.3`, SC26-457 `1.2` (`is blank`) — unchanged in kind from
  `prose-subtoken-spans-2026-08-29.md`: the span covers only the trailing
  padding of a row fragment the model split off.
* SC26-457 `1.3` — the `Z` style code, fail-closed for want of a second book.
