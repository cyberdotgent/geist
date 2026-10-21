// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/document.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace geist_apache::service {

struct BookSummary {
  std::string id;
  std::string title;
  std::string document_number;
  std::string built;
  std::uint64_t size = 0;
  bool readable = false;
};

struct BookDetail : BookSummary {
  std::string language;
  std::string format_version;
  std::string build_version;
  std::string copyright;
  std::string security;
  std::string date;
  std::vector<std::string> authors;
  std::size_t topic_count = 0;
  std::size_t toc_count = 0;
  std::size_t resource_count = 0;
};

struct TopicSummary {
  std::string id;
  std::string title;
  std::string heading_level;
  std::size_t source_index = 0;
  std::uint32_t topic_number = 0;
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  bool in_toc = false;
  std::uint32_t toc_level = 0;
  std::uint32_t toc_style = 0;
};

struct SourceCoordinates {
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  std::size_t token_begin = 0;
  std::size_t token_end = 0;
  std::uint32_t byte_begin = 0;
  std::uint32_t byte_end = 0;
};

struct Degradation {
  std::string block;
  std::string reason;
  std::string detail;
  SourceCoordinates source;
};

struct Diagnostic {
  std::string severity;
  std::string route;
  std::string family;
  std::string reason;
  std::string detail;
  SourceCoordinates source;
  std::vector<Degradation> degradations;
  std::size_t degradation_count = 0;
};

struct TopicDetail : TopicSummary {
  Diagnostic diagnostic;
};

enum class TopicFormat { markdown, html };

struct TopicContent {
  TopicDetail topic;
  TopicFormat format = TopicFormat::markdown;
  std::string media_type;
  std::string content;
};

struct ResourceSummary {
  std::string id;
  std::string name;
  std::string kind;
  std::string stored_format;
  std::string description;
  std::string layout;
  std::uint64_t size = 0;
};

enum class ResourceRepresentation { stored, web };

class UnsupportedRepresentation : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct ResourceContent {
  ResourceSummary resource;
  ResourceRepresentation representation = ResourceRepresentation::stored;
  std::string media_type;
  std::vector<std::uint8_t> bytes;
};

// One immediate child of the configured shelf. `open` is the only operation
// allowed to turn its opaque public id back into a document; transports never
// receive or reconstruct a filesystem path.
struct BookSource {
  BookSummary summary;
  std::function<std::shared_ptr<const geist::BooDocument>()> open;
};

class Catalog {
public:
  explicit Catalog(std::vector<BookSource> books);

  const std::vector<BookSummary>& books() const noexcept;
  std::optional<BookDetail> book(const std::string& book_id) const;
  std::optional<std::vector<TopicSummary>> topics(
      const std::string& book_id) const;
  std::optional<std::vector<TopicSummary>> toc(
      const std::string& book_id) const;
  std::optional<TopicDetail> topic(const std::string& book_id,
                                   const std::string& topic_id) const;
  std::optional<TopicContent> topic_content(
      const std::string& book_id, const std::string& topic_id,
      TopicFormat format) const;
  std::optional<std::vector<ResourceSummary>> resources(
      const std::string& book_id) const;
  std::optional<ResourceSummary> resource(
      const std::string& book_id, const std::string& resource_id) const;
  std::optional<ResourceContent> resource_content(
      const std::string& book_id, const std::string& resource_id,
      ResourceRepresentation representation) const;

private:
  const BookSource* source(const std::string& book_id) const noexcept;
  std::vector<BookSource> sources_;
  std::vector<BookSummary> books_;
};

} // namespace geist_apache::service
