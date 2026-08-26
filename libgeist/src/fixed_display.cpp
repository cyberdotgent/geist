#include "geist/detail/internal.hpp"

#include <cctype>

namespace geist::detail {
namespace {

bool is_fixed_display_marker(char ch) {
  switch (ch) {
    case '$':
    case ';':
    case '(':
    case ')':
    case '*':
    case '!':
    case '-':
    case '\'':
    case ':':
    case '=':
    case '<':
    case '>':
    case '/':
    case '"':
      return true;
    default:
      return false;
  }
}

} // namespace

FixedDisplayRow assemble_fixed_display_row(
    const std::vector<std::string>& fragments) {
  FixedDisplayRow row;
  auto size = std::size_t{0};
  for (const auto& fragment : fragments) {
    size += fragment.size();
  }
  row.text.reserve(size);
  row.source_columns.reserve(size);
  for (std::size_t fragment_index = 0; fragment_index < fragments.size();
       ++fragment_index) {
    const auto& fragment = fragments[fragment_index];
    row.text += fragment;
    for (std::size_t column = 0; column < fragment.size(); ++column) {
      row.source_columns.push_back({fragment_index, column});
    }
  }
  return row;
}

void blank_fixed_display_marker_fields(FixedDisplayRow& row,
                                       bool allow_adjacent) {
  for (std::size_t cursor = 0; cursor + 2 < row.text.size(); ++cursor) {
    if (!is_fixed_display_marker(row.text[cursor])) {
      continue;
    }
    const auto padded =
        std::isspace(static_cast<unsigned char>(row.text[cursor + 1])) != 0 &&
        std::isspace(static_cast<unsigned char>(row.text[cursor + 2])) != 0;
    if (!padded) {
      continue;
    }
    const auto at_field_boundary =
        cursor == 0 ||
        std::isalnum(static_cast<unsigned char>(row.text[cursor - 1])) == 0;
    const auto adjacent_overflow_marker =
        allow_adjacent &&
        (row.text[cursor] == '<' || row.text[cursor] == '>' ||
         row.text[cursor] == '/' || row.text[cursor] == '"');
    if (at_field_boundary || adjacent_overflow_marker) {
      // Keep the byte and its source mapping: display-column controls are
      // measured before structural marker fields are suppressed.
      row.text[cursor] = ' ';
    }
  }
}

} // namespace geist::detail
