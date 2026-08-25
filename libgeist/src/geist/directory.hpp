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
};

} // namespace geist
