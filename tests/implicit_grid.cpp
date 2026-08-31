// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/layout/implicit_grid.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    geist_test::record_failure();
    return;
  }
}


bool contains(const std::vector<std::vector<std::string>>& rows,
              const std::string& key,
              const std::string& value) {
  for (const auto& row : rows) {
    if (row.size() == 2 && row[0] == key &&
        row[1].find(value) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool contains_fragment(const std::vector<std::vector<std::string>>& rows,
                       const std::string& fragment) {
  for (const auto& row : rows) {
    for (const auto& cell : row) {
      if (cell.find(fragment) != std::string::npos) {
        return true;
      }
    }
  }
  return false;
}

geist::detail::DecodedLogicalRecordSource synthetic_b_rows() {
  geist::detail::DecodedLogicalRecordSource record;
  record.logical_record = 1;
  for (int row = 0; row < 6; ++row) {
    const auto append = [&](std::uint16_t encoded,
                            geist::detail::TokenWords words) {
      record.encoded_tokens.push_back({encoded, 1});
      record.tokens.push_back(std::move(words));
    };
    append(0x01, {1, '.'});
    append(0x00, {1});
    append(0x12, {'>'});
    append(0x09, {' ', ' ', ' '});
    append(0x40 + row,
           {'k', 'e', 'y', static_cast<std::uint16_t>('0' + row)});
    append(0x50 + row,
           geist::detail::TokenWords(11, static_cast<std::uint16_t>(' ')));
    append(0x60 + row,
           {'v', 'a', 'l', static_cast<std::uint16_t>('0' + row)});
  }
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  return record;
}

} // namespace

int main() {
  const auto synthetic = synthetic_b_rows();
  const auto synthetic_markers =
      geist::detail::source_row_markers({synthetic}, 3);
  require(synthetic_markers.size() == 6 &&
              synthetic_markers.front().marker == ">" &&
              synthetic_markers.front().following_text == "key0",
          "source row marker ownership was not retained");
  require(geist::detail::source_row_markers({synthetic}, 4).empty(),
          "wrong column origin fabricated source row markers");
  const auto synthetic_grid = geist::detail::extract_implicit_grid(
      {synthetic}, {{3, 3}, {7, 4}, {18, 5}});
  require(synthetic_grid && synthetic_grid->semantic_rows.size() == 6 &&
              contains(synthetic_grid->semantic_rows, "key5", "val5"),
          "repeated source-owned B rows were not classified");
  require(!geist::detail::extract_implicit_grid(
               {synthetic}, {{3, 3}, {7, 4}, {12, 5}}),
          "single-group CFONT heading activated an implicit grid");

}
