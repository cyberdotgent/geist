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

## Geist BOO Reader for macOS

A native AppKit reader built with Objective-C++ and the public `libgeist` API.
It uses the same published topic styling as the Qt reader, with a macOS
toolbar, contents sidebar, book-wide search, one window per book, and font
settings in the app menu. It builds by default on macOS without Qt:

```sh
open "build/macos/Geist BOO Reader.app"
```

The bundle identifier is `gent.cyber.geist-viewer`. See
[macOS build and distribution notes](macos/README.md) for signing, sandboxing,
and universal builds.

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

HTTP responses carry weak ETags for conditional GET and HEAD requests. Rendered
book pages and objects include both the module and libgeist build revisions,
the BOO file's modification time, size and available file identity, the route,
and rendering settings. Shelf validators also include the directory's books and
effective title. Embedded assets use content fingerprints, included in generated
CSS, JavaScript and icon URLs. Raw downloads use the file identity and expose
its `Last-Modified` date; derived responses omit that date because renderer or
configuration changes can alter them without changing the book file.

Book responses and assets are fresh for five minutes in browsers and one week
in shared caches (`public, max-age=300, s-maxage=604800`). Shelves use one minute
and five minutes respectively. Authenticated requests receive a private cache
policy. After freshness expires, a matching ETag receives `304 Not Modified`.
An update does not evict still-fresh responses: deployment may require a proxy
purge for immediate visibility, and browser responses can remain fresh for five
minutes. The persistent metadata cache described below is separate from HTTP
caching.

