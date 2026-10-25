# Geist BOO Reader for Windows

A native Win32 reader and a separate consumer of `libgeist`, like the AppKit
reader in `macos/` and the Qt reader in `gui/`. It is plain Win32 and COM
(no ATL, MFC, Qt or WebView2) and shows topics in the system's MSHTML
WebBrowser control. MinGW-w64 and MSVC both build it, and the result is one
standalone `geist-boo-reader.exe` that needs nothing but Windows: libgeist,
libpng, zlib, giflib and the C/C++ runtime are linked statically.

## Build

`GEIST_BUILD_WIN32_READER` accepts `AUTO` (the default: built for Windows
targets), `ON` and `OFF`.

MSVC, with static vcpkg libraries and the static runtime, as CI does:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
  -DGEIST_BUILD_GUI=OFF
cmake --build build --config Release --target geist-boo-reader
```

A build against the default dynamic triplet works too, but then needs the
vcpkg DLLs beside it and fails `win32_reader_executable_test`.

MinGW-w64, cross from Linux (Debian/Ubuntu: `g++-mingw-w64-x86-64`):

```
win32/build_mingw_dependencies.sh /abs/deps
cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=win32/toolchain-mingw-w64.cmake \
  -DCMAKE_PREFIX_PATH=/abs/deps -DGEIST_BUILD_GUI=OFF
cmake --build build-mingw --target geist-boo-reader
```

The script downloads zlib, libpng and giflib release tarballs, verifies their
pinned SHA-256 sums and builds static libraries and licence texts into the
prefix; the project's CMake never downloads anything.

`win32/verify_exe.py <exe> [--require-notices]` reads the PE file and checks
that only Windows DLLs are imported, that exactly one RT_MANIFEST resource
(id 1) asks for per-monitor v2 DPI awareness, Common Controls 6, `asInvoker`
and long paths, and that the icon, version and licence-notice resources are
present. CTest runs it as `win32_reader_executable_test`.

## Using it

Each open book has its own window: a contents tree, the topic view, and a
flat toolbar with sidebar, back, forward, previous and next topic, table of
contents, the book search, book information and print. The search scans the
title and text of every topic on a worker thread; results show a snippet,
and clearing the field (or Escape) restores the contents. Links inside the
book navigate in the window; links out of it (http, ftp, mailto) open with
the system. Edit > Settings sets the font family and the contents and topic
sizes for all windows (stored under
`HKCU\Software\Geist\Geist BOO Reader`); View > Larger/Smaller Topic Text
steps the topic size.

Books open from File > Open Book (several at once), the command line
(`geist-boo-reader.exe book.boo ...`), drag and drop of one or more files onto
a window, or a double-click. A second launch hands its books to the reader
already running for the user, so they open as windows of one process
(`--new-instance` opts out). There is no installer; to open `.boo` files by
double-click, use *Open .boo Files with This Reader* in Settings, which
registers the executable at its current location for the current user under
`HKCU\Software\Classes` (*Remove* undoes it). Windows keeps a choice the user
made earlier in *Open with*; pick the reader there once if it does not take
over. `--version` prints the reader and library versions.

File > Print Topic prints the current topic through MSHTML's own print
dialog. File > Print Book asks for a printer, then composes the book as the
macOS reader does: the cover and title topics first and unnumbered (a plain
title page when the book has neither), a generated table of contents with the
printed page numbers in place of the book's CONTENTS topic, every later page
numbered, and topics too wide for a portrait page printed on landscape pages
(scaled down if still too wide). Large books take a while; the progress window
can cancel.

## Design notes

- **Serving the book.** A temporary, process-local asynchronous pluggable
  protocol (`IInternetSession::RegisterNameSpace`) answers `geist:` URLs:
  `geist://<host>/topic/<id>[#fragment]` and `geist://<host>/object/<id>`.
  Every book a window opens registers under a fresh host, so one handler
  serves all windows and a window's history never reaches into a book it no
  longer shows. Nothing is written to the registry for it, and its interfaces
  are declared in the urlmon headers of both MinGW-w64 and MSVC.
- **Document mode.** Pages carry `X-UA-Compatible: IE=edge` and the reader
  sets `FEATURE_BROWSER_EMULATION` (11001) for its executable name under
  HKCU, so MSHTML uses IE11 standards mode. IE11 predates CSS logical
  properties, so `ie11-compat.css` restates the shared stylesheet's
  margin-block/padding-inline/border-inline rules as physical ones; the
  library's HTML is unchanged.
- **Print Book** lays every topic out in a hidden WebBrowser at the
  printable width (1 CSS pixel = 1/96 inch, 2x optical zoom for resolution),
  measures block and line starts with a small script, splits it into pages
  with the same rule as the macOS reader (`print_plan.cpp`, tested on every
  platform) and draws each page into the printer DC through
  `IViewObject::Draw` and GDI, so the numbering and the contents' page numbers
  are exact and landscape pages can be mixed in. Topic pages are therefore
  raster images (about 192 dpi), unlike Print Topic. With
  `GEIST_PRINT_TO_DIRECTORY` set the pages are written as bitmaps to that
  directory instead of a printer, for testing.
- **High DPI and look.** The manifest declares PerMonitorV2 awareness and
  Common Controls 6; fonts come from the system message font for each DPI,
  the toolbar uses Segoe Fluent Icons or Segoe MDL2 Assets glyphs (words on
  systems without them), the tree uses the Explorer theme, and
  `WM_DPICHANGED` rescales fonts, toolbar, tree and splitter. MSHTML scales
  its content with `DOCHOSTUIFLAG_DPI_AWARE`; on a monitor whose DPI differs
  from the system DPI the reader adds an optical zoom. Dark mode is not
  supported.
- **Icon.** `geist-boo-reader.ico` is made from `gui/geist-hardcopy.svg`, the
  icon the macOS app uses, by `make_icon.py` (committed, because the CI
  Windows runner has no SVG rasteriser).
- **Licences.** libpng, zlib and giflib are inside the executable, so their
  licence texts are too: Help > Licences shows them with this project's
  LICENSE. The CI artifact also carries them as files.
