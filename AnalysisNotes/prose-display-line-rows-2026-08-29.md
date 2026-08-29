# Prose rows from the display-line model (issue #58) — 2026-08-29

Workflow and hosted trail for the second placeholder slice: the shapes the
first slice (`prose-topic-placeholder-runs-2026-08-29.md`) could not prove,
plus the row-model change they turned out to require.  The normative format
facts are in `Format/logical-controls.md`, "Display Lines Govern Reflowed
Prose Too", and `Format/markup.md`, "Drawn box regions in prose" and
"Verbatim `CZ` regions".

## Starting point

`build/bootrace <book> --coverage` over all 34 fixtures at `main` `f8607f6`:
**5,363 / 7,362 typed**.  The class this slice owns:

```
prose topic rejected: placeholder run '?' is followed by visible text at record N token M   315
prose topic rejected: placeholder glyph '?' inside prose text at record N token M            96
```

## Procedure

1. A scratch probe (`decode_logical_record_sources` + `typed_route_inventory`
   per book) parsed the rejection message, resolved the *logical* record
   number it names to the topic's record index, and printed the record's
   display lines around the failing token: length byte, token range, hosted
   display text (`display_line_text`) and a per-column class string
   (`.` space, `B` `U+2500`–`U+25FF`, `?` `U+FFFF`, `x` other).
2. The sites were clustered first on **whether the failing token is the
   display line's length byte or a token inside the line**, then on the
   column class string of the failing line.  That single split accounted for
   179 of the 384 sites at once.
3. Each shape was decided against hosted BookServer, always on at least two
   books, before it was modelled.
4. `bootrace --coverage` per book before/after, whole-corpus `boo2git
   --force` before/after with the changed files split into moved topics and
   already-typed topics, and a hosted word-level sample of both sets.

## What the display lines proved

* **The length byte is the row-control slot, always and only.**  This is the
  finding the whole slice rests on.  The prose family had been inferring row
  boundaries from token geometry (`marker_at`: a width-1 token followed by a
  space run).  The record payload already carries them.  Where the record's
  display lines parse, a token *inside* a line is never a slot and the length
  byte always is, whatever dictionary word it resolves to.
* Consequences and their hosted evidence are tabulated in
  `Format/logical-controls.md`.  The four that unlocked topics: the byte is
  never the origin run; an empty display line is the only paragraph break; a
  `c.<xx>`-only line is a body control line; a `U+2500`-only line is the
  reader's `<hr>`.
* **Any control-operand-only line may stand inside a drawn box**, not only a
  `cfont` one.  `CSELECT` is the case that proves it.
* **`cz OFF SCREEN` and `cz OFF LBLBOX` are verbatim regions** exactly like
  `cz OFF XMP`, and inside a verbatim region a box-drawing run is display
  content.  Hosted even names the second one: `<pre width="132"><!-- lblbox
  -->`.
* The `cz FLOW FN` "row terminator `.`" recorded earlier is simply the next
  display line's length byte; under the display-line model it never reaches
  the body's cells, so the trim is dead code for a record that parses.

## Implementation

* `libgeist/src/prose_topic_lines.cpp` — `opens_display_line`,
  `row_control_length_byte`, `blank_display_line`, `body_control_line`,
  `display_rule_line`; `marker_at` constrained to length bytes; verbatim
  box-word cells; the footnote terminator relaxation.
* `libgeist/src/prose_topic_boxes.cpp` — `control_only_line` generalised from
  `cfont` to any control.
* `libgeist/src/prose_topic_cz.cpp` — `screen` and `lblbox` share the `xmp`
  verbatim path.

## Hosted DTs used

| Book | DT | Book | DT |
| --- | --- | --- | --- |
| ACPZMST1 | `19920319123146` | QSYSNEWG | `19910524085706` |
| DREICMST | `19911219125856` | SC09-138 | `19910321130500` |
| FA1PLMM0 | `19910927114801` | SC09-2417-00 (`SC09-241`) | `19961114175628` |
| GC23-046 | `19920330095121` | SC24-546 | `19940323131240` |
| GC28-183 | `19930625102617` | SC26-457 | `19911220191142` |
| GG24-4302-00 | `19950308184737` | SC31-605 | `19911015203151` |
| IBMMMSTR | `19911004151140` | SC31-711 | `19941010174546` |
| IEAC6MST | `19920124000100` | SC33-033 | `19930422134757` |
| ITPPIBOK | `19910628074854` | SC34-425 | `19921112160049` |
| N2AH1MST | `19910329000100` | SC41-485 | `19951003131222` |
| OFCUSEOV | `19900805103816` | SG24-204 | `19971218054640` |
| PRG1SORT | `19900829171904` | SH12-565 | `19941206115523` |
| QS3X36CM | `19910524075122` | SH20-918 | `19910520154851` |
| QSYSINFO | `19910524120827` | GX27-3999-00 (`GX27-399`) | `19950730184057` |

