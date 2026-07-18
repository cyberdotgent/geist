# BOO rendering issues

Comparison scope: every directory under `render/` was inventoried. For books
present in `BookServerCache/`, the cached `booksrc` HTML was compared with the
corresponding `boo2git` Markdown. The topic names below are BookManager topic
IDs; paths point at the local output. Differences in whitespace alone are
reported only when they change fixed-width/preformatted presentation.

## `BOO/IEAC6MST.BOO`

- Fixed-width/preformatted BookServer topics are reflowed in Markdown: `NOTICES`, `EDITION`, `CONTENTS`, `FIGURES`, `FRONT_1`, `PREFACE.2`, `CHANGES`, `1.0`, `1.1.1`, `1.1.2`, `1.2`, `1.2.1`–`1.2.4`.
- Emphasis/style spans are split across partial words in `COVER`, `NOTICES`, `EDITION`, `FRONT_1.1`, and `CHANGES`.
- `FIGURES` loses BookServer figure links and maps some entries to the wrong Markdown topic/image targets.
- `COVER`, `EDITION`, `FRONT_1`, `PREFACE.2`, and `1.2` have materially low text agreement after normalization.

## `BOO/ITPPIBOK.BOO`

- Fixed-width/preformatted layout is reflowed in `EDITION`, `SPECIAL`, `CONTENTS`, `FIGURES`, `PREFACE`, `PREFACE.2`, `1.1`, `1.2`, `1.2.1`, `1.3`, `1.3.1`–`1.3.3`.
- Torn emphasis/style spans occur in `TITLE`, `COVER`, `EDITION`, and `SPECIAL`.
- `FIGURES` and `TABLES` lose or alter BookServer links/targets; `TABLES` has only moderate normalized text agreement.
- `TITLE`, `COVER`, `PREFACE.1`, `PREFACE.2`, `PREFACE.4`, `1.0`, `1.2`, and `1.3` have low text agreement.

## `BOO/N2AH1MST.BOO`

- Fixed-width/preformatted layout is reflowed in `NOTICES`, `EDITION`, `CONTENTS`, `FRONT_1`, `FRONT_1.1`, `PREFACE`, `PREFACE.2`, `PREFACE.4`, `PREFACE.4.1`, `PREFACE.4.2`, `PREFACE.4.3.1`, and `1.0`.
- Torn emphasis/style spans occur in `TITLE`, `NOTICES`, `EDITION`, `FRONT_1.2`, `PREFACE.2`, `PREFACE.4`, and `PREFACE.4.3.1`.
- `PREFACE.4.4` and `CHANGES` could not be found in the hosted catalog, so no rendering verdict is possible.
- `TITLE`, `EDITION`, `FRONT_1`, `PREFACE`, `PREFACE.3`, `PREFACE.4`, `PREFACE.4.1`, `PREFACE.4.2`, and `1.0` have low text agreement.

## `BOO/OFCUSEOV.BOO`

- Fixed-width/preformatted layout is reflowed in `EDITION`, `FRONT1`, `PREFACE`, `PREFACE.2`–`PREFACE.5.2`, `PREFACE.5.5`, `PREFACE.6`, `CONTENTS`, and `1.0`–`1.2`.
- Torn emphasis/style spans occur in `COVER`, `PREFACE`, `PREFACE.1`, `PREFACE.6`, `1.1`, and `1.2`.
- `PREFACE.1` and `PREFACE.6` contain extra decoded/rendered tokens not present in BookServer output, suggesting bad boundary/markup extraction rather than simple reflow.
- `TITLE`, `COVER`, `EDITION`, `FRONT1`, `PREFACE`, `PREFACE.1`, `PREFACE.2`, `PREFACE.5`, `PREFACE.5.2`–`PREFACE.5.5`, and `PREFACE.6` have low text agreement.

## `BOO/OFCWPUSR.BOO`

- The cache contains BookServer HTML for this book, but the earlier backlog did not include it. It needs the same topic-by-topic comparison pass before issue status can be considered complete.
- Initial inspection shows the same likely classes as `OFCUSEOV`: fixed-width/preformatted sections reflowed and emphasis spans split across words. Treat these as leads, not confirmed topic-level findings, until the cache is regenerated/compared.

## `BOO/packet.boo`

- Torn emphasis/style spans occur in `COVER`, `TITLE`, `EDITION`, `PREFACE`, `1.1`, `1.2`, `1.3`, `2.1`, `2.1.1`, `2.1.3`, `2.1.4`, and `2.2.1`.
- Fixed-width/preformatted layout is reflowed in `CONTENTS`, `1.3`, `2.1.3`, `2.1.4`, and `2.2.1`.
- `FIGURES` loses the BookServer figure-to-image/topic links; `CONTENTS` also differs in link-oriented structure.
- `COVER`, `1.1`, `1.2`, `1.3`, `2.1`, `2.1.3`, `2.1.4`, and `2.2.1` have meaningful normalized differences.

## `BOO/QS3X36CM.BOO`

- Fixed-width/preformatted layout is reflowed in `EDITION`, `1.0`, `1.1`, `2.0`, `2.1`, `2.2`, `2.3`, and `A.0`.
- `COVER` contains torn emphasis/style spans and has low text agreement.
- The remaining differences are primarily layout/structure differences; normalized text agreement is otherwise high.

## `BOO/QSYSINFO.BOO`

- Fixed-width/preformatted layout is reflowed in `EDITION`, `FRONT_1`, `FRONT_2`, `FRONT_2.1`, `1.1`, `1.1.1`–`1.1.6`, `1.2`, `1.2.1`, `1.2.1.1`, `1.2.2`–`1.2.4`.
- Torn emphasis/style spans occur in `COVER`, `NOTICES`, `1.2.1.1`, and `1.2.4`.
- `CONTENTS` was not found in the hosted catalog.
- `COVER`, `EDITION`, `FRONT_1`, and `1.1.5` have low text agreement; `NOTICES` also loses its fixed-width callout structure.

## `BOO/QSYSNEWG.BOO`

- Fixed-width/preformatted layout is reflowed in `EDITION`, `CONTENTS`, `FRONT_1`, `PREFACE`, `1.2`, `1.3`, `1.4`, `1.5`, `1.5.1`, `1.5.2`, `1.5.3`, `1.5.4`, `1.6`, `1.7`, and `1.8.1`.
- The `CFONT` visual-box tearing in `1.0` is fixed; the remaining difference there is intentional Markdown reflow of BookServer's `<pre width="80">` body.
- `2.0` and `2.1` have been fixed for `SREFIG`/`SRFIG` trailing text, text figures, and fixed-row `CFONT` spans; retain regression coverage.
- Remaining torn emphasis occurs in `COVER`, `NOTICES`, `EDITION`, `PREFACE`, `1.1`, `1.2`, `1.4`, `1.5.3`, `1.7`, and `1.8.1`.
- `1.8` was not found in the hosted catalog. `COVER`, `EDITION`, `FRONT_1`, `1.1`, `1.2`, `1.4`, and `1.5.2` have low text agreement.

## Hosted comparison unavailable

The hosted catalog returned “book could not be located” for the cached sample
topics of these local BOOs, so these are comparison gaps rather than confirmed
renderer bugs: `GG24-4302-00`, `GX27-3999-00`, `SC09-2417-00`, `SC24-5520-00`,
`SC24-5527-02`, and `SC28-1881-05`.

The raw per-topic backlog and scores remain in `todo.md`; this report is the
condensed, BOO-grouped issue list intended for rendering work.
