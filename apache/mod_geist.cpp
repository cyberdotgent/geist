// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// mod_geist: browse IBM BookManager BOO books over HTTP.
//
// A book is addressed by its own path in the document root, so a book at
// htdocs/packet.boo is served from /packet.boo, and everything it contains
// hangs below that:
//
//   /packet.boo                 the book index
//   /packet.boo/topic/<id>      one rendered topic
//   /packet.boo/object/<id>     one stored image
//   /packet.boo/download        the BOO file itself
//   /packet.boo/asset/<name>    the module's own CSS, JS and icons
//
// With `BooIndex On`, a directory holding books is itself browsable, after
// BookServer's bookshelf page:
//
//   /books/                     every .boo in that directory, by title
//
// A book's identity in this URL space is its *file path*, never its document
// number.  Routing on the document number would make serving one book depend
// on scanning its whole directory, which would cost the module its property
// that a lone .boo dropped anywhere is servable with no configuration at all;
// it would also need BookServer's revision picker, since two revisions of one
// document share a number but are two files.  The document number stays a
// resolution input -- the shelf can map it back to a filename -- not an
// address.
//
// libgeist renders a book but does not decide what a link means; this module
// brings its own URL space, link map and page chrome, exactly as the Qt
// reader brings its own. Nothing here is shared with it.

#include "geist/boo.hpp"

#include "assets.hpp"

#include "geist/probe.hpp"

#include <httpd.h>
#include <http_config.h>
#include <http_core.h>
#include <http_log.h>
#include <http_protocol.h>
#include <http_request.h>
#include <apr_file_info.h>
#include <apr_strings.h>
#include <apr_time.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" module AP_MODULE_DECLARE_DATA geist_module;

// Gives the APLOG_* macros this module's log index.
APLOG_USE_MODULE(geist);

namespace {

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

enum class Tri { unset, off, on };
enum class Theme { unset, automatic, light, dark };

struct DirConfig {
  Tri download = Tri::unset;
  Theme theme = Theme::unset;
  // Whether a directory of books lists itself. Off unless asked: turning it
  // on publishes the names and titles of every book in the directory, which
  // is a disclosure decision only the operator can make.
  Tri index = Tri::unset;
  // The heading the shelf carries, after BookServer's BKCTITLE.
  const char* index_title = nullptr;
};

// A book, opened once per process and shared by every worker thread.
// libgeist documents that after open() every const operation on a document
// and its TOC entries is safe from any number of threads with no external
// synchronisation, which is exactly what a threaded MPM needs.
struct Book {
  std::unique_ptr<geist::BooDocument> document;
  apr_time_t mtime = 0;
  apr_off_t size = 0;

