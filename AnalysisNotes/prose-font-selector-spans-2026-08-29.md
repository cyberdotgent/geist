# Prose font/selector span model (issue #58) — 2026-08-29

Workflow and hosted evidence for the `CFONT`/`CSELECT` span geometry of the
typed prose family (`libgeist/src/font_span_ir.cpp`,
`libgeist/src/prose_topic_lines.cpp`, `libgeist/src/prose_topic_blocks.cpp`).
The normative format facts derived here are in `Format/markup.md`, sections
"Style-Code Presentation Verified Against Hosted BookServer" and "Spans And
The Display Row"; this note keeps the procedure and the measurement.

## Procedure

1. `build/bootrace <book> --coverage` over all 34 fixtures, grouping the
   rejection column with the same normalisation as
   `normalize_rejection_reason`, to size each cluster.
2. For every candidate rule, positioned evidence first: `bootrace <book>
   <topic> --segments` for the `cfont`/`cselect` operand, `--fonts` for the
   book's `CFONTDEF` table and the decoded span text, and a token dump built
   from `bootrace --all` ownership cells (token id, encoded value, decoded
   word, prefix) to see what the span columns actually address.
3. Every rule confirmed on the hosted reader for at least two books before
   adoption, comparing the exact `<pre>` row and its inline tags.
4. Re-measure with the coverage sweep after each rule; any topic that leaves
   the typed route is treated as a defect, not a trade.

## What each rejection cluster turned out to be

### "non-highlight font code" (40 topics on the baseline)

Two unrelated causes.

- **A `,` separator glued to the style code.** The last operand triple of a
  `CFONT` can end with a prefix-1 comma token, so the decoded operand reads
  `cfont 4 4 R,`.  Byte-level: ACPZMST1 record 6, tokens 42..46 are
  `cfont`/`4`/`4`/`R`/`<prefix 1>,` and token 47 is the four-cell origin run
  of the row `    GUPI`.  Hosted `FRONT_1.1` (DT 19920319123146) serves
  `    <B>GUPI</B>`, identical to what record 7's comma-less
  `cfont 4 4 R 9 3 R` produces for `    <B>GUPI</B> <B>end</B>`.  GC28-183
  record 146 stores `cfont 17 1 E,` for each `.` row of topic `2.2.1`, which
  hosted serves as `                 <samp>.</samp>`.  The comma is therefore
  an operand separator, and `decode_font_control_spans` drops it from the
  final operand word only; a comma anywhere else still fails closed.
- **Codes the style table did not carry.** `Q` (PKDEF) and the underscored
  highlight phrases `5`..`9`.  Hosted renderings, two books each where the
  code occurs twice: `Q` is `<dfn>` (PRG1SORT `2.1.4` `<dfn>*CURLIB</dfn>`,
  SC26-457 `3.4.1.2` `<dfn>LIST</dfn>`), `5` is `<U>` (QSYSNEWG `2.3`
  `cfont 59 3 5` -> `<U>see</U>`, SC34-425 `1.8` `cfont 42 11 5` ->
  `<U>underscored</U>`), `7` is `<B><U>` (QSYSNEWG `5.1.4`, SG24-204 `5.2.1`
  `cfont 33 1 7 34 1 2` -> `<B><U>L</B></U><B>U</B>`), `9` is `<TT><U>`
  (OFCUSEOV `5.2` `cfont 25 1 9` -> `<TT><U>/</TT></U>`).  `HP5`..`HP9` are
  the underscored forms of `H0`, `HP1`, `HP2`, `HP3`, `HP4`; the three
  verified rows match that family exactly.  Markdown has no underscore run,
  so `5`/`6` lower to emphasis, `7` to strong, `8` to strong emphasis and `9`
  to code, consistent with the `CZ` slice's coarser "5..9 keep plain
  emphasis" rule and strictly closer to hosted where the code is verified.

### "font or selector span exceeds the display line" (314 topics)

Three causes, all of them the row model rather than the operand.

- **Row-marker glyphs lost their columns.** The revision-bar/box glyph that
  opens a row was assigned the `marker` role and dropped without a cell, and
  the space behind it was dropped with it, shifting every column of the row
  by two.  Hosted prints the glyph: ACPZMST1 `1.2.3.1` serves
  ` | A local resource ...` for `cfont 5 5 3 11 8 3` (`local` at column 5,
  `resource` at 11) and GG24-395 `PREFACE.3` serves ` | Part 1,
  "Introduction"` for `cselect 3 22 HDRHPRT100`.  The row now keeps blank
  cells for the glyph and its spacing, before `text_begin`, so no text
  changes and the geometry lands.
- **Two-column rows split at the wide gap.** A lone space run of three or
  more cells opened a new row, cutting definition rows in half.  A span of
  the row that still reaches past the cells written so far proves the row has
  not ended: ACPZMST1 `3.6` (`cselect 43 3 SPTUSERID` + `cfont 3 6 2` for
  `   Userid                   User ID (topic 4.3)`) and DREICMST `2.8.1`
  (`cfont 3 3 2 7 4 2 17 3 2 21 4 2` for `   RFT Name      Log Type`).
- **Trailing display padding.** A span can end past the last materialised
  cell because the stored row keeps its trailing padding.  Hosted styles only
  the visible text (ACPZMST1 `8.1` `cselect 3 38 HDRXCCOE` on a 40-column
  row, GG24-395 `PREFACE.3` `cselect 3 22` on a 23-column row), and a
  highlight never continues onto the next row — every wrapped phrase carries
  its own triple per row — so the span end clamps to the row.  A span that
  *starts* past the row still fails the topic closed.

### "span starts inside a word" (221 topics)

