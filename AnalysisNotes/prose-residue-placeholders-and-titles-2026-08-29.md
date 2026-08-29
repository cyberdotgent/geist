# The prose residue: placeholders and `ST` titles (issue #58) — 2026-08-29

Workflow, hosted trail and residual for the slice that took the prose
family's placeholder and `ST`-title rejection classes down.  The normative
format facts derived here are in `Format/logical-controls.md`, sections
"A Box-Drawing Run Inside A Row Is Display Text Too", "The List Bullet May Be
A Two-Byte Token" and "An `ST` Control With No Payload Is An Empty Title".

## Starting point

`build/bootrace <book> --coverage` over all 34 fixtures at `main` `8343096`:
**6,544 / 7,362 typed**.  The four classes this slice owns:

```
placeholder run '?' is followed by visible text at record N token M   81
text segment begins with control-like word 'X' in record N            73
ST title was never completed                                          26
visible text precedes the ST title                                    13
placeholder glyph '?' inside prose text at record N token M           10
```

`main` moved to `e61da6d` while the slice ran (the generated TOC/INDEX family
and the `CZ` regions/body-control slices landed); every "Measured" figure
below is against that merge point, where the same classes read 92 / 28 / 13 /
15 and the baseline is **6,762**.

## Tooling

`bootrace <book> <topic> --lines` was added for this slice.  It dumps every
display line of every record of the topic: index, the length byte's token and
encoded value, the line's token range, the hosted display text
(`display_line_text`) and a **per-column class string** — `.` space, `B` box
drawing `U+2500`–`U+25FF`, `?` the decoder's unmapped word `U+FFFF`, `x` any
other word.  None of this slice is decidable from `--segments`, `--records` or
`--tokens`: the class string is what separates a drawn rule that opens a row
from a rule glued into a text run, and it does so without reading the
flattened string at all.  Example, the shape the whole placeholder residue
turned on:

```
44  line=17 prefix_token=85 length=19 tokens=[86,99) cols=63
    class='.......xxx.xxBBB.xxxxxx.xxxxxxxxx.xxx.xxxxxxxxx.xx.x.xxxxxxxxxx'
    text='       The >>___ symbol indicates the beginning of a statement.'
```

## What the display lines proved

### The placeholder classes

1. **A box-drawing run with a displayed word in front of it on its own
   display line is that row's text.**  This is the rule
   `demote_display_line_owned_controls` already applies to control-shaped
   words, read for box runs.  Four books, four shapes: the inline `>>___` of a
   syntax-diagram description (SC24-546 record 44 line 17), the main path of a
   railroad diagram (SC09-2417-00 record 715 line 26, inside `cz OFF SYNTAX`),
   SC33-033's ASCII-dash interface fence (record 75 line 14, whose first and
   last cell are drawn and whose dashes between them are ASCII), and
   SC24-5520-00 record 45 line 12's `    <-________ 4 bytes _____->` caption.
   The word in front must be *displayed*: a control opcode is a visible token
   of its line and draws nothing, which is what keeps ACPZMST1 record 284
   token 28 the documented `ST` title marker slot.
2. **The list bullet may be a two-byte dictionary token.**  QS3X36CM record 7
   token 81 is value 56323 width 2, one word `U+2666`; IBMMMSTR record 44
   token 145 is value 46595 width 2.  Both open list rows hosted serves
   verbatim.  The one-byte restriction only ever protected the decoder's
   unmapped word, where a width-2 `?` really is a question mark.
3. **A subject-index display line inside a drawn box does not interrupt the
   box.**  Hosted carries no `SI` bytes at all, so an `SI` line between two
   rows of a box is no more an interruption than a `cfont` control line is.
   QSYSNEWG record 80's `What Are Entry Fields?` box holds `SI field, field
   keys` and `SI keys, field`; IEAC6MST record 63's `Performance
   Consideration` box holds two more.  The skipped line's words are lowered as
   index terms and its tokens take the index roles, so the block-conservation
   check still refuses to claim an unprinted word as prose.

### The `ST` classes

4. **An `ST` control with no payload token is an empty title**, not a broken
   one.  SC09-138 record 1228: `csourcefn EDCUPRAG`, boundary, `ST`
   (token 25), boundary, `cfont 3 5 E` whose payload is a three-cell origin
   and the word `chars`.  Hosted DT 19910321130500 heads the topic
   `<H3> 8.1.1.1 </H3>` and opens the body `   <samp>chars</samp>`, while the
   book's CONTENTS lists the topic as `chars` — the catalog string is the
   book's separate projection, so the positional corroboration the
   ST-title-mismatch slice introduced has nothing to compare.  The heading is
   the topic identity prefix alone, with no trailing separator.  Checked the
   same way on 2.1.1.7, 4.1.1, 4.1.2, 8.1.1.2, 8.1.1.5 and 8.7.2.1.
5. **A length byte in front of the `ST` is not display text.**  SC26-457
   record 549 token 0 is the first display line's length byte, encoded value
   39, spelling `'`; hosted DT 19911220191142 heads 3.14.2.3 with no
   apostrophe.  (This is the same corruption the "corrupt catalog titles"
   residual records for IBMMMSTR 1.6 and 1.12, seen from the other end.)