  // Where a cross reference spelled in this book actually lands. Built once,
  // lazily, because harvesting it renders every topic.
  struct Destination {
    std::string topic;
    std::string fragment;
    std::string resource;
  };
  std::map<std::string, Destination> links;
  bool links_built = false;
  std::mutex links_mutex;
};

std::mutex& cache_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<std::string, std::shared_ptr<Book>>& cache() {
  static std::unordered_map<std::string, std::shared_ptr<Book>> books;
  return books;
}

// ---------------------------------------------------------------------------
// Shelf cache
// ---------------------------------------------------------------------------
//
// Two caches, because the two kinds of state have opposite economics.
//
// A book's *identity* -- what a listing shows -- costs about a millisecond to
// read with `geist::probe_book` and a couple of hundred bytes to keep, so it
// is cached per file, validated against that file's own mtime and size, and
// never evicted: six thousand books is around a megabyte.  Only the file that
// changed is re-probed.
//
// The rendered *shelf* is a derived aggregate over all of them -- sorted,
// deduplicated, serialised -- and reasoning about repairing one in place is
// exactly the fiddly work worth refusing.  It is thrown away whole whenever
// the directory's signature changes and rebuilt from the cached identities,
// which costs milliseconds because no book is re-read.

// One book's identity, as the shelf shows it.
struct ShelfEntry {
  std::string filename; // the book's identity in this URL space
  std::string title;
  std::string document_number;
  std::string built;    // the book's own build stamp, not the file's mtime
  apr_off_t size = 0;
  // A book that cannot be read still gets a row: the operator needs to see
  // that it is there and broken, not silently lose it from the shelf.
  // A book that cannot be read is still listed, but says only that much: the
  // reason names a local path, so it goes to the log instead.
  bool readable = true;
};

// A cached identity, with what proves it still current.
struct ShelfMeta {
  apr_time_t mtime = 0;
  apr_off_t size = 0;
  ShelfEntry entry;
};

// The rendered listing for one directory.
struct Shelf {
  std::string signature; // what the directory looked like when this was built
  std::string html;
  apr_time_t newest = 0; // newest book mtime, for Last-Modified
};

std::mutex& shelf_mutex() {
  static std::mutex mutex;
  return mutex;
}

// Keyed by the book's full path.
std::map<std::string, ShelfMeta>& shelf_meta() {
  static std::map<std::string, ShelfMeta> meta;
  return meta;
}

// Keyed by the directory's full path.
std::map<std::string, std::shared_ptr<Shelf>>& shelf_cache() {
  static std::map<std::string, std::shared_ptr<Shelf>> shelves;
  return shelves;
}

// Held across a rebuild so that a directory whose signature just changed is
// rebuilt once rather than once per concurrent request. The rebuilders that
// lose the race re-check the cache and find the finished shelf.
std::mutex& shelf_build_mutex() {
  static std::mutex mutex;
  return mutex;
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

bool ends_with_boo(const char* path) {
  if (path == nullptr) {
    return false;
  }
  const std::string value(path);
  if (value.size() < 4) {
    return false;
  }
  std::string tail = value.substr(value.size() - 4);
  std::transform(tail.begin(), tail.end(), tail.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return tail == ".boo";
}

// Percent-decoding for one path segment. httpd's own helpers differ across
// releases, and a path segment is small enough to decode directly.
std::string url_decode(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  const auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const int hi = hex(value[i + 1]);
      const int lo = hex(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(value[i]);
  }
  return out;
}

const char* esc(request_rec* r, const std::string& value) {
  return ap_escape_html(r->pool, value.c_str());
}

// A path segment, safe to place in a URL.
std::string seg(request_rec* r, const std::string& value) {
  return ap_escape_path_segment(r->pool, value.c_str());
}

// The book's own URL: the request URI with any path info removed.
std::string base_uri(request_rec* r) {
  std::string uri(r->uri != nullptr ? r->uri : "");
  const std::string info(r->path_info != nullptr ? r->path_info : "");
  if (!info.empty() && uri.size() >= info.size() &&
      uri.compare(uri.size() - info.size(), info.size(), info) == 0) {
    uri.erase(uri.size() - info.size());
  }
  while (uri.size() > 1 && uri.back() == '/') {
    uri.pop_back();
  }
  return uri;
}

DirConfig* config_for(request_rec* r) {
  return static_cast<DirConfig*>(
      ap_get_module_config(r->per_dir_config, &geist_module));
}

bool download_allowed(DirConfig* config) {
  return config == nullptr || config->download != Tri::off;
}

const char* theme_attribute(DirConfig* config) {
  if (config == nullptr) {
    return "";
  }
  switch (config->theme) {
  case Theme::light:
    return " data-theme=\"light\"";
  case Theme::dark:
    return " data-theme=\"dark\"";
  case Theme::automatic:
  case Theme::unset:
    break;
  }
  return ""; // auto: let the browser decide via prefers-color-scheme.
}

// ---------------------------------------------------------------------------
// Book cache
// ---------------------------------------------------------------------------

std::shared_ptr<Book> open_book(request_rec* r) {
  apr_finfo_t info;
  if (apr_stat(&info, r->filename, APR_FINFO_MIN, r->pool) != APR_SUCCESS ||
      info.filetype != APR_REG) {
    return nullptr;
  }

  const std::string key(r->filename);
  {
    std::lock_guard<std::mutex> guard(cache_mutex());
    const auto found = cache().find(key);
    // A rebuilt book must not be served from a stale parse.
    if (found != cache().end() && found->second->mtime == info.mtime &&
        found->second->size == info.size) {
      return found->second;
    }
  }

  auto book = std::make_shared<Book>();
  try {
    book->document = std::make_unique<geist::BooDocument>(
        geist::BooDocument::open(r->filename));
  } catch (const std::exception& error) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_geist: cannot open %s: %s",
                  r->filename, error.what());
    return nullptr;
  }
  book->mtime = info.mtime;
  book->size = info.size;

  std::lock_guard<std::mutex> guard(cache_mutex());
  cache()[key] = book;
  return book;
}

void ensure_links(Book& book) {
  std::lock_guard<std::mutex> guard(book.links_mutex);
  if (book.links_built) {
    return;
  }
  book.links_built = true;
  for (const auto& entry : book.document->table_of_contents()) {
    for (const auto& target : entry.link_targets()) {
      const Book::Destination destination{entry.id, target.fragment,
                                          target.resource};
      book.links.emplace(target.id, destination);
      // A figure or table is spelled both bare and with its object prefix.
      if (target.kind == geist::LinkTargetKind::figure) {
        book.links.emplace("FIG" + target.id, destination);
      } else if (target.kind == geist::LinkTargetKind::table) {
        book.links.emplace("TBL" + target.id, destination);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

geist::HtmlRenderOptions render_options(request_rec* r, Book& book,
                                        const std::string& base,
                                        const std::string& current_topic) {
  ensure_links(book);

  geist::HtmlRenderOptions options;
  options.resolve_topic =
      [r, base](const std::string& id) -> std::optional<std::string> {
    return base + "/topic/" + seg(r, id);
  };
  options.resolve_resource =
      [r, base](const std::string& id) -> std::optional<std::string> {
    return base + "/object/" + seg(r, id);
  };
  options.resolve_anchor =
      [r, &book, base, current_topic](
          const std::string& spelled) -> std::optional<std::string> {
    const auto found = book.links.find(spelled);
    if (found == book.links.end()) {
      // A generated list -- contents, index, the figure and table lists --
      // names a topic by the topic's own id, which is not a link target any
      // topic reports. Without this every entry of those lists is dead.
      if (book.document->find_toc_entry(spelled) != nullptr) {
        return base + "/topic/" + seg(r, spelled);
      }
      return std::nullopt; // A genuinely local anchor; "#id" reaches it.
    }
    if (!found->second.resource.empty()) {
      static const std::string marker = "resource:";
      const auto& reference = found->second.resource;
      const auto id = reference.compare(0, marker.size(), marker) == 0
                          ? reference.substr(marker.size())
                          : reference;
      return base + "/object/" + seg(r, id);
    }
    if (found->second.topic == current_topic) {
      return found->second.fragment.empty()
                 ? std::nullopt
                 : std::optional<std::string>("#" + found->second.fragment);
    }
    std::string url = base + "/topic/" + seg(r, found->second.topic);
    if (!found->second.fragment.empty()) {
      url += "#" + found->second.fragment;
    }
    return url;
  };
  return options;
}

void emit_head(request_rec* r, Book& book, const std::string& base,
               const std::string& title, DirConfig* config) {
  const auto& properties = book.document->book_properties();
  const std::string book_title =
      properties.title.empty() ? properties.short_title : properties.title;

  ap_rprintf(r,
             "<!doctype html>\n<html lang=\"en\"%s>\n<head>\n"
             "<meta charset=\"utf-8\">\n"
             "<meta name=\"viewport\" content=\"width=device-width, "
             "initial-scale=1\">\n"
             "<title>%s</title>\n"
             "<link rel=\"stylesheet\" href=\"%s/asset/book.css\">\n"
             "<link rel=\"stylesheet\" href=\"%s/asset/geist.css\">\n"
             "<link rel=\"icon\" href=\"%s/asset/icons/index.svg\">\n"
             "</head>\n<body>\n",
             theme_attribute(config),
             esc(r, title.empty() ? book_title : title + " - " + book_title),
             base.c_str(), base.c_str(), base.c_str());
}

void emit_button(request_rec* r, const std::string& base, const char* icon,
                 const std::string& href, const char* label, bool enabled) {
  if (enabled) {
    ap_rprintf(r,
               "<a class=\"geist-btn\" href=\"%s\" title=\"%s\" "
               "aria-label=\"%s\"><img src=\"%s/asset/icons/%s.svg\" alt=\"\">"
               "</a>\n",
               href.c_str(), label, label, base.c_str(), icon);
  } else {
    ap_rprintf(r,
               "<span class=\"geist-btn\" aria-disabled=\"true\" title=\"%s\">"
               "<img src=\"%s/asset/icons/%s.svg\" alt=\"%s\"></span>\n",
               label, base.c_str(), icon, label);
  }
}

// The toolbar, after BookServer's banner: index, contents, previous, next,
// then the book's identity, then download and details.
void emit_toolbar(request_rec* r, Book& book, const std::string& base,
                  const std::string& current_topic, DirConfig* config) {
  const auto& toc = book.document->table_of_contents();
  const auto& properties = book.document->book_properties();

  std::string previous;
  std::string next;
  if (!current_topic.empty()) {
    for (std::size_t i = 0; i < toc.size(); ++i) {
      if (toc[i].id != current_topic) {
        continue;
      }
      if (i > 0) {
        previous = base + "/topic/" + seg(r, toc[i - 1].id);
      }
      if (i + 1 < toc.size()) {
        next = base + "/topic/" + seg(r, toc[i + 1].id);
      }
      break;
    }
  }

  ap_rputs("<header class=\"geist-bar\">\n", r);
  emit_button(r, base, "index", base, "Book index", true);
  emit_button(r, base, "contents", base, "Contents", true);
  emit_button(r, base, "prev", previous, "Previous topic", !previous.empty());
  emit_button(r, base, "next", next, "Next topic", !next.empty());

  const std::string title =
      properties.title.empty() ? properties.short_title : properties.title;
  ap_rprintf(r, "<span class=\"geist-title\">%s", esc(r, title));
  if (!properties.document_number.empty()) {
    ap_rprintf(r, "<span class=\"geist-doc\">%s</span>",
               esc(r, properties.document_number));
  }
  ap_rputs("</span>\n", r);

  if (download_allowed(config)) {
    emit_button(r, base, "download", base + "/download", "Download the book",
                true);
  }
  emit_button(r, base, "info", base, "Book details", true);
  ap_rputs("</header>\n", r);
}

void emit_toc(request_rec* r, Book& book, const std::string& base,
              const std::string& current_topic) {
  ap_rputs("<nav class=\"geist-toc\" aria-label=\"Contents\">\n"
           "<input id=\"geist-filter\" class=\"geist-filter\" type=\"search\" "
           "placeholder=\"Filter topics\" aria-label=\"Filter topics\">\n"
           "<ol id=\"geist-toc-list\">\n",
           r);
  for (const auto& entry : book.document->table_of_contents()) {
    const bool current = entry.id == current_topic;
    ap_rprintf(r,
               "<li style=\"padding-left:%urem\"><a href=\"%s/topic/%s\"%s>"
               "<span class=\"geist-toc-id\">%s</span>%s</a></li>\n",
               static_cast<unsigned>(entry.level) / 2u, base.c_str(),
               seg(r, entry.id).c_str(),
               current ? " aria-current=\"page\"" : "", esc(r, entry.id),
               esc(r, entry.title));
  }
  ap_rputs("</ol>\n</nav>\n", r);
}

void emit_tail(request_rec* r, const std::string& base) {
  ap_rprintf(r, "<script src=\"%s/asset/geist.js\"></script>\n</body>\n</html>\n",
             base.c_str());
}

// ---------------------------------------------------------------------------
// Routes
// ---------------------------------------------------------------------------

int serve_index(request_rec* r, Book& book, const std::string& base,
                DirConfig* config) {
  ap_set_content_type(r, "text/html; charset=utf-8");
  emit_head(r, book, base, std::string(), config);
  emit_toolbar(r, book, base, std::string(), config);
  ap_rputs("<div class=\"geist-shell\">\n", r);
  emit_toc(r, book, base, std::string());
  ap_rputs("<main class=\"geist-main\">\n<div class=\"geist-book\">\n", r);

  const auto& properties = book.document->book_properties();
  const auto& directory = book.document->directory();
  const auto& metadata = book.document->metadata();

  ap_rprintf(r, "<h1 class=\"geist-topic-head\">%s</h1>\n",
             esc(r, properties.title.empty() ? properties.short_title
                                             : properties.title));

  // The identity block BookServer puts above a book's contents.
  ap_rputs("<div class=\"geist-meta\"><table>\n", r);
  const auto meta_row = [&](const char* name, const std::string& value) {
    if (!value.empty()) {
      ap_rprintf(r, "<tr><td>%s</td><td>%s</td></tr>\n", name, esc(r, value));
    }
  };
  meta_row("Document Number", properties.document_number);
  meta_row("Build Date", directory.date + " " + directory.time);
  meta_row("Build Version", properties.build_version.empty()
                                ? properties.version
                                : properties.build_version);
  meta_row("Language", properties.language);
  meta_row("Topics", std::to_string(book.document->table_of_contents().size()));
  meta_row("Stored objects", std::to_string(book.document->resources().size()));
  meta_row("File size", std::to_string(metadata.file_size) + " bytes");
  ap_rputs("</table></div>\n", r);

  if (download_allowed(config)) {
    ap_rprintf(r,
               "<p><a href=\"%s/download\">Download this book</a></p>\n",
               base.c_str());
  }

  // The book's own contents topic, when it has one, so the index reads as
  // BookServer's does; otherwise the sidebar is the contents.
  const auto* contents = book.document->find_toc_entry("CONTENTS");
  if (contents != nullptr) {
    const auto options = render_options(r, book, base, "CONTENTS");
    const auto html = contents->html_fragment(options);
    ap_rwrite(html.data(), static_cast<int>(html.size()), r);
  }

  ap_rputs("</div>\n</main>\n</div>\n", r);
  emit_tail(r, base);
  return OK;
}

int serve_topic(request_rec* r, Book& book, const std::string& base,
                const std::string& topic_id, DirConfig* config) {
  const auto* entry = book.document->find_toc_entry(topic_id);
  if (entry == nullptr) {
    return HTTP_NOT_FOUND;
  }

  const auto options = render_options(r, book, base, topic_id);
  const auto html = entry->html_fragment(options);

  ap_set_content_type(r, "text/html; charset=utf-8");
  emit_head(r, book, base, entry->title, config);
  emit_toolbar(r, book, base, topic_id, config);
  ap_rputs("<div class=\"geist-shell\">\n", r);
  emit_toc(r, book, base, topic_id);
  ap_rputs("<main class=\"geist-main\">\n<div class=\"geist-book\">\n", r);
  ap_rprintf(r,
             "<h1 class=\"geist-topic-head\">"
             "<span class=\"geist-topic-id\">%s</span>%s</h1>\n",
             esc(r, entry->id), esc(r, entry->title));
  ap_rwrite(html.data(), static_cast<int>(html.size()), r);
  ap_rputs("</div>\n</main>\n</div>\n", r);
  emit_tail(r, base);
  return OK;
}

// A stored object. An IBM proprietary format is rendered to PNG; anything the
// book stores with a real media type -- the v1.4 web formats -- is served
// exactly as stored, under the type the file itself states.
int serve_object(request_rec* r, Book& book, const std::string& object_id) {
  const auto& resources = book.document->resources();
  const auto found =
      std::find_if(resources.begin(), resources.end(),
                   [&](const geist::ResourceEntry& entry) {
                     return entry.id == object_id;
                   });
  if (found == resources.end()) {
    return HTTP_NOT_FOUND;
  }

  std::vector<std::uint8_t> bytes;
  std::string content_type;
  try {
    if (found->stored_format.find('/') != std::string::npos) {
      // Already a web format: pass it through untouched.
      bytes = book.document->read_resource_data(object_id);
      content_type = found->stored_format;
    } else {
      bytes = book.document->read_resource_png(object_id);
      content_type = "image/png";
    }
  } catch (const std::exception& error) {
    ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
                  "mod_geist: object %s (%s) cannot be served: %s",
                  object_id.c_str(), found->stored_format.c_str(),
                  error.what());
    return HTTP_NOT_FOUND;
  }
  if (bytes.empty()) {
    return HTTP_NOT_FOUND;
  }

  ap_set_content_type(r, apr_pstrdup(r->pool, content_type.c_str()));
  ap_set_content_length(r, static_cast<apr_off_t>(bytes.size()));
  ap_rwrite(bytes.data(), static_cast<int>(bytes.size()), r);
  return OK;
}

int serve_download(request_rec* r, DirConfig* config) {
  if (!download_allowed(config)) {
    return HTTP_FORBIDDEN;
  }

  apr_file_t* file = nullptr;
  if (apr_file_open(&file, r->filename, APR_READ | APR_BINARY, APR_OS_DEFAULT,
                    r->pool) != APR_SUCCESS) {
    return HTTP_NOT_FOUND;
  }
  apr_finfo_t info;
  if (apr_stat(&info, r->filename, APR_FINFO_MIN, r->pool) != APR_SUCCESS) {
    apr_file_close(file);
    return HTTP_NOT_FOUND;
  }

  const char* slash = std::strrchr(r->filename, '/');
  const char* name = slash != nullptr ? slash + 1 : r->filename;

  ap_set_content_type(r, "application/octet-stream");
  ap_set_content_length(r, info.size);
  apr_table_setn(r->headers_out, "Content-Disposition",
                 apr_psprintf(r->pool, "attachment; filename=\"%s\"", name));

  apr_size_t sent = 0;
  ap_send_fd(file, r, 0, info.size, &sent);
  apr_file_close(file);
  return OK;
}

int serve_asset(request_rec* r, const std::string& name) {
  for (std::size_t i = 0; i < geist_httpd::kEmbeddedAssetCount; ++i) {
    const auto& asset = geist_httpd::kEmbeddedAssets[i];
    if (name != asset.name) {
      continue;
    }
    ap_set_content_type(r, asset.content_type);
    ap_set_content_length(r, static_cast<apr_off_t>(asset.size));
    // Compiled in, so it only ever changes when the module does.
    apr_table_setn(r->headers_out, "Cache-Control", "public, max-age=86400");
    ap_rwrite(asset.data, static_cast<int>(asset.size), r);
    return OK;
  }
  return HTTP_NOT_FOUND;
}

// ---------------------------------------------------------------------------
// Shelf
// ---------------------------------------------------------------------------

// The shelf's own chrome. The token names and values are geist.css's, so a
// shelf and the books it links to look like one thing in both themes.
constexpr const char* kShelfStyle = R"CSS(
:root {
  --bg: #ffffff; --bg-soft: #f6f7f9; --bg-sunken: #eef0f3;
  --fg: #1b1f24; --fg-muted: #5b636d; --border: #d8dce1;
  --accent: #0b5fa5; --accent-soft: #e7f0f9;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #16191d; --bg-soft: #1d2126; --bg-sunken: #23282e;
    --fg: #e6e9ed; --fg-muted: #9aa3ad; --border: #333a42;
    --accent: #6cb6ff; --accent-soft: #1b2b3a;
  }
}
:root[data-theme="dark"] {
  --bg: #16191d; --bg-soft: #1d2126; --bg-sunken: #23282e;
  --fg: #e6e9ed; --fg-muted: #9aa3ad; --border: #333a42;
  --accent: #6cb6ff; --accent-soft: #1b2b3a;
}
* { box-sizing: border-box; }
body {
  margin: 0 auto; max-width: 72rem; padding: 0 1.25rem 3rem;
  background: var(--bg); color: var(--fg);
  font: 16px/1.55 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
}
a { color: var(--accent); }
.geist-shelf-head { padding: 1.75rem 0 1rem; }
.geist-shelf-head h1 { margin: 0; font-size: 1.5rem; }
.geist-shelf-head p { margin: .35rem 0 0; color: var(--fg-muted);
  font-size: .875rem; }
.geist-shelf-filter { margin: 0 0 1rem; }
.geist-shelf-filter input {
  width: 100%; padding: .55rem .75rem; font: inherit; color: var(--fg);
  background: var(--bg-soft); border: 1px solid var(--border);
  border-radius: .375rem;
}
.geist-shelf { width: 100%; border-collapse: collapse; font-size: .9375rem; }
.geist-shelf th, .geist-shelf td {
  text-align: left; padding: .5rem .75rem;
  border-bottom: 1px solid var(--border); vertical-align: top;
}
.geist-shelf thead th {
  position: sticky; top: 0; background: var(--bg-sunken);
  font-size: .8125rem; text-transform: uppercase;
  letter-spacing: .04em; color: var(--fg-muted);
}
.geist-shelf tbody tr:hover { background: var(--accent-soft); }
.geist-shelf-file, .geist-shelf-size {
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  font-size: .875rem; color: var(--fg-muted); white-space: nowrap;
}
.geist-shelf-size { text-align: right; }
.geist-shelf-error {
  margin-left: .5rem; padding: .05rem .4rem; border-radius: .25rem;
  background: var(--bg-sunken); color: var(--fg-muted); font-size: .75rem;
}
@media (max-width: 40rem) {
  .geist-shelf thead { display: none; }
  .geist-shelf tr { display: block; padding: .5rem 0;
    border-bottom: 1px solid var(--border); }
  .geist-shelf td { display: block; border: 0; padding: .1rem .25rem; }
}
)CSS";

// Client-side filtering over the rendered table.
constexpr const char* kShelfScript = R"JS(<script>
(function () {
  var box = document.getElementById('geist-filter');
  if (!box) return;
  var rows = Array.prototype.slice.call(
    document.querySelectorAll('.geist-shelf tbody tr'));
  var keys = rows.map(function (row) { return row.textContent.toLowerCase(); });
  box.addEventListener('input', function () {
    var needle = box.value.toLowerCase().trim();
    for (var i = 0; i < rows.length; i++) {
      rows[i].hidden = needle !== '' && keys[i].indexOf(needle) === -1;
    }
  });
})();
</script>
)JS";


// One directory entry that is a readable regular .boo file.
struct ShelfFile {
  std::string path;
  std::string name;
  apr_time_t mtime = 0;
  apr_off_t size = 0;
};

// Escapes for HTML text and quoted attributes. The shelf is built into a
// cached string rather than written straight out, so it escapes without a
// request pool of its own.
std::string html_escape(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    switch (c) {
    case '&': out += "&amp;"; break;
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '"': out += "&quot;"; break;
    case '\'': out += "&#39;"; break;
    default: out.push_back(c);
    }
  }
  return out;
}

