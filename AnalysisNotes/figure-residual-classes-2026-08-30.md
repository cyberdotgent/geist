# The figure model's residual classes, 2026-08-30

Slice of issue #58 aimed at the ~34 topics still on the legacy route because
of `libgeist/src/figure_block_ir.cpp` and `figure_document_lowering.cpp`.
The normative byte-level facts are in `Format/markup.md` (the paragraphs
beginning "A figure caption is a run of display lines", "A `CSELECT` inside
a figure region", "A bare `SRSPT<id>`" and "A figure region may carry
several picture selectors"); this note records the measurements.

Typed-route coverage over the 34 BOO fixtures, `bootrace <book> --coverage`
before (a build of `main` `e50c60f`) and after: **7,032 -> 7,066 of 7,362
(95.5% -> 96.0%)**.  34 topics moved to the typed route, **none moved off
it**, and every book's count grew or stayed equal.

| book | before | after |
| --- | ---: | ---: |
| DREICMST | 369 | 370 |
| FA1PLMM0 | 411 | 414 |
| IEAC6MST | 197 | 201 |
| ITPPIBOK | 251 | 254 |
| PRG1SORT | 204 | 205 |
| QSYSNEWG | 152 | 153 |
| SC09-138 | 518 | 522 |
| SC09-2417-00 | 320 | 322 |
| SC24-5520-00 | 642 | 644 |
| SC26-457 | 350 | 353 |
| SC34-425 | 241 | 244 |
| SH12-565 | 279 | 281 |
| SH20-918 | 191 | 196 |

Rejection classes, before -> after:

| class | before | after |
| --- | ---: | ---: |
| `figure 'X' declined: figure region contains prose row 'X'` | 13 | 0 |
| `figure X declined: drawn figure contains a selector` | 11 | 0 |
| `figure X declined: figure region contains several pictures` | 3 | 0 |
| `figure X declined: figure region contains structural control SRSPT*/SRV*` | 6 | 0 |
| `figure X declined: drawn figure has text after its caption 'X'` | 1 | 0 |
| `figure region declined: figure region contains prose row 'X'` | 5 | 5 |

## 1. "figure region contains prose row" is a caption, not prose (13 topics)

Every one of the 13 anchored cases is the caption's own text, cut in two by
the Layout IR.  Two cuts:

* the row opens at the **marker word in front of the title**.  IEAC6MST
  `1.2` record 50 line 25 is the whole caption
  `   Figure  1-1. Overview of IPCS`, but the ownership ledger reads token
  130 (`Figure`, a one-byte dictionary token followed by a space run) as the
  row's `marker_slot` and the row's text is `1-1. Overview of IPCS.`.
  ITPPIBOK `5.2` record 169 token 192 is the sentence `.` after
  `... Computer System`, and the row after it is
  `The dashed lines               indicate domain boundaries.`;
* the row ends at the **length byte that opens the continuation line**.
  SC09-2417-00 `2.2.4.5` record 236 line 10 ends at `... the Record` and
  line 11 (`              Length`) opens at token 53, whose word 0 is the
  0x2666 placeholder; the classifier read it as an unmapped cell and stopped
  the caption there.

The fix reads the caption off the region's own display lines: the
`Figure <n>.` line plus every following line indented to the title column
(`Format/markup.md`).  A row whose remaining cells all sit on those lines is
caption material whatever the Layout IR made of it, and a display line's
length byte inside such a row is structure -- so the `.` that IEAC6MST
record 50 token 141 spells (the length byte of the `SREFIG` line) no longer
joins the caption text.  Hosted DT `19920124000100` prints
`   Figure  1-1. Overview of IPCS` with no trailing period, and so do we
now; the same correction shows up in the already-typed QSYSNEWG `E.5.2.1`.

The remaining five `figure region declined: ...` are **anchorless**
selectors, a different shape and left open: SG24-204 `4.1.1`, `4.2.1`,
`5.1` are pictures *inline in a prose sentence*
(`Click on the <img ...> button to start the setup process.`, hosted DT
`19971218054640`), and XWEBDEMO `1.0` and SC33-033 `4.40` are a bare
picture selector whose segment payload is the following prose.  Both need an
inline picture in the prose model, not a figure block.

## 2. The caption's provenance was short by its second line (fixed)

Handed over by a tracing slice and confirmed: a drawn figure's caption that
soft-wraps claimed only the first line's tokens.  The wrapped line was
classified as a *body* line, so its words were claimed by the verbatim block
while the caption's slices stopped at the first row.

`bootrace SC24-5520-00.boo 1.1.26 --explain-offset 16706` before:

```
source	logical_record=187	segment=3	tokens=272:273	boo_bytes=255939:255941	text=FINDSPACE
```

-- and nothing after it, although `Operation` is rendered.  After:

```
source	logical_record=187	segment=3	tokens=272:273	boo_bytes=255939:255941	text=FINDSPACE
source	logical_record=187	segment=3	tokens=275:276	boo_bytes=255943:255945	text=Operation
```

`0x3e7c7:0x3e7c9` is exactly the byte range the handover named.  Re-checked
over every preformatted caption of DREICMST `2.20.2.5`, GC23-046 `6.9.3.3`,
GG24-4302-00 `3.2.14`, SC09-138 `8.1.10.5`, SC24-5520-00 `1.1.26` and
`3.8.1.9.1`: **9 caption tails traced to nothing before, 0 after** (25
captions checked, all resolve to their own bytes now).

## 3. "drawn figure contains a selector" (11 topics)

`cselect <column> <length> <target>` inside a figure region covers a span of
the display line it precedes (`Format/markup.md`).  Only two of the eleven
put the link in the **caption** (SH20-918 `2.1` `FIGBDE` and `FIGSTRUC`,
`2.5` `FIGSE`); those become `CrossReferenceInlineIR` inside
`FigureCaptionIR`, and the rendered caption now reproduces hosted word for
word:

```
Figure 5. Basic Document Elements. The tags that identify each element are
in parentheses after the element type. The contents of a paragraph unit are
shown in [Figure 4](<#FIGTITEM>). An implied paragraph is one for which no
:P tag is entered. The contents of a table are shown in
[Figure 8 in topic 2.2.6](<#FIGTABLE>).
```

A caption that carries a link is emitted as plain text rather than
emphasised: a `*` closer that follows a space is not a CommonMark delimiter,
so splitting an emphasised run around a link produces literal asterisks, and
hosted shows no emphasis on a caption anyway.

The other nine put the link **inside the drawn body**, which is character
art reproduced column for column and carries no inline model (it already
drops the `<I>`/`<cite>`/`<samp>` styling hosted shows inside the same
`<pre>`).  Those figures are admitted with the body block marked
`figure-body-cross-reference` degraded, naming each link and its target, so
the loss is reported rather than silent.  The label is taken literally:
SC09-138 `NOTICES` really does select `"Notices" in         |`, nine spaces
and the box's right rule included, and hosted DT `19910321130500` wraps
exactly that in its anchor.

## 4. "several pictures" (3 topics)

SC26-457 `3.2.1`, `B.1.3` and SC34-425 `2.1.2` each carry two book-resource
selectors with one caption; hosted stacks the images and puts the caption
beneath.  The block records the pictures after the first, matches one
`PICTURE n` placeholder to each in source order, and lowers one image block
per picture.  An external image alongside another picture is still declined.

## 5. The tail

* **`SRVPREF` / `SRVMODE` are body text** (SH12-565 `APPENDIX1.9.5.2.1`,
  `APPENDIX1.9.5.3.1`).  The control decoder read a *word* of a drawn line
  as a structural control because it is spelled like one; structural
  segments are now judged by whether the opcode opens a display line, the
  same rule the `C`-controls already used.
* **`SRSPT<id>` is a second anchor** (FA1PLMM0 `H.5`, SC24-5520-00
  `3.8.3.6`, SC34-425 `2.5.3`, `2.5.4`).  Hosted opens it on the line that
  follows the control; Markdown can only place an anchor in front of a whole
  verbatim block, so an anchor that does not open the body's first line is
  marked `figure-body-anchor-position` degraded.
* **`drawn figure has text after its caption`** (PRG1SORT `1.1.4.3.2`) is
  the caption's continuation behind three `SI` index lines; the caption now
  steps over control and index lines and reproduces hosted's two-line
  caption.

## Verification

* Whole-corpus `boo2git --force` over all 34 books, baseline built from
  `main` `e50c60f`: **54 changed files**.  34 are the moved topics; the
  other 20 are already-typed topics this slice corrects --
  * 17 cross references and three `figures.md` entries whose anchor id was
    truncated (`2-1.md#BDE` -> `2-1.md#FIGBDE`, `8-5-4-5.md#FREEC2` ->
    `8-5-4-5.md#FIGFREEC2`, `h-5.md#FIGUNIQ36` -> `h-5.md#FIGFIGUNIQ36`),
    now matching hosted's `<a name="FIGBDE">`;
  * QSYSNEWG `E.5.2.1`, whose caption lost a trailing `.` that was the next
    display line's length byte (hosted DT `19910524085706` prints
    `Figure  E-10. Work Station Function on 3151 ASCII Keyboards without Key
    Pad`).
* Hosted comparison of **all 34 moved topics across 13 books**, word
  multiset against the live BookServer page (boo2git's navigation links and
  render-diagnostic comment excluded, since neither is book content):
  **better 29, equal 5, worse 0**.  Every "equal" page is a superset of
  hosted with nothing missing; the residue is Markdown link machinery
  (`.md` targets, `resource:1.png`, `<a id=...>`).
* `ctest -LE slow`: 50/50.  `ctest -L slow`: 15/15.  Ratchet raised to
  7,066.

## Left open

`figure region declined: figure region contains prose row` (5, the inline
picture selector above), GX27-3999-00 `NOTICES` and `A.0`
(`control line carries payload`), SC09-138 `3.1.9`
(`figure region has no picture selector (unterminated)`).
