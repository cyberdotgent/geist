#include "geist/detail/internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace geist {

std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');

  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i != 0) {
      output << ' ';
    }
    output << std::setw(2) << static_cast<unsigned>(bytes[i]);
  }

  return output.str();
}

const char* to_string(BooPageRole role) noexcept {
  switch (role) {
  case BooPageRole::file_header:
    return "file_header";
  case BooPageRole::directory:
    return "directory";
  case BooPageRole::dictionary:
    return "dictionary";
  case BooPageRole::content:
    return "content";
  case BooPageRole::logical_records:
    return "logical_records";
  case BooPageRole::unknown:
    return "unknown";
  }
  return "unknown";
}

const char* to_string(ResourceLayout layout) noexcept {
  switch (layout) {
  case ResourceLayout::legacy_v12:
    return "legacy_v12";
  case ResourceLayout::legacy_v13:
    return "legacy_v13";
  case ResourceLayout::converted_v14:
    return "converted_v14";
  case ResourceLayout::unknown:
    return "unknown";
  }
  return "unknown";
}

} // namespace geist