- **A one-byte word at the row origin was read as a glued marker slot.**  The
  "glued alphanumeric one-byte word" rule fired for the first visible token
  of a freshly opened row, where nothing precedes it.  ACPZMST1 `6.2` record
  305: `cfont 4 4 R,` + a four-cell origin run + the one-byte word `GUPI` +
  a fill/origin pair; hosted serves `    <B>GUPI</B>`.
- **A styled glyph opening a row was read as the row marker.**  A pending
  span that covers exactly the glyph's own columns proves it is display text:
  FA1PLMM0 `3.5.1` `cfont 5 2 E 8 3 E ...` over `     // JOB COPY ...` is
  served as `<samp>//</samp> <samp>JOB</samp> ...`, GC28-183 `2.2.3`
  `cfont 5 2 E 15 4 E` over `     //        PEND` as
  `<samp>//</samp>        <samp>PEND</samp>`.  Decoder placeholder runs are
  excluded (they carry no character), and so is the `CZ` dialect, whose rows
  carry explicit marker slots (SC09-2417-00 `2.1.3.4`).
- **Genuine mid-word emphasis, left fail-closed.**  Hosted really does style
  part of a word: GC23-046 `6.0` `cfont 43 1 1` -> `SMPWRK<I>x</I>`,
  SG24-204 `5.2.1` -> `<B><U>L</B></U><B>U</B>`.  Admitting it needs a
  sub-token slice in the inline ownership ledger (the ledger currently owns
  whole tokens, and a split word makes two inlines claim one token), so this
  stays a documented residual.

### "font span inside a selector span" (31 topics)

Hosted nests the phrase inside the link (`<a href="8.2..."><B>&quot;Check_On_
Event</B> ... <B>8.2</B></a>`, ACPZMST1 `8.1`).  The typed model keeps the
cross reference, which owns the words, and drops the decoration; a font span
that only partly meets a selector span still fails the topic closed.

### Side effect: the display-line length byte

Fixing the row-origin case exposed one more misreading of the same rule: a
one-byte alphabetic word in the row-control byte range is the *next* display
line's length byte, so it stands at a row boundary.  SC26-457 `3.14.2.8`
record 560 has `(` + `and` + `their` inside a row and hosted serves
`(and their associated entries)`; the slot rule now requires the next token
to be a space run, a row-marker glyph, or the end of the stream, which keeps
the documented N2AH1MST record 17 `to:` + `access` + `/` case a slot.

## Measured

Baseline: main `88cbc81`, built from `git archive 88cbc81 libgeist` into a
scratch tree.  `bootrace --coverage` over every fixture, N2AH1MST included.

- **4,103 -> 4,505 of 7,362 typed topics (55.7% -> 61.2%, +402)**; 402 topics
  moved legacy -> typed and none moved the other way.  26 of the 34 books
  gain; largest: SC26-457 +60, SC09-138 +50, IEAC6MST +37, ACPZMST1 +34,
  QSYSNEWG +30, GG24-395 +28, SC24-5520-00 +25, GC28-183 +21,
  SC24-5527-02 +20.
- Rejection clusters, before -> after: font/selector span exceeds the display
  line 314 -> 88, span starts inside a word 221 -> 116, span ends inside a
  word 79 -> 67, non-highlight font code 40 -> 2, font span inside a selector
  span 31 -> 0, blank span 27 -> 22.

- Whole-corpus `boo2git --force` before/after (N2AH1MST excluded from the
  export, as in earlier runs): **452 changed topic files, 0 added, 0
  removed**.  402 are the topics that moved to the typed route; the other 50
  are topics that were already typed and whose rendering the row-geometry
  fixes corrected.  No topic left the typed route.

## Hosted verification

Two samples, both comparing hosted body words and hosted inline markup
(`<B>`/`<I>`/`<U>`/`<samp>`/`<kbd>`/`<var>`/`<cite>`/`<dfn>`/`<tt>`/`<a
href>`) against the Markdown of both builds, with Markdown syntax and
escaping stripped and anchors (`<a name>`) excluded:

- **59 moved topics across 23 books**: typed better on 54, equal on 5, worse
  on none.  Books absent from the hosted catalog (SC24-5520-00,
  SC24-5527-02, SC28-1881-05, SC09-2417-00, GX27-3999-00, packet) and
  N2AH1MST are excluded from the sample.
- **43 already-typed topics across 15 books whose export changed**: the new
  rendering is closer to hosted on 39 and equal on 4; none regressed.  This
  is the sample that covers the risk of the row-marker column change.

| Difference class | Typed behaviour | Decision |
| --- | --- | --- |
| Adjacent same-style words merge into one phrase (`**user name**` for hosted `<B>user</B> <B>name</B>`) | one span | keep: the prose family's documented convention; tests updated |
| Markdown table header instead of `<B>` header cells (ACPZMST1 6.3.1) | structural | keep: the header is marked by the table syntax |
| Reflow-off page-reference rows join into one paragraph (QS3X36CM 2.0) | word-equal to hosted, row breaks lost | accept: the prose flow model; the same `<p>` hosted serves |
| `HP5`/`HP6` underscore lowered to emphasis, `HP7` to strong, `HP9` to code | Markdown has no underscore | keep: closest available, and the CZ slice already lowers 5..9 to emphasis |
| Mid-word span boundary (`SMPWRK<I>x</I>`) | rejects the topic | fail closed: needs a sub-token inline slice |
| Hosted-only row-marker glyph (`|`) | dropped from text, column kept | keep: unchanged from the prose family |

## Residual span-family rejections

`span starts inside a word` 116, `exceeds the display line` 88 (spans whose
row still begins after the span's first column), `ends inside a word` 67,
`blank span` 22, malformed font control 8, `overlapping font spans` 3,
`font style code` 2 (`W`/`Z`, one book each, no second-book evidence).
