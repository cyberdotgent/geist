# Agent Guidance

## Project Purpose

Geist is a collaborative reverse-engineering project for the IBM BookManager
`BOO` format. The repository is currently mostly reference material: sample BOO
books and official IBM reader assets. Treat it as a format-research repository,
not as an application with an established build system.

## Repository Layout

- `README.md` explains the project goal.
- `BOO/` contains sample `.BOO` files for format analysis. These are binary
  fixtures and should be preserved exactly unless the user explicitly asks to
  replace or add samples.
- `Format/` is the canonical place to document any and all findings about the
  IBM BookManager file format itself. Put format structures, field layouts,
  byte-level evidence, compression/indexing observations, and format hypotheses
  there.
- `AnalysisNotes/` is for analysis notes that are not themselves BookManager
  file-format facts, such as environment setup, tool usage, URL-to-file mapping,
  reader behavior, and workflow notes.
- `libgeist/` is the planned self-contained C/C++ library for parsing BOO
  files. Library implementation belongs under `libgeist/src/`. Example programs
  showing how to use the library belong under `libgeist/examples/`.
- `build/` is the repository-local CMake binary directory. Configure CMake
  directly into `build/` instead of creating multiple nested build directories
  under it. Its generated contents are ignored and should not be committed.
- `Official Readers/` contains IBM Softcopy Reader / BookManager binaries,
  help files, dictionaries, images, and configuration used as reference
  material. Consider this vendored historical software. Do not refactor,
  reformat, normalize line endings, rename files, or edit binaries here unless
  the task specifically requires it.
- `AGENTLOG.md` records agent activity. Update it for every workload with the
  date, scope, commands/tools used when relevant, files changed, and commit
  identifier once available.

## Analysis Sources

- Use the attached IDA Pro MCP instance for BookManager BookServer reader
  analysis when available. The current target is the BookManager BookServer
  reader opened from this repository.
- Do not use the IDA MCP `rename` tool for function renames while it
  auto-links renamed functions into a `/vibe/` function-list folder. Use
  IDA Python APIs such as `ida_name.set_name` for function renames instead, and
  keep the IDA function-list folders free of agent/tool-specific categories.
- After analyzing IDA functions deeply enough to assign high-confidence purpose
  names, rename them in the IDB with `ida_name.set_name`, add types/comments
  where they materially improve future analysis, and save the IDB. Prefer
  descriptive names tied to verified behavior over speculative IBM-internal
  terminology.
- A running BookServer reader is available at
  `http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/CCONTENTS?DT=19910524075122`.
  Use it as an analysis source for reader behavior and for comparing against a
  superset of the books included in this repository.
- The Java version of the reader and its JNI libraries in `Official Readers/`
  are a secondary source for analysis. Prefer static inspection unless the user
  explicitly asks to execute vendored binaries or the environment implications
  are clear.

## Working Rules

- Before changing anything, inspect the relevant files and keep changes tightly
  scoped. This repo contains many binary files where accidental rewrites are
  costly.
- Keep `libgeist` self-contained. Do not add upstream libraries, vendored code,
  package-manager dependencies, or copied external parser code unless the user
  explicitly changes this direction.
- Implement BOO parsing logic in `libgeist/src/`. Keep `libgeist/examples/`
  focused on small command-line examples that demonstrate library usage, not on
  duplicate parsing implementations.
- The intended example tools are:
  - `booinfo`: lists BOO metadata.
  - `bootoc`: lists the table of contents.
  - `boorsrc`: extracts images and other media resources.
  - `boorender`: renders a chapter to Markdown.
- `boorsrc` must export image and media data exactly as stored in the BOO
  container. Do not convert, transcode, decode, re-encode, or normalize image
  payloads; leave image parsing/rendering to downstream libraries chosen by
  `libgeist` users.
- Preserve original filenames, capitalization, extensions, and paths. Some
  reader assets refer to each other by legacy, case-sensitive-looking names even
  on Windows.
- Quote paths with spaces, especially anything under `Official Readers/`.
- Do not assume text encodings for BOO data. Use binary-safe reads and explicit
  byte offsets when analyzing `.BOO` files.
- When documenting discoveries about the BOO format, include the evidence:
  sample filename, byte offsets, hex values, decoded interpretation, and any
  assumptions still unresolved.
- Put BookManager file-format findings in `Format/`. Put supporting notes that
  are about tools, environment behavior, URL mapping, or reader operation in
  `AnalysisNotes/`.
- Commit and push after each change and after each distinct workload. Keep
  commits scoped to the files intentionally changed for that workload.
- Update `AGENTLOG.md` as part of each workload before committing.
- Keep generated scratch output out of the repository unless it is intentionally
  useful research data. Prefer temporary files outside the repo or clearly named
  ignored artifacts if an ignore file is later added.

## Build And Test

The repository now contains a `libgeist/` CMake stub for the planned parser
library. Do not expand the build system beyond what is needed for the requested
library or example work.

On Windows, use the installed MSVC toolchain for CMake configure/build
validation. Do not use GCC, MinGW, Strawberry Perl GCC, or other non-MSVC
toolchains for Windows builds unless the user explicitly asks for a comparison.
Use the single repository-local build directory directly, for example
`cmake -S libgeist -B build` followed by `cmake --build build`. Do not create
additional build directories such as `build/libgeist-msvc`,
`build/libgeist-msvc-2026`, or ad-hoc comparison directories unless the user
explicitly asks for a separate build tree.

For current validation:

- Use `git status --short` before and after edits to check the working tree.
- Use `rg --files` to inspect tracked file layout quickly.
- For binary research scripts added in the future, include small focused tests
  that exercise known BOO fixture offsets without modifying the fixtures.

## Binary And Vendor Asset Handling

- Treat `.BOO`, `.boo`, `.dll`, `.exe`, `.jar`, `.dic`, `.flt`, `.bmp`, `.gif`,
  `.jpg`, `.pdf`, `.res`, and `.TAB` files as binary/reference assets.
- Avoid opening binary files with commands that may emit large unreadable output
  unless you intentionally need a small bounded sample.
- Prefer checksums, file sizes, structured binary reads, or hex dumps over
  text-oriented tools for binary files.
- Never run vendored executables from `Official Readers/` unless the user
  explicitly asks and the environment implications are clear.

## Documentation Style

- Keep documentation factual and evidence-driven.
- Write `Format/` documentation to be complete enough for an independent
  implementer to build a BOO reader from scratch without reading `libgeist`
  source code or consulting the proprietary IBM reader binaries. Include field
  sizes, byte order, offsets, valid/observed values, parsing steps, cross-file
  relationships, and unresolved gaps whenever known.
- Distinguish verified format facts from hypotheses.
- Favor concise tables for field layouts and byte structures once a stable
  interpretation emerges.
- If adding code later, document the format behavior near tests or parser code,
  and link back to the fixture/evidence used to derive it.