// Every .boo in `directory`, in no particular order. Subdirectories are not
// followed: a shelf is the books in one directory, as BookServer's collection
// is, and recursing would make the cost of listing unbounded in the depth of
// someone else's tree.
std::vector<ShelfFile> scan_shelf(request_rec* r, const char* directory) {
  std::vector<ShelfFile> files;
  apr_dir_t* dir = nullptr;
  if (apr_dir_open(&dir, directory, r->pool) != APR_SUCCESS) {
    return files;
  }
  apr_finfo_t info;
  while (apr_dir_read(&info, APR_FINFO_NAME | APR_FINFO_MIN, dir) ==
         APR_SUCCESS) {
    if (info.name == nullptr || info.name[0] == '.') {
      continue; // no dotfiles, and never "." or ".."
    }
    if (!ends_with_boo(info.name)) {
      continue;
    }
    const std::string path =
        std::string(directory) + "/" + std::string(info.name);
    // `apr_dir_read` reports the type for the name it walked; a symlink to a
    // book still stats as a regular file, which is what we want.
    apr_finfo_t stat_info;
    if (apr_stat(&stat_info, path.c_str(), APR_FINFO_MIN, r->pool) !=
            APR_SUCCESS ||
        stat_info.filetype != APR_REG) {
      continue;
    }
    files.push_back({path, std::string(info.name), stat_info.mtime,
                     stat_info.size});
  }
  apr_dir_close(dir);
  return files;
}

