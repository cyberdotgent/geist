# Agent Log

## 2026-08-25 - Decode BOO and external CSELECT image targets

- Restored all 15 confirmed `GG24-395` `PICnn` resources and retained visible
  table prose across selector, logical-record, and `SRETBL` boundaries while
  suppressing generated `PICTURE nn` and subject-index metadata.
- Decoded XWEBDEMO `LNK` alternative groups into inline images or labeled
  `/bookmgr/`, HTTP, and FTP links, including video and audio targets; repaired
  the associated Figure 2/Figure 3 selector spans and figure list labels.
- Added fixture regressions for every affected GG24 topic and each observed
  XWEB selector form. All nine CTest tests pass; complete cached comparisons
  rendered 226/226 GG24-395 and 13/13 XWEBDEMO topics with no fetch failures.
- Opened #36 for the independently confirmed XWEBDEMO title/body layout-control
  leakage found during the required full-book recheck.

## 2026-08-25 - Suppress generated BookManager layout controls

- Removed leaked page/layout controls (`c.cc`, `cfont`, generated topic
  metadata, and inline `:H3`/`:H4`) from visible Markdown while retaining the
  surrounding prose.
- Recognized reflow-off blocks carrying duplicated `cmenu`/`cmitem` metadata
  as normal introductory prose; the separately decoded menu remains the single
  Markdown subtopic list instead of exposing controls inside a code fence.
- Added a 229-topic negative regression scan plus positive preface prose/menu
  assertions. All nine CTest tests and full Markdown rendering of all 34 BOO
  fixtures pass.
- Replayed the complete cached GG24 BookServer comparison: heuristic flags fell
  from 41 to 34, with no leaked control tokens and no new defect class.

## 2026-08-25 - Complete GG24 figure and table rendering

- Preserved explicit row boundaries in tables whose blank leading header cell
  represents a real unlabeled column, without changing the continuation-row
  behavior used by existing AS/400 tables.
- Kept both halves of wide fixed-report rows separated by BOO display markers;
  GG24 RMF reports now retain fields such as interval, resource-group,
  service-rate, page-in-rate, and storage columns.
- Emitted `PICnn` resources even when their `CSELECT` has no adjacent display
  text, restoring GG24 resource 25, and suppressed matching ASCII placeholder
  boxes whenever an image-backed figure is available.
- Added regressions for tables 5 and 15, figures 12/13 and 20/42, all 34 local
  BOO fixtures, all nine CTest tests, and a cached 229-topic GG24 BookServer
  replay. Regenerated the complete checked-in GG24 render.

## 2026-08-25 - Preserve prose attached to topic headings

- Made TOC-heading replacement match title boundaries case-insensitively while
  tolerating BOO whitespace differences and the observed `*` separator.
- Preserved the remainder of a heading record as a normal paragraph instead of
  discarding it, covering GG24 topics `5.0`, `7.0`, `8.0`, `10.1`, and `11.0`;
  retained later-record prose in `5.1.1` is covered by the same regression set.
- Re-ran all 229 GG24 topics against freshly fetched BookServer pages and an
  offline cache replay. The audit found no additional defect class; it did
  confirm residual CFONT corruption already described by issue #9 and control,
  figure, table, and edition problems already tracked by #6, #7, and #1.
- Validated all CTest tests and regenerated the complete checked-in GG24 render.

## 2026-08-25 - Bound topic decoding to the declared content run

- Fixed GG24 `COMMENTS` corruption by removing the experimental decoder's
  fallback scan of every class-`0x0001` page in the file. The directory content
  run ends at physical page 117; later class-`0x0001` pages belong to a separate
  stream and had extended the final topic from four records to 1,308.
- Added regression coverage for directory logical-record count, the exact
  `COMMENTS` range, bounded output size, and retained questionnaire text.
- Validated the complete CTest suite and all 34 BOO fixtures through `booinfo`
  and `bootoc`.

## 2026-08-25 - Fix P0 topic and body-loss rendering failures

- Fixed split topic headers such as `GG24-4302-00` topic `2.6`, whose standalone
  `SH2.6` boundary precedes its `CTOPICN`, heading, and title metadata.
- Preserved subject-index prose aligned with four-space margins and stopped
  treating ordinary uppercase words beginning with `SH` (`SHOULD`, `SHIPPED`,
  and `SHARING`) as controls.
- Made `CFONT` span projection use display characters while retaining UTF-8
  byte boundaries, fixing torn words and malformed Markdown in GG24, SC24-546,
  and PRG1SORT.
- Reconstructed SC31-605 fixed-width table bodies when styled headings and
  later data records use different separator layouts. Added regression coverage
  for representative topics in every range reported by issue #13.
- Validated the complete CMake/CTest suite and strict UTF-8 rendering checks,
  then regenerated the checked-in `render/` books for manual review.

## 2026-08-25 - Expand the BOO dataset from BookServer

- Selected 20 books at deterministic alphabetic quantiles of the 5,819-entry
  hosted BookServer catalog, avoiding selection based on current renderer
  behavior.
- Downloaded the original `application/x-boo` payloads under their
  server-provided filenames and added them to `BOO/` without modification.
- Recorded exact BookServer IDs, timestamps, sizes, topic counts, document
  numbers, URLs, and SHA-256 values in
  `AnalysisNotes/bookserver-dataset-2026-08-25.md`.
- Validated every new fixture with `booinfo` and `bootoc` before insertion.
- Made the whole-book audit robust to invalid UTF-8 emitted by an individual
  local topic: preserve the comparison with replacement characters and flag it
  as `invalid-utf8` instead of aborting the remaining book.
