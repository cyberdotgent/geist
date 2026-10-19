// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

namespace geist {

// Parsed fields from the physical directory page. Names stay neutral where the
// reverse-engineered format meaning is not settled yet.
struct BooDirectory {
  std::uint32_t page_number = 0;
  std::string version_text;
  std::uint8_t version_variant = 0;
  std::uint8_t token_threshold = 0;
  std::uint16_t last_page_number = 0;
  std::uint16_t token_map_offset = 0;
  std::uint16_t dictionary_start_page = 0;
  std::uint16_t dictionary_page_count = 0;
  std::uint16_t content_page_index_offset = 0;
  std::uint16_t logical_record_count = 0;
  std::uint16_t content_start_page = 0;
  std::uint16_t content_page_count = 0;
  std::uint16_t stream_table_offset = 0;
  std::uint16_t stream_table_count = 0;
  std::uint16_t secondary_table_offset = 0;
  std::string date;
  std::string time;
  // Raw directory fields, preserved independently of their interpretation.
  // The prefix's extended-reference role is reader-derived for version 1.3.
  std::uint8_t extended_token_prefix = 0; // 0x0015; observed F0.
  // Big-endian word at 0x004c; code page 500 in observed version-1.2 books.
  // Preserve zero and other values, without applying version-specific defaults.
  std::uint16_t dictionary_text_parameter = 0;
};

} // namespace geist