// What the directory looks like right now, as one comparable string.
//
// It digests every file's name, mtime and size, not summary statistics over
// them.  A count with the newest mtime and the total size looks sufficient
// and is not: restoring a book from a backup with `cp -p` moves its mtime
// *backwards*, and renaming one moves no timestamp at all, so both leave
// every one of those three numbers untouched while the shelf is now wrong.
//
// It is derived from what is on disk and never from a counter this process
// keeps, because each httpd child holds its own cache: a process-local serial
// would make two children emit different ETags for identical bytes and set
// any proxy in front of them thrashing.
std::string shelf_signature(std::vector<ShelfFile> files,
                            apr_time_t* newest) {
  std::sort(files.begin(), files.end(),
            [](const ShelfFile& a, const ShelfFile& b) {
              return a.name < b.name;
            });

  std::uint64_t digest = 1469598103934665603ULL; // FNV-1a
  const auto fold = [&digest](const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
      digest = (digest ^ bytes[i]) * 1099511628211ULL;
    }
  };

  apr_time_t latest = 0;
  for (const auto& file : files) {
    latest = std::max(latest, file.mtime);
    fold(file.name.data(), file.name.size());
    fold(&file.mtime, sizeof(file.mtime));
    fold(&file.size, sizeof(file.size));
  }
  if (newest != nullptr) {
    *newest = latest;
  }

  char hex[32];
  const int written = std::snprintf(hex, sizeof(hex), "%zu-%016llx",
                                    files.size(),
                                    static_cast<unsigned long long>(digest));
  return std::string(hex, written > 0 ? static_cast<std::size_t>(written) : 0);
}

