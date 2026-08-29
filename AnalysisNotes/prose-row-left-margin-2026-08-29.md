# The prose row's left margin under the display-line model (issue #58) — 2026-08-29

Repair of the render regression recorded at the end of
`prose-display-line-rows-2026-08-29.md`: nine topics that `main` typed before
the display-line row slice merged with the sub-token span slice, and that then
failed closed on `row columns are unproven` (6) and `nested or misaligned list
items` (3).  The normative facts derived here are in `Format/markup.md`, "The
three-column left margin" and "The list bullet is display structure between
controls", and in `Format/logical-controls.md`, "Display Lines Govern Reflowed
Prose Too" and "A Control-Shaped Word Inside A Row Is Display Text".

## Starting point

`build/bootrace <book> --coverage` over all 34 fixtures at `main` `5430892`:
**6,285 / 7,362 typed**.  The two classes this slice owns: `row columns are
unproven` **47**, `nested or misaligned list items` **43**.

## What the two rules turned out to require

Both classes are **one defect**: the row's left margin.

The sub-token span slice had proved the three-column revision margin and
applied it from `marker_at`, the token-geometry row-boundary test.  The
display-line slice then constrained `marker_at` to display-line length bytes,
which is right — but the change bar is *not* a length byte, it is the first
content token *inside* the line.  So the margin correction stopped firing and
`row_control_length_byte`, which opens the row instead, took the one-cell
space before the bar for the row's whole origin run.

`display_line_text` had been right all along.  ACPZMST1 record 459 line 11
(tokens 114..118) renders as

```
 |     XC_NOTIFY_MSG, VM PWSCS will dispatch the messages, using the
```

with cells `' '`(tok 115) `'|'`(tok 116) inserted-space `'    '`(tok 117) and
`XC` at column 7, while the row model built

```
     (a one-cell row)
    XC_NOTIFY_MSG, VM PWSCS will dispatch the messages, using the
```

— text at column 4, three columns short, so `cfont 7 13 4` landed inside
`XC_NOTIFY` and crossed the gap after the comma: `row columns are unproven`.
The three list topics are the identical shape one level up: OFCUSEOV record
839 line 0 is `<length byte> ' ' '|' '    ' U+2666 '  ' Leave`, hosted
` |     °   Leave the prompt blank ...`, bullet at column 7 and text at column
11 — the columns the *unrevised* items of the same list carry — so the item's
origin came out 1 instead of 7 and the list read as nested.

**Rule adopted.** Where a record's display lines parse and a change bar stands
in one line's leading whitespace, the row's margin is the line's own columns:
walk the line's cells, take the column of the first word as the row origin,
and give the origin run the cells of the space run that ends the whitespace.
Nothing is assumed about the width — the documented three columns fall out of
one leading space cell plus the bar plus the assembler's space.
(`libgeist/src/prose_topic_lines.cpp`, `change_bar_margin_line` /
`row_control_length_byte`.)

That alone restored **all nine** topics and moved 65 topics legacy → typed
with no topic moving the other way: `row columns are unproven` 47 → 25,
`nested or misaligned list items` 43 → 1.

### The residual 25: the bullet between two controls

The 25 remaining rows were all `delta 5` bullet rows (the class the span slice
recorded as "a bullet row whose margin the model does not reproduce").  They
are not a margin problem at all: the bullet glyph was being **dropped**.
`collect_stream`'s `emit_unclaimed` kept a `U+2666` glyph as a stream item
only in the CZ dialect and assigned it `padding` otherwise, so a bullet that
reaches the stream between two control segments disappeared with its column.

GG24-4302-00 `PREFACE.2` is the clean case: one bibliography list whose rows
are the payload of sixteen consecutive `cfont 7 7 C 15 2 C 18 9 C ...`
controls, continued across a record boundary.  Record 31's first display line
(tokens 0..4: length byte value 25, three-cell origin, `U+2666`, two-cell gap,
`IMS`) arrives before that record's first control, so its bullet took the
unclaimed path and the row's text moved from column 7 to column 2 while its
fifteen siblings stayed at 7.  Hosted (DT 19950308184737) serves all
seventeen identically.  The glyph is now display structure in both dialects.

### One control misparse this uncovered, and the class it opened

Emitting the bullet exposed a pre-existing word loss.  `classify()` reads any
identifier-shaped word beginning `SR` as a structural anchor control, and
SH12-565 `4.3.5` has a five-item list `LOGMODE / RUSIZES / PSNDPAC / SRCVPAC /
SSNDPAC.` whose fourth item was being swallowed as an anchor — hosted (DT
19941206115523) prints all five.  With the bullet restored the row became a
textless bullet row and the topic fell out of the typed route, which is the
honest reading but still a loss.

