# Prose "placeholder run followed by visible text" (issue #58) — 2026-08-29

Workflow and hosted trail for the slice that resolved the prose family's
largest rejection class.  The format facts derived here are in
`Format/markup.md`, sections "Structured subject-index display lines" and
"Drawn box regions in prose"; the display-line structure they both build on
is in `Format/logical-controls.md`, "Display Lines Inside A Record Payload".

## Starting point

`build/bootrace <book> --coverage` over all 34 fixtures at `main`
`843404c`: **3,768 / 7,362 typed**, of which 294 topics rejected with

```
prose topic rejected: placeholder run '?' is followed by visible text at record N token M
```

(the class was 493 topics before the `CZ` slice landed and re-routed part of
it).  `is_placeholder_run` covers the box-drawing block `U+2500`-`U+25FF`
plus the decoder's unmapped word `U+FFFF`; the family refused any such run
that a visible token followed on the same row.

## Procedure

1. A scratch probe (`decode_logical_record_sources` +
   `extract_prose_topic_ir` per TOC topic) parsed the rejection message,
   printed a ±12-token window around the failing token with encoded width,
   value, spacing prefix and decoded code points, and emitted one line per
   failing topic so the 294 sites could be clustered.
2. Two shapes accounted for essentially all of them:
   * a one-byte token holding one or two `U+FFFF` words right after an `SI`
     keyword (≈250 sites) — structured subject-index entries;
   * a `U+250C` corner followed by more box-drawing runs (≈240 sites) —
     boxes drawn straight into the prose display lines.
3. Both were resolved against the **display-line** structure of the record
   rather than against the flattened decoded string.  A second probe mode
   printed every display line of a topic with its length byte, token range
   and hosted display text (`figure_display_glyph` per word); those lines
   match the hosted `<pre>` output character for character, which made the
   grammar decidable instead of guessed.
4. `bootrace --coverage` per book before/after, whole-corpus `boo2git
   --force` before/after, and a hosted word-level sample.

## What the display lines proved

* An `SI` entry is exactly one display line and hosted displays no part of
  it.  The "visible tail after `?`" recorded earlier in `Format/markup.md`
  was a flattening artifact: in `QSYSNEWG.BOO` record 40 the tail
  `| If your display station screen is blank, ...` is the *next* display
  line, and hosted serves exactly that line.
* A drawn box is a run of consecutive display lines whose corner/rail
  columns line up; hosted prints them verbatim.  `QSYSNEWG` 1.0,
  `OFCUSEOV` 1.10/1.18.2 and `SH20-918` 3.0 were compared line by line
  against hosted and are identical.

## Implementation

* `libgeist/src/display_lines.{hpp,cpp}` — the record display-line parser,
  the hosted display text of a line, and its per-column words/tokens, moved
  out of `figure_block_ir.cpp` so the prose family shares one
  implementation (that move is render-neutral: whole-corpus differential,
  zero changed files).
* `libgeist/src/prose_topic_lines.cpp` — a structured `SI` line: the
  placeholders take the new `index_structure` ledger role, the entry ends
  with its display line, and the fields stay opaque.  The rule fires only
  where the family rejected before.
* `libgeist/src/prose_topic_boxes.cpp` — `plan_boxes` finds closed box
  regions from display-line geometry; `prose_topic_lines.cpp` emits one
  verbatim row per region line and `prose_topic_blocks.cpp` groups them into
  a `ProseBlockKindIR::preformatted` block (the block kind the `CZ` slice
  introduced), which lowers to `PreformattedBlockIR`.  A region whose tokens
  a table or figure span already owns is left to that span; a candidate
  without a matching bottom rule is not a region and the topic keeps
  failing.

## Hosted DTs used

| Book | DT |
| --- | --- |
| QSYSINFO | `19910524120827` |
| SC09-138 | `19910321130500` |
| SH20-918 | `19910520154851` |
| OFCUSEOV | `19900805103816` |
| IEAC6MST | `19920124000100` |
| QSYSNEWG | `19910524085706` |
| SG24-204 | `19971218054640` |
| SH12-565 | `19941206115523` |
| FA1PLMM0 | `19910927114801` |
| GC23-046 | `19920330095121` |

`SG24-204` needed care: the live `FINDBOOK` catalog serves seven different
redbooks under that id, and only DT `19971218054640` answers with the
fixture's topic titles (`4.1 The Transaction Server for OS/2 Warp`).
`SC24-5527-02` remains absent from the catalog and is excluded from hosted
sampling.

## Measured

* `bootrace --coverage` over all 34 books: **3,768 -> 4,036 of 7,362 typed
  topics (51.2% -> 54.8%, +268)**.  Per book: QSYSINFO +105, SC09-138 +52,
  SH20-918 +47, OFCUSEOV +28, IEAC6MST +6, QSYSNEWG +6, SC24-5527-02 +6,
  SG24-204 +6, SH12-565 +6, FA1PLMM0 +3, SC24-5520-00 +2, GC23-046 +1.  No
  book regressed and no topic moved typed -> legacy.
* The rejection class itself: 294 -> 86 topics.
* Whole-corpus `boo2git --force` before/after: **268 changed files, 0 added,
  0 removed** — exactly the 268 topics that moved to the typed route.  No
  file changed for a topic that stayed on the same route, so this slice has
  no deliberate fail-closes and no collateral render change.
* Hosted word-level sample of **48 moved topics across 10 books**: typed
  loses **no** hosted word on any of them (`missing = 0` everywhere), while
  the legacy render loses words on 32.  Verdict: typed better on 32, equal
  on 13, and three rows where one of the two metrics prefers legacy, each
  decided below.

| Difference class | Typed behaviour | Decision |
| --- | --- | --- |
| Box outline and box rows hosted shows and legacy drops or reflows | preformatted block with the hosted display lines | keep (most of the 32 "better" rows) |
| `CFONT` highlighting inside a drawn box (hosted `<B>In</B> <B>a</B> <B>Hurry?</B>`) | dropped: a preformatted block carries no inline style | accept, same as the drawn-figure family |
| Common indent of a box region | stripped, as the `cz OFF XMP` block does | accept: geometry preserved, no text lost |
| Structured `SI` fields (`3HI1`, `0`, `4XMP@`) that legacy printed into the paragraph | hidden | keep: hosted prints none of them |
| Markdown table cell order versus the hosted `<pre>` grid (IEAC6MST 5.3.3, 5.4.2) | table rows carry `<br>` cell continuations | accept: same words, no loss; legacy loses 2 and 1 hosted words on those topics |
| Hosted splits a word at markup (`<B>O</B>scar`, QSYSNEWG 5.2.1) | one word | accept: the typed text is character-identical to hosted |
| Topics not served by the hosted edition (GC23-046 FRONT_1, QSYSNEWG A.3.1/C.2, SG24-204 BACK_1.1) | — | excluded from the verdict |

## Residual (86 topics)

Largest remaining shapes in the class: SC09-2417-00 16, SC24-546 10,
SC24-5520-00 9, SC24-5527-02 9, packet 7, GC23-046 5, IEAC6MST 5.  They are
box candidates whose bottom rule is missing or drawn at a different column,
box regions whose rows a `CSELECT` crosses, and single `U+2500` runs glued
into a text run (`SC24-546` 1.3.1 `The >> ___ symbol indicates ...`), all of
which stay fail-closed.