// The identity of every book in `files`, reading only the ones whose cached
// identity no longer matches the file on disk.
std::vector<ShelfEntry> shelf_entries(request_rec* r,
                                      const std::vector<ShelfFile>& files) {
  std::vector<ShelfEntry> entries;
  entries.reserve(files.size());
  for (const auto& file : files) {
    {
      std::lock_guard<std::mutex> guard(shelf_mutex());
      const auto found = shelf_meta().find(file.path);
      if (found != shelf_meta().end() && found->second.mtime == file.mtime &&
          found->second.size == file.size) {
        entries.push_back(found->second.entry);
        continue;
      }
    }

    ShelfEntry entry;
    entry.filename = file.name;
    entry.size = file.size;
    try {
      const auto summary = geist::probe_book(file.path);
      entry.title = summary.properties.title.empty()
                        ? summary.properties.short_title
                        : summary.properties.title;
      entry.document_number = summary.properties.document_number;
      entry.built = summary.directory.date.empty()
                        ? std::string()
                        : summary.directory.date + " " + summary.directory.time;
    } catch (const std::exception& error) {
      // A book copied into place non-atomically is truncated for as long as
      // the copy runs, and reads as a broken container. Show the row, log the
      // reason, and let the next request past the finished copy fix it: the
      // mtime and size will have moved, so the entry revalidates by itself.
      entry.readable = false;
      // The reason names the file's full path, which belongs in the log and
      // not in a page served to the world.
      ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
                    "mod_geist: cannot read %s for the shelf: %s",
                    file.path.c_str(), error.what());
    }
    if (entry.title.empty()) {
      entry.title = file.name; // never show a nameless row
    }

    {
      std::lock_guard<std::mutex> guard(shelf_mutex());
      shelf_meta()[file.path] = ShelfMeta{file.mtime, file.size, entry};
    }
    entries.push_back(std::move(entry));
  }
  return entries;
}

