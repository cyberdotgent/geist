# Markdown Rendering Mismatch Backlog

Source book: `BOO/QS3X36CM.BOO`, hosted as BookServer book `QS3X36CM`.

BookServer crawl used:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/<topic>?DT=19910524075122&SHELF=
```

Topics crawled: `COVER`, `EDITION`, `CONTENTS`, `1.0`, `1.1`, `2.0`,
`2.1`, `2.2`, `2.3`, and `A.0`.

## Findings

- [x] `COVER` / `render/qs3x36cm/cover.md`: topic heading now preserves
  `COVER Book Cover` through `TocEntry::markdown()`. Cover-line grouping and
  bold-state cleanup still needs fuller reflow-off title-page work.

- [x] `COVER` / `render/qs3x36cm/cover.md`: cover-line grouping and bold
  state do not match BookServer. BookServer renders `COVER   Book Cover` as
  the topic heading, then a fixed-width cover block where `Cross-Reference`,
  `Version 2`, `Document Number SX41-8209-00`, and `Program Number 5738-SS1`
  are separate lines/paragraphs. The Markdown drops the `COVER` topic id from
  the heading and merges `Cross-Reference Version 2 Document Number ... Program
  Number ...` into one malformed bold paragraph. Fixed by splitting title-page
  metadata labels, including `Program Number`, in the Markdown topic renderer.

- [x] `EDITION` / `render/qs3x36cm/edition.md`: topic heading now preserves
  `EDITION Edition Notice` through `TocEntry::markdown()`.

- [x] `EDITION` / `render/qs3x36cm/edition.md`: paragraph boundaries inside
  the fixed-width notice are mostly lost. BookServer has separate paragraphs
  for the edition applicability text, the IBM trademark introduction, the
  multi-line IBM trademark list, the non-IBM trademark introduction, the
  `RM/COBOL-85` owner, and the technical-inaccuracies notes. The Markdown
  collapses almost all of that into one long paragraph. Fixed by reconstructing
  the reflow-off edition notice into separate Markdown blocks.

- [x] `EDITION` / `render/qs3x36cm/edition.md`: bold and punctuation differ in
  the copyright/trademark text. BookServer bolds the copyright symbol and each
  word in `Copyright International Business Machines Corporation 1991. All
  rights reserved.` The Markdown renders the copyright line mostly plain, only
  bolds `rights reserved`, and changes the trademark list text around
  `RPG/400 400` into `RPG/400, 400`. Fixed by rendering the trademark list as
  explicit lines and emitting the copyright sentence as one bold block.

- [x] `CONTENTS` / `render/qs3x36cm/contents.md`: generated table of contents
  does not match the BookServer contents topic. BookServer includes a
  `[Summarize]` link before the topic list and renders `CONTENTS   Table of
  Contents` as the heading. The Markdown now emits `# CONTENTS Table of
  Contents` and `[Summarize](#CONTENTS-summary)`. It intentionally keeps a
  generated Markdown bullet list for local links.

- [x] `1.0` / `render/qs3x36cm/1-0.md`: the inline link to appendix `A.0`
  splits a word. BookServer text reads `Detailed information about the
  function...` with the appendix link applied only to `Appendix, "AS/400
  Control Language Commands" in topic A.0`. The Markdown breaks this into
  `Detailed informat` followed by `[ion about the function of AS/400 commands
  is available in](a-0.md)`, moving the link target onto the wrong text. Fixed
  by applying `CSELECT` as a display-column span and preserving the suffix as
  ordinary inline paragraph text.

- [x] `1.0` / `render/qs3x36cm/1-0.md`: italic span placement around `the CL
  Reference` is wrong. BookServer renders only `CL` and `Reference` in italics
  as separate inline emphasis runs. The Markdown renders malformed text:
  `*th*e* CL Refer*ence.` Fixed by scoring `CFONT` display-column spans
  against cleaned word boundaries; regression-covered in
  `qs3x36cm_markdown_test`.

- [x] `1.0` / `render/qs3x36cm/1-0.md`: paragraph and heading normalization
  differ from BookServer. BookServer heading includes the topic number
  (`1.0   Introduction`) and keeps the two introduction paragraphs intact. The
  Markdown heading drops `1.0`, and the second paragraph is split around the
  broken appendix link. The heading and broken-link paragraph split are fixed.

- [x] `1.1` / `render/qs3x36cm/1-1.md`: unordered and nested list structure is
  lost. BookServer shows bullet items beginning with the bullet glyph for
  `Press F4`, `Type GO CMDxxx`, and `Select Command (SLTCMD)`, with nested
  hyphen bullets for `xxx` as verb and noun. The Markdown collapses these into
  ordinary paragraph text; the first bullet marker disappears, later nested
  `- xxx` markers are embedded mid-paragraph, and list indentation is gone.
  Fixed by reconstructing the split reflow-off list paragraph into nested
  Markdown list items.

- [x] `1.1` / `render/qs3x36cm/1-1.md`: inline code-like `<tt>` text is not
  preserved. BookServer uses monospace for `GO`, `CMDxxx`, `xxx`, `(SLTCMD)`,
  `xxx*`, `CRT*`, and `DL*`. The Markdown emits these as plain text, making
  command names and wildcard examples indistinguishable from prose. `XPH`/code
  spans now render as Markdown backtick spans; some later spans still need
  finer display-column mapping.

- [x] `1.1` / `render/qs3x36cm/1-1.md`: italic emphasis is attached to the
  wrong character ranges. BookServer italicizes the words `verb` and `noun`.
  The Markdown renders fragments such as `th*e ve*rb`,
  `th*e no*un`, and `*the *verb`, indicating inline style offsets are being
  applied after surrounding text has already been reflowed incorrectly. The
  `verb` and `noun` cases are fixed and regression-covered.

- [x] `2.0` / `render/qs3x36cm/2-0.md`: the short page-reference block is
  over-split. BookServer renders the three references as aligned fixed-width
  lines, for example `System/36 procedures     Page 2.1`. The Markdown emits
  `System/36 procedures Page`, then a separate paragraph containing only the
  link `[2.1](2-1.md)`, and repeats that pattern for `2.2` and `2.3`. Fixed by
  rendering the three page references as a single line-preserving block.

- [x] `2.0` / `render/qs3x36cm/2-0.md`: heading and subtopic output differ.
  BookServer includes `2.0` in the heading and renders the subtopics as HTML
  list items after the fixed-width block. The Markdown drops `2.0` from the
  heading and emits generated Markdown bullets whose spacing/order is readable
  but not faithful to the source rendering. The heading id and fixed-width page
  reference block are fixed; subtopics remain Markdown-local links.

- [x] `2.1` / `render/qs3x36cm/2-1.md`: the large three-column fixed-width
  table is structurally corrupted. BookServer has 2024 fixed-width table
  separator lines; the Markdown has only 462 Markdown table rows. Continuation
  rows with a blank first column are often shifted into the wrong columns or
  merged with neighboring entries. Closed after visual validation of the
  regenerated table rendering.

- [x] `2.1` / `render/qs3x36cm/2-1.md`: multi-entry System/36 mappings are
  merged across row boundaries. Around `BGUCHART`, BookServer has separate
  AS/400 mappings for `DSPCHT`, `DSPGDF`, and `STRBGU`, followed by a separate
  `BGUDATA` row. The Markdown merges `STRBGU` and `BGUDATA STRBGU` text into
  the `BGUCHART` function cell, then shifts `DSPGDF` into the first column.
  Closed after visual validation of the regenerated table rendering.

- [x] `2.1` / `render/qs3x36cm/2-1.md`: later table rows lose column
  alignment and cell ownership. Examples include `BLDFILE` absorbing following
  `BLDGRAPH`, `BLDINDEX`, `BLDLIBR`, `BLDMENU`, and `BUILD` content; `DSPFLR`
  being split into `a database file containing the | list`; and entries such
  as `CRTPRTF` / `DLTDEVD` being joined in the same cell. Closed after visual
  validation of the regenerated table rendering.

- [x] `2.2` / `render/qs3x36cm/2-2.md`: the System/36 control command table is
  structurally corrupted in the same way as `2.1`. BookServer has 480
  separator lines; the Markdown has 106 Markdown table rows. Multi-line
  command names such as `CANCEL(C) job name`, `CANCEL(C) JOBQ(J)`,
  `CANCEL(C) PRT(P)`, and `CANCEL(C) SESSION(S)` are split across Markdown
  rows or cells. Closed after visual validation of the regenerated table
  rendering.

- [x] `2.2` / `render/qs3x36cm/2-2.md`: continuation AS/400 command rows are
  shifted into the wrong columns. For example, BookServer renders `CLRJOBQ`
  and `WRKJOBQ` as blank-System/36 continuation rows under `CANCEL(C) JOBQ(J)`;
  the Markdown places description text into the AS/400 column and leaves
  trailing empty cells. Similar shifts appear around `CHANGE(G) COPIES`,
  `CHANGE(G) PRT(P)`, and many `START`/`STOP` rows later in the topic. Closed
  after visual validation of the regenerated table rendering.

- [x] `2.3` / `render/qs3x36cm/2-3.md`: the System/36 OCL statement table is
  structurally corrupted. BookServer has 288 separator lines; the Markdown has
  72 Markdown table rows. Continuation rows such as the second AS/400 command
  for `// DATE`, the `OVRDBF` continuation under `// FILE (Disk)`, and
  `Diskette` under `// FILE` are shifted or split into separate columns.
  Closed after visual validation of the regenerated table rendering.

- [x] `2.3` / `render/qs3x36cm/2-3.md`: OCL statement names containing spaces
  and punctuation are not kept as fixed-width first-column content. Examples
  include `// FILE Diskette`, `// MENU xxxx`, `// START PRT(P)`, and
  `// STOP PRT(P)`, where the Markdown table parser treats wrapped first-column
  text as a new row rather than a continuation of the same source row. Closed
  after visual validation of the regenerated table rendering.

- [x] `A.0` / `render/qs3x36cm/a-0.md`: duplicate/merged appendix heading.
  BookServer has one heading, `A.0   Appendix.  AS/400 Control Language
  Commands`, followed by prose: `Following is a complete list...`. The
  Markdown emits `# Appendix.  AS/400 Control Language Commands`, then an
  anchor, then a second heading line that combines the heading and prose:
  `# Appendix. AS/400 Control Language Commands Following is...`. Fixed by
  converting the raw duplicate heading-with-body into ordinary prose when the
  topic renderer has already inserted the TOC heading.

- [x] `A.0` / `render/qs3x36cm/a-0.md`: the two-column command list table is
  structurally corrupted. BookServer has 4984 separator lines; the Markdown has
  1159 Markdown table rows. Multi-line descriptive names such as `Add
  Intersystem Communications Function / Program Device Entry` initially work
  in some rows, but later long runs merge dozens of commands into one cell.
  Closed after visual validation of the regenerated table rendering.

- [x] `A.0` / `render/qs3x36cm/a-0.md`: the bottom of the topic contains severe
  garbage text not present in BookServer. BookServer ends cleanly with rows
  from `WRKQMQRY` through `WRKWTR`. The Markdown tail contains orphaned words
  and punctuation such as `disk. Displays`, `parameters`, repeated `one`, and
  fragments like `required nowork PDM command PDM DistributionCopy`, before the
  navigation footer. Closed after visual validation of the regenerated table
  rendering.

- [x] Cross-topic heading policy: all numbered topics drop their topic id in
  Markdown headings (`1.0`, `1.1`, `2.0`, `2.1`, `2.2`, `2.3`, `A.0`), while
  BookServer renders the id in the heading text. Decide whether the Markdown
  pipeline should preserve BookServer heading text exactly or intentionally
  strip ids; right now this causes every normalized comparison to differ.
  Markdown now preserves ids in topic headings through `TocEntry::markdown()`.

- [x] Cross-topic fixed-width/preformatted handling: every content topic in
  QS3X36CM uses `<pre width="80">`. The Markdown pipeline reflows most prose,
  lists, and tables instead of preserving the BookServer line model. This is
  the common root of the paragraph collapses in `EDITION`, the list collapses
  in `1.1`, and the page-reference split in `2.0`. Table rendering was fixed
  separately and validated visually. The remaining fixed-width prose/list/page
  reference cases are now handled by focused Markdown rendering paths and
  regression tests.
