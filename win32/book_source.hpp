// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The reader's book, its URL space and its link map. This half of the Win32
// reader is plain C++ over the public libgeist API, so it is compiled and
// tested on every platform; the windowing half lives beside it.
//
// URLs are geist://<host>/topic/<id>[#fragment] and geist://<host>/object/<id>.
// Every open book registers a distinct host, so the one process-wide
// protocol handler can tell which window's book a request belongs to.

#pragma once

#include "geist/document.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace geist_reader {

struct ContentsRow {
  std::string id;
  std::string title;
  std::uint32_t level = 0;
};

struct BookInformation {
  std::string title, short_title, document_number, language, version,
      build_version, date, security, copyright, authors;
  std::size_t topic_count = 0;
  std::size_t resource_count = 0;
  std::uint64_t file_size = 0;
  std::uint64_t page_count = 0;
  std::filesystem::path file;
};

struct SearchHit {
  std::string id;
  std::string title;
  std::string snippet;
};

struct Response {
  std::string mime;
  std::string bytes;
};

struct ReaderUrl {
  std::string host;
  enum class Kind { topic, object } kind = Kind::topic;
  std::string id;
  std::string fragment;
};

// Percent-encodes everything but RFC 3986 unreserved characters.
std::string encode_segment(const std::string& value);
std::string decode_segment(const std::string& value);
std::optional<ReaderUrl> parse_url(const std::string& url);
// Markdown punctuation and link syntax removed, whitespace collapsed.
std::string clean_snippet(const std::string& text);
std::string trim(const std::string& text);
bool equals_ignoring_case(const std::string& a, const std::string& b);

// Finds `needle` in `haystack` (both UTF-8) and returns the byte offset of
// the first match. The Win32 reader supplies a locale-aware, case- and
// diacritic-insensitive matcher; the default folds ASCII case only.
using Matcher = std::function<std::optional<std::size_t>(
    const std::string& haystack, const std::string& needle)>;
std::optional<std::size_t> ascii_matcher(const std::string& haystack,
                                         const std::string& needle);

class BookSource {
public:
  explicit BookSource(std::string host);

  // Opens a book, replacing the current one. Returns false and sets `error`
  // when the file cannot be read as a BOO book.
  bool open(const std::filesystem::path& path, std::string& error);
  void close();

  bool has_book() const;
  const std::string& host() const { return host_; }
  const std::string& title() const { return title_; }
  const std::vector<ContentsRow>& contents() const { return contents_; }
  const BookInformation& information() const { return information_; }
  const std::filesystem::path& path() const { return information_.file; }

  // Print mode drops the reader's topic header, makes links plain text and
  // hides the repeated heading on the cover and title topics.
  void set_print_mode(bool value) { print_mode_ = value; }
  // The consumer stylesheet inlined into every topic page.
  void set_stylesheet(std::string css) { stylesheet_ = std::move(css); }
  // Extra markup placed at the end of <head> (scripts, meta tags).
  void set_head_extra(std::string html) { head_extra_ = std::move(html); }

  std::string topic_url(const std::string& id,
                        const std::string& fragment = {}) const;
  std::string object_url(const std::string& id) const;

  // Answers a geist: URL for this book's host; nullopt when it is not one
  // of ours or names nothing in the book.
  std::optional<Response> serve(const std::string& url);
  std::optional<std::string> topic_page(const std::string& id) const;
  std::optional<std::string> object_png(const std::string& id);

  // Scans every topic's title and text. Safe to call from a worker thread:
  // it works on a snapshot of the open document.
  std::vector<SearchHit> search(const std::string& query,
                                const Matcher& matcher = ascii_matcher) const;

private:
  struct Destination {
    std::string topic, fragment, resource;
  };
  std::shared_ptr<const geist::BooDocument> snapshot() const;

  std::string host_;
  mutable std::mutex mutex_;
  std::shared_ptr<const geist::BooDocument> document_;
  std::shared_ptr<const std::unordered_map<std::string, Destination>> links_;
  std::map<std::string, std::optional<std::string>> objects_;
  std::string title_;
  std::vector<ContentsRow> contents_;
  BookInformation information_;
  std::string stylesheet_;
  std::string head_extra_;
  bool print_mode_ = false;
};

} // namespace geist_reader
