# A verbatim region keeps its own left margin (2026-08-30)

Workflow note for the slice that stopped `ProseBlockIR` stripping the common
indent of a preformatted region.  The normative facts are in `Format/markup.md`
§"The region's left margin is content" and §"Verbatim `CZ` regions"; this note
records how the evidence was gathered.  It follows
`verbatim-fixed-layout-regions-2026-08-30.md`, which measured the gap and left
it open.

## Where the margin was stripped

Two sites, both in the prose family, both removing the smallest leading-space
run shared by the region's non-blank rows:

- `libgeist/src/prose_topic_cz.cpp`, `CzBlocks::preformatted` -- the
  `cz OFF XMP` / `SCREEN` / `LBLBOX` example block.
- `libgeist/src/prose_topic_blocks.cpp`, `build_box_block` -- the
  flattened-dialect drawn box region.

Nothing else strips.  The figure family (`figure_document_lowering.cpp`) and
the fixed-table family (`fixed_table_document_lowering.cpp`) copy their line
text through unchanged, and both already agreed with hosted about columns.

## Establishing the margin per region kind

The Markdown a fenced block carries names no region kind, so the census was
taken with a throwaway probe: a build that appends `origin->detail` to the
opening fence (`GEIST_FENCE_PROBE`), and a build that skips both strips
(`GEIST_KEEP_MARGIN`), both in a scratch `build_dbg/` tree.  Exporting the
whole corpus twice with the probe fence gives, per kind, the shift needed to
find our block as a contiguous run of the hosted `<pre>` lines.

Corpus block census by kind: fixed table 1,683, figure 982, drawn box 856,
`cz OFF XMP` 486, and eight others.  Only two books carry `cz OFF XMP`
(SC09-2417-00 263, packet 223); drawn boxes appear in 24 books, with OFCUSEOV
(230), SC24-546 (109) and SH20-918 (84) leading.

Full-population comparison against hosted (every block of the two prose kinds,
646 topics, no sampling):

| Kind | strip | keep |
| --- | --- | --- |
| fixed table region | 95.5% exact (sampled 399) | not changed |
| figure block | 80.5% exact (sampled 303) | not changed |
| `cz OFF XMP` example block | 0 of 486 exact | 454 of 486 (93.4%) |
| drawn box region | 0 of 856 exact | 628 of 856 (73.4%) |

The strip is wrong for both prose kinds and the margin is per region, not per
book: SC09-2417-00 `3.1.3.5` serves its `#pragma mapinc` example at column 5
and its three DDS listings at column 10 on one page.  The residual "no match"
counts (32 and 228) are identical before and after, so no block that already
differed for another reason moved.

**The `cz OFF XMP` caution does not apply here.**
`prose-font-control-residuals-2026-08-30.md` records that the unguarded
two-space-run margin rule "re-indents the verbatim rows of SC09-2417-00
`4.1.9.4`'s `cz OFF XMP` COBOL listing by ten columns".  That rule *infers* a
margin from a display line's cells; this slice infers nothing and only stops
shifting rows the row model already read at their columns.  SC09-2417-00
`4.1.9.4` is exact against hosted after the change: hosted serves the listing
at column 5, which is where the rows already were.

## Line-for-line movement

`final.py` from the verbatim slice, re-run unchanged on that slice's export,
still reports its published numbers: of 410 pre-existing verbatim blocks in
its seed-3 sample, 139 (33.9%) exact and 183 (44.6%) exact after restoring the
region left margin.

Re-run on the current tree with one identical block population (same sample,
same "pre-existing" classification taken from the pre-verbatim-slice export,
block text read from the before and after exports of this slice) and with
hosted's CRLF stripped, which the original did not do:

| | blocks | exact | exact after restoring the margin |
| --- | ---: | ---: | ---: |
| before this slice | 547 | 234 (42.8%) | 172 (31.4%) |
| after this slice | 547 | **406 (74.2%)** | 0 |

Every margin-restorable block converted; the 128 "content differs" and 10
"interior spacing differs" blocks are unchanged, and one block moved from
"content differs" to "all lines present, not contiguous", not the reverse.
The 42.8% baseline is higher than the note's 33.9% because the corpus moved
between the two slices and because OFCUSEOV is served with CRLF, which the
original comparator did not strip.

## Corpus effect

`boo2git --force` over all 34 fixtures, before and after: 646 `.md` files
differ, in 26 books.  Every one of the 646 has the same line count, the same
line content, and only more leading spaces on the lines that changed -- the
shape check is mechanical, so no topic can lose a word.  The word-conservation
sweep confirms it: 646 changed topics checked, 0 lost an alphanumeric word,
0 gained one.

Typed-route coverage is unchanged at 7,032 / 7,362, histogram `typed` 7,031 /
`typed-degraded` 1 / `legacy-fallback` 326 / `best-effort` 4 / `failed` 0.

## Re-pinned tests

Four assertions pinned the stripped form and were re-pinned to hosted's
columns, each quoting the hosted page and DT:

- `libgeist/tests/prose_topic_ir_synthetic.cpp`: QSYSNEWG `1.0` box top rule
  (DT `19910524085706`, column 4), packet `3.2` example block and packet
  `2.4.1` `cz OFF EXMP` body (DT `20260614112503`, column 5).
- `libgeist/tests/packet_markdown.cpp`: the `axports` and NET/ROM broadcast
  example blocks (same DT, column 5).