6. **An `SR<id>` word alone on its display line in front of the `ST` is the
   envelope anchor.**  It reaches the body stream only because the *next*
   line's length byte is glued to the opcode word, so `classify` sees
   `SRHDRPCHECK.` and refuses the identifier.  SC09-138 record 1229 display
   line 8 is exactly `SRHDRPCHECK` and line 9 exactly `ST`; hosted serves
   `<a name="HDRPCHECK"><H3> 8.1.1.2 </H3></a>`, and likewise 4.1.1
   (`HDRETOHEAP`), 4.1.3 (`HDRETOSIZE`), 8.1.1.5 (`HDRENVIRON`).

Findings 4–6 occur in one book each.  A sweep of every legacy topic of the 34
fixtures (818 at the slice's own baseline, `bootrace --segments` per topic)
found the empty-`ST` shape in SC09-138 only, 40 topics; the false hit
SC09-2417-00 3.3.1.7 is a second `ST` control, a different class.  Each of the
three is a direct reading of a rule already proven on many books — the length
byte, the envelope anchor variant — rather than a new rule of its own.

## Implementation

* `libgeist/src/display_lines.{hpp,cpp}` — `format_display_line_ir`, the
  trace line behind `bootrace --lines`.
* `libgeist/src/geist/document.hpp`, `libgeist/src/document.cpp`,
  `libgeist/examples/bootrace.cpp` — the `--lines` mode.
* `libgeist/src/prose_topic_lines.cpp` — `display_word_precedes_in_line`, the
  generalised box-run branch, the two-byte bullet, and the boxed index lines.
* `libgeist/src/prose_topic_boxes.cpp` — `index_line`, and `BoxRegion` now
  carries the index lines it skipped.
* `libgeist/src/prose_topic_stream.cpp` — the empty `ST` control, the
  length byte in front of the title, and the pre-title envelope anchor.
* `libgeist/src/prose_topic_ir.cpp`,
  `libgeist/src/prose_topic_document_lowering.cpp`,
  `libgeist/src/topic_document_lowering.cpp` — an empty title skips the
  positional corroboration, takes the control's own tokens as provenance, and
  lowers to a heading that is the identity prefix alone.

## Hosted DTs used

The table in `prose-display-line-rows-2026-08-29.md`.  SC24-5520-00,
SC24-5527-02, SC28-1881-05, GG24-395, XWEBDEMO and packet are absent from the
hosted catalog and are excluded from every sample.

## Measured

Against `main` `e61da6d`:

* `bootrace --coverage` over all 34 books: **6,762 → 6,843 of 7,362
  (91.9 % → 93.0 %, +81)**.  No book regressed and no topic moved
  typed → legacy at any step of the slice.  Per book: SC09-138 +40,
  IBMMMSTR +11, SC24-5527-02 +6, SC24-5520-00 +5, SC24-546 +4, QSYSNEWG +3,
  SC31-605 +3, GC23-046 +2, IEAC6MST +2, and +1 each in FA1PLMM0,
  GG24-4302-00, ITPPIBOK, QS3X36CM and SC26-457.
* Rejection classes: `placeholder glyph inside prose text` **15 → 0**,
  `ST title was never completed` **28 → 0**, `visible text precedes the ST
  title` **13 → 0**, `placeholder run followed by visible text` **92 → 60**.
* Whole-corpus `boo2git --force` before/after (the baseline built from
  `git archive origin/main` in a separate build directory, so it does not pick
  up the rebuilt library): **88 changed files, 0 added, 0 removed** — the 81
  moved topics plus **7 already-typed topics the slice corrects**: FA1PLMM0
  11.3.1/15.3/15.4/FRONT_1 and SC33-033 3.0/4.132/PREFACE.1, each of which
  gains the one blank column of the closing `U+2500` of its interface fence,
  matching hosted's own trailing blank cell.

## Hosted verification

Character-level: both sides reduced to their alphanumeric character sequence,
so a word hosted splits at markup (`<samp>page</samp>` inside a drawn rule)
compares equal to the same characters glued in the export.  Verdict is the
higher `difflib` similarity ratio.

* **68 servable moved topics across 12 books** (`FA1PLMM0 GC23-046
  GG24-4302-00 IBMMMSTR IEAC6MST ITPPIBOK QS3X36CM QSYSNEWG SC09-138 SC24-546
  SC26-457 SC31-605`): typed better **65**, equal **3**, worse **0**; **61 are
  character-identical to hosted**.  A word-level cross-check confirms typed
  loses no hosted word on any of them.
* **The 7 corrected already-typed topics**: equal on all 7, worse on 0 — as
  expected of a change that adds one blank column.
* Excluded: IBMMMSTR EDITION and SC31-605 GLOSSARY are not served by the
  hosted edition.

Difference classes, all decided:

| Class | Typed behaviour | Decision |
| --- | --- | --- |
| A drawn rule glued into a sentence or a railroad diagram | printed, with the hosted display glyph of each word | keep — the class the slice exists for |
| Hosted styles words inside a drawn rule (`>>__<samp>page</samp>__(`) | one glued run, character-identical | accept: the character sequence matches; only a word-level metric sees a difference |
| An `SI` line inside a drawn box | hidden, lowered as an index term | keep: hosted prints none of it |
| Adjacent same-style words merged into one span (`GO CMDxxx`) | one span | accept, the documented convention |
| The inner ASCII-`-` list of QS3X36CM 1.1 | reflowed into a paragraph | recorded residual, see below |

## Residual

**519 legacy topics.**  What this slice leaves in its own classes:

* `placeholder run followed by visible text` **60**, all box-drawing runs that
  **open** their display line and are therefore not disproved.  Three shapes,
  each read off `--lines`: a drawn box whose bottom rule is missing or drawn
  at other columns (FA1PLMM0 record 453 lines 19–21, SC24-5520-00 record 218
  lines 30–32 where a row label stands outside the left rail); a box whose top
  rule lies in the previous record (OFCUSEOV record 65 opens with a side row);
  and the *alternative branch* of a railroad diagram, whose line begins with
  the branch rail (SC24-546 record 45 line 16
  `                      |_optional_item_|`).  A rule that admits a run
  opening its own line was considered and refused: it cannot be separated from
  an unclosed drawn box, which would then reflow into a paragraph instead of
  failing closed.
* `text segment begins with control-like word` — untouched here, and after the
  TOC/INDEX family landed the remainder is a set of one-book shapes.  Hosted
  evidence gathered for two of them, so the next slice does not have to:
  * **`c.rev`** is the revision-level control, a *two-byte* token, so the
    length-byte guard does not apply to it.  PRG1SORT record 17 line 9 is
    `c.rev REL3 |`; hosted DT 19900829171904 serves PREFACE.5 as
    `<H2>| PREFACE.5   Related Online Information</H2>` with every body line
    in the ` | ` change-bar margin, and prints neither `c.rev` nor `REL3`.
    (This replaces the earlier note "no hosted evidence was gathered".)
    Second occurrence: SC24-5520-00 6.11.8, which the hosted catalog does not
    serve.
  * **`SRELIS<id>`** is *not* an anchor.  OFCUSEOV 2.1.1 stores
    `SRLISopone`/`SRLISoptwo` and `SRELISopone`/`SRELISoptwo`, and hosted DT
    19900805103816 serves exactly two anchors, `<a name="LISopone">` and
    `<a name="LISoptwo">`: `SRELIS<id>` closes the list `SRLIS<id>` and prints
    nothing.  `reserved_structural` lists `srlist`/`srelist`, which match
    neither spelling.  Four topics (OFCUSEOV 2.1.1, 5.4.2.1, 6.1, 6.5.5).
  * `SRHDRAIXHIGH` (SC31-711 PREFACE.2) is the body form of finding 6 above —
    an `SR<id>` alone on its display line with the next line's length byte
    glued to it; hosted serves `<a name="HDRAIXHIGH">`.  One book, so it is
    left closed rather than generalised out of the envelope.
  * `SRGLS` (SC24-5527-02 GLOSSARY) and `SRFIGFIGUNIQ8` (SC33-033 4.8) are
    reserved opcodes with their own families; a generic anchor is the wrong
    answer for them.
* **The ASCII `-` list bullet is not modelled.**  QS3X36CM record 7 display
  lines 27 and 33 are `       -   xxx may be the verb part of the command.`
  with the bullet at column 7 and the text at 11, nested inside a `U+2666`
  list whose bullet is at column 3.  Hosted DT 19910524075122 serves them as
  list items; the typed route reflows them into a paragraph, losing no
  character.  `qs3x36cm_markdown_test` is re-pinned on that reading.  A rule
  ("a one-cell `-` that is the first displayed cell of its line and is
  followed by a gap run of at least two cells") looks safe but was not
  validated on a second book and is left for a later slice.

Corpus-wide top reasons after this slice: placeholder run 60, `SRMSG` outside
the prose model 55, unclaimed table token 31, misaligned figure prefixes 26,
menu label mismatch 20 + 8, malformed font control 18, unclaimed visible token
18.
