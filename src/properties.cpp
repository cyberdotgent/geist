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

namespace geist::detail {

BooBookProperties build_book_properties(
    const std::vector<BooLogicalControl>& controls) {
  BooBookProperties properties;
  for (const auto& control : controls) {
    if (control.key == "CLANGUAGE") {
      properties.language = control.value;
    } else if (control.key == "CVERSION") {
      properties.version = control.value;
    } else if (control.key == "CBLDVERS") {
      properties.build_version = control.value;
    } else if (control.key == "CREFLOW") {
      properties.reflow = ascii_lower(control.value) == "on";
    } else if (control.key == "CTITLE") {
      properties.title = control.value;
    } else if (control.key == "CSTITLE") {
      properties.short_title = control.value;
    } else if (control.key == "CCOPYRIGHT") {
      properties.copyright = control.value;
    } else if (control.key == "CSECURITY") {
      properties.security = control.value;
    } else if (control.key == "CDATE") {
      properties.date = control.value;
    } else if (control.key == "CAUTHOR") {
      if (!control.value.empty()) {
        properties.authors.push_back(control.value);
      }
    } else if (control.key == "CDOCNUM") {
      properties.document_number = control.value;
    }
  }
  return properties;
}

} // namespace geist::detail
