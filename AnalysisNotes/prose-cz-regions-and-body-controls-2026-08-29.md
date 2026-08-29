# `CZ` object regions, flattened footnotes and body-control length bytes (2026-08-29)

Supporting notes for the issue #58 slice that took the unmodelled `cz off
table` / `cz off efig` regions, the `SRFTN<id>` footnotes of the flattened
dialect, and the `body control <metadata opcode> is outside the prose model`
family.  The normative facts are in
[`Format/markup.md`](../Format/markup.md), sections "`CZ` object regions:
`TABLE`, `FIG`" and "Footnotes in the flattened dialect", and in
[`Format/logical-controls.md`](../Format/logical-controls.md), section
"A Metadata Opcode In The Body Is A Display-Line Length Byte".

## Starting point

`build/bootrace <book> --coverage` on `main` (`8343096`): **6,544 / 7,362
typed (88.9%)**.  The five clusters in this slice's brief measured

| Rejection | Topics |
| --- | ---: |
| `cz off table: CZ layout cz off table is not modelled` | 35 |
| `cz off efig: CZ layout cz off efig is not modelled` | 29 |
| `body control SRMSG is outside the prose model` | 54 |
| `control SRFTN<id> carries visible payload '('` (and `'\|'`, `':'`, `'-'`) | 58 |
| `body control <metadata opcode> is outside the prose model` | 58 |

## What each turned out to delimit

### `cz OFF TABLE` and `cz OFF FIG`

Both are *boundaries only*.  The rows they enclose belong to the `SRTBL` /
`SRFIG` structural span, which `prose_topic_spans.cpp` already plans -- and
already planned successfully for every one of these 64 topics, since
`plan_spans` runs before the stream pass that was rejecting them.  Nothing
about the region model had to grow: the `CZ` builder now recognises the four
tag names, requires an admitted span of the matching kind, lets the opener
carry no rows, and lets the closer carry the following body text as
paragraphs (the rule `cz OFF EXMP` and the list closers already follow).

The second half of the fix is that `build_cz_blocks` never placed spans at
all: `LineBuild::span_marks` was consumed only by the flattened
`build_blocks`.  `CzBuilder::place_spans` now gives each span the same
position and `anchors_before` the flattened dialect gives it.  A span landing
between two items of one `cz FLOW UL` also has to split the list, exactly as
an anchor does, or the lowering never visits that position -- six topics
(SC09-2417-00 `6.2.3`, `7.4.4`, `APPENDIX1.1.1.12/15/16`, SC09-138 `5.2.3`)
failed on `prose span was never placed` until `span_at` was added beside
`anchor_at` in `prose_topic_document_lowering.cpp`.

### `SRFTN<id>` in the flattened dialect

Not a defect in the CZ footnote model but a *second* spelling of the same
structure.  The flattened dialect has no `cz FLOW FN`, so the body sits in the
`SRFTN<id>` control's own payload; the payload's first visible token is
usually the footnote's reference glyph, which is why every rejection quoted
`(`.  The prose stream now decides the dialect once per topic (does the body
carry any `CZ` control at all -- the test `Format/markup.md` already
prescribes) and, in the flattened dialect, lets `SRFTN<id>` fall through to
the ordinary body-anchor path that already lowers "anchor + payload as body
text", which is byte-for-byte what the typed footnote block lowers to.

### The metadata opcodes in the body

The largest surprise, and the same class of mistake as `c.<xx>`: the one-byte
dictionary tokens spelling `cbacklevel` (45), `chdlevel` (48),
`cforwardlevel`, `cparent`, `cmitem`, `csourcefn`, `ctopicn`, `csummary` sit
in the same low value range as the display-line length bytes, so the flattened
splitter opens a metadata segment on a byte that is row geometry.  Gated on
`title_seen` (the envelope is never affected, because it precedes `ST`), a
segment whose first token is a display line's prefix token now re-enters the
token stream so the display-row pass gives it the row-control slot it gives
every other length byte.

The first attempt required the whole segment to be that single token; that
matched only three topics, because the splitter usually keeps the row's text
in the same segment (SC31-711 `1.4` record 24 segment 13).  Relaxing the test
to the segment's *first* token took the class to zero.

### `SRMSG` -- decided, and declined

These 55 topics are **genuine message catalogs, and the prose family is right
to decline them**; no coverage moves without extending the message/trap
families, which is not this slice.  Evidence: N2AH1MST `4.0` (DT
`19910329000100`) is served as a run of `<a name="MSG AMA100I">` anchors with
`<B>Explanation:</B>` / `<B>Source:</B>` / `<B>System</B> <B>Action:</B>` /
`<B>System</B> <B>Programmer</B> <B>Response:</B>` fields -- the exact shape
`message_ir.cpp` and `trap_catalog_ir.cpp` model.  Per-family declines over
all 55 (read from `TypedLoweringTraceIR::declined`):

| Family | Decline | Topics |
| --- | --- | ---: |
| message catalog | `message topic has no numeric SRMSG catalog boundary` | 33 |
| message catalog | `message topic metadata/heading envelope is incomplete` | 20 |
| trap catalog | `trap catalog header contains an unsupported control: c.sp` | 19 |
| trap catalog | `trap entry has no labelled field: <id>` | 15 |
| trap catalog | headline/font-span shapes, title row, `cselect`, `cz` header | 21 |

Distribution: N2AH1MST 27, SC34-425 22, SC09-138 3, SC24-546 1,
GX27-3999-00 1.  Handed to whoever takes the message families next; the
`c.sp`-in-the-header decline (19 topics) looks like the cheapest of them,
since `c.sp` is already modelled in the prose family.

## Result

**Coverage 6,544 → 6,700 / 7,362 (91.0%)**, no book regressing and no topic
moving typed → legacy.  Rejection classes: `cz off table` 35 → 0, `cz off
efig` 29 → 0, `SRFTN<id> carries visible payload` 58 → 0, `body control
<metadata opcode>` 58 → 0 (three survive as `visible token 'cparent' inside
the table region`), `SRMSG` 54 → 55 unchanged in substance.

Corpus differential (`boo2git --force` over all 34 fixtures, before and
after): **156 changed files = exactly the 156 moved topics**, symmetric
difference empty in both directions, **zero already-typed topics changed**.

## Hosted comparison

123 of the 156 moved topics are servable, across 22 books.  Method as in
`prose-topic-cz-2026-08-28.md`, with four comparator normalisations added
because they are renderer conventions on both sides, not properties of this
slice:

- `<br>` inside a Markdown table cell is a cell line break, split to a space;
- `boo2git`'s `Previous | Index | Next` navigation rule is stripped;
- an image's alt text is dropped (hosted's alt is `PICTURE n`, ours repeats
  the caption -- see the residual below);
- tokens made only of `|`, `_` and `-` are dropped on both sides, because our
  side already strips `|` as the Markdown cell delimiter while hosted keeps
  the drawn rails.

Result: **better 118, equal 4, worse 1**.  Counting *hosted words lost*
instead of sequence ratio: typed loses fewer on 113, the same on 9, and more
on 1 -- and that one (SH20-918 `3.33.13`, typed 23 vs legacy 19) is entirely
`*` emphasis delimiters and `°` bullets that the comparator strips from our
side; legacy additionally loses the real word `period.`.

The single lower ratio, **SH20-918 `D.3`**, is a *cell-order* artefact, not a
loss: the multiset difference is empty for typed except two rail-glued
hosted tokens, while legacy loses 46 words and truncates a dozen more (`De`,
`Docum`, `GM`, `Novi`, `exper`, `featur`).  Hosted reads the drawn table
line by line across four columns; the typed table emits it cell by cell.

Difference classes decided:

| Class | Decision |
| --- | --- |
| typed merges a `CFONT` run covering consecutive words into one emphasis (`***audio interface;***` where hosted writes `<B><I>audio</B></I> <B><I>interface;</B></I>`) | Renderer convention, semantically identical. Accepted; `packet_markdown_test` re-pinned. |
| the figure block repeats the caption as the image alt text | Pre-existing figure-lowering convention, flagged by an earlier slice too. Accepted; normalised out of the comparison. |
| Markdown table cell order vs hosted's line-by-line `<pre>` reading | Accepted; content conserved exactly. |
| definition entries written `- **term:** definition` where hosted writes `<dt>term<dd>definition` | Existing cross-family renderer convention. Accepted. |
| escaped Markdown punctuation (`2\.4\.4`, `\*APPC`) | Renderer convention; unescapes to the source text. Accepted. |

## Residuals recorded, not hidden

- **Emphasis and rails inside a `cz OFF LBLBOX` region.** packet `1.3`'s audio
  callout is a verbatim region; hosted bolds words *inside* its
  `<pre width="132"><!-- lblbox -->` (`by  <B>tapping</B> <B>the</B>
  <B>discriminator</B> …`) and draws a left rail on every row
  (`   | For most people …  |`).  A `PreformattedBlockIR` carries no inlines,
  so the emphasis is dropped, and the reproduced rows put each row's leading
  `|` on a row of its own.  Every hosted word is conserved (the topic scores
  1.0000 against hosted, legacy 0.9931), and this is the verbatim-region model
  an earlier slice established, so it is left for the region owner.
- **`( )` footnote reference glyph.** GC23-046 `5.1.1`'s footnote body starts
  `( ) MVS/XA is a trademark …`; the row model reads the `(` as a marker slot
  plus origin pair and both glyphs disappear.  Hosted prints them.  The word
  content is otherwise identical.
- **`cz OFF ARTWORK`** (3 topics: GX27-3999-00 `FRONT_1`, `2.4`, SC41-485
  `COMMENTS`) is an inline picture region closed by `cz OFF EHP0`, with a
  `cselect <col> <len> PIC<n>` and no `SRFIG` anchor.  Admitting it needs a
  figure block that can start from a bare picture selector; left closed.
- **The figure-region frame around a table** now fails on its own reason in
  SC41-485 `1.2`-`1.6` (`figure region contains a table (visible token '.'
  outside any table)`); the `.` sits between `SRETBL` and `SREFIG` and
  `plan_frame` admits only spacing there.  Five topics.
- **XWEBDEMO `1.4.1`'s inline cross-reference** resolves to `#MONET1` while
  hosted links `#FIGMONET1`; the selector target is the bare id and the anchor
  keeps the `SRFIG` prefix.  Both routes have this, so it is not a regression,
  but it is a broken link in the export.

## Process note

`git stash` must not be used in this repository while other agents are active:
the stash stack is shared across every worktree.  Taking the pristine baseline
for the corpus differential with `git stash push` / `git stash pop` popped a
*different* agent's entry that had landed on the stack meanwhile.  Both sides
were recovered (`git fsck --no-reflog` for the dangling stash commits, then
`git stash store` to put the other agent's entry back and `git stash apply`
for this one), and no work was lost.  Use a temporary commit or a second
`git worktree` for baselines instead.  Note also that `build/boo2git` and
`build/boorender` link `build/libgeist.so`, so copying the executables aside
does **not** capture a baseline -- copy the export output, or the library.
