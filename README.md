# Geist

Tools for reading IBM BookManager `BOO` books.

## libgeist

A self-contained C++17 library that parses a BOO container and renders its
topics to Markdown or HTML. It has no dependencies beyond `libpng` and
`giflib`, and it decides nothing about presentation: a consumer supplies the
link resolvers, the page chrome and the URL space it wants
(`src/geist/html.hpp`). The BOO format itself is documented in
`doc/boo-spec/`, completely enough to implement a reader without reading this
source.

Command-line tools, in `examples/`:

| Tool | Does |
|---|---|
| `booinfo` | book metadata |
| `bootoc` | table of contents |
| `boorsrc` | list and extract stored images (`--list`, `--extract`, `--png`) |
| `boorender` | render a topic to Markdown or HTML |
| `bootrace` | trace rendered output back to the source bytes |
| `boo2git` | export a whole book to a folder tree |

```sh
cmake -S . -B build && cmake --build build
./build/booinfo book.boo
./build/boorsrc --png book.boo 1 figure.png
```

### Installing

```sh
cmake -S . -B build && cmake --build build
sudo cmake --install build          # or: sudo make install
```

Standard GNU locations under the prefix (`/usr/local` by default): the
libraries in `lib`, the public headers in `include/geist`, the tools and the
reader in `bin`, and a CMake package so a consumer can

```cmake
find_package(geist REQUIRED)
target_link_libraries(app PRIVATE geist::geist)
```

The Apache module is the exception: httpd owns where modules and their
configuration live, so it installs to httpd's own directories rather than the
prefix, and it is enabled on install. `DESTDIR` is honoured throughout, and
`-DGEIST_APACHE_ENABLE=OFF` installs the configuration without enabling it,
which is what a distribution package usually wants.

## Geist Hardcopy Reader

A Qt6 desktop reader, laid out after IBM's BookManager SoftCopy Reader: a
topic tree, a divider, and a topic pane, with separate font-size controls for
each. Built by default when Qt6 is present.

```sh
./build/gui/geist-hardcopy book.boo
```

Needs Qt6 Widgets, WebEngineWidgets and PrintSupport
(`apt install qt6-base-dev qt6-webengine-dev`, or `brew install qt`). Pass
`-DGEIST_BUILD_GUI=ON` to make a missing Qt an error rather than a skip, and
`-DGEIST_BUILD_GUI=OFF` to never build it.

## mod_geist

An Apache 2.4 module that serves a BOO book over HTTP. A book is addressed by
its own path, so `htdocs/packet.boo` is served from `/packet.boo`:

| URL | Serves |
|---|---|
| `/packet.boo` | the book index |
| `/packet.boo/topic/<id>` | one rendered topic |
| `/packet.boo/object/<id>` | one stored image |
| `/packet.boo/download` | the BOO file |

With `BooIndex On`, a directory of books is browsable too, after BookServer's
bookshelf page: `/books/` lists every `.boo` beside it by title, with its file
name, document number, build stamp and size, and a filter box. A book's
identity stays its file path and never its document number, so a single book
uploaded anywhere is servable without any directory being scanned.

IBM's proprietary image formats are rendered to PNG; objects a book stores in
a web format are served byte for byte under the media type the book itself
records. The module links libgeist statically and compiles in its own CSS,
JavaScript and icons, so there is one `.so` to deploy and nothing beside it.

Needs `apache2-dev libapr1-dev libaprutil1-dev` (Debian/Ubuntu) or
`httpd-devel apr-devel apr-util-devel` (Fedora/RHEL); it is built when those
are present, and `-DGEIST_BUILD_APACHE=ON` makes their absence an error.

Installing enables it: the module lands in httpd's module directory and a
`geist.load`/`geist.conf` pair is installed and enabled (a symlink from
`mods-enabled` on Debian and Ubuntu, a `conf.modules.d` drop-in elsewhere).
Restart httpd and a book in the document root is browsable. The shipped
configuration binds `.boo` files to the handler and carries the defaults:

Four settings, valid in the server config, a `<Directory>` or `<Files>` block,
and in `.htaccess` where `AllowOverride` permits it:

```apache
GeistDownload On     # offer the BOO file for download (default On)
GeistTheme    auto   # auto (default), light or dark
BooIndex      Off    # list the books in a browsed directory (default Off)
BooIndexTitle "IBM SoftCopy Library"   # heading; defaults to the directory name
```

`BooIndex` is off by default because turning it on publishes the name and
title of every book in the directory. It slots into the directory-index chain
rather than replacing it, so precedence is what an operator already expects: a
real `DirectoryIndex` file wins, then the book list, then `mod_autoindex`. A
directory holding no books is declined and listed as usual. The page carries
`X-Robots-Tag: noindex, nofollow`, since it puts every book one hop from a
single URL and a crawl is expensive; drop that with `mod_headers` to have it
indexed.

Books are cached per httpd child, so memory grows with what has been browsed
and `MaxConnectionsPerChild` is what reclaims it. Install a book atomically --
write it under a temporary name in the same directory and `mv` it into place
-- because a book read while it is still being copied is a truncated file, and
is listed as unreadable until the copy finishes.

## Licence

Apache-2.0; see `LICENSE`. Bundled Qt, libpng and giflib components are
covered by their own licences.