The display line settles it, and settles the whole class.  A control-shaped
word with **another displayed word in front of it on its own display line**
stands inside a row, so it is that row's display text.  A corpus sweep of
every structural segment in the 34 fixtures (**14,392**) splits as

| Shape | Segments |
| --- | ---: |
| opens its display line, nothing after | 9,138 |
| opens its display line, text after | 4,987 |
| record's display lines do not parse | 61 |
| a displayed word in front of it | ~200 |

and every one of the ~200 is prose — `SRVAPPS`, `SRVBLDS`, `SREPLACE`, `SREF`,
`SRCVPAC`, `SRPI`, `SRC1`, `SRCFILE`, `SRTF5`, … — while **no** `SREFIG`,
`SRFIG*`, `SRGLS`, `SRHDR*`, `SRLIS*`, `SRLEN`, `SRTBL` or `SRFTN*` anchor is
among them.  The same test applies to the `text` segments the split produced
without any control at all, which is the whole `text segment begins with
control-like word` `SR…` family: DREICMST record 430 line [195,205) reads
`       the command is saved in the SRC.` and the flattened string had cut
`SRC.` off as its own segment.

`demote_display_line_owned_controls` (`libgeist/src/display_lines.cpp`, called
from `decode_logical_record_sources`) marks such a segment `display_text`; the
segment boundary stays, because the flattened string really did split there,
and the prose stream pushes the payload as body text in place instead of
rejecting it as a control-like word.

**Measured on its own baseline** (the margin fix above): **6,377 → 6,422
(+45)**, no book regressing and no topic moving typed → legacy; `text segment
begins with control-like word` **99 → 81**, and the remaining `SR…` cases (12)
are all words that *do* open their display line.  Two topics moved from this
reason to `ST title does not match` (35 → 37), staying legacy.

**Tried and reverted**: the split consumes the separator it fires on, so
SH12-565 `3.1.6`'s example command loses one comma (`F QH,F XY,SRV=(3,2,2)` →
`F QH,F XYSRV=(3,2,2)`; the legacy route drops `,SRV=(3,2,2)` entirely).
Giving the demoted segment everything back to the previous segment's end
recovers the comma but costs **60 topics**, because that gap also carries
padding the previous segment's own model relies on.  Left as a residual.

Two pinned tests moved with it, both hosted-checked before re-pinning:
IEAC6MST `4.3.4.1`'s table envelope now composes (the `visible source between
table lines` class is pinned on IEAC6MST `7.9` instead), and QS3X36CM `2.1`
reaches the typed route, where hosted (DT 19910524075122) rules `#STRTUP1`'s
two AS/400 commands off as two table rows -- which the typed table reproduces
and the legacy one had merged into a single cell, gluing `list ofsubsystem`,
`submitwith` and `systemBASIC` across its row breaks.

## Implementation

* `libgeist/src/prose_topic_lines.cpp` — `Margin`, `change_bar_margin_line`,
  and its use in `row_control_length_byte`.
* `libgeist/src/prose_topic_stream.cpp` — the list bullet is a stream item in
  the flattened dialect too; a `display_text` segment is body text.
* `libgeist/src/display_lines.{hpp,cpp}` —
  `demote_display_line_owned_controls`.
* `libgeist/src/geist/detail/control_ir.hpp` — `ControlSegmentIR::display_text`.
* `libgeist/src/logical.cpp` — runs the demotion after `decode_control_segments`.

## Measured

* `bootrace --coverage` over all 34 books: **6,285 → 6,422 of 7,362
  (85.4% → 87.2%, +137)**.  **No book regressed and no topic moved typed →
  legacy** at any step.  Per book: SC24-5527-02 +21, ACPZMST1 +18, QSYSNEWG
  +13, SC24-546 +13, SC26-457 +12, GG24-395 +8, SC09-138 +7, OFCUSEOV +6,
  IEAC6MST +5, SC24-5520-00 +5, SH12-565 +5, PRG1SORT +4, SC34-425 +4,
  GC23-046 +3, SH20-918 +3, FA1PLMM0 +2, SC09-2417-00 +2, and +1 each in
  DREICMST, GG24-4302-00, ITPPIBOK, QS3X36CM, QSYSINFO and SC28-1881-05.
