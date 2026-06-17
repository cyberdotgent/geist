#pragma once

#include "geist/export.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace geist {

struct TocEntry {
  std::string id;
  std::string title;
  std::uint32_t level = 0;
  std::uint32_t style = 0;
  std::string heading_level;
  std::uint32_t topic_number = 0;
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  // GML-style raw projection of the decoded BookManager topic records.
  std::vector<std::string> raw_records;

  GEIST_API std::string markdown() const;
};

} // namespace geist
