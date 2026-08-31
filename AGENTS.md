# Agent Guidance

## Project Purpose

Geist is `libgeist`, a self-contained C++ library for parsing the IBM
BookManager `BOO` format, plus the published specification of that format. The
repository root *is* the library: `src/`, `examples/`, `tests/`, `doc/`, and
`CMakeLists.txt`. The format is understood; current work is fixing bugs and
closing correctness gaps against real books.

## Repository Layout

- `src/` — library implementation, grouped by pipeline stage. Each `.cpp`
  folder has a matching private-header folder under `src/geist/detail/`:
  - `core/` — encoding, IO, strings, properties, resources.
  - `container/` — the physical container: records, segments, source rows, TOC.
  - `layout/` — display-line geometry, grids, font spans, cell ownership.
  - `ir/` — typed per-topic IR builders (`ir/prose/` for the prose family).
  - `lowering/` — DocumentIR and the `*_document_lowering` translators.
  - `render/` — Markdown/HTML renderers, diagnostics, trace.
  - `api/` — the public entry points behind `geist/*.hpp`.
  - `img/`, `cp/` — image codecs (GDF/MMR/PNG) and EBCDIC code pages.
- `src/geist/*.hpp` is the public API surface and `src/` is the public include
  directory, so those headers stay flat: `#include "geist/document.hpp"`.
  Private headers are `geist/detail/<group>/<name>.hpp`.
- `examples/` — small command-line programs demonstrating library usage, not
  duplicate parsing implementations:
  - `booinfo` — book metadata.
  - `bootoc` — table of contents.
  - `boorsrc` — extracts images and other media resources.
  - `boorender` — renders a topic to Markdown/HTML.
  - `bootrace` — parser trace for a topic (links statically).
  - `boo2git` — exports a whole book into a folder tree.
- `tests/` — CTest executables. `tests/fixtures/packet.boo` is the only BOO
  fixture that may be redistributed; tests reach it via `GEIST_FIXTURE_DIR`.
  Preserve it exactly.
- `doc/boo-spec/` — the canonical, published BOO format specification
  (AsciiDoc). All byte-level format findings go here.
- `build/`, `build_rls/` — the repository-local CMake binary directories, both
  gitignored. Do not create additional nested build trees.

## Working Rules

- Keep `libgeist` self-contained. The only dependencies are `libpng` and
  `giflib` (see `vcpkg.json`). Do not add upstream libraries, vendored code, or
  copied external parser code unless the user explicitly changes this.
- Keep implementation files split by class, object, interface, or tightly
  scoped helper area. Do not grow a monolithic parser source file; add parser
  behavior to the matching file or create a focused new one.
- `boorsrc --extract` must export media exactly as stored in the BOO container:
  no conversion, transcoding, re-encoding, or normalization. Explicit
  conversion modes such as `boorsrc --png` may decode supported assets, but
  must report unsupported legacy formats clearly rather than writing guessed or
  partially converted bytes.
- Do not assume text encodings for BOO data. Use binary-safe reads and explicit
  byte offsets; prefer hex dumps and structured reads over text-oriented tools.
- Reproduce a bug with a test before fixing it. Prefer a synthetic test that
  builds its own records; use the `packet.boo` fixture only when the bug needs
  a real container.
- When a fix changes documented format behavior, update `doc/boo-spec/` with
  the evidence: byte offsets, hex values, and decoded interpretation.
- Commit and push after each distinct workload, scoped to the files
  intentionally changed.
- Keep scratch output out of the repository.

## Build And Test

Configure directly into the single repository-local build directory:

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

On Windows use the installed MSVC toolchain — not GCC, MinGW, or Strawberry
Perl GCC — unless the user explicitly asks for a comparison. Windows builds get
`libpng`/`giflib` from vcpkg and copy the runtime DLLs beside the executables;
Linux and macOS use OS-provided development packages discovered by CMake.

The `doc/boo-spec/` target only exists when an AsciiDoc toolchain is installed
and is never part of `all`.

## Documentation Style

- Keep documentation factual and evidence-driven, and distinguish verified
  format facts from hypotheses.
- `doc/boo-spec/` is published with the library. It describes the *format*,
  never the decoder: no source filenames, function names, internal type names,
  or tool command lines.
- Write the spec to be complete enough for an independent implementer to build
  a BOO reader from scratch without reading `src/`: field sizes, byte order,
  offsets, observed values, parsing steps, cross-file relationships, and
  unresolved gaps.
- Favor concise tables for field layouts once a stable interpretation emerges.