* Rejection classes: **`row columns are unproven` 47 → 2**, **`nested or
  misaligned list items` 43 → 1**, **`text segment begins with control-like
  word` 99 → 81** (the whole `SR…` half of that class), and `visible token …
  claimed by no block` 34 → 31.  Only `ST title does not match` grew (35 → 37),
  by two topics that stay legacy under a different reason.
* Whole-corpus `boo2git --force` before/after: **556 changed files, 0 added,
  0 removed** — the **137** moved topics plus **419** already-typed topics the
  margin fix corrects.  The correction is always the same kind: a paragraph
  the lost margin had split at every change-bar row is one paragraph again
  (ACPZMST1 `2.4` `**Log Messages**` + three fragments → one definition
  paragraph, which hosted serves inside a single `<p>`).

## Hosted verification

Word-level comparison of hosted body text against the exported Markdown, with
Markdown syntax/escaping stripped, `<br>` cell joins split, the hosted-only
change bar and `°` bullet dropped, and edge punctuation normalised.

**The nine regressed topics**, each against hosted, against the current legacy
output, and against their pre-regression typed output (`main` `51a686e`, built
from `git archive`):

| Topic | typed now | legacy | pre-regression typed |
| --- | --- | --- | --- |
| ACPZMST1 `8.14.1` | identical | miss 1 | identical |
| ACPZMST1 `8.18.1` | identical | miss 1 | identical |
| SC24-546 `6.2.11` | identical | miss 2 | identical |
| SC26-457 `2.1` | miss 4 | miss 6 | miss 4 |
| SC26-457 `3.9.1.1` | miss 8 / extra 5 | miss 14 | miss 8 / extra 6 |
| SC26-457 `3.14.1.2` | miss 12 | miss 43 | miss 12 |
| ACPZMST1 `8.2` | miss 12 | miss 18 | miss 12 |
| SC26-457 `3.2.2.2` | miss 59 | miss 114 | miss 59 |
| OFCUSEOV `6.4.3` | miss 3 | **miss 76** | miss 3 |

Equal-or-better than both on all nine, better than legacy on all nine, and
better than the pre-regression typed output on one.  The residual counts are
comparison artefacts, checked one by one: hosted glues `EMPTY|NOEMPTY` and the
drawn box rails `|______|` into single tokens that the cell-splitting drops.

* **78 moved topics across 19 books**: typed better **65**, equal **11**,
  worse **2**; **28 are word-identical to hosted**.  Both "worse" rows were
  read: ACPZMST1 `7.3.1` is an `&amp;` entity the comparator unescapes on the
  hosted side only (typed recovers the word `use` legacy loses), and SH12-565
  `3.1.6` is the swallowed comma above plus railroad-diagram tokens hosted
  glues -- its preformatted rows are character-identical to hosted's, and
  legacy drops the whole `,SRV=(3,2,2)` tail.
* **143 of the already-typed corrected topics, across 22 books**: better 6,
  equal 137, **worse 0**; 59 word-identical.  The export changes are
  paragraph structure, not words, as expected of a margin fix.

Hosted DTs are the table in `prose-display-line-rows-2026-08-29.md`;
SC24-5520-00, SC24-5527-02, SC28-1881-05, GG24-395, XWEBDEMO and packet are
absent from the hosted catalog and excluded from the samples.

## Residual

**940 legacy topics.**  The two classes this slice owns are down to three
topics, all genuinely fail-closed:

* QSYSNEWG `A.4` (`nested or misaligned list items`);
* SG24-204 `5.3.3`, where the row reads `... as shown inFigure 90 in topic
  4.2.2.4.` — the space before `Figure` is missing, so the selector span
  reaches from inside `inFigure` across a gap;
* PRG1SORT `2.1.4`, a `(Source file name)` row whose span starts inside the
  parenthesis.

`text segment begins with control-like word` keeps **81**: 34 `ctocdef=0` (one
per book, every `CONTENTS` topic), 29 `cidelm`/5 `citerm`/1 `ctoce` (the
`INDEX` topics) — a generated-list slice, not a prose one — and 12 `SR…` words
that *do* open their display line, so this rule says nothing about them.

One known character loss: SH12-565 `3.1.6` drops the comma the segment split
consumed (see "Tried and reverted" above); typed still recovers `,SRV=(3,2,2)`
which legacy drops whole.

Corpus-wide top reasons after this slice: placeholder run 82, control-like
word at a text-segment start 81, missing metadata envelope 69, `SRMSG` outside
the prose model 54, ST-title mismatch 37, `cz off table` 34, unclaimed table
token 31, `cz off efig` 28.
