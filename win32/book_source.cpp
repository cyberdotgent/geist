// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "book_source.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <regex>
#include <utility>

namespace geist_reader {
namespace {

const char kScheme[] = "geist://";

std::string escaped(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    switch (c) {
    case '&': out += "&amp;"; break;
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '"': out += "&quot;"; break;
    default: out += c;
    }
  }
  return out;
}

bool is_continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

// Moves `offset` back by up to `count` code points.
std::size_t back_code_points(const std::string& text, std::size_t offset,
                             std::size_t count) {
  while (offset > 0 && count > 0) {
    --offset;
    while (offset > 0 && is_continuation(static_cast<unsigned char>(text[offset])))
      --offset;
    --count;
  }
  return offset;
}

std::size_t forward_code_points(const std::string& text, std::size_t offset,
                                std::size_t count) {
  while (offset < text.size() && count > 0) {
    ++offset;
    while (offset < text.size() &&
           is_continuation(static_cast<unsigned char>(text[offset])))
      ++offset;
    --count;
  }
  return offset;
}

} // namespace

std::string encode_segment(const std::string& value) {
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : value) {
    if (std::isalnum(c) && c < 0x80) out += static_cast<char>(c);
    else if (c == '-' || c == '.' || c == '_' || c == '~') out += static_cast<char>(c);
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::string decode_segment(const std::string& value) {
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      int high = digit(value[i + 1]), low = digit(value[i + 2]);
      if (high >= 0 && low >= 0) {
        out += static_cast<char>(high * 16 + low);
        i += 2;
        continue;
      }
    }
    out += value[i];
  }
  return out;
}

std::optional<ReaderUrl> parse_url(const std::string& url) {
  if (url.size() < sizeof kScheme - 1 ||
      !equals_ignoring_case(url.substr(0, sizeof kScheme - 1), kScheme))
    return std::nullopt;
  std::string rest = url.substr(sizeof kScheme - 1);
  ReaderUrl parsed;
  const auto hash = rest.find('#');
  if (hash != std::string::npos) {
    parsed.fragment = decode_segment(rest.substr(hash + 1));
    rest.erase(hash);
  }
  const auto query = rest.find('?');
  if (query != std::string::npos) rest.erase(query);
  const auto slash = rest.find('/');
  if (slash == std::string::npos) return std::nullopt;
  parsed.host = rest.substr(0, slash);
  std::transform(parsed.host.begin(), parsed.host.end(), parsed.host.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const std::string path = rest.substr(slash);
  if (path.rfind("/topic/", 0) == 0) {
    parsed.kind = ReaderUrl::Kind::topic;
    parsed.id = decode_segment(path.substr(7));
  } else if (path.rfind("/object/", 0) == 0) {
    parsed.kind = ReaderUrl::Kind::object;
    parsed.id = decode_segment(path.substr(8));
  } else {
    return std::nullopt;
  }
  if (parsed.id.empty()) return std::nullopt;
  return parsed;
}

std::string trim(const std::string& text) {
  const char* space = " \t\r\n\f\v";
  const auto first = text.find_first_not_of(space);
  if (first == std::string::npos) return {};
  const auto last = text.find_last_not_of(space);
  return text.substr(first, last - first + 1);
}

bool equals_ignoring_case(const std::string& a, const std::string& b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) ==
                  std::tolower(static_cast<unsigned char>(y));
         });
}

std::string clean_snippet(const std::string& text) {
  static const std::pair<std::regex, const char*> rules[] = {
      {std::regex(R"(\[([^\]]+)\]\([^)]*\))"), "$1"},
      {std::regex(R"(\\([[:punct:]]))"), "$1"},
      {std::regex(R"([`*_#>]+)"), " "},
      {std::regex(R"(\s+)"), " "},
  };
  std::string result = text;
  for (const auto& rule : rules) result = std::regex_replace(result, rule.first, rule.second);
  return trim(result);
}

std::optional<std::size_t> ascii_matcher(const std::string& haystack,
                                         const std::string& needle) {
  if (needle.empty()) return std::nullopt;
  auto found = std::search(haystack.begin(), haystack.end(), needle.begin(),
                           needle.end(), [](char x, char y) {
                             return std::tolower(static_cast<unsigned char>(x)) ==
                                    std::tolower(static_cast<unsigned char>(y));
                           });
  if (found == haystack.end()) return std::nullopt;
  return static_cast<std::size_t>(found - haystack.begin());
}

BookSource::BookSource(std::string host) : host_(std::move(host)) {}

bool BookSource::has_book() const { return snapshot() != nullptr; }

std::shared_ptr<const geist::BooDocument> BookSource::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return document_;
}

void BookSource::close() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    document_.reset();
    links_.reset();
    objects_.clear();
  }
  title_.clear();
  contents_.clear();
  information_ = {};
}

