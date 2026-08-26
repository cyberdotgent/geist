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

std::vector<FixedDisplayGridRow> derive_fixed_display_grid_rows(
    const std::vector<AssembledLogicalRecord>& records,
    std::uint32_t first_logical_record,
    std::size_t start_token,
    std::size_t key_column,
    std::size_t value_column) {
  struct TokenRef {
    std::uint32_t record = 0;
    std::size_t token = 0;
    std::string text;
  };
  std::vector<TokenRef> tokens;
  for (std::size_t record = 0; record < records.size(); ++record) {
    for (std::size_t token = record == 0 ? start_token : 0;
         token < records[record].tokens.size(); ++token) {
      const auto& span = records[record].tokens[token];
      if (span.output_begin == span.output_end) {
        continue;
      }
      TokenWords words(records[record].words.begin() +
                           static_cast<std::ptrdiff_t>(span.output_begin),
                       records[record].words.begin() +
                           static_cast<std::ptrdiff_t>(span.output_end));
      tokens.push_back({first_logical_record +
                            static_cast<std::uint32_t>(record),
                        token,
                        token_words_to_ascii(words)});
    }
  }
  const auto spaces = [](const std::string& text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](const auto ch) {
             return ch == ' ';
           });
  };
  const auto trim = [](std::string text) {
    while (!text.empty() && text.front() == ' ') {
      text.erase(text.begin());
    }
    while (!text.empty() && text.back() == ' ') {
      text.pop_back();
    }
    return text;
  };
  const auto join = [&](std::size_t begin, std::size_t end) {
    std::string text;
    for (auto index = begin; index < end; ++index) {
      text += tokens[index].text;
    }
    return trim(std::move(text));
  };
  const auto row_origin = [&](std::size_t index) {
    return index > 0 && index + 1 < tokens.size() &&
           spaces(tokens[index].text) &&
           tokens[index].text.size() == key_column &&
           !spaces(tokens[index + 1].text);
  };
  const auto continuation_origin = [&](std::size_t index) {
    if (index == 0 || index + 1 >= tokens.size() ||
        !spaces(tokens[index].text) ||
        tokens[index].text.size() != value_column ||
        spaces(tokens[index + 1].text)) {
      return false;
    }
    // The preceding token is the fixed marker field. Its contents are opaque:
    // observed books use punctuation, alphabetic words, question-fill runs,
    // and blank padding. The exact value-column padding token is the stable
    // physical-layout evidence.
    return true;
  };

  std::vector<FixedDisplayGridRow> rows;
  for (auto origin = std::size_t{0}; origin < tokens.size();) {
    while (origin < tokens.size() && !row_origin(origin) &&
           !continuation_origin(origin)) {
      ++origin;
    }
    if (origin >= tokens.size()) {
      break;
    }
    const auto continuation = continuation_origin(origin);
    const auto content_begin = origin + 1;
    auto value_begin = content_begin;
    auto key_only = false;
    if (!continuation) {
      auto column = key_column;
      while (value_begin < tokens.size() && column < value_column) {
        if (continuation_origin(value_begin)) {
          key_only = true;
          break;
        }
        if (value_begin + 1 < tokens.size() &&
            continuation_origin(value_begin + 1)) {
          ++value_begin;
          key_only = true;
          break;
        }
        const auto length = tokens[value_begin].text.size();
        if (spaces(tokens[value_begin].text) &&
            column + length == value_column) {
          ++value_begin;
          break;
        }
        if (!tokens[value_begin].text.empty() &&
            tokens[value_begin].text.back() == ' ' &&
            column + length - 1 == value_column) {
          ++value_begin;
          break;
        }
        if (!tokens[value_begin].text.empty() &&
            tokens[value_begin].text.back() == ' ' &&
            column + length > value_column &&
            column + length <= value_column + 2) {
          column += length;
          ++value_begin;
          break;
        }
        column += length;
        ++value_begin;
      }
      if (value_begin >= tokens.size() ||
          (!key_only && column > value_column)) {
        ++origin;
        continue;
      }
    }
    if (key_only) {
      const auto marker = value_begin > content_begin ? value_begin - 1
                                                      : value_begin;
      const auto key = join(content_begin, marker);
      if (!key.empty()) {
        rows.push_back({key, {}, false, tokens[content_begin].record});
      }
      origin = value_begin;
      continue;
    }
    auto next = value_begin;
    while (next < tokens.size() && !row_origin(next) &&
           !continuation_origin(next)) {
      ++next;
    }
    // The source fragment immediately before an origin is the fixed marker
    // field (possibly an alphabetic word such as `action`), or layout padding
    // for a value-only continuation. It is not owned by the preceding cell.
    auto content_end = next;
    if (next < tokens.size() && content_end > value_begin) {
      --content_end;
    }
    const auto key = continuation ? std::string{}
                                  : join(content_begin, value_begin);
    const auto value = join(value_begin, content_end);
    if ((!value.empty() || !key.empty()) && (continuation || !key.empty())) {
      rows.push_back({key, value, continuation, tokens[content_begin].record});
    }
    origin = next;
  }
  return rows;
}

} // namespace geist::detail
