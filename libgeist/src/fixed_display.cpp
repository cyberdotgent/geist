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

bool is_left_box_border(std::uint16_t word) {
  return word == 0x250c || word == 0x251c || word == 0x2514;
}

bool is_right_box_border(std::uint16_t word) {
  return word == 0x2510 || word == 0x2524 || word == 0x2518;
}

bool is_box_junction(std::uint16_t word) {
  return word == 0x252c || word == 0x253c || word == 0x2534;
}

std::string fixed_form_cell_text(const std::vector<std::uint16_t>& words,
                                 std::size_t begin,
                                 std::size_t end,
                                 bool& has_row_marker) {
  std::string text;
  for (auto cursor = begin; cursor < end; ++cursor) {
    const auto word = words[cursor];
    if (word == 0x2666) {
      has_row_marker = true;
      continue;
    }
    if (word == 0x2500) {
      text += "\\_";
      continue;
    }
    text += token_words_to_ascii({word});
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.erase(text.begin());
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.pop_back();
  }
  return text;
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

std::optional<FixedFormGrid> extract_box_fixed_form_grid(
    const std::vector<std::uint16_t>& words) {
  for (std::size_t top = 0; top < words.size(); ++top) {
    if (words[top] != 0x250c) {
      continue;
    }
    std::vector<std::size_t> separators{0};
    auto top_end = top + 1;
    for (; top_end < words.size() && top_end - top <= 200; ++top_end) {
      if (words[top_end] == 0x252c) {
        separators.push_back(top_end - top);
      } else if (words[top_end] == 0x2510) {
        separators.push_back(top_end - top);
        break;
      } else if (words[top_end] != 0x2500) {
        break;
      }
    }
    if (top_end >= words.size() || words[top_end] != 0x2510 ||
        separators.size() < 3) {
      continue;
    }
    const auto width = separators.back() + 1;
    FixedFormGrid grid;
    grid.separator_columns = separators;
    auto next_row_starts = true;
    auto cursor = top_end + 1;
    while (cursor < words.size()) {
      auto candidate = cursor;
      while (candidate < words.size() &&
             words[candidate] != 0x2502 &&
             !is_left_box_border(words[candidate])) {
        ++candidate;
      }
      if (candidate + width > words.size()) {
        break;
      }

      if (is_left_box_border(words[candidate])) {
        auto valid_border =
            is_right_box_border(words[candidate + separators.back()]);
        auto full_horizontal = true;
        for (std::size_t column = 1;
             valid_border && column < separators.back(); ++column) {
          const auto at_separator =
              std::find(separators.begin(), separators.end(), column) !=
              separators.end();
          if (at_separator) {
            valid_border = is_box_junction(words[candidate + column]);
          } else {
            valid_border = words[candidate + column] == 0x2500;
            full_horizontal = full_horizontal && valid_border;
          }
        }
        if (valid_border) {
          grid.physical_rows.push_back(
              {FixedFormPhysicalRowKind::border, {}});
          next_row_starts = full_horizontal;
          const auto bottom = words[candidate] == 0x2514;
          cursor = candidate + width;
          if (bottom) {
            const std::vector<std::uint16_t> tail(
                words.begin() + static_cast<std::ptrdiff_t>(cursor),
                words.end());
            if (extract_box_fixed_form_grid(tail)) {
              return std::nullopt;
            }
            return grid;
          }
          continue;
        }
      }

      auto valid_row = words[candidate] == 0x2502;
      for (const auto separator : separators) {
        valid_row = valid_row &&
                    words[candidate + separator] == 0x2502;
      }
      if (!valid_row) {
        cursor = candidate + 1;
        continue;
      }
      FixedFormPhysicalRow row;
      auto row_marker = false;
      for (std::size_t column = 0; column + 1 < separators.size(); ++column) {
        row.cells.push_back(fixed_form_cell_text(
            words,
            candidate + separators[column] + 1,
            candidate + separators[column + 1],
            row_marker));
      }
      const auto empty = std::all_of(
          row.cells.begin(), row.cells.end(),
          [](const auto& cell) { return cell.empty(); });
      row.kind = empty ? FixedFormPhysicalRowKind::spacer
                       : next_row_starts || row_marker
                             ? FixedFormPhysicalRowKind::row_start
                             : FixedFormPhysicalRowKind::continuation;
      grid.physical_rows.push_back(std::move(row));
      next_row_starts = false;
      cursor = candidate + width;
    }
  }
  return std::nullopt;
}


} // namespace geist::detail