// Drops cached identities for books that are no longer in `directory`.
// Nothing stale is ever *served* -- a book request stats the file before it
// consults the cache, and the shelf is built from the directory scan -- so a
// deleted book only wastes memory. This is what stops that leaking.
void forget_missing(const std::string& directory,
                    const std::vector<ShelfFile>& files) {
  std::vector<std::string> present;
  present.reserve(files.size());
  for (const auto& file : files) {
    present.push_back(file.path);
  }
  std::sort(present.begin(), present.end());

  const std::string prefix = directory + "/";
  std::lock_guard<std::mutex> guard(shelf_mutex());
  for (auto it = shelf_meta().lower_bound(prefix); it != shelf_meta().end();) {
    if (it->first.compare(0, prefix.size(), prefix) != 0) {
      break;
    }
    // Only this directory's own books: a path with another '/' below the
    // prefix belongs to a subdirectory with its own shelf.
    const bool nested =
        it->first.find('/', prefix.size()) != std::string::npos;
    if (!nested && !std::binary_search(present.begin(), present.end(),
                                       it->first)) {
      it = shelf_meta().erase(it);
    } else {
      ++it;
    }
  }
}

// The shelf page. Self-contained: the module's book stylesheet is served from
// a book's own URL, and a directory has no book to hang that route off, so
// the little CSS this page needs travels with it.
std::string render_shelf(request_rec* r, const std::string& heading,
                         std::vector<ShelfEntry> entries,
                         DirConfig* config) {
  std::sort(entries.begin(), entries.end(),
            [](const ShelfEntry& a, const ShelfEntry& b) {
              const auto fold = [](const std::string& value) {
                std::string lower(value);
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                return lower;
              };
              const auto left = fold(a.title);
              const auto right = fold(b.title);
              if (left != right) {
                return left < right;
              }
              return a.filename < b.filename; // stable for equal titles
            });

  std::string out;
  out.reserve(4096 + entries.size() * 512);
  out += "<!doctype html>\n<html lang=\"en\"";
  out += theme_attribute(config);
  out += ">\n<head>\n<meta charset=\"utf-8\">\n";
  out += "<meta name=\"viewport\" content=\"width=device-width, "
         "initial-scale=1\">\n<title>";
  out += html_escape(heading);
  out += "</title>\n<style>\n";
  out += kShelfStyle;
  out += "</style>\n</head>\n<body>\n";

  out += "<header class=\"geist-shelf-head\"><h1>";
  out += html_escape(heading);
  out += "</h1><p>";
  out += std::to_string(entries.size());
  out += entries.size() == 1 ? " book" : " books";
  out += "</p></header>\n";

  // Filtering is done here rather than server-side: BookServer offers a
  // find-by-title box, libgeist exposes no search index, and a table already
  // in the browser can be filtered without asking the server anything.
  out += "<div class=\"geist-shelf-filter\">"
         "<input type=\"search\" id=\"geist-filter\" "
         "placeholder=\"Filter by title, file name or document number\" "
         "aria-label=\"Filter books\"></div>\n";

  out += "<table class=\"geist-shelf\">\n<thead><tr>"
         "<th scope=\"col\">Book title</th>"
         "<th scope=\"col\">File</th>"
         "<th scope=\"col\">Document number</th>"
         "<th scope=\"col\">Built</th>"
         "<th scope=\"col\" class=\"geist-shelf-size\">Size</th>"
         "</tr></thead>\n<tbody>\n";

  for (const auto& entry : entries) {
    const std::string href =
        std::string(ap_escape_path_segment(r->pool, entry.filename.c_str()));
    out += entry.readable ? "<tr>" : "<tr class=\"geist-shelf-broken\">";
    out += "<td><a href=\"";
    out += href;
    out += "\">";
    out += html_escape(entry.title);
    out += "</a>";
    if (!entry.readable) {
      out += "<span class=\"geist-shelf-error\" title=\"This file is not a "
             "readable BOO book; see the server log\">unreadable</span>";
    }
    out += "</td><td class=\"geist-shelf-file\">";
    out += html_escape(entry.filename);
    out += "</td><td>";
    out += html_escape(entry.document_number);
    out += "</td><td>";
    out += html_escape(entry.built);
    out += "</td><td class=\"geist-shelf-size\">";
    out += std::to_string((entry.size + 1023) / 1024);
    out += " KB</td></tr>\n";
  }

  out += "</tbody>\n</table>\n";
  out += kShelfScript;
  out += "</body>\n</html>\n";
  return out;
}

