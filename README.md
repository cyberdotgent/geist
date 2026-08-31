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

IBM's proprietary image formats are rendered to PNG; objects a book stores in
a web format are served byte for byte under the media type the book itself
records. The module links libgeist statically and compiles in its own CSS,
JavaScript and icons, so there is one `.so` to deploy and nothing beside it.

Needs `apache2-dev libapr1-dev libaprutil1-dev` (Debian/Ubuntu) or
`httpd-devel apr-devel apr-util-devel` (Fedora/RHEL); it is built when those
are present, and `-DGEIST_BUILD_APACHE=ON` makes their absence an error.

```apache
LoadModule geist_module /usr/lib/apache2/modules/mod_geist.so
```

Two settings, valid in the server config, a `<Directory>` or `<Files>` block,
and in `.htaccess` where `AllowOverride` permits it:

```apache
GeistDownload On     # offer the BOO file for download (default On)
GeistTheme    auto   # auto (default), light or dark
```

## Licence

Apache-2.0; see `LICENSE`. Bundled Qt, libpng and giflib components are
covered by their own licences.
