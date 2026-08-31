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
- `gui/` — the **Geist Hardcopy Reader**, an optional Qt6 desktop reader
  modelled on IBM's BookManager SoftCopy Reader. It is a pure consumer of the
  public API: no parsing or rendering logic lives here, and Qt appears in no
  other directory. Built by default when Qt6 is present (see Build And Test).
  `gui/book_url.hpp` and `gui/book_source.*` define the reader's own URL space
  (`geist://book/topic/<id>`, `geist://book/object/<id>`) and answer it with
  bytes. That policy belongs to the reader and uses Qt types freely; it is not
  a shared component, and another consumer should not try to link it.
- `apache/` — **mod_geist**, an optional Apache 2.4 module serving a book over
  HTTP. Like the reader it brings its own URL space, link map and chrome, and
  shares no code with `gui/`. It links libgeist statically and compiles its
  CSS, JS and icons in via `apache/tools/bundle.py`, so one `.so` deploys with
  nothing beside it; add new assets under `apache/assets/` and the bundler
  picks them up. Built by default when `apxs` and the APR headers are present.
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

The GUI reader is controlled by `GEIST_BUILD_GUI`:

- `AUTO` (default) builds it when Qt6 `Widgets`, `WebEngineWidgets` and
  `PrintSupport` are all found, and skips it with an explanation otherwise.
- `ON` turns a missing Qt into a configure error. **CI must use `ON`**, or a
  broken Qt setup silently degrades to a green library-only build.
- `OFF` never builds it.

Qt is never a dependency of `libgeist` itself, and the user supplies it:
`qt6-base-dev` + `qt6-webengine-dev` on Linux, `brew install qt` on macOS, and
on Windows an `aqtinstall`/official Qt build pointed at with
`-DCMAKE_PREFIX_PATH`. vcpkg is not used for Qt: its `qtwebengine` port is a
from-source Chromium build. Never download Qt from the CMake files; discover
it, and print the install command when it is missing.

## Consumers And Extension Points

`libgeist` renders a book; it does not decide what a link means. Every such
decision is a hook a consumer fills in, and `geist/html.hpp` already carries
them: `resolve_topic`, `resolve_anchor`, `resolve_resource`,
`resolve_external` and `resolve_cross_book`, plus `id_prefix`, and
`stylesheets`/`inline_stylesheet` for the page around a fragment.
`TocEntry::link_targets()` exists so a consumer can build its own book-wide
link map, and `html_fragment()` is for a consumer that owns the page.

- Each consumer brings its own batteries: its URL space, its link map, its
  page chrome, its caching. The Qt reader does this in `gui/book_source.*`;
  a future Apache module does it again in its own terms, against APR and the
  request pool.
- Do **not** lift a consumer's policy into `libgeist` to share it between
  consumers. Their URL spaces, lifetimes and caching genuinely differ, and
  the resulting duplication -- roughly a link index and a handful of
  resolvers -- is smaller than the coupling it would replace.
- If a consumer cannot express what it needs, the fix is a new attachment
  point in `libgeist`, never consumer policy moved inside it.

## Licensing

- The repository is licensed **Apache-2.0**; `LICENSE` carries the canonical
  text. Every source file starts with the two-line header:

  ```
  // Copyright 2026 Yvan Janssens
  // SPDX-License-Identifier: Apache-2.0
  ```

  New source files must carry it too. Use `#` comments for CMake, Python and
  YAML, and keep a shebang on the first line.
- Do not add a `NOTICE` file without a concrete reason. Apache-2.0 makes it
  sticky: every downstream redistributor must carry it forward forever.
  Copyright belongs in the per-file header and in `LICENSE`.
- Apache-2.0 is compatible with GPLv3 and LGPLv3 but **not** with GPLv2-only.
  Keep that in mind before taking on a dependency or courting a consumer that
  is GPLv2-only.
- The Windows distribution ships `LICENSE` beside the binaries, which is what
  Apache-2.0 section 4 requires for our own code. It does **not** yet ship the
  third-party notices for the Qt, Chromium, libpng and giflib binaries it
  bundles; that is an open compliance gap, and Qt is conveyed under LGPLv3, so
  it must stay dynamically linked and its corresponding source must be offered.

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
