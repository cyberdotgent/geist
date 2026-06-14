# Agent Log

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
