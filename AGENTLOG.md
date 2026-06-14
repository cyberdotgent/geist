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