// ---------------------------------------------------------------------------
// Handler
// ---------------------------------------------------------------------------

// The handler name the shipped configuration binds to .boo files. An
// explicit SetHandler/AddHandler wins; otherwise a .boo file is recognised by
// its own name, so the module still works with no configuration at all.
constexpr const char* kHandlerName = "geist-book";

int geist_handler(request_rec* r) {
  const bool named =
      r->handler != nullptr && std::strcmp(r->handler, kHandlerName) == 0;
  if (!named && !ends_with_boo(r->filename)) {
    return DECLINED;
  }
  if (r->method_number != M_GET) {
    return HTTP_METHOD_NOT_ALLOWED;
  }

  auto book = open_book(r);
  if (book == nullptr) {
    return DECLINED; // Not a book we can serve; let the core have it.
  }

  DirConfig* config = config_for(r);
  const std::string base = base_uri(r);
  std::string info(r->path_info != nullptr ? r->path_info : "");
  while (!info.empty() && info.back() == '/') {
    info.pop_back();
  }

  // Every entry point catches: an exception unwinding into httpd's C frames
  // is undefined behaviour and would take the worker with it.
  try {
    if (info.empty()) {
      return serve_index(r, *book, base, config);
    }
    const auto second = info.find('/', 1);
    const std::string kind = info.substr(1, second - 1);
    const std::string rest =
        second == std::string::npos ? std::string() : info.substr(second + 1);

    if (kind == "topic" && !rest.empty()) {
      return serve_topic(r, *book, base, url_decode(rest), config);
    }
    if (kind == "object" && !rest.empty()) {
      return serve_object(r, *book, url_decode(rest));
    }
    if (kind == "asset" && !rest.empty()) {
      return serve_asset(r, rest);
    }
    if (kind == "download" && rest.empty()) {
      return serve_download(r, config);
    }
    return HTTP_NOT_FOUND;
  } catch (const std::exception& error) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_geist: %s", error.what());
    return HTTP_INTERNAL_SERVER_ERROR;
  } catch (...) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_geist: unknown error");
    return HTTP_INTERNAL_SERVER_ERROR;
  }
}

// The shelf handler.
//
// It runs on a directory request, between mod_dir and mod_autoindex, so the
// precedence is the one an operator already expects: a real DirectoryIndex
// file wins, then the shelf, then autoindex. Running *before* mod_autoindex
// also matters because autoindex answers 403 rather than declining when
// `Options -Indexes` is set, which would hide the shelf behind a permission
// error it has nothing to do with.
int geist_dir_handler(request_rec* r) {
  if (r->handler == nullptr ||
      std::strcmp(r->handler, DIR_MAGIC_TYPE) != 0) {
    return DECLINED;
  }
  DirConfig* config = config_for(r);
  if (config == nullptr || config->index != Tri::on) {
    return DECLINED;
  }
  if (r->method_number != M_GET) {
    return DECLINED; // not ours to refuse; let the core answer it
  }
  if (r->filename == nullptr) {
    return DECLINED;
  }

  try {
    std::string directory(r->filename);
    while (directory.size() > 1 && directory.back() == '/') {
      directory.pop_back();
    }

    const auto files = scan_shelf(r, directory.c_str());
    if (files.empty()) {
      // Nothing to add, so add nothing: a directory with no books is left to
      // DirectoryIndex and mod_autoindex exactly as it was before the shelf
      // was switched on. An empty shelf here would hide whatever else the
      // directory holds behind a page saying there is nothing in it.
      return DECLINED;
    }
    apr_time_t newest = 0;
    const auto signature = shelf_signature(files, &newest);

    std::shared_ptr<Shelf> shelf;
    {
      std::lock_guard<std::mutex> guard(shelf_mutex());
      const auto found = shelf_cache().find(directory);
      if (found != shelf_cache().end() &&
          found->second->signature == signature) {
        shelf = found->second;
      }
    }

    if (shelf == nullptr) {
      // One rebuild, not one per concurrent request: whoever loses the race
      // finds the finished shelf on the re-check below.
      std::lock_guard<std::mutex> build(shelf_build_mutex());
      {
        std::lock_guard<std::mutex> guard(shelf_mutex());
        const auto found = shelf_cache().find(directory);
        if (found != shelf_cache().end() &&
            found->second->signature == signature) {
          shelf = found->second;
        }
      }
      if (shelf == nullptr) {
        auto built = std::make_shared<Shelf>();
        built->signature = signature;
        built->newest = newest;

        std::string heading;
        if (config->index_title != nullptr) {
          heading = config->index_title;
        } else {
          const auto slash = directory.find_last_of('/');
          heading = slash == std::string::npos ? directory
                                               : directory.substr(slash + 1);
          if (heading.empty()) {
            heading = "Bookshelf";
          }
        }

        built->html =
            render_shelf(r, heading, shelf_entries(r, files), config);
        forget_missing(directory, files);

        std::lock_guard<std::mutex> guard(shelf_mutex());
        shelf_cache()[directory] = built;
        shelf = built;
      }
    }

    ap_set_content_type(r, "text/html; charset=utf-8");
    // The shelf puts every book in a collection one hop from a single URL,
    // which is exactly the shape a crawler follows; and a crawl is expensive
    // here because serving any topic builds that book's whole link map. An
    // operator who does want the shelf indexed can drop this header with
    // mod_headers.
    apr_table_setn(r->headers_out, "X-Robots-Tag", "noindex, nofollow");
    // Derived from what is on disk, so every child process agrees.
    apr_table_setn(r->headers_out, "ETag",
                   apr_psprintf(r->pool, "\"%s\"", shelf->signature.c_str()));
    if (shelf->newest > 0) {
      ap_update_mtime(r, shelf->newest);
      ap_set_last_modified(r);
    }
    const int conditional = ap_meets_conditions(r);
    if (conditional != OK) {
      return conditional;
    }
    ap_set_content_length(r, static_cast<apr_off_t>(shelf->html.size()));
    ap_rwrite(shelf->html.data(), static_cast<int>(shelf->html.size()), r);
    return OK;
  } catch (const std::exception& error) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_geist: shelf: %s",
                  error.what());
    return HTTP_INTERNAL_SERVER_ERROR;
  } catch (...) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_geist: shelf: unknown");
    return HTTP_INTERNAL_SERVER_ERROR;
  }
}

