# Geist BOO Reader for macOS

This AppKit reader is a separate consumer of `libgeist`. It uses Objective-C++
to call the public C++ API and WebKit to display the same HTML and stylesheet
as the Qt reader. It builds only on macOS and does not need Qt.

## Build

```sh
cmake -S . -B build -DGEIST_BUILD_MAC_READER=ON
cmake --build build --target geist-boo-reader
```

The app is `build/macos/Geist BOO Reader.app`, with bundle identifier
`gent.cyber.geist-viewer`. `GEIST_BUILD_MAC_READER` accepts `AUTO` (the
default), `ON`, and `OFF`. The native target is included by default on macOS
and never added on other platforms. The build copies libpng and giflib and
their licence texts into the app, rewrites their Mach-O imports, and signs the
local bundle ad hoc. `LICENSE` and `THIRD-PARTY-NOTICES`, which carries the
acknowledgement for the Hershey font data inside libgeist, are bundle
resources. The system supplies AppKit, WebKit, and zlib.

Each open book has its own window, contents tree, URL handler, and WebKit
history. The app menu's Settings window controls the font family and contents
and topic sizes for all windows. The toolbar search scans all topics in the
current book; clearing it restores the contents tree.
File > Print Book renders every topic as paginated pages, puts the cover and
title pages first, replaces the BOO contents topic with a table of contents
showing the printed page numbers, and numbers every page after the cover and
title. Wide fixed-layout topics use landscape pages to preserve their columns.
The book is prepared before the system print dialog opens; large books may
take time.

## Universal build

Configure the same `build/` directory with
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` and rebuild. CMake must discover
universal libpng and giflib binaries for both architectures; the bundling
step fails if either copied library lacks a requested architecture. A normal
single-architecture Homebrew installation does not supply the opposite slice.
Check every binary with `lipo -archs` before distribution.

## Experimental release DMG

The release workflow builds the native reader on `macos-15`, with both
architectures and a macOS 13.0 Ventura deployment target. It builds the
existing libpng and giflib dependencies as checksum-pinned universal static
libraries, includes their licences, and uses system zlib. No Homebrew runtime
is required. `macos/build_release_dependencies.sh` prepares this dependency
prefix outside the checkout. The normal CMake build does not download it.

The independent **macOS universal DMG** Actions workflow can be dispatched
for preflight artifacts without creating a release. Tag-triggered releases
attach the experimental DMG and SHA-256 checksum and append the warning in
`macos/RELEASE-NOTES.txt` to the release notes. Existing tags are not moved.
These 0.x builds are ad-hoc signed, not Developer ID signed or notarized;
Gatekeeper may block them. The DMG includes instructions for the per-app
Privacy & Security override, without disabling Gatekeeper globally.

CI verifies both Mach-O slices, deployment minimums, system-only imports,
the bundle signature and DMG integrity. It runs the test suite natively and
Intel CLI smoke tests under Rosetta. This does not prove Ventura runtime
compatibility: users must test opening, navigation, search and printing on
Ventura before that compatibility is considered verified.

## Mac App Store preparation

Register the explicit App ID `gent.cyber.geist-viewer` in the Apple Developer
account, then create the matching App Store Connect app record. This repo
already supplies the bundle identity, document type, icon, App Sandbox
entitlements, and read-only access to files selected with the open panel.
The sandbox also permits outgoing connections, which the system WebKit
processes need to launch even for the reader's local book URLs.
For a distribution build, configure with:

```sh
cmake -S . -B build -DGEIST_BUILD_MAC_READER=ON \
  -DGEIST_MAC_SANDBOX=ON \
  -DGEIST_MAC_SIGN_IDENTITY="<distribution identity>" \
  -DGEIST_MAC_PROVISIONING_PROFILE="<profile path>"
cmake --build build --target geist-boo-reader
codesign --verify --strict --deep --verbose=2 "build/macos/Geist BOO Reader.app"
```

The identity and profile come from the developer account and are not stored
in this repository. Test the sandboxed build with books opened through the
app's open panel. Packaging and upload to App Store Connect remain separate
release steps; the repository does not use an Apple account during ordinary
builds.
