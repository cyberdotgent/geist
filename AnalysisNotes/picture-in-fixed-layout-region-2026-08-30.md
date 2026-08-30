# A picture inside a fixed-layout region (2026-08-30)

Slice of issue #58 that retired the four rejection strings a `PIC<n>`
selector inside an `SRTBL` envelope produced. The normative format facts are
in `Format/markup.md` §"Selectors inside a table envelope"; this note records
what hosted does, what was measured, and what is still open.

## What hosted does

Fetched with `curl` (latin-1, parsed in python) at the DTs in
`verbatim-fixed-layout-regions-2026-08-30.md`: `GG24-395` is hosted as
`GG24-3950-01` (DT `19941215160749`), `GX27-3999-00` under its own name (DT
`19950730184057`).

Hosted serves the picture **inline, in the region's own markup**, replacing
exactly the columns the selector names and leaving everything else in place:

- Region with no `cz OFF TABLE` -- served inside the topic's `<pre>`.
  `GG24-395` `3.3.8` `TBLUNIQ14` has `cselect 3 11 PIC69` and the display line
  `    PICTURE 69     SystemView Host Management ...`; hosted emits three
  spaces, `<a href="picture-69?mode=zoom"><img ... alt="PICTURE 69"></a>`, then
  16 spaces and the line's own words. Strip the tags and the line is 19
  columns of whitespace and the text -- exactly the source line with the
  placeholder words blanked out.
- Region with `cz OFF TABLE` -- served as a real HTML `<table>`, with the
  `<img>` inside the cell whose bytes spell the placeholder. `GX27-3999-00`
  `3.2` `NOSENV2` puts `PICTURE 16`..`PICTURE 19` in the first column of four
  rows and hosted renders four `<img>` cells.

The `alt` text is always the placeholder words (`alt="PICTURE 69"`), which is
what `figure_block_ir` already assumed for standalone figures.

**Hosted's `.GIF` set is incomplete**, and that is not a rendering rule.
`GG24-3950-01` returns 404 for `P67.GIF` and `P87.GIF`; `GX27-3999-00` returns
404 for `P9.GIF`..`P12.GIF`. For those selectors hosted prints the `PICTURE n`
words instead of an image, although the BOO catalog holds every one of them
(`boorsrc --list`: GG24-395 ids 67, 68, 69, 82 and 91 are the same stored
bytes at offset 921466). Geist emits the resource, which is the better answer
for a BOO-derived export, and this is why three hosted pages show placeholder
text where our Markdown shows an image.

## The model

Not "admit the selector" (the reverted attempt, which reproduced the
placeholder words verbatim and so replaced hosted's `<img>` with the text
`PICTURE 69`; `lazy_open_gg24_test` caught it), but "represent the picture":

- `FixedTableBlockIR::pictures` (`FixedTablePictureIR`): resource id,
  placeholder words, the preformatted line the picture sits on, and the
  line-relative columns it covers. `admit_preformatted` records it, blanks
  those columns in the reproduced line, and fails closed unless the blanked
  span spells the compiler's own `PICTURE <n>` -- which is what proves the
  selector's columns are the same coordinates the line text is indexed by.
- The verbatim lowering emits one `FigureBlockIR` per picture, in line order,
  ahead of the fenced block; a fenced block carries no inline, so the image
  stands beside the art whose blanked slot it belongs in. The destination is
  `resource:<n>` and the renderer derives `alt` from it, so the alt text is
  hosted's.
- A region the source declared a `:table` keeps its Markdown table, and the
  picture becomes an `ImageInlineIR` over the cell line that spells its
  placeholder -- the same replacement `line_link` already does for a cell
  link. The resource must be in the book's catalog or the topic fails closed.
- Picture-target spelling, the resource id and the placeholder words are the
  figure family's (`figure_picture_target`, `figure_picture_resource`,
  `figure_picture_placeholder`, exported from `figure_block_ir.hpp`), so both
  families recognise exactly the same selectors.

## Measured

Typed route: **7,032 -> 7,053 of 7,362 (95.52% -> 95.80%)**, 21 topics gained,
none lost. Per book: `GG24-395` 205 -> 223, `GX27-3999-00` 25 -> 28.

Whole-corpus `boo2git --force` before/after (a separate `origin/main` build):
27 files differ, all in the two books -- 21 newly typed topics, 2 legacy
topics whose render-diagnostic reason text changed (`GG24-395` `3.3.4`,
`3.3.15`), `GG24-395/figures.md`, and the two `render-diagnostics.tsv`.

- Images: 394 -> 398 references, no destination lost anywhere in the corpus.
  The four gained are `GG24-395` `67.png` (3.3.6), `87.png`, `88.png` and
  `89.png` (3.3.14), which the legacy route had drawn as placeholder text and
  ASCII rules.
- Links: 3 dangling destinations repaired (`#TBLNOSENVI`, `#TBLNOSTAB`,
  `#TBLNOSENV2` now exist because the typed route emits the table anchor),
  none broken. `figures.md` follows `3.3.14`'s figure anchors from the legacy
  `#FCRR101`/`#FCRR102` to `#FIGFCRR101`/`#FIGFCRR102`, which is the spelling
  hosted serves (`<a name="FIGFCRR101">`).
- Line-for-line against hosted's `<pre>`: 18 of the 20 non-`cz OFF TABLE`
  topics match exactly, including every blanked picture line. `3.3.6` and
  `3.3.14` match except that hosted prints the placeholder words where its
  `.GIF` is missing (above). `3.2.2` matches except for two `SI` index lines
  (below).

## Still open

- `GG24-395` `3.3.4` still declines: its `SI VTAM, SNA` line owns a positioned
  display cell, so `collect_index_lines` leaves it in the region and the
  picture's placeholder is not on the line after the selector. The same rule
  makes `3.2.2` reproduce two `SI` lines hosted does not show. Extending the
  index-line rule to positioned lines would fix both and is a separate slice:
  it changes which lines every fixed-layout region reproduces.
- `GX27-3999-00` `1.3` and `2.6` are `cz OFF TABLE` regions hosted serves as
  `<table>`, but their column model declines for unrelated reasons ("visible
  source between table lines"), so they take the verbatim fallback. Nothing is
  lost; the grid is not recovered.
- `GX27-3999-00` `3.2` exposes an existing limit of the cell-link model:
  `cselect 24 10 HDRAPB` covers the wrapped continuation lines of its cell as
  well, so three lines carry a link hosted does not. That is `line_link`'s
  coverage rule, not the picture work.
- The `°` bullet is dropped from table cells (both routes), the known
  per-book display-translation gap.