// Core refuses trailing path info for a plain file unless a handler claims
// it, which would make /packet.boo/topic/1.0 a 404 before we ever ran.
int geist_fixups(request_rec* r) {
  const bool named =
      r->handler != nullptr && std::strcmp(r->handler, kHandlerName) == 0;
  if (named || ends_with_boo(r->filename)) {
    r->used_path_info = AP_REQ_ACCEPT_PATH_INFO;
  }
  return DECLINED;
}

// ---------------------------------------------------------------------------
// Module plumbing
// ---------------------------------------------------------------------------

void* create_dir_config(apr_pool_t* pool, char*) {
  return apr_pcalloc(pool, sizeof(DirConfig));
}

void* merge_dir_config(apr_pool_t* pool, void* base_config, void* add_config) {
  auto* base = static_cast<DirConfig*>(base_config);
  auto* add = static_cast<DirConfig*>(add_config);
  auto* merged = static_cast<DirConfig*>(apr_pcalloc(pool, sizeof(DirConfig)));
  merged->download = add->download != Tri::unset ? add->download : base->download;
  merged->theme = add->theme != Theme::unset ? add->theme : base->theme;
  merged->index = add->index != Tri::unset ? add->index : base->index;
  merged->index_title =
      add->index_title != nullptr ? add->index_title : base->index_title;
  return merged;
}

const char* set_download(cmd_parms*, void* dir_config, const char* value) {
  auto* config = static_cast<DirConfig*>(dir_config);
  if (strcasecmp(value, "on") == 0) {
    config->download = Tri::on;
  } else if (strcasecmp(value, "off") == 0) {
    config->download = Tri::off;
  } else {
    return "GeistDownload takes On or Off";
  }
  return nullptr;
}

const char* set_theme(cmd_parms*, void* dir_config, const char* value) {
  auto* config = static_cast<DirConfig*>(dir_config);
  if (strcasecmp(value, "auto") == 0) {
    config->theme = Theme::automatic;
  } else if (strcasecmp(value, "light") == 0) {
    config->theme = Theme::light;
  } else if (strcasecmp(value, "dark") == 0) {
    config->theme = Theme::dark;
  } else {
    return "GeistTheme takes auto, light or dark";
  }
  return nullptr;
}

const char* set_index(cmd_parms*, void* dir_config, const char* value) {
  auto* config = static_cast<DirConfig*>(dir_config);
  if (strcasecmp(value, "on") == 0) {
    config->index = Tri::on;
  } else if (strcasecmp(value, "off") == 0) {
    config->index = Tri::off;
  } else {
    return "BooIndex takes On or Off";
  }
  return nullptr;
}

const char* set_index_title(cmd_parms* parms, void* dir_config,
                            const char* value) {
  auto* config = static_cast<DirConfig*>(dir_config);
  config->index_title = apr_pstrdup(parms->pool, value);
  return nullptr;
}

// OR_ALL so every directive works in .htaccess wherever AllowOverride permits,
// and per-directory or per-file in the server config.
const command_rec geist_directives[] = {
    AP_INIT_TAKE1("GeistDownload", reinterpret_cast<cmd_func>(set_download),
                  nullptr, OR_ALL,
                  "Offer the BOO file for download: On (default) or Off"),
    AP_INIT_TAKE1("BooIndex", reinterpret_cast<cmd_func>(set_index), nullptr,
                  OR_ALL,
                  "On to list the .boo books in a browsed directory; Off "
                  "(the default) to leave the directory to DirectoryIndex and "
                  "mod_autoindex"),
    AP_INIT_TAKE1("BooIndexTitle",
                  reinterpret_cast<cmd_func>(set_index_title), nullptr, OR_ALL,
                  "Heading for the book list; defaults to the directory name"),
    AP_INIT_TAKE1("GeistTheme", reinterpret_cast<cmd_func>(set_theme), nullptr,
                  OR_ALL,
                  "Colour theme: auto (default), light or dark"),
    {nullptr, {nullptr}, nullptr, 0, RAW_ARGS, nullptr}};

void register_hooks(apr_pool_t*) {
  ap_hook_fixups(geist_fixups, nullptr, nullptr, APR_HOOK_MIDDLE);
  ap_hook_handler(geist_handler, nullptr, nullptr, APR_HOOK_MIDDLE);
  // Ordered explicitly rather than by LoadModule order: after mod_dir so a
  // real DirectoryIndex file still wins, and before mod_autoindex so that
  // `Options -Indexes` -- which autoindex answers with 403 rather than
  // declining -- cannot hide the shelf behind a permission error.
  static const char* const after_dir[] = {"mod_dir.c", nullptr};
  static const char* const before_autoindex[] = {"mod_autoindex.c", nullptr};
  ap_hook_handler(geist_dir_handler, after_dir, before_autoindex,
                  APR_HOOK_MIDDLE);
}

} // namespace

extern "C" module AP_MODULE_DECLARE_DATA geist_module = {
    STANDARD20_MODULE_STUFF,
    create_dir_config,
    merge_dir_config,
    nullptr,
    nullptr,
    geist_directives,
    register_hooks};