SC24-5520-00, SC24-5527-02, SC28-1881-05, GG24-395 and packet are absent from
the hosted catalog and are excluded from hosted sampling.

## Measured

* `bootrace --coverage` over all 34 books: **5,363 → 6,106 of 7,362
  (72.8% → 82.9%, +743)**.  No book regressed and no topic moved
  typed → legacy.
* Rejection classes: placeholder run 315 → 81, placeholder glyph 96 → 18,
  glued body control 107 → 3, "block has no visible text" 20 → 0, and the
  row-geometry fix also cleared span-exceeds-display-line 134 → 24,
  span-starts-inside-word 152 → 89 and ST-title mismatch 79 → 35.
* Whole-corpus `boo2git --force` before/after: **1,403 changed files, 0 added,
  0 removed** — 743 moved topics, 659 already-typed topics whose row model the
  fix corrected, and `errors.log` (path text only).
* Hosted word-level sample of **77 moved topics across 27 books**: typed
  better 64, equal 13, **worse 0**.  Hosted sample of **93 already-typed
  changed topics across 25 books**: better 9, equal 84, **worse 0**.

| Difference class | Typed behaviour | Decision |
| --- | --- | --- |
| A paragraph the old row model split mid-sentence | one paragraph, matching hosted's single `<p>` | keep — the largest same-route class |
| A duplicated trailing `.` (DREICMST `1.1`) or a dropped one-byte word (FA1PLMM0 `17.2.3.1` `a`, ACPZMST1 `4.6` a whole example row) | restored/removed to match hosted | keep |
| `CFONT` phrase emphasis (`**IMS CS/2**` vs `**IMS** **CS/2**`) | one emphasis inline over the styled run | keep: hosted styles the whole phrase |
| Cover rule lines | dropped; hosted serves `<hr>` | accept: no word lost, same as legacy.  Residual: the Document IR has no thematic-break node, so the rule cannot be lowered |
| `CSELECT` / `CFONT` inside a drawn box (SC31-711, GG24-4302-00 `NOTICES`) | preformatted rows, link and bold dropped | accept: same precedent as every other drawn box; the rows are character-identical to hosted |
| Markdown escaping of a topic that moved route (`5\.1`, `SX41\-8209\-00`) | escaped | accept; tests re-pinned |

## Merged measurement and one recorded regression

Re-measured after merging `main` `51a686e` (the sub-token span slice):
**5,720 -> 6,271 of 7,362 (77.7% -> 85.2%, +551)**.  Every book grew.

Nine topics that `main` typed now fail closed, all through rules the two
slices share rather than through wrong output: ACPZMST1 `8.14.1`/`8.18.1`,
SC24-546 `6.2.11`, SC26-457 `2.1`/`3.9.1.1`/`3.14.1.2` hit the new "row
columns are unproven" span rule on rows whose columns this slice moved, and
ACPZMST1 `8.2`, SC26-457 `3.2.2.2`, OFCUSEOV `6.4.3` hit "nested or
misaligned list items" because a list item's origin now differs from the
previous item's (1 vs 7 and 4 vs 7 -- lists interleaved with drawn screen
boxes).  They fall back to the legacy renderer, which loses hosted words on
eight of the nine (up to 68 in OFCUSEOV `6.4.3`), so this is a real render
regression for those topics and not only a coverage one.  Handed forward:
the origins those three lists disagree on are worth re-deriving from the
display-line indent rather than from the row builder's `implied_origin`.

Whole-corpus `boo2git --force` against `main` `51a686e`: **1,502 changed
files, 0 added, 0 removed** -- 560 topics that moved legacy -> typed, the 9
above, and 932 already-typed topics the row fix corrects.  Hosted sample of
**75 moved topics across 26 books**: better 63, equal 12, worse 0; and of
**99 already-typed changed topics across 26 books**: better 9, equal 90,
worse 0.

## Residual

**1,256 legacy topics.**  The class this slice owns is down to 81 + 18.  What
is left in it, all fail-closed:

* box candidates whose bottom rule is missing or drawn at a different column,
  including the paired answer boxes of SC24-5520-00 forms (`____ ____` on one
  line);
* railroad syntax diagrams (`>>__ _GLOBAL__ __ ____________ __><`, SC24-546,
  SC09-2417-00, GG24-4302-00) — a `U+2500`/ASCII mixture inside a text run;
* the `------ General-use programming interface ------` boundary fence of
  SC33-033, whose rule is drawn with ASCII dashes between a `U+250C` and a
  `U+2500`;
* single `U+2500` runs glued into a text run (`The >>___ symbol indicates the
  beginning of a statement`, SC24-546 `1.3.1`, SC28-1881-05 `1.2`).

Corpus-wide top reasons after this slice: control-like word at a text-segment
start 99, span ends/starts inside a word 91 + 89, placeholder run 81, missing
metadata envelope 69, `SRMSG` outside the prose model 54, unclaimed table
token 48.
