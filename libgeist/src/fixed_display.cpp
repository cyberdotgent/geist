#include "geist/detail/internal.hpp"

#include <cctype>
#include <stdexcept>

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
                                       bool allow_adjacent,
                                       const std::vector<bool>&
                                           protected_columns) {
  for (std::size_t cursor = 0; cursor + 2 < row.text.size(); ++cursor) {
    if (!is_fixed_display_marker(row.text[cursor])) {
      continue;
    }
    // Styled punctuation is lexical display content. Structural marker fields
    // occupy unstyled columns, so a CFONT-owned candidate must survive even
    // when its glyph and following padding otherwise resemble a marker.
    if (cursor < protected_columns.size() && protected_columns[cursor]) {
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

std::vector<ReconstructedFixedDisplayRow> reconstruct_fixed_display_rows(
    const std::vector<FixedDisplayFragment>& fragments) {
  std::vector<ReconstructedFixedDisplayRow> rows;
  for (const auto& fragment : fragments) {
    if (rows.empty() || fragment.starts_row) {
      rows.emplace_back();
    }
    auto& row = rows.back();
    const auto required = fragment.display_column + fragment.text.size();
    if (row.text.size() < required) {
      row.text.resize(required, ' ');
      row.sources.resize(required);
      row.source_present.resize(required, false);
    }
    for (std::size_t index = 0; index < fragment.text.size(); ++index) {
      const auto column = fragment.display_column + index;
      const auto ch = fragment.text[index];
      if (row.source_present[column] && row.text[column] != ' ' && ch != ' ' &&
          row.text[column] != ch) {
        throw std::invalid_argument(
            "fixed-display fragments overlap with different text");
      }
      // A real source byte supersedes synthetic padding. Whitespace remains
      // source-owned because its original column can matter to later CFONT or
      // CSELECT projection.
      if (!row.source_present[column] || ch != ' ') {
        row.text[column] = ch;
        row.sources[column] = {fragment.logical_record,
                               fragment.token_index,
                               fragment.source_word_begin + index};
        row.source_present[column] = true;
      }
    }
  }
  return rows;
}

std::vector<std::vector<std::string>> aggregate_fixed_form_rows(
    const std::vector<FixedFormPhysicalRow>& physical_rows,
    std::size_t column_count) {
  if (column_count == 0) {
    throw std::invalid_argument("fixed form must have at least one column");
  }

  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> pending;
  const auto flush = [&]() {
    if (pending.empty()) {
      return;
    }
    rows.push_back(std::move(pending));
    pending.clear();
  };
  const auto append = [&](const FixedFormPhysicalRow& row) {
    if (row.cells.size() != column_count) {
      throw std::invalid_argument(
          "fixed form physical row does not match the discovered grid");
    }
    if (pending.empty()) {
      pending.resize(column_count);
    }
    for (std::size_t column = 0; column < column_count; ++column) {
      if (row.cells[column].empty()) {
        continue;
      }
      if (!pending[column].empty()) {
        pending[column] += "<br>";
      }
      pending[column] += row.cells[column];
    }
  };

  for (const auto& physical : physical_rows) {
    switch (physical.kind) {
      case FixedFormPhysicalRowKind::row_start:
        flush();
        append(physical);
        break;
      case FixedFormPhysicalRowKind::continuation:
        append(physical);
        break;
      case FixedFormPhysicalRowKind::border:
        flush();
        break;
      case FixedFormPhysicalRowKind::spacer:
        break;
    }
  }
  flush();
  return rows;
}


} // namespace geist::detail
