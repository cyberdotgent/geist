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

There is no committed parser, package manifest, or automated test suite yet.
Do not invent a build pipeline just to satisfy a task.

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
- Distinguish verified format facts from hypotheses.
- Favor concise tables for field layouts and byte structures once a stable
  interpretation emerges.
- If adding code later, document the format behavior near tests or parser code,
  and link back to the fixture/evidence used to derive it.
