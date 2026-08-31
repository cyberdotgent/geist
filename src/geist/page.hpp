#pragma once

#include "geist/export.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace geist {

constexpr std::uint32_t boo_page_size = 4096;

enum class BooPageRole {
  file_header,
  directory,
  dictionary,
  content,
  logical_records,
  unknown,
};

struct BooPage0Header {
  std::uint16_t directory_page_number = 0;
  std::uint16_t unknown_0002 = 0;
  std::uint32_t unknown_0004 = 0;
  std::string copyright_text;
  std::optional<std::array<std::uint8_t, 4>> unknown_0102;
};

// Contiguous run of physical pages with the same first 16-bit page-class word.
// The role is a best-effort classification derived from verified directory
// fields and should be treated as advisory.
struct BooPageRun {
  std::uint32_t start_page = 0;
  std::uint32_t page_count = 0;
  std::uint16_t page_class = 0;
  BooPageRole role = BooPageRole::unknown;
};

GEIST_API const char* to_string(BooPageRole role) noexcept;

} // namespace geist
