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

std::string trim_right_spaces(std::string value) {
  while (!value.empty() && value.back() == ' ') {
    value.pop_back();
  }
  return value;
}

std::string trim_ascii(std::string value) {
  while (!value.empty() &&
         (std::isspace(static_cast<unsigned char>(value.back())) != 0 ||
          static_cast<unsigned char>(value.back()) < 0x20 ||
          value.back() == '?')) {
    value.pop_back();
  }
  std::size_t first = 0;
  while (first < value.size() &&
         (std::isspace(static_cast<unsigned char>(value[first])) != 0 ||
          static_cast<unsigned char>(value[first]) < 0x20 ||
          value[first] == '?')) {
    ++first;
  }
  if (first != 0) {
    value.erase(0, first);
  }
  return value;
}

std::string ascii_lower(std::string value) {
  for (auto& ch : value) {
    ch = static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

bool ascii_equals_case_insensitive(const std::string& left,
                                   const std::string& right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(left[i])) !=
        std::tolower(static_cast<unsigned char>(right[i]))) {
      return false;
    }
  }
  return true;
}

bool ascii_starts_with_case_insensitive(const std::string& value,
                                        std::size_t offset,
                                        std::string_view prefix) {
  if (offset + prefix.size() > value.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(value[offset + i])) !=
        std::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

bool ascii_starts_with_case_insensitive(const std::string& value,
                                        std::string_view prefix) {
  return ascii_starts_with_case_insensitive(value, 0, prefix);
}

void replace_all_case_insensitive(std::string& value,
                                  const std::string& needle,
                                  const std::string& replacement) {
  std::size_t offset = 0;
  while (offset < value.size()) {
    if (ascii_starts_with_case_insensitive(value, offset, needle)) {
      value.replace(offset, needle.size(), replacement);
      offset += replacement.size();
    } else {
      ++offset;
    }
  }
}

std::string capitalize_bookmanager_words(std::string value) {
  bool capitalize_next = true;
  for (auto& ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isalpha(byte) != 0) {
      if (capitalize_next) {
        ch = static_cast<char>(std::toupper(byte));
      }
      capitalize_next = false;
    } else {
      capitalize_next = (ch == ' ' || ch == ':' || ch == '(' || ch == '-');
    }
  }
  return value;
}

std::string normalize_logical_control_value(const std::string& key,
                                            std::string value) {
  if (key == "CDOCNUM") {
    for (auto& ch : value) {
      ch = static_cast<char>(
          std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
  }

  if (key == "CTITLE" || key == "CSTITLE") {
    value = capitalize_bookmanager_words(value);
    replace_all_case_insensitive(value, "AS/400", "AS/400");
    replace_all_case_insensitive(value, "(TM)", "(TM)");
    replace_all_case_insensitive(value, "Officevision", "OfficeVision");
    replace_all_case_insensitive(value, "Cross-Reference", "Cross-Reference");
    return value;
  }

  if (key == "CCOPYRIGHT") {
    replace_all_case_insensitive(value, "IBM", "IBM");
    return value;
  }

  if (key == "CDATE") {
    return capitalize_bookmanager_words(value);
  }

  return value;
}

std::string normalize_toc_title(std::string value) {
  for (const auto* marker : {"<IMAGE>", "<INTERNET>", "<OTHER>", "<>"}) {
    replace_all_case_insensitive(value, marker, "");
  }
  value = trim_ascii(std::move(value));
  value = capitalize_bookmanager_words(value);
  std::string normalized;
  normalized.reserve(value.size());
  bool first_word = true;
  for (std::size_t cursor = 0; cursor < value.size();) {
    if (std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
      normalized.push_back(value[cursor++]);
      continue;
    }

    const auto word_begin = cursor;
    while (cursor < value.size() &&
           std::isspace(static_cast<unsigned char>(value[cursor])) == 0) {
      ++cursor;
    }
    const auto word = value.substr(word_begin, cursor - word_begin);
    std::string output_word;
    std::size_t part_begin = 0;
    bool first_part = true;
    while (part_begin <= word.size()) {
      const auto part_end = word.find('-', part_begin);
      auto part = word.substr(part_begin,
                              part_end == std::string::npos
                                  ? std::string::npos
                                  : part_end - part_begin);
      const auto lower_part = ascii_lower(part);
      const bool is_minor =
          lower_part == "a" || lower_part == "an" || lower_part == "and" ||
          lower_part == "for" || lower_part == "in" || lower_part == "of" ||
          lower_part == "on" || lower_part == "or" || lower_part == "the" ||
          lower_part == "to" || lower_part == "with";
      if (!(first_word && first_part) && is_minor) {
        part = lower_part;
      }
      if (!first_part) {
        output_word.push_back('-');
      }
      output_word += part;

      if (part_end == std::string::npos) {
        break;
      }
      part_begin = part_end + 1;
      first_part = false;
    }

    normalized += output_word;
    first_word = false;
  }
  value = normalized;
  replace_all_case_insensitive(value, "AS/400", "AS/400");
  replace_all_case_insensitive(value, "(TM)", "(TM)");
  replace_all_case_insensitive(value, "Officevision", "OfficeVision");
  replace_all_case_insensitive(value, "Cross-Reference", "Cross-Reference");
  replace_all_case_insensitive(value, "Ocl", "OCL");
  replace_all_case_insensitive(value, "Dbcs", "DBCS");
  replace_all_case_insensitive(value, "User Id", "User ID");
  if (!value.empty() && (value.back() == '>' || value.back() == '/')) {
    value.pop_back();
    value = trim_ascii(std::move(value));
  }
  return value;
}

std::string normalize_toc_id(std::string value) {
  for (auto& ch : value) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::string collapse_ascii_whitespace(std::string value) {
  std::string output;
  output.reserve(value.size());
  bool in_space = false;
  for (const auto ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isspace(byte) != 0 || byte < 0x20) {
      in_space = true;
      continue;
    }
    if (in_space && !output.empty()) {
      output.push_back(' ');
    }
    output.push_back(ch);
    in_space = false;
  }
  return trim_ascii(output);
}

} // namespace geist::detail
