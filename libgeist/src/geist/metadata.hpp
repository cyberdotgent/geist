#pragma once

#include "geist/page.hpp"

#include <cstdint>
#include <filesystem>

namespace geist {

// File-level physical metadata that is available immediately after open().
struct BooMetadata {
  std::filesystem::path path;
  std::uint64_t file_size = 0;
  std::uint32_t page_size = boo_page_size;
  std::uint32_t page_count = 0;
};

} // namespace geist