bool BookSource::open(const std::filesystem::path& path, std::string& error) {
  std::shared_ptr<geist::BooDocument> opened;
  try {
    opened = std::make_shared<geist::BooDocument>(geist::BooDocument::open(path));
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  }
  close();
  auto links = std::make_shared<std::unordered_map<std::string, Destination>>();
  try {
    for (const auto& entry : opened->table_of_contents()) {
      contents_.push_back({entry.id, trim(entry.title), entry.level});
      for (const auto& target : entry.link_targets()) {
        Destination destination{entry.id, target.fragment, target.resource};
        (*links)[target.id] = destination;
        if (target.kind == geist::LinkTargetKind::figure)
          (*links)["FIG" + target.id] = destination;
        if (target.kind == geist::LinkTargetKind::table)
          (*links)["TBL" + target.id] = destination;
      }
    }
  } catch (const std::exception& exception) {
    contents_.clear();
    error = exception.what();
    return false;
  }
  const auto& book = opened->book_properties();
  title_ = trim(book.title.empty() ? book.short_title : book.title);
  auto& info = information_;
  info.title = trim(book.title);
  info.short_title = trim(book.short_title);
  info.document_number = trim(book.document_number);
  info.language = trim(book.language);
  info.version = trim(book.version);
  info.build_version = trim(book.build_version);
  info.date = trim(book.date);
  info.security = trim(book.security);
  info.copyright = trim(book.copyright);
  for (const auto& author : book.authors) {
    if (!info.authors.empty()) info.authors += ", ";
    info.authors += author;
  }
  info.topic_count = contents_.size();
  info.resource_count = opened->resources().size();
  info.file_size = opened->metadata().file_size;
  info.page_count = opened->metadata().page_count;
  info.file = path;
  std::lock_guard<std::mutex> lock(mutex_);
  document_ = opened;
  links_ = links;
  return true;
}

std::string BookSource::topic_url(const std::string& id,
                                  const std::string& fragment) const {
  std::string url = kScheme + host_ + "/topic/" + encode_segment(id);
  if (!fragment.empty()) url += "#" + encode_segment(fragment);
  return url;
}

std::string BookSource::object_url(const std::string& id) const {
  return kScheme + host_ + "/object/" + encode_segment(id);
}

std::optional<std::string> BookSource::topic_page(const std::string& topic_id) const {
  std::shared_ptr<const geist::BooDocument> document;
  std::shared_ptr<const std::unordered_map<std::string, Destination>> links;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    document = document_;
    links = links_;
  }
  if (!document) return std::nullopt;
  const auto* entry = document->find_toc_entry(topic_id);
  if (!entry) return std::nullopt;
  geist::HtmlRenderOptions render;
  render.resolve_topic = [this](const std::string& id) -> std::optional<std::string> {
    return topic_url(id);
  };
  // Only stored objects are served; any other reference (an image on a
  // server) stays as written.
  render.resolve_resource = [this, document](const std::string& id)
      -> std::optional<std::string> {
    for (const auto& resource : document->resources())
      if (resource.id == id) return object_url(id);
    return std::nullopt;
  };
  render.resolve_anchor = [this, document, links, topic_id](const std::string& id)
      -> std::optional<std::string> {
    const auto found = links->find(id);
    if (found == links->end()) {
      if (document->find_toc_entry(id)) return topic_url(id);
      return std::nullopt;
    }
    const auto& dest = found->second;
    if (!dest.resource.empty()) {
      const std::string prefix = "resource:";
      return object_url(dest.resource.rfind(prefix, 0) == 0
                            ? dest.resource.substr(prefix.size())
                            : dest.resource);
    }
    if (dest.topic == topic_id) {
      if (dest.fragment.empty()) return std::nullopt;
      return "#" + encode_segment(dest.fragment);
    }
    return topic_url(dest.topic, dest.fragment);
  };
  const std::string title = escaped(trim(entry->title));
  const std::string header = "<div class='reader-topic-header'><span class='reader-topic-id'>" +
                             escaped(topic_id) + "</span> <span class='reader-topic-title'>" + title +
                             "</span></div>";
  // X-UA-Compatible must precede every element but <title> and <meta>.
  return "<!doctype html><html lang='en'><head>"
         "<meta http-equiv='X-UA-Compatible' content='IE=edge'>"
         "<meta charset='utf-8'><title>" + title + "</title><style>" + stylesheet_ +
         "\n.reader-topic-header{margin:0 0 1rem 0;padding:0 0 .35rem 0;}"
         ".reader-topic-id{font-style:italic;margin-right:.5rem;}"
         ".reader-topic-title{font-weight:700;}</style>" +
         head_extra_ + "</head><body class='geist-book'>" + header +
         entry->html_fragment(render) + "</body></html>";
}

std::optional<std::string> BookSource::object_png(const std::string& id) {
  std::shared_ptr<const geist::BooDocument> document;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    document = document_;
    const auto cached = objects_.find(id);
    if (document && cached != objects_.end()) return cached->second;
  }
  if (!document) return std::nullopt;
  std::optional<std::string> data;
  try {
    const auto bytes = document->read_resource_png(id);
    data = std::string(bytes.begin(), bytes.end());
  } catch (const std::exception&) {
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (document == document_) objects_[id] = data;
  return data;
}

std::optional<Response> BookSource::serve(const std::string& url) {
  const auto parsed = parse_url(url);
  if (!parsed || parsed->host != host_) return std::nullopt;
  if (parsed->kind == ReaderUrl::Kind::topic) {
    auto page = topic_page(parsed->id);
    if (!page) return std::nullopt;
    return Response{"text/html", std::move(*page)};
  }
  auto png = object_png(parsed->id);
  if (!png) return std::nullopt;
  return Response{"image/png", std::move(*png)};
}

std::vector<SearchHit> BookSource::search(const std::string& query,
                                          const Matcher& matcher) const {
  std::vector<SearchHit> hits;
  const auto document = snapshot();
  if (!document || query.empty()) return hits;
  for (const auto& entry : document->table_of_contents()) {
    std::string body;
    try {
      body = entry.markdown();
    } catch (const std::exception&) {
      continue;
    }
    const auto in_title = matcher(entry.title, query);
    const auto in_body = matcher(body, query);
    if (!in_title && !in_body) continue;
    std::string snippet = entry.title;
    if (in_body) {
      const std::size_t start = back_code_points(body, *in_body, 45);
      const std::size_t end = forward_code_points(body, start, 110);
      snippet = clean_snippet(body.substr(start, end - start));
    }
    hits.push_back({entry.id, entry.title, snippet});
  }
  return hits;
}

} // namespace geist_reader