- Fetched and compared all 4,266 topics across the 20 books, then reviewed them
  in three balanced lightweight-agent batches. Created one tracking issue per
  book (#15–#34), four new shared defect issues (#11–#14), and added cross-book
  evidence to existing defect issues #3 and #6–#9.

## 2026-08-25 - Add reproducible whole-book rendering audits

- Added `tools/bookserver_book_audit.py` to inventory every BOO TOC topic,
  cache its exact hosted BookServer HTML, render local Markdown, and emit a
  structural/text comparison manifest plus normalized diffs.
- Documented the reusable audit and issue-evidence workflow in
  `AnalysisNotes/whole-book-rendering-audits.md`, including the verified hosted
  identity, timestamp, URL, and fixture checksum for `GG24-4302-00`.
- Completed a 229-topic capture for `GG24-4302-00` in temporary storage: all
  hosted requests succeeded and an offline rerun reproduced the 229-row audit.
- Validated the local baseline with a complete CMake build and all eight CTest
  tests passing.

## 2026-06-17 - Add logical record trace tooling

- Added a stable libgeist trace surface for decoded logical records:
  `BooDocument::decoded_logical_records()`,
  `BooDocument::font_definitions()`, and
  `BooDocument::trace_logical_records(topic_id)`.
- Added `bootrace`, a diagnostic CLI that prints a three-column topic trace of
  logical record number, decoded control stream, and normalized GML projection,
  plus `CFONTDEF` and `CFONT` span details for formatting analysis.
- Added `tools/bookserver_html_compare.py`, a dependency-free Python normalizer
  that fetches or reads BookServer chapter HTML, preserves heading/paragraph and
  nested bold/italic markers, and diffs the normalized stream against local
  Markdown output.

## 2026-06-17 - Replace GDF byte scanner with record parser

- Re-analyzed legacy kind `G` rendering through the connected `ephimage.dll`
  and newly loaded `IMGDF2.FLT` IDBs. Verified that BookServer loads
  `isgdi32.dll`, `imgdf2.flt`, and `ebgif2.flt` for kind `G`, and that
  `IMGDF2.FLT` parses a header plus opcode/length-framed GDF records before
  calling ISGDI functions such as `CLine`.
- Replaced the previous `libgeist/src/boo_gdf.cpp` coordinate-run scanner with
  a conservative GDF record parser. It now reads the declared picture extent,
  skips unknown records by the IBM filter's length rule, and renders only
  verified line/current-point opcodes (`0x21`, `0x61`, `0x81`, `0xc1`,
  `0xe1`).
- Staged a temporary IBM DLL/filter harness under `tmp/gdf-harness` using
  `ephimage.dll`, `ISGDI32.DLL`, `IMGDF2.FLT`, and `EBGIF2.FLT`; the high-level
  `ephimage` call returned failure for `GG66-3212-00.boo` kind `G`, so the
  self-contained renderer was validated visually against the IDB-derived record
  grammar instead.
- Updated `Format/GDF.md` with the header layout, record framing rules, opcode
  evidence, and the reason byte-scanning produced false fan lines.
- Validated with `cmake --build build --target boorsrc` and PNG renders for
  `BOO\GG66-3212-00.boo` resources `1` and `2` into `tmp\`. Both now produce
  visually recognizable chart images without the previous spurious fan lines.

## 2026-06-14 - Add legacy GDF PNG rendering

- Implemented a private `legacy-gdf` rendering path in `libgeist`: added
  `boo_gdf.cpp` to decode observed GDDM/GDF vector payloads by reading IBM
  hexadecimal floating-point coordinate runs into an RGBA canvas, then routed
  `BooDocument::read_resource_png()` through the existing PNG encoder.
- Kept raw extraction unchanged and kept unsupported legacy MMR/MET, JPEG,
  TIFF, and CGM payloads reporting a clear unsupported conversion error.
- Added `boo_gdf.cpp` to the CMake source list and shared the internal
  `RgbaImage` helper across the PNG and GDF implementation files.
- Updated `Format/assets.md` with the current GDF rendering scope and remaining
  limitations: no full GDDM command grammar, color/style attributes, filled
  areas, or exact text/font semantics yet.
- Validated with `cmake --build build`, `boorsrc --png BOO\GG66-3212-00.boo 1
  build\geist-gdf-1.png`, `boorsrc --png BOO\GG66-3212-00.boo 2
  build\geist-gdf-2.png`, `boorsrc --list BOO\GG66-3212-00.boo`, and expected
  unsupported errors for `GG66-3212-00.boo` resource `3` and
  `GG24-4302-00.boo` resource `1`.
- Random BOO fixtures selected for resource-list coverage:
  `GV40-0405-00.boo`, `GX27-3909-04.boo`, `S544-3115-00.boo`,
  `SC26-3073-00.boo`, and `SC24-5461-00.boo`. The first three completed
  `boorsrc --list`; `SC26-3073-00.boo` timed out after printing
  `No assets found.`; `SC24-5461-00.boo` timed out without output.
- During validation, stale `boo2git.exe` processes from the build tree held
  `build\Debug\geist.dll`; stopped those processes before relinking.
- Commit: be59308.

## 2026-06-14 - Fix Markdown rendering for flat BOO tables

- Investigated `render/qs3x36cm/2-2.md` for `QS3X36CM.BOO` topic `2.2`,
  which was rendering `SRTBLtbluniq2` as paragraphs instead of a Markdown
  table.
- Fixed raw GML projection so ordinary table text beginning with `C` is
  preserved as text instead of being suppressed as an unknown generated
  control; this restores entries such as `Cancel(c)`, `Clrjobq`, and
  `Clears ...`.
- Added Markdown table assembly for `:table`/`:etable` blocks, including a
  fallback for flat three-column cross-reference tables where the decoded
  logical stream contains ordered cells but no explicit row/cell records.
- Updated `Format/markup.md` with the verified `QS3X36CM.BOO` table evidence
  and the remaining row/column-layout limitation.
- Regenerated `render/qs3x36cm/2-2.md` from `boorender --md`.
- Removed the persisted five-random-BOO validation instruction from
  `AGENTS.md` after the user rescinded that workflow.
- Commit: `6af412a`.

## 2026-06-14 - Add boo2git Markdown export tool

- Added `libgeist/examples/boo2git.cpp`, a Git-hosted Markdown exporter that
  writes `INDEX.md`, one topic Markdown file per TOC entry, and PNG-rendered
  resources into a destination folder.
- Implemented CLI help, verbose logging, `--force`, and an interactive
  non-empty destination confirmation guard.
- Added topic filename generation, TOC links, topic/anchor link rewriting, and
  picture-resource link normalization for `picN`/`pictureN` references to
  rendered `N.png` resources when available.
- Added `boo2git` to the CMake example build.
- Rendered `BOO\QS3X36CM.BOO` into `render\qs3x36cm`. The book produced
  `INDEX.md` and 10 topic files; the parser reported 0 resources for this
  fixture, so no PNGs were emitted for that rendered output.
- Validated with `cmake --build build`, `boo2git --help`, non-empty
  destination rejection with `n`, `boo2git --force --verbose
  BOO\QS3X36CM.BOO render\qs3x36cm`, and a targeted `boorsrc --png` check for
  `BOO\SC26-4221-08.boo` resource `1`.
- Residual risk: full `boo2git` conversion of larger asset-bearing fixtures
  such as `SC26-4221-08.boo` and `GG24-4302-00.boo` exited early without a
  C++ exception diagnostic after opening the book, while `booinfo` and direct
  `boorsrc --png` still worked on the tested converted GIF resource.
- Commit: `a523416`.

## 2026-06-14 - Align raw GML output with packet BookMaster source

- Treated `BOO/packet.script` as the authoritative BookMaster source for
  `BOO/packet.boo` tag names, tag punctuation, and source-level usage.
- Updated the raw GML projection to prefer BookMaster-style tags: topic
  `CHDLEVEL`/`ST` pairs now render as tags such as `:h1.`, `:preface.`,
  `:cover.`, `:tipage.`, and `:toc.`; `CZ Flow` records render to their
  source tag (`:p.`, headings, lists, etc.); generated menus render as
  `:ul.`, `:li.`, and `:eul.`; links render as `:hdref refid='...'`.
- Suppressed generated navigation metadata controls such as `SH`, `CTOPICN`,
  `CPARENT`, `CSUMMARY`, and `CSOURCEFN` from source-style raw output.
- Preserved visible trailing text carried by some `CFONT` records, including
  `:note.` text, while leaving full inline highlighted-phrase reconstruction as
  an open decoder task.
- Updated `Format/markup.md` to document `packet.script` as authoritative and
  to replace the old libgeist-specific projection table with the current
  BookMaster-style projection behavior.
- Validated with `cmake --build build -- /m:1`,
  `boorender packet.boo 1.0 --raw`, `boorender packet.boo preface --raw`,
  `boorender QS3X36CM.BOO 2.0 --raw`, and five filesystem BOO fixtures:
  `SC26-4559-01.boo`, `SC26-9642-00.boo`, `SC24-5595-01.boo`,
  `SC26-3229-01.boo`, and `SC26-3042-00.boo`.
- No failed untracked BOO fixture was added for this workload.
- Commit: `4ed8a54`.

## 2026-06-14 - Add Markdown rendering API

- Added `TocEntry::markdown()` for topic-level rendering and
  `BooDocument::markdown()` for whole-book rendering. Both reuse a shared
  Markdown projection over the existing GML-style raw records in
  `libgeist/src/boo_markdown.cpp`.
- Updated `boorender --md` to emit Markdown for either the full document or a
  selected topic id, matching the existing `--raw` topic/full-book behavior.
- Added the new renderer source to CMake and added MSVC `/FS` to avoid the
  parallel static-library PDB writer failure observed during validation.
- Validated with `cmake --build build`, full-book Markdown rendering for
  `BOO\QS3X36CM.BOO`, and topic Markdown rendering for topic `1.0`.
- Random BOO Markdown validation sample from the on-disk `BOO/` directory:
  `SC26-4381-00.boo`, `SC24-5520-00.boo`, `GH09-8078-03.boo`,
  `GX09-1269-00.boo`, and `SC23-0375-00.boo`. `boorender --md` passed for
  four files. `SC24-5520-00.boo` failed, and the failure also reproduced with
  `booinfo` and `boorender --raw`, so it is recorded as an existing parser/tool
  failure fixture rather than a Markdown-specific failure.
- Commit: `d5eccc6`.

## 2026-06-14 - Add recent packet BOO fixture

- Added `BOO/packet.boo` and `BOO/packet.script` as intentionally included
  special fixtures supplied by the user. These are recent files written by a
  third party and should be preserved exactly.
- Added narrow `.gitattributes` entries for these two packet files with `-text`
  so Git does not normalize their bytes.
- Commit: `8f12fb9`.

## 2026-06-14 - Verify legacy GDF image format mapping

- Investigated the hypothesis that legacy BOO image payloads are GDF using the
  `transmog.exe` IDA instance and filesystem BOO fixtures.
- Verified in `TransmogConvertLegacyPicturesToWorkFiles` that legacy descriptor
  kind `0xc7`/`G` dispatches to `TransmogConvertGdfToGif`, while `0xc9`/`I`
  dispatches to `TransmogConvertMmrToGif`; GDF is therefore one legacy picture
  family, not the universal legacy image payload format.
- Renamed helper functions in the Transmogrifier IDB:
  `TransmogTranslateEbcdicBufferToAscii`,
  `TransmogSwap32IfBigEndianMode`, `TransmogSwap16IfBigEndianMode`, and
  `TransmogWriteIndexedBitmapAsGif`. Moved the renamed entries out of the
  accidental `/vibe` function folder and removed that folder from the IDB.
- Scanned BOO fixtures from the filesystem, including untracked files. Verified
  local legacy descriptor counts: `2576` `G`/GDF descriptors and `19843`
  `I`/MMR-style descriptors.
- Updated `Format/assets.md` with the corrected `id, kind, length, offset`
  descriptor layout, GDF-vs-MMR evidence, IDA function evidence, and fixture
  byte examples from `GG66-3212-00.boo` and `GG24-4302-00.boo`.
- Updated libgeist to label `I` resources as `legacy-mmr` instead of the generic
  `legacy-image`; `G` remains `legacy-gdf`.
- Validated with `cmake --build build`, `boorsrc --list GG66-3212-00.boo`, and
  `boorsrc --list GG24-4302-00.boo`. The latter printed the expected
  `legacy-mmr` rows but did not terminate before the command timeout.
- Commit: `dc31a64`.

## 2026-06-14 - Add libpng-backed resource PNG conversion

- Added CMake support for PNG rendering dependencies: `find_package(PNG
  REQUIRED)` and `find_package(GIF REQUIRED)`, with Windows builds using vcpkg
  manifest dependencies `libpng` and `giflib`.
- Added `libgeist/vcpkg.json` pinned to vcpkg baseline
  `44819aa2a6c10e56065e2b0330e7d6c89d1d2574`.
- Added `BooDocument::read_resource_png()` and `boorsrc --png <book.boo>
  <asset-id> [output-file]`.
- Implemented converted GIF-to-PNG rendering through giflib for GIF decoding and
  libpng for PNG encoding. Kept `boorsrc --extract` as exact raw byte export.
- Legacy BookManager image payloads now fail clearly in `--png` mode because the
  ImageMark/MMR/GDF/MET pixel-stream decoders are not implemented yet.
- Validated vcpkg install/build of `libpng 1.6.58`, `giflib 6.1.3`, and `zlib
  1.3.2` into `build/vcpkg_installed`; `cmake --build build`; copied runtime
  DLLs beside the tools (`gif.dll`, `libpng16d.dll`, `zd.dll`); successful
  `boorsrc --png` conversion of `SC26-4221-08.boo` asset `1` to a 14x26 PNG;
  raw `boorsrc --extract` of the same asset; expected unsupported error for
  `GG24-4302-00.boo` legacy asset `1`; and whole-book `boorender --raw` against
  five randomly selected filesystem fixtures from `BOO/`:
  `SC26-4222-06.boo`, `SC24-5766-03.boo`, `SC26-3393-00.boo`,
  `SC24-5520-01.boo`, and `GH24-5218-01.boo`.
- No failed untracked BOO fixture was added for this workload.
- Commit: `7e1dff7`.

## 2026-06-14 - Split libgeist implementation files

- Split the former monolithic `libgeist/src/boo.cpp` into focused
  implementation units: document API, I/O, encoding, logical-record decoding,
  GML markup projection, page-run classification, book properties, resources,
  string helpers, TOC/topic handling, and public utilities.
- Added `libgeist/src/geist/detail/boo_detail.hpp` for private parser helper
  declarations shared by the implementation files; the public API header remains
  unchanged.
- Updated `libgeist/CMakeLists.txt` to build the split sources for both the
  shared and static libraries.
- Added a persistent instruction in `AGENTS.md` to keep `libgeist`
  implementation files split by class, object, interface, or tightly scoped
  helper area instead of growing a monolithic parser source.
- Validated with `cmake --build build`, `booinfo`, `bootoc`, and `boorsrc` on
  `QS3X36CM.BOO`, plus whole-book `boorender --raw` against five randomly
  selected filesystem fixtures from `BOO/`: `SB35-4268-00.boo`,
  `GH09-8096-02.boo`, `SC09-2416-00.boo`, `SC23-3130-00.boo`, and
  `SC24-5527-02.boo`.
- No failed untracked BOO fixture was added for this workload.
- Commit: `386790d`.

## 2026-06-14 - Correct random BOO fixture selection

- Corrected the persistent BOO test instruction so random fixture selection
  enumerates the `BOO/` directory on disk, including untracked files, instead
  of using `git ls-files`.
- Validated whole-book raw GML decoding with `boorender --raw` against five
  randomly selected filesystem fixtures from `BOO/`: `GH24-5223-04.boo`,
  `SC27-9126-00.boo`, `SC26-4697-01.boo`, `SC27-9121-00.boo`, and
  `SC26-4721-01.boo`. All selected fixtures passed; `SC26-4721-01.boo` is slow
  and needed a separate longer single-fixture run after the first sample command
  timed out.
- No failed untracked BOO fixture was added for this workload.
- Commit: `7513c1e`.

## 2026-06-14 - Make boorender default to whole-book raw output

- Removed the `--all` pseudo-topic from `boorender`.
- Updated usage to `boorender <book.boo> [topic-id] (--raw|--md)`: omitting
  `topic-id` now emits the whole-book GML stream for `--raw`, while specifying
  a topic id still emits that topic's `TocEntry::raw_records`.
- Validated with `cmake --build build`, whole-book raw output without a topic
  id, topic-specific raw output, `--md` with and without a topic id, usage
  errors, missing-topic error handling, and five random tracked BOO fixtures
  through whole-book `--raw`.
- Commit: `4a50331`.

## 2026-06-14 - Expose whole-book raw GML records

- Added `BooDocument::raw_gml_records()` to expose a document-level GML-style
  raw stream for the entire decoded BOO, including cover, edition, contents,
  chapters, appendices, and other decoded topics.
- Populated the document-level stream from the full decoded topic list rather
  than the displayed TOC list, while preserving `TocEntry::raw_records` for
  topic-specific access.
- Added `boorender <book.boo> --all --raw` to exercise the whole-book API
  surface; topic-specific `boorender <book.boo> <topic-id> --raw` still uses
  the matching `TocEntry`.
- Deduplicated decoded topic ids before exposing topics as raw GML, avoiding
  duplicate topic output from overlapping decoder candidate pages, and tightened
  `SH<id>` projection so body text that begins with `sh` is not emitted as a
  false `:topic` tag.
- Validated with `cmake --build build`, whole-book raw checks for
  `QS3X36CM.BOO` covering `COVER`, `EDITION`, `CONTENTS`, and `2.0`, five
  random tracked BOO fixtures through `boorender --all --raw`, topic-specific
  raw output, `boorender --md`, and missing-topic error handling.
- Commit: `3d9eb55`.

## 2026-06-14 - Add generic SR anchor markup support

- Fixed the generic `SR<id>` markup gap exposed by `QS3X36CM.BOO` topic `2.0`
  links to `sptproc`, `sptcontrol`, and `sptocl`.
- Updated the GML-style raw projection so generic spot/section anchors render
  as `:anchor id='<id>'.`, while existing `SRFIG` and `SRTBL` projections still
  render as figure/table anchors.
- Updated `Format/markup.md` to document that `CSELECT` may target generic
  anchors as well as topics, figures, tables, and pictures, with fixture
  evidence from `QS3X36CM.BOO`.
- Validated with `cmake --build build`, targeted `boorender --raw` checks for
  topics `2.0`, `2.1`, `2.2`, and `2.3` in `QS3X36CM.BOO`, five random tracked
  BOO `CONTENTS` raw checks, `boorender --md`, and missing-topic error
  handling.
- Commit: `c7f96c7`.

## 2026-06-14 - Document markup controls and emit GML-style raw output

- Expanded `Format/markup.md` with a GML-style raw projection table covering
  the currently identified BookManager markup controls: topic headers, TOC
  controls, font definitions/spans, selectable links, menus, figures, tables,
  layout/reflow controls, index controls, book metadata controls, and generic
  fallback preservation for other `C...` controls.
- Changed `TocEntry::raw_records` to hold the GML-style raw projection instead
  of the previous flat experimental decoder string. The projection drops
  unresolved `?` separator placeholders and emits colon-prefixed tags such as
  `:topic`, `:hlevel`, `:tocentry`, `:font`, `:link`, `:fig`, `:layout`, and
  `:p`.
- Updated the raw projection splitter to recognize repeated controls embedded
  in a decoded record while avoiding false `SH` matches inside prose such as
  "Sharing".
- Validated with `cmake --build build`, `boorender --raw CONTENTS` on five
  random tracked BOO fixtures with no `?` placeholders in the output,
  representative `boorender --raw 1.0` and `boorender --raw CONTENTS` samples,
  `boorender --md`, and missing-topic error handling.
- Commit: `f1dca41`.

## 2026-06-14 - Move raw topic data onto TOC entries

- Reworked the topic API after review: removed the public `BooTopic` vector and
  `read_topic_raw_markup()` method, and made decoded topic data a property of
  each `TocEntry`.
- `TocEntry` now carries heading level, topic number, logical-record bounds,
  and raw decoded records for the topic id it references.
- Updated `boorender` to resolve the requested id through
  `BooDocument::find_toc_entry()` and output `TocEntry::raw_records` for
  `--raw`, while keeping `--md` as the explicit not-implemented placeholder.
- Validated with `cmake --build build`, `boorender --raw CONTENTS` on five
  random tracked BOO fixtures, `boorender --raw 1.0` on `QS3X36CM.BOO`,
  `boorender --md`, and missing-topic error handling.
- Commit: `e7f91a2`.

## 2026-06-14 - Implement topic lookup and raw boorender output

- Added topic decoding/fetching support to `libgeist`: `BooTopic` objects now
  expose topic id, title, heading level, logical record bounds, and raw decoded
  records; `BooDocument::topics()`, `find_topic()`, and
  `read_topic_raw_markup()` expose topic access without performing document
  rendering.
- Replaced the placeholder chapter-rendering API with topic raw-markup access,
  keeping Markdown or other rendering policy outside the library.
- Updated `boorender` to accept `boorender <book.boo> <topic-id> --raw` for
  raw decoded markup output and `--md` for the explicit
  `Markdown support is not yet implemented` placeholder.
- Validated with the MSVC CMake build in `build/`, `boorender --raw CONTENTS`
  on five random tracked BOO fixtures, `boorender --raw 1.0` on
  `QS3X36CM.BOO`, `boorender --md`, and missing-topic error handling.
- Commit: `0943124`.

## 2026-06-14 - Document topic/page storage and decoded markup controls

- Investigated how individual documentation pages are addressed using the
  connected `ephwam.dll` IDB and decoded BOO fixture records. Verified that
  pages are logical topics bounded by adjacent entries in the directory
  `0x003c` topic-start index, and that TOC `CTOCE` targets are public topic ids
  matching target `SH<id>` headers rather than physical addresses.
- Renamed the topic-header helper functions in IDA using `ida_name.set_name`:
  `BooFindTopicControlValue` at `0x121f636` and
  `BooGetCurrentTopicIdFromHeader` at `0x121f7d6`, then saved the IDB. Avoided
  relying on the MCP `rename` helper after it reported an unwanted folder
  attribute.
- Compiled and ran a temporary decoder dump tool under `build/` against
  `QS3X36CM.BOO`, `OFCUSEOV.BOO`, `GG24-4302-00.boo`, and
  `SC26-4221-08.boo` to collect decoded topic header, `CFONTDEF`, `CFONT`,
  `CSELECT`, `SRFIG`, `SRTBL`, and `CZ` control evidence. Removed the temporary
  build artifacts after use.
- Verified the SGML history claim against external historical references:
  BookManager's decoded controls are not raw SGML, but they preserve IBM
  GML/BookMaster/SCRIPT lineage through tags and style names such as `:h1`,
  `:figlist`, and `HP1`.
- Added `Format/topics.md` and `Format/markup.md`, updated the format index,
  and linked TOC documentation to the topic storage model.
- Commit: `62de12b`.

## 2026-06-14

- Updated `AGENTS.md` with repository workflow guidance for `Format/`,
  `AnalysisNotes/`, the attached IDA Pro MCP instance, the running BookServer
  reader URL, the Java/JNI reader as a secondary source, and the requirement to
  commit and push after each workload.
- Created this `AGENTLOG.md` to record agent activity going forward.
- Updated `AGENTS.md` with `libgeist/` guidance: self-contained BOO parser
  library scope, `src/` versus `examples/` roles, planned example tools
  (`booinfo`, `bootoc`, `boorsrc`, `boorender`), and exact-as-stored media
  extraction behavior.
- Stubbed `libgeist/` so it compiles: added a CMake project, a self-contained
  `geist` static library, public BOO document API, and initial `booinfo`,
  `bootoc`, `boorsrc`, and `boorender` examples. Verified with
  `cmake -S libgeist -B C:\tmp\geist-libgeist-build`,
  `cmake --build C:\tmp\geist-libgeist-build`, and smoke runs against
  `BOO\QS3X36CM.BOO`.
- Updated `AGENTS.md` to require the installed MSVC toolchain for Windows CMake
  configure/build validation, avoiding GCC/MinGW-style toolchains unless
  explicitly requested for comparison.
- Added repository-local `build/` output guidance and `build/.gitignore` so
  CMake binary directories can be created inside the writable repo while keeping
  generated build artifacts untracked.
- Verified `libgeist/` with the installed MSVC toolchain. Initial
  `cmake -S libgeist -B build/libgeist-msvc` failed because CMake selected
  `Visual Studio 17 2022` and no VS 2022 instance was installed. Reconfigured
  with `cmake -S libgeist -B build/libgeist-msvc-2026 -G "Visual Studio 18 2026"
  -A x64` and built successfully with
  `cmake --build build/libgeist-msvc-2026`, producing `geist.lib` and the
  example executables under `build/libgeist-msvc-2026/Debug/`.
- Updated `libgeist/` to build both shared and static library variants. The
  default `geist::geist` target now points to the shared `geist` target used by
  the example tools, while `geist::geist_static` exposes the static variant.
  Added Windows DLL export annotations and validated with
  `cmake -S libgeist -B build/libgeist-msvc-shared -G "Visual Studio 18 2026"
  -A x64`, `cmake --build build/libgeist-msvc-shared`, and a `booinfo` smoke
  run against `BOO\QS3X36CM.BOO`.
- Analyzed the first two 4096-byte pages of `BOO\QS3X36CM.BOO` and
  `BOO\OFCUSEOV.BOO` to identify initial BOO header structure. Documented
  verified page size, page-0 copyright/header fields, page-1 directory fields,
  page run correlations, timestamp evidence, and unresolved hypotheses in
  `Format/boo-header.md`.
- Re-verified the documented BOO header findings directly from both fixtures.
  Confirmed page sizes, page counts, page-0 signature/copyright fields,
  page-1 `page_count - 1`, `0x0100` and `0x0000` page run start/count
  correlations, table offsets/count repetition, and EBCDIC timestamp fields.
  No changes were needed to `Format/boo-header.md`.
- Added `Format/README.md` as the format-notes index and stubbed topic files
  for page organization, compression/encoded content, and assets/media
  resources so future BOO findings have stable documentation locations.
- Split compression and encoding documentation into separate `Format/`
  topic files: `compression.md` now covers compression-specific questions and
  `encoding.md` covers character encoding, tokenization, and control bytes.
- Added a persistent `AGENTS.md` documentation instruction requiring `Format/`
  notes to be complete enough for independent BOO reader implementations
  without consulting `libgeist` source or proprietary IBM reader binaries.
- Cross-checked the BOO header findings against the attached
  `bookmgr.exe.i64` IDA database. Found that low-level BOO parsing is delegated
  through `ephwam.dll` imports such as `Scm_Bopen`, `Scm_Binfo`,
  `Scm_Bkiopen`, and `Scm_BKIDatetime`, so the active IDB validates the API
  boundary but does not directly verify raw page-0/page-1 offsets. Documented
  this reader-code evidence and limitation in `Format/boo-header.md`.
- Corrected the IDA cross-check documentation location: removed the
  reader/API-boundary analysis note from `Format/boo-header.md` and moved it to
  `AnalysisNotes/ida-bookmgr-api-boundary.md`, because it describes IDA target
  coverage and imported reader behavior rather than BOO byte-format facts.
- Removed agent/workflow-style update rules from `Format/README.md`; the file
  now stays focused on indexing BOO format documentation while persistent
  contributor instructions remain in `AGENTS.md`.
- Used the attached `bookmgr.exe.i64` IDA database to identify and improve the
  CGI `bookmgr.cfg` configuration subsystem. Renamed and typed
  `ConfigLoadBookmgrCfg`, `ConfigStoreBookmgrCfg`, `SettingsSetValue`,
  `SettingsGetValue`, `ConfigCleanAdminValue`, `ConfigCleanupSettings`, and
  `CgiHandleAdminSettings`; renamed the in-memory settings globals to
  `g_settings_keys`, `g_settings_values`, and `g_settings_count`; added
  high-confidence local variable names and function comments; saved the IDB.
- Removed the MCP-created `/vibe/` function-list folder from the active
  `bookmgr.exe.i64` IDB by unlinking its function entries and deleting the
  empty folder via `ida_dirtree`, then saved the IDB. Updated `AGENTS.md` to
  avoid the IDA MCP `rename` tool for function renames while it auto-creates
  tool-specific function folders.
- Analyzed `ephwam.dll` book-open/header parsing through `Scm_Bopen`,
  `sub_1217D8D`, `sub_1216DE9`, and `sub_1217645`. Updated
  `Format/boo-header.md` to document page 0 as a directory-page locator, expand
  the reader-consumed version-2 directory fields, record the version-3 branch
  fields known from code, and distinguish decoded logical book header controls
  from raw on-disk bytes. Updated `Format/pages.md` and `Format/README.md` to
  match the corrected header interpretation.
- Analyzed the logical book-header control storage path in `ephwam.dll`,
  including `sub_12217C6`, `sub_121EEE1`, `sub_1216189`, `sub_1218250`,
  `sub_1218AC5`, `sub_1218593`, and `sub_121AC63`. Added
  `Format/logical-controls.md` to document that `CLANGUAGE=`, `CVERSION=`,
  `CTITLE=`, `CDOCNUM=`, and related controls are stored as tokenized logical
  records rather than raw strings; documented record-page framing, compact
  record lengths, token references, token-map/dictionary resolution, and the
  remaining dictionary grammar gap. Updated `Format/boo-header.md`,
  `Format/encoding.md`, and `Format/README.md` accordingly. Attempted to query
  the hosted CGI URL, but the connection was refused from this environment, so
  this workload used the local CGI/library IDBs and repository fixtures as
  evidence.
- Designed and implemented a more concrete `libgeist` BOO reader API surface.
  `BooDocument` now parses and exposes physical file metadata, page-0 directory
  locator data, directory-page fields, page runs, advisory page roles, exact
  page reads, and placeholders for decoded logical controls, TOC entries,
  resources, and rendering. Updated `booinfo` to report the parsed structure and
  validated the MSVC build plus smoke runs against both repository BOO fixtures.
- Continued the logical-control decoder analysis in `ephwam.dll`. Renamed the
  core token/dictionary functions in IDA with `ida_name.set_name`, including
  `BooSeekDictionaryTokenRecord`, `BooApplyDictionaryDeltaRecord`,
  `BooResolveExtendedTokenReference`, `BooResolveTokenTextRecord`,
  `BooReadNextLogicalRecord`, `BooExpandLogicalRecordTokens`, and
  `BooDecodeTokenWordsToText`, then saved the IDB. Updated `AGENTS.md` to make
  high-confidence IDA renaming after analysis a persistent instruction, and
  expanded `Format/logical-controls.md` with dictionary page/block framing,
  delta operation-byte modes, token-buffer reconstruction behavior, and the
  remaining narrower implementation gaps.
- Finished the documented dictionary group-entry/root-index layout and
  translation-table loader analysis. Renamed additional high-confidence
  `ephwam.dll` routines in IDA (`BooLoadTranslationTablePage`,
  `BooCompareTokenWordStrings`, `BooEncodeUnicodeWordsToSearchBytes`, and
  `BooEncodeUnicodeWordToSearchByte`) and saved the IDB. Updated
  `Format/logical-controls.md` with the directory-page root index at directory
  offset `0x0026`, compact-length-prefixed version-2 token-key entries from
  both BOO fixtures, the translation-table page loading formula, and token-word
  table/index decoding. Updated `Format/encoding.md` to summarize the verified
  translation-table and search-key encoding behavior.
- Finished the remaining dictionary index continuation-payload analysis.
  Traced `BooSeekDictionaryTokenRecord` in IDA, added comments at the control
  byte, continuation-cursor, page-jump, and terminal-delta offsets, and saved
  the IDB. Verified against both BOO fixtures that root entries use
  `key + prefix_length + prefix_bytes + BE16 page`, dictionary-page top entries
  use `key + prefix_length + prefix_bytes + nested terminal entries`, and
  terminal entries use `key + delta_record_bytes`. Updated
  `Format/logical-controls.md`, `Format/encoding.md`, and
  `Format/boo-header.md` accordingly.
- Implemented an experimental tokenized logical-control decoder in `libgeist`.
  The decoder reconstructs dictionary token strings from documented
  version-2 anchor/delta records, resolves logical-record token references via
  the directory token map and extended-token keys, and extracts known
  `C...=` controls when decoded records contain them. Updated `booinfo` to print
  decoded controls or `none decoded`. Validated with
  `cmake -S libgeist -B build/libgeist-msvc-2026 -G "Visual Studio 18 2026"
  -A x64`, `cmake --build build/libgeist-msvc-2026`, and `booinfo` runs against
  both bundled BOO fixtures. The fixtures currently report no decoded controls,
  so further work is still needed on exact stream assembly and/or
  translation-table-backed token text conversion.
- Cleaned the polluted `build/` directory by removing stale nested CMake build
  trees and stopping the stale Strawberry `cmake`/`ninja` processes that held
  one old tree locked. Reconfigured and rebuilt `libgeist` directly in the
  single `build/` directory with `cmake -S libgeist -B build -G "Visual Studio
  18 2026" -A x64` and `cmake --build build`. Updated `AGENTS.md` so future
  validation uses `build/` directly and does not create multiple nested build
  folders unless explicitly requested.
- Fixed the experimental logical-control decoder so `booinfo` decodes controls
  from both bundled BOO fixtures. Replaced the earlier direct-CP037 dictionary
  shortcut with CP500 byte-to-token-word mapping for version-2 dictionary
  literal bytes, scanned the documented content stream pages before the
  trailing `0x0001` logical-record pages, made control-key extraction
  case-insensitive, bounded values at auxiliary control boundaries, and stopped
  after `CDOCNUM` like the IBM reader. Verified with `cmake --build build`,
  `build\Debug\booinfo.exe BOO\OFCUSEOV.BOO`, and
  `build\Debug\booinfo.exe BOO\QS3X36CM.BOO`. Updated
  `Format/logical-controls.md` with the content-stream and CP500 table details.
- Verified that the hosted BookServer CGI page is accessible through the Docker
  fetch MCP even though ordinary shell/web access failed. Added
  `AnalysisNotes/bookserver-docker-fetch.md` with the exact MCP method and
  updated `AGENTS.md` to use `mcp__MCP_DOCKER.fetch` or
  `mcp__MCP_DOCKER.fetch_content` for future hosted-CGI analysis.
- Analyzed the `booinfo` metadata mismatch against the connected `ephwam.dll`
  IDB and the hosted BookServer CGI. Confirmed the documentation was too loose
  about the difference between dictionary literal CP500 byte-to-token-word
  mapping, dictionary delta table transforms, logical-record spacing, and final
  BOO translation-table output decoding. Updated `Format/logical-controls.md`,
  `Format/encoding.md`, and `Format/README.md` with the corrected independent
  reader pipeline and BookServer comparison evidence.
- Updated the `libgeist` experimental logical-control decoder to assemble
  logical records from resolved token records using the reader-observed
  spacing/suppression control words and initial control-key uppercasing path.
  Tightened control-value boundary detection, normalized decoded metadata
  display values for the bundled code-page-500 fixtures, and verified
  `booinfo` against both bundled BOO files plus the hosted BookServer titles
  for `QS3X36CM` and `OFCUSEOV`. Validated with `cmake --build build`,
  `build\Debug\booinfo.exe BOO\QS3X36CM.BOO`, and
  `build\Debug\booinfo.exe BOO\OFCUSEOV.BOO`.
- Promoted decoded logical header controls into typed `BooBookProperties` on
  `BooDocument`, covering language, version, build version, reflow, title,
  short title, copyright, security, date, authors, and document number. Updated
  `booinfo` to present these book properties instead of requiring callers to
  interpret raw `C...` logical-control keys. Validated with
  `cmake --build build`, `build\Debug\booinfo.exe BOO\QS3X36CM.BOO`, and
  `build\Debug\booinfo.exe BOO\OFCUSEOV.BOO`.
- Analyzed the connected `ephwam.dll` IDB for BOO compression, encryption,
  digital signatures, hashes, CRCs, and checksums. Found no standard
  compression layer and no encryption/signature/hash/checksum implementation in
  the sampled BOO open/read path. Renamed and typed high-confidence IDA
  routines including `BooReadPhysicalPageIntoBuffer`,
  `BooGetOrLoadPageBuffer`, `BooReadBE16`,
  `BooGetCurrentSummaryText`, and `BooValidateBookFileTailPlaceholder`, added
  explanatory IDA comments, and saved the IDB. Updated `Format/compression.md`,
  added `Format/security.md`, and updated `Format/README.md`.
- Analyzed table-of-contents storage using the hosted BookServer `CCONTENTS`
  output, decoded fixture logical records, and the connected `ephwam.dll` IDB.
  Identified the directory content-page record index at `0x0034`, topic count
  at `0x003e`, topic-start index at `0x003c`, topic header controls
  (`SH...`, `CTOPICN`, `CPARENT`, `CFORWARDLEVEL`, `CBACKLEVEL`, `CSUMMARY`,
  `CHDLEVEL`, `ST`), and literal `CTOCE` entries stored inside the
  `CONTENTS` topic. Renamed and typed topic/index helper routines in IDA,
  including `BooSelectTopicByNumber`, `BooSeekTopicStartRecord`,
  `BooGetTopicStartRecordNumber`, `BooLookupPagedU16Index`,
  `BooLocateLogicalRecordByNumber`, `BooFindIndexOrdinalForRecordNumber`,
  `BooSaveLogicalCursorState`, and `BooRestoreLogicalCursorState`, then saved
  the IDB. Added `Format/table-of-contents.md` and updated
  `Format/README.md`.
- Implemented experimental table-of-contents reading in `libgeist` by retaining
  decoded logical records, extracting `CTOCE` entries into `BooDocument::toc_`,
  and exposing the documented nesting level and style through `TocEntry`.
  Updated `bootoc` to render the parsed TOC with indentation and style numbers.
  Validated with `cmake --build build`,
  `build\Debug\bootoc.exe BOO\QS3X36CM.BOO`, and
  `build\Debug\bootoc.exe BOO\OFCUSEOV.BOO`; the bundled fixtures produce 10
  and 201 TOC entries respectively.
- Updated `bootoc` to follow the hosted BookServer `CCONTENTS` page shape:
  page title, metadata line (`Title`, `Document Number`, `Build Date`,
  `Build Version`, and `Book Path`), `# CONTENTS Table of Contents`, compact
  `[Summarize]` TOC block, and copyright. Validated against both hosted
  BookServer pages via Docker fetch MCP and with `cmake --build build`,
  `build\Debug\bootoc.exe BOO\QS3X36CM.BOO`, and
  `build\Debug\bootoc.exe BOO\OFCUSEOV.BOO`.
- Corrected the `bootoc` output after the BookServer-shape change proved too
  literal for the CLI. Restored the previous indented topic list with style
  numbers, kept only the requested metadata preamble (`Title`, document number,
  build date, build version, and path), and reverted the extra TOC title-case
  normalization. Validated with `cmake --build build`,
  `build\Debug\bootoc.exe BOO\QS3X36CM.BOO`, and
  `build\Debug\bootoc.exe BOO\OFCUSEOV.BOO`.
- Persisted the instruction to test BOO parsing changes against five randomly
  selected `BOO/` fixtures when exact fixtures are not specified. Validated the
  current `bootoc` build with `cmake --build build` and a random sample:
  `GX27-3999-00.boo` failed strict last-page-count validation,
  `SC23-0083-03.boo` parsed successfully with 93 output lines,
  `SC09-2417-00.boo` failed strict last-page-count validation,
  `SC28-1881-05.boo` failed strict last-page-count validation, and
  `SC24-5527-02.boo` failed strict last-page-count validation.
- Persisted the follow-up instruction that randomly selected BOO fixtures which
  fail parser/tool validation should be committed individually, while unrelated
  BOO files remain untracked. Added the four failing fixtures from the previous
  random `bootoc` run: `GX27-3999-00.boo`, `SC09-2417-00.boo`,
  `SC28-1881-05.boo`, and `SC24-5527-02.boo`.
- Analyzed why the four committed failure fixtures did not parse. The connected
  `ephwam.dll` IDB shows `BooReadPhysicalPageIntoBuffer` seeks to
  `((directory_page + logical_page) << 12) - 4096`, so directory page fields are
  1-based logical pages relative to the directory page. The failing fixtures
  have shifted directory pages (`6`, `43`, `35`, and `15`), making the prior
  `last_page + 1 == file_page_count` assumption wrong. Updated `libgeist` to
  convert logical directory/content/dictionary pages to physical file pages,
  updated `Format/boo-header.md`, `Format/pages.md`, and
  `Format/table-of-contents.md`, then validated with `cmake --build build` and
  `bootoc` on the four failure fixtures plus `QS3X36CM.BOO`, `OFCUSEOV.BOO`,
  and `SC23-0083-03.boo`.
- Investigated `bootoc` garbage at the end of `GG24-4302-00.boo`. The parser
  was scanning all decoded records for `CTOCE`, then still treated a trailing
  non-entry sequence after the TOC's `ETOC` marker as a real entry
  (`CTOCE 0 0 005E0000 ...`). Updated TOC extraction to parse only the
  `CONTENTS` topic scope, stop/trim at `ETOC`, and ignore style-0 `CTOCE`
  candidates. Updated `Format/table-of-contents.md`, added
  `GG24-4302-00.boo` as the regression fixture, and validated with
  `cmake --build build`, `bootoc BOO\GG24-4302-00.boo`, and the existing BOO
  fixture set used by recent parser checks.
- Analyzed image asset storage using the attached `ephwam.dll.i64` IDB, local
  BookServer binaries, and `GG24-4302-00.boo`. Verified that `Scm_Makeres`
  retrieves raw 4096-byte resource pages without image conversion, while
  BookServer delegates GIF creation to the `ephimage`/ImageMark path. Identified
  a pre-directory embedded-resource area in `GG24-4302-00.boo`, documented the
  page-0 resource descriptor layout, absolute image payload offsets, ImageMark
  payload prefix evidence, and extraction rules in `Format/assets.md`; updated
  `Format/README.md`.
- Added the newly provided `Official Readers/Transmogrifier/` reader assets to
  version control and checked the connected IDA Pro instances. Confirmed live
  reachable IDA instances for `transmog.exe` on port `13339` and
  `ISGDI32.DLL` on port `13340`, both rooted in the Transmogrifier folder.
- Continued image asset analysis using the Transmogrifier utility and its live
  `transmog.exe` IDA Pro instance. Renamed the identified conversion/rewrite
  functions in IDA with `ida_name.set_name`, including the legacy picture
  conversion, version 1.4 rewrite, object-data append, description-writing, and
  GIF/PNG/TIFF/JPEG/CGM dimension-reader paths, then saved the IDB. Documented
  the version 1.2/1.3 legacy descriptor layout as
  `id[8] + kind[1] + length[3] + offset[4]`, the legacy kind-byte conversion
  paths (`G`, `I`, `M`), and the version 1.4 converted object layout with
  `type="image/..."`, width, and height descriptions in `Format/assets.md`.
  Attempted to run `transmog.exe` on a copied `GG24-4302-00.boo` fixture under
  `C:\tmp`; it created the expected working/output directories but failed before
  conversion with `The specified module could not be found.` Recorded the
  runtime attempt in `AnalysisNotes/transmogrifier-runtime.md`.
- Verified loaded BookServer support for version 1.3 and 1.4 image/object
  descriptor layouts using the `bookmgr.exe` IDB on port `13337` and the
  `ephimage.dll` IDB on port `13341`. Renamed the BookServer version 1.4 direct
  object path functions (`BookServerFindConvertedObjectDescriptors`,
  `BookServerReadConvertedObjectDescription`,
  `BookServerExtractConvertedObjectDataToFile`, and
  `BookServerServePictureObject`) and the legacy helper functions
  (`EphImageMain`, `EphImageFindLegacyPictureDescriptor`, and
  `EphImageConvertLegacyPicture`) with `ida_name.set_name`, then saved both
  IDBs. Documented that 1.3 legacy descriptors live at
  `0x0118 + (16 * picture_count)` with layout
  `id[8] + kind[1] + length_be24[3] + offset_be32[4]`, while the BookServer
  1.4 converted-object path recognizes directory bytes `01 00` and uses
  descriptor groups where groups 1 and 2 are
  `id[8] + length_be32[4] + offset_be32[4]`. Updated `Format/assets.md` and
  `Format/README.md`.
- Implemented experimental asset support in `libgeist`. Added resource layout
  metadata and raw resource byte reading to the public C++ API, populated
  `BooDocument::resources()` from the documented version 1.2, 1.3, and 1.4
  page-0 descriptor tables, and replaced the `boorsrc` placeholder with
  `--list`/`-l` and `--extract`/`-e` modes. During random fixture validation,
  `SC26-4221-08.boo` showed that the previously documented version 1.4 group
  ordering was reversed for real converted-object fixtures: group 1 is object
  data and group 2 is description data. Updated `Format/assets.md` with the
  corrected group ordering and fixture byte evidence. Validated with
  `cmake --build build`, `boorsrc --list BOO\GG24-4302-00.boo`,
  `boorsrc --list BOO\SC26-4221-08.boo`, extraction of asset `1` from both
  legacy and converted fixtures into `build\`, and a five-file random
  `boorsrc --list` sample covering `SC26-3089-00.boo`, `SC24-5455-01.boo`,
  `SC24-5680-00.boo`, `SC26-3119-02.boo`, and `SC24-5519-01.boo`.
- Began MMR/kind `I` image rendering research using the attached BookServer
  and Transmogrifier IDBs plus local legacy fixtures. Verified from reader
  symbols that MMR decoding lives in the `ephimage`/Transmogrifier image path
  (`process_mmr_pict`, `dinitmmr`, `dlinemmr`, `decline`, `deceol`, `readcd`)
  rather than in `bookmgr.exe`. Added an experimental self-contained MMR decoder
  source file and library wiring for later continuation, but left the public
  `legacy-mmr` PNG renderer as an explicit unsupported stub because tested
  offsets/bit order still fail before producing a complete image. Documented the
  observed kind `I` wrapper fields and fixture evidence for
  `GG24-4302-00.boo` and `GG66-3212-00.boo` in `Format/assets.md`. Validation
  fixtures selected/used: `GG24-4302-00.boo` resource `1`,
  `GG66-3212-00.boo` resources `3` and `4`, and random BOO listings
  `SC26-3089-00.boo`, `SC24-5455-01.boo`, `SC24-5680-00.boo`,
  `SC26-3119-02.boo`, and `SC24-5519-01.boo`.
- Continued MMR/kind `I` rendering research against the live
  `ephimage.dll.i64` IDB. Verified that `process_mmr_pict` starts the first
  compressed segment at payload-relative `0x50`, using the big-endian word at
  payload offset `0x48` as a segment length and subtracting the 8-byte segment
  header before calling the decompressor. Updated the experimental
  `libgeist/src/boo_mmr.cpp` framing to use that offset while keeping the
  public `legacy-mmr` PNG renderer disabled until the IBM line decoder is
  ported. Built an ignored 32-bit harness under `tmp/` that calls the exported
  `ephimage` entry from `Official Readers/BookSrv-Win32/ephimage.dll`; rendered
  `GG24-4302-00.boo` resource `1` through IBM's DLL to
  `tmp/ibm-gg24-4302-1.gif`, then converted it to
  `tmp/ibm-gg24-4302-1.png`. The reference output is a recognizable 960x832
  black-and-white "Parallel S/390 microprocessors" diagram. Documented the
  export table, wrapper offsets, segment framing, reference GIF header, and
  remaining self-contained decoder gap in `Format/assets.md`.
- Split image payload format details out of `Format/assets.md` so that document
  stays focused on how assets are stored in the BOO container. Moved legacy kind
  `I` MMR wrapper/decoder evidence to `Format/MMR.md`, legacy kind `G` GDF notes
  to `Format/GDF.md`, and version 1.4 web-image header/dimension notes to
  `Format/WebImages.md`. Updated `Format/README.md` to index the new payload
  format documents.
- Fixed PACKET `PREFACE` highlighted phrase rendering. Verified through Docker
  fetch that hosted BookServer renders `PREFACE` email text as one paragraph
  with `wec@bam.moe` bold plus italic and the following `!` outside the span.
  Updated `libgeist` to preserve `CFONT` style spans as source-style inline
  `:hpN.` records, merge normal font-only continuations into the preceding
  paragraph, and render `HP1`/`HP2`/`HP3` as Markdown emphasis. Regenerated the
  checked-in PACKET `preface.md` and documented the BookServer URL/source trail
  in `Format/markup.md` and `AnalysisNotes/bookserver-docker-fetch.md`.
- Restored definition-list and fixed-questionnaire structure for `SC41-485`
  and `SC31-711`. Preserved empty `DT`/`DD` controls, associated definition
  terms with descriptions, recognized full-width `?` form tables, reconstructed
  `__` worksheet items (including a following `CFONT` continuation), and kept
  message labels with their descriptions. Added fixture regressions for all
  eight `SC41-485:1.2.5` codes plus `SC31-711:2.4.1` and `2.4.5`. Replayed the
  full cached BookServer audits: 36/36 and 82/82 topics rendered successfully.
- Removed the fixture-specific May 1991 edition substitution. Edition headings,
  applicability text, copyright years, and government-rights notes now come
  from each BOO's decoded `VNOTICE` records. Added a GG24 regression alongside
  the existing QS3X36CM assertions, then fetched and compared all 229 GG24
  BookServer topics successfully.
- Corrected SC31-605 action-code table alignment. Combined the two-line styled
  heading into `Action Code`, `Event Type`, and `Event or Alert Text`, retained
  leading cells that cross logical-record boundaries, and ignored intervening
  visual border rows. Added exact assertions for boundary codes `05`, `0F`,
  `19`, `23`, `63`, `6F`, and `7B`; all tests and the 110-topic cached audit
  passed.
- Corrected cross-book `CFONT` projection for marker-led rows, visual-bar
  definitions, span-only continuations, and UTF-8 text. Display-prefix padding
  now establishes the correct coordinate origin, pending spans are consumed by
  their own continuation, exact whole-word recovery avoids partial tokens, and
  unsafe partial-word spans are omitted instead of corrupting prose. Added
  fixture regressions for GG24, SC41, SC31, SH12, and SC24 and replayed the
  seven complete cached BookServer audits used by issue 9.
- Removed generated BookMaster spacing, selector-kind, slash, and heading-row
  sentinels from XWEBDEMO visible output while preserving ordinary slash text
  and cross-book title spacing.  TOC titles now discard verified selector
  metadata.  Added XWEBDEMO fixture regressions, passed all nine tests, and
  replayed a fresh 13-topic BookServer audit before closing issue 36; remaining
  semantic fixed-row carry-over is tracked separately by issue 40.
- Added explicit `SRMSG` catalog boundaries and anchors, prevented generated
  message catalogs from being swallowed by XMP, normalized catalog
  introductions, and removed message-row edge markers.  Added SC31-711 fixture
  regressions and retained all cross-book tests.  A fresh 82-topic audit found
  40 heuristic flags; issue 37 remains open because semantic fixed-row
  carry-over in topics 3.3 and 4.3.4 depends on issue 40's row ownership work.
- Reconstructed fixed-row `VNOTICE` boundaries before whitespace collapse.
  GG24's edition notice now has distinct applicability, ordering, feedback,
  address, and information-license paragraphs, suppresses its visual `(`/`*`
  markers, and cannot merge the following copyright record into the notice
  body.  Added fixture regressions, retained the QS3 edition behavior, and ran
  a fresh live 229-topic GG24 audit before issue triage.
- Reconstructed SC31-605 chapter 3 event/qualifier grids as four-column
  semantic tables.  Separator inference now distinguishes their five-boundary
  heading from the chapter 2 action grid, five-hex-digit codes delimit rows
  across logical-record joins, and wrapped qualifier text retains its column.
  Added dense, sparse, boundary, and wrapped-row regressions and fetched all
  110 hosted topics for the pre-close comparison.
- Restored structural ownership for fixed `CFONT`/`CSELECT` rows. Figure
  source and collating grids now remain preformatted; SC34 FLM/MNOTE/glossary
  catalogs retain physical rows and anchors; GG24 carry-over prefixes and
  `c.cc` boundaries no longer merge fixed commands into prose; cross-book
  selector alternatives are metadata rather than table text; and SC31-605
  event-type rows 06/07 remain separate. Added cross-book fixture regressions
  while preserving SC31-711's semantic numeric trap catalogs.
- Removed fixed-prose row markers from SC31-711 topic 3.3 without treating
  ordinary punctuation as layout metadata. The filter introduction, note,
  conversion discussion, and warning now retain their complete text while
  dropping the decoded `(`, `)`, `-`, `<`, `>`, and carry-over fragments.
- Repaired SC31-711 topics 1.4, 5.0, and GLOSSARY for issue 41. Pending CFONT
  spans now consume visible `CFORWARDLEVEL` operands, `c.cp` stays structural,
  numeric SRMSG anchors stop at the first ID token, fully styled heading rows
  drop only unstyled suffix carry-over, and glossary terms receive stable
  term-specific anchors. Added early/middle/late fixture assertions, passed all
  nine tests, and completed online plus cache-only 82-topic audits for final
  manual flag triage. The final pass had 44 heuristic flags; all were reviewed,
  issue 41 targets were clean, and independent residuals were filed as issues
  42--45 before leaving the SC31-711 tracker open.
- Reclassified mixed-case symbolic `SRMSG` operands as semantic trap catalogs
  while retaining uppercase FLM-style fixed catalogs, and preserved decoded
  publication `CFONT` rows across TOC/title-body repair. Added SC31-711
  regressions for symbolic traps and early/late publication lists, passed all
  nine cross-book tests, and completed a fresh live 82-topic BookServer audit
  before closing issue 42 and transferring residual fixed-form cases to issue
  48.
- Continued issue 58 P0 after a session handoff. Record-terminal empty-payload
  `SRMSG` controls now own their overflowed separator row in the next record's
  leading text segment (MSG2350's leaked `=`), the fixed-row series check keeps
  compact-envelope evidence because the positioned ledger marks every marker
  slot as a geometry-only boundary (MSG2267 ordinals, value-34 MSG739/MSG2108),
  and the isolated `message_section_blocks_ir` layer selects MSG508's explicit
  fallback from a row-less recovered paragraph. All 396 message headlines are
  asserted to begin exactly once with their ID with balanced placeholders.
  Refreshed `lazy_open` and `sc31_711_cross_references` expectations that
  predated the typed message, generated-list, and glossary renderers; the
  glossary introduction labels (`Contrast with:`) lost hosted bold styling in
  typed glossary lowering and remain a recorded follow-up.
- Sped up the test loop without changing library behaviour. Every
  `require`-family helper now records failures through
  `tests/test_failures.hpp` and keeps running, so one execution enumerates all
  failing expectations while the process still exits non-zero. Split the
  1,047-line `lazy_open` integration test into thirteen per-book
  `lazy_open_*` executables sharing `lazy_open_support.hpp`, so CTest runs
  them in parallel and a failure names its book. SC34-425 alone costs ~98 s
  and SC31-711's message/glossary catalogs ~140 s; those are the profiling
  targets if decoded-IR caching is pursued.
