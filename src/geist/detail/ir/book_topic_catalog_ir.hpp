// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/toc.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

// Topic-header evidence available at the public TopicInfo boundary.  Logical
// record bounds are the finest provenance exposed there; token and byte source
// locations are intentionally not inferred.
struct BookTopicHeaderEvidenceIR {
  std::string title;
  std::string heading_level;
  std::uint32_t topic_number = 0;
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  std::size_t topic_info_index = 0;
};

// A contents-catalog projection may carry a different display title and
// heading presentation from the topic header.  Preserve every projection in
// source order; consumers must state whether header or TOC label evidence is
// authoritative for their semantic boundary.
struct BookTopicTocEvidenceIR {
  std::string raw_id;
  std::string title;
  std::uint32_t level = 0;
  std::uint32_t style = 0;
  std::string heading_level;
  std::uint32_t topic_number = 0;
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  std::size_t toc_index = 0;
};

struct BookTopicCatalogEntryIR {
  // Retains the spelling at the first evidence boundary.  Resolution is ASCII
  // case-insensitive, matching BookManager topic identity behavior.
  std::string raw_topic_id;
  std::optional<BookTopicHeaderEvidenceIR> topic_header;
  std::vector<BookTopicTocEvidenceIR> toc_entries;
};

struct BookTopicCatalogIR {
  std::vector<BookTopicCatalogEntryIR> topics;
};

std::optional<BookTopicCatalogIR>
build_book_topic_catalog_ir(const std::vector<TopicInfo> &topics,
                            const std::vector<TocEntry> &toc,
                            std::string *error = nullptr);
bool verify_book_topic_catalog_ir(const std::vector<TopicInfo> &topics,
                                  const std::vector<TocEntry> &toc,
                                  const BookTopicCatalogIR &catalog,
                                  std::string *error = nullptr);
const BookTopicCatalogEntryIR *
find_book_topic_catalog_entry(const BookTopicCatalogIR &catalog,
                              const std::string &raw_topic_id);

} // namespace geist::detail
