// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/probe.hpp"

#include "geist/detail/core/internal.hpp"

#include <utility>

namespace geist {

using namespace detail;

BooBookSummary probe_book(const std::filesystem::path& path) {
  const auto bytes = read_file(path);

  auto prologue = read_container_prologue(bytes, path);
  BooBookSummary summary;
  summary.metadata = std::move(prologue.metadata);
  summary.directory = std::move(prologue.directory);

  // The book's controls live in the logical records that precede the first
  // topic, and the header closes at the record filing `cdocnum=`.  Decoding
  // stops there: everything after it is topic content this call does not read.
  std::vector<LogicalRecordTokenBoundaries> header_token_boundaries;
  const auto header_records = decode_experimental_logical_records(
      bytes, summary.directory, nullptr, &header_token_boundaries,
      /*stop_after_book_header=*/true);

  header_token_boundaries.resize(header_records.size());
  summary.properties = build_book_properties(
      extract_book_logical_controls(header_records, header_token_boundaries));
  return summary;
}

} // namespace geist
