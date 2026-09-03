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

Needs Qt6 Widgets, WebEngineWidgets and PrintSupport; see
[Building and installing](#building-and-installing). Tagged releases carry it
prebuilt: a Windows zip, and Linux AppImages for amd64 and arm64 that bundle
Qt and need nothing installed.

## mod_geist

An Apache 2.4 module that serves a BOO book over HTTP. A book is addressed by
its own path, so `htdocs/packet.boo` is served from `/packet.boo`:

| URL | Serves |
|---|---|
| `/packet.boo` | the book index |
| `/packet.boo/topic/<id>` | one rendered topic |
| `/packet.boo/object/<id>` | one stored image |
| `/packet.boo/download` | the BOO file |
| `/books/` | every book in a directory, with `BooIndex On` |

A book's identity is its file path and never its document number, so a single
book uploaded anywhere is servable without any directory being scanned, and
nothing has to be registered or indexed first. Renaming a book therefore
changes its URL.

IBM's proprietary image formats are rendered to PNG; objects a book stores in
a web format are served byte for byte under the media type the book itself
records. The module links libgeist statically and compiles in its own CSS,
JavaScript and icons, so there is one `.so` to deploy and nothing beside it.

Needs httpd's build toolchain; see
[Building and installing](#building-and-installing). Installing enables it:
the module lands in httpd's module directory and a `geist.load`/`geist.conf`
pair is installed and enabled (a symlink from `mods-enabled` on Debian and
Ubuntu, a `conf.modules.d` drop-in elsewhere). Restart httpd and a book in the
document root is browsable. The shipped configuration binds `.boo` files to
the handler and carries the defaults.

### Settings

Four, valid in the server config, a `<Directory>` or `<Files>` block, and in
`.htaccess` where `AllowOverride` permits it:

```apache
GeistDownload On     # offer the BOO file for download (default On)
GeistTheme    auto   # auto (default), light or dark
BooIndex      Off    # list the books in a browsed directory (default Off)
BooIndexTitle "..."  # pin the shelf's name; by default it comes from .title
```

### Book shelves

`BooIndex On` makes a directory of books browsable, after BookServer's
bookshelf page:

```apache
<Directory /var/www/html>
    BooIndex On
</Directory>
```

Browsing that directory then lists every book in it -- title, file name,
document number, build stamp and size -- sorted by title, with a filter box
that matches on any column, in the browser. A book opened from a shelf
carries a button back to it in its toolbar, which appears only when the
book's own directory actually has a shelf.

What is listed is exactly the `.boo` files in that one directory. The
extension is matched case-insensitively, because these books predate
case-sensitive filesystems and a library routinely holds both `GC28-1251.boo`
and `ACPZMST1.BOO`; the dot is part of the match, so a file merely ending in
those letters is not a book. Subdirectories are not followed, and dotfiles and
everything else in the directory are ignored. A file that is not a readable
BOO container is still listed, marked unreadable, with the reason logged
rather than shown -- it names a local path.

It is off by default because turning it on publishes the name and title of
every book in the directory, which only the operator can decide. It slots into
the directory-index chain rather than replacing it, so precedence is what an
operator already expects: a real `DirectoryIndex` file wins, then the book
list, then `mod_autoindex`. A directory holding no books declines, so a shelf
never hides what autoindex would have shown.

The page carries `X-Robots-Tag: noindex, nofollow`, since it puts every book
one hop from a single URL and a crawl is expensive -- serving any topic builds
that book's whole cross-reference map. Drop the header with `mod_headers` to
have it indexed.

#### Naming a shelf

A shelf is named by the directory it lists. Put the name on the first line of
a file called `.title` beside the books:

```sh
echo 'IBM SoftCopy Library' > /var/www/html/.title
```

Only the first line is read, leading and trailing whitespace and a DOS
carriage return are trimmed, the text is capped at 200 characters, and it is
escaped wherever it lands -- it is a name, not markup. A `.title` that is
missing, empty or blank leaves the shelf headed `Book Index of /path`, after
mod_autoindex's `Index of /path`. `BooIndexTitle` overrides both, for a name
that belongs to the server rather than to the library. Whichever wins also
labels the back button on every book below it.

#### Keeping up with the directory

A shelf is rebuilt when the directory changes, and served from memory
otherwise -- adding, removing, renaming, replacing or restoring a book, and
editing `.title`, all move the page's `ETag`, so a conditional request gets a
`304` until something actually changes. Change is detected from every file's
name, mtime and size rather than from a count or a newest timestamp, because
restoring a book with `cp -p` moves its mtime backwards and a rename moves no
timestamp at all.

Books are cached per httpd child, so memory grows with what has been browsed
and `MaxConnectionsPerChild` is what reclaims it. The first request to a large
shelf after a restart pays for reading every book's identity, and each child
pays once.

Install a book atomically -- write it under a temporary name in the same
directory and `mv` it into place -- because a book read while it is still
being copied is a truncated file, and is listed as unreadable until the copy
finishes.

## Installing from APT

Debian and Ubuntu packages are published to a signed APT repository for
Ubuntu 22.04, 24.04 and 26.04 and Debian 12 and 13, on amd64 and arm64:

```sh
sudo install -d -m 0755 /etc/apt/keyrings
curl -fsSL https://cyberdotgent.github.io/geist/geist-archive-keyring.asc \
  | sudo gpg --dearmor -o /etc/apt/keyrings/geist-archive-keyring.gpg

echo "deb [signed-by=/etc/apt/keyrings/geist-archive-keyring.gpg]" \
     "https://cyberdotgent.github.io/geist" \
     "$(. /etc/os-release && echo $VERSION_CODENAME) stable" \
  | sudo tee /etc/apt/sources.list.d/geist.list

sudo apt update && sudo apt install geist-tools
```

| Package | Contains |
|---|---|
| `libgeist0` | the shared library |
| `libgeist-dev` | headers and the CMake package |
| `geist-tools` | `booinfo`, `bootoc`, `boorsrc`, `boorender`, `bootrace`, `boo2git` |
| `geist-reader` | the Qt desktop reader |
| `libapache2-mod-geist` | the Apache module, enabled on install |

Two tracks share each codename. **stable** is built from git tags and is kept
indefinitely; **unstable** is built from every push to `main`, keeping the
five most recent builds of each package. Swap the word `stable` for
`unstable` in the line above to follow it. An unstable version sorts below the
release it anticipates, so a machine on unstable upgrades onto stable when the
release lands rather than being stranded above it.

The packaged Apache module links the shared library rather than a static copy,
so a libgeist fix reaches it through an ordinary upgrade.

## Building and installing

C++17 and CMake 3.16 or newer. Building and installing are one step apart:

```sh
cmake -S . -B build && cmake --build build
sudo cmake --install build
```

### What each component needs

The library and its command-line tools are always built. The Qt reader and the
Apache module are `AUTO`: each is built when its dependencies are present and
skipped with an explanation when they are not, so a missing Qt or httpd is
never an error unless you ask for one.

| Component | Needs | Debian/Ubuntu | Fedora/RHEL |
|---|---|---|---|
| **libgeist**, tools, tests | a C++17 compiler, CMake, libpng, giflib | `build-essential cmake libpng-dev libgif-dev` | `gcc-c++ cmake libpng-devel giflib-devel` |
| **Geist Hardcopy Reader** (optional) | Qt6 Widgets, WebEngineWidgets, PrintSupport | `qt6-base-dev qt6-webengine-dev` | `qt6-qtbase-devel qt6-qtwebengine-devel` |
| **mod_geist** (optional) | apxs, the APR headers, pkg-config, Python 3 | `apache2-dev libapr1-dev libaprutil1-dev pkgconf python3` | `httpd-devel apr-devel apr-util-devel pkgconf-pkg-config python3` |

Python 3 is a build-time tool only: `apache/tools/bundle.py` compiles the
module's CSS, JavaScript and icons into the `.so`. Nothing at run time needs
it.

Everything at once, on Debian and Ubuntu:

```sh
sudo apt install build-essential cmake libpng-dev libgif-dev \
    qt6-base-dev qt6-webengine-dev \
    apache2-dev libapr1-dev libaprutil1-dev pkgconf python3
```

Just the library and tools:

```sh
sudo apt install build-essential cmake libpng-dev libgif-dev
```

On macOS, `brew install cmake libpng giflib` and `brew install qt` for the
reader; the Apache module is Linux-tested. On Windows the library and tools
build with MSVC, and vcpkg supplies libpng and giflib -- set `VCPKG_ROOT` or
pass `-DCMAKE_TOOLCHAIN_FILE=<vcpkg.cmake>`; there is no Apache module there.

### Choosing what to build

```sh
cmake -S . -B build -DGEIST_BUILD_GUI=OFF -DGEIST_BUILD_APACHE=ON
```

`AUTO` (the default) builds a component if it can, `ON` makes a missing
dependency a hard error -- which is what CI should use, so a silent skip
cannot pass for a green build -- and `OFF` never builds it.

Tests need nothing beyond the library's own dependencies:

```sh
ctest --test-dir build
```

### Where it goes

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

After installing a new module, **restart** httpd rather than reloading it: a
graceful reload re-reads the configuration while the old module is still
mapped, so a newly added directive is rejected and the server stops.

## Licence

Apache-2.0; see `LICENSE`. Bundled Qt, libpng and giflib components are
covered by their own licences.