Needs httpd's build toolchain; see
[Building and installing](#building-and-installing). Installing enables it:
the module lands in httpd's module directory and a `geist.load`/`geist.conf`
pair is installed and enabled (a symlink from `mods-enabled` on Debian and
Ubuntu, a `conf.modules.d` drop-in elsewhere). Restart httpd and a book in the
document root is browsable. The shipped configuration binds `.boo` files to
the handler and carries the defaults.

### Settings

These settings are valid in the server config, a `<Directory>` or `<Files>` block, and in
`.htaccess` where `AllowOverride` permits it:

```apache
GeistDownload On     # offer the BOO file for download (default On)
GeistTheme    auto   # auto (default), light or dark
BooIndex      Off    # list the books in a browsed directory (default Off)
BooIndexTitle "..."  # pin the shelf's name; by default it comes from .title
GeistAPI      Off    # expose the shelf's read-only REST API (default Off)
GeistMCP      Off    # expose the shelf's read-only MCP endpoint (default Off)
HideVersion   Off    # Off (default) reports the version; On says nothing
```

`GeistCacheDir /var/cache/mod_geist` selects the persistent shelf metadata cache.
It is valid in server and virtual host configuration; virtual hosts inherit
it unless they override it. Relative paths resolve against `ServerRoot`.
Cache files contain book identities, not rendered HTML or BOO contents. A missing
or unusable cache falls back to probing books normally.

The module reports which version is running in three places: a footer on
every page it renders, a `mod_geist/<version> libgeist/<version>` component
in the `Server` header (as mod_php and mod_ssl do), and one line in the error
log at startup, which also carries the `git describe` revision:

```
[geist:notice] mod_geist/0.1.0 (v0.1-12-gbcb1d189) loaded, libgeist/0.1.0 (v0.1-12-gbcb1d189)
```

The libgeist half is read at run time, not baked in, so it names the library
actually loaded rather than the one the module was compiled against -- which
is the distinction that matters once the two are packaged apart.

`HideVersion On` removes version identification from the footer and adds
nothing to the `Server` header. If a machine interface is enabled, its footer
link remains. The header is assembled once at startup, so that half follows
`HideVersion` as set at server scope; inside a `<Directory>` it still removes
the version identification from the footer.
httpd's own `ServerTokens Prod` suppresses every component including this
one, so an operator who wants nothing disclosed needs only that.

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

#### Machine interfaces

The REST/OpenAPI and MCP interfaces are independent, optional views of a
book shelf. They are both off by default and are effective only where the
inherited `BooIndex` setting is also `On`:

```apache
<Directory /var/www/html/books>
    BooIndex On
    GeistAPI On
    GeistMCP On

    # Authentication and authorization remain ordinary Apache policy.
    AuthType Basic
    AuthName "BOO library"
    Require valid-user
</Directory>
```

Neither directive enables the other or `BooIndex`. A descendant
`BooIndex Off` disables both there, even if their own settings remain inherited.
Disabled routes answer as ordinary missing paths, so they do not disclose that
a machine interface is installed. When either interface is enabled, `.geist`
is reserved below that shelf.

`GeistAPI On` exposes a conventional, read-only JSON API below
`/.geist/api/v1` and its OpenAPI 3.1.2 description at
`/.geist/openapi.json`. It lists the immediate shelf, book metadata, the table
of contents, every indexed topic, rendering diagnostics, and stored resources.
Topic content is returned as native Markdown or HTML rendered from the same
DocumentIR. Resource content distinguishes `stored`, which is byte-for-byte
the BOO payload, from the explicit `web` representation, which passes through
stored web media and converts supported legacy images to PNG. Book ids are
exact shelf-relative filenames; local filesystem paths and document numbers
are never accepted as identities. The API permits `GET` and `HEAD` and does
not enable cross-origin browser access by default.

`GeistMCP On` exposes Streamable HTTP at `/.geist/mcp`. It implements the
current stateless MCP 2026-07-28 request model and the sessionless 2025-11-25
initialization model for existing clients; it does not offer GET/SSE streams or
mint session ids. Its read-only tools are `list_books`, `get_book`,
`list_topics`, `get_topic`, `list_resources`, and `get_resource`. The same
content is also available through `geist://shelf` resource templates. Requests
with no `Origin` are accepted; a supplied origin must match the request origin.
Use Apache `Require`, TLS and the normal virtual-host controls for access rather
than a separate Geist token. The shelf footer links to `/.geist/mcp/setup`, a
short setup page for Codex and Claude Code. The API footer link opens the
OpenAPI document directly.

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
name and file metadata rather than from a count or a newest timestamp, because
restoring a book with `cp -p` moves its mtime backwards and a rename moves no
timestamp at all.

Books are cached per httpd child, so memory grows with what has been browsed
and `MaxConnectionsPerChild` is what reclaims it.

With `BooIndex On`, shelves warm automatically in a background thread after
Apache forks its children and drops privileges. Startup does not wait for the
walk. The walk discovers directories under each virtual host's document root
and filesystem `<Directory>` roots that enable `BooIndex`, including wildcard
roots. Apache evaluates `<Directory>`, `<DirectoryMatch>` and permitted
`.htaccess` settings for each directory. An inherited `BooIndex Off` suppresses
book probing there; discovery continues because a descendant may enable it
again. Directory symlinks are not followed. Regex roots outside the document
root, URL-only settings such as `<Location>`, and request-dependent `<If>`
settings are handled on requests.

Virtual hosts sharing a cache directory share one walk: a shelf is warmed when
any of them enables it. A nonblocking file lock elects one child per tree. A generation
marker prevents later children repeating a completed startup walk; a restart
or graceful reload walks again. The selected child writes one versioned metadata
snapshot per shelf to `GeistCacheDir`. Other children and later server runs reuse
those identities after checking the live filenames, sizes, modification and
change times, and device/inode identifiers where available. Restarts therefore
scan the tree but avoid reading unchanged books. Metadata checks assume ordinary
filesystem changes; they are not cryptographic verification of BOO contents.

Snapshots use bounded, explicit binary fields and a corruption checksum. Unknown
versions, decoder revisions, oversized or malformed snapshots cause a cache miss.
Writes use private temporary files and atomic replacement; failed probes are not
persisted. Cache files and locks must be writable only by Apache's trusted worker
identity. Shutdown cancels the walk between books and joins the thread before
its configuration is released. A first request during warming builds any missing
identities normally, and shelves reached later refresh their cache on demand.
HTML is still rendered under the request's own URL, theme and title settings.

`BooIndexPreload` is retained for compatibility as an extra asynchronous walk
root, respecting `BooIndex` settings. New configurations need only `BooIndex On`.

Install a book atomically -- write it under a temporary name in the same
directory and `mv` it into place -- because a book read while it is still
being copied is a truncated file, and is listed as unreadable until the copy
finishes.

## Installing from APT

Debian and Ubuntu packages are published to a signed APT repository for
Ubuntu 24.04 and 26.04 and Debian 12 and 13, on amd64:

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
so a libgeist fix reaches it through an ordinary upgrade. `libgeist-dev`
carries no static library: linking libgeist statically is a
build-from-source arrangement.

The repository is published as a single commit each time, replacing the
branch rather than adding to it -- the archive accumulates in the tree, not
in git history, so its size tracks what it currently holds. Only amd64 is
built; arm64 users build from source, or say so and the matrix takes it back.

## Building and installing

C++17 and CMake 3.16 or newer. Building and installing are one step apart:

```sh
cmake -S . -B build && cmake --build build
sudo cmake --install build
```

A build with no `CMAKE_BUILD_TYPE` is an unoptimised build, and this tree is
templated enough that the difference is large -- listing a 1.4 MB book's
resources takes 0.29 s built `Release` and 1.9 s with no type. So `Release` is
the default when none is given, and the configure step says so; pass
`-DCMAKE_BUILD_TYPE=Debug` for a debug build.

Every tool, and the reader, answer `--version` with their own version and the
libgeist they are running against:

```
$ booinfo --version
booinfo/0.3.4 (v0.3.4) libgeist/0.3.4 (v0.3.4)
```

The library's half is asked for at run time, so a tool running against a
libgeist it was not built against reports both -- which is what tells you a
package was not upgraded. mod_geist reports the same pair in its page footer,
its `Server` header and its startup log.

### What each component needs

The library and its command-line tools are always built. The Qt reader and the
Apache module are `AUTO`: each is built when its dependencies are present and
skipped with an explanation when they are not, so a missing Qt or httpd is
never an error unless you ask for one.
The native macOS reader is also `AUTO` and is built on macOS only.

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

On macOS, `brew install cmake libpng giflib` builds the native reader;
`brew install qt` also builds the Qt reader. The Apache module is Linux-tested.
On Windows the library and tools
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

When Python 3 is available, CTest also checks the external-corpus regression
gate. Privately held books are not required by CTest or redistributed; see
[Corpus regression checks](doc/corpus-regression.md) for identity-pinned
coverage and text-conservation comparisons.

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

Installation creates `/var/cache/mod_geist` with mode `0750`, owned by the
httpd worker user and group. Set `GEIST_APACHE_CACHEDIR`,
`GEIST_APACHE_CACHE_USER` and `GEIST_APACHE_CACHE_GROUP` at CMake configuration
time to override the directory and identity. The default identity is
`www-data` for Debian configuration layouts and `apache` otherwise; other
installations must supply their worker identity. Staged `DESTDIR` installs
set permissions but leave ownership to packaging. Debian packages use the
worker identity in `/etc/apache2/envvars` on installation and upgrade.
If you override `GeistCacheDir` in httpd configuration, create that directory
with equivalent permissions yourself. Cache contents may be removed when
httpd is stopped.

After installing a new module, **restart** httpd rather than reloading it: a
graceful reload re-reads the configuration while the old module is still
mapped, so a newly added directive is rejected and the server stops.

## Licence

Apache-2.0; see `LICENSE`. Bundled Qt, libpng and giflib components are
covered by their own licences.
