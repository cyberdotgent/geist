#include "geist/detail/implicit_grid.hpp"

#include <algorithm>
#include <cctype>

namespace geist::detail {
namespace {

enum class Signature { a, b };

struct Boundary {
  std::size_t token = 0;
  std::size_t prefix = 0;
  bool continuation = false;
};

bool one_byte(const DecodedLogicalRecordSource& record,
              std::size_t token,
              std::uint16_t value) {
  return token < record.encoded_tokens.size() &&
         record.encoded_tokens[token].width == 1 &&
         record.encoded_tokens[token].value == value;
}

bool signature_a(const DecodedLogicalRecordSource& record,
                 std::size_t token) {
  static constexpr std::uint16_t pattern[] =
      {0x09, 0x17, 0x00, 0x7a, 0x00, 0x17, 0x00};
  if (token + std::size(pattern) > record.encoded_tokens.size()) {
    return false;
  }
  for (std::size_t index = 0; index < std::size(pattern); ++index) {
    if (!one_byte(record, token + index, pattern[index])) {
      return false;
    }
  }
  return true;
}

bool signature_b(const DecodedLogicalRecordSource& record,
                 std::size_t token) {
  if (token < 3 || !one_byte(record, token, 0x09) ||
      !one_byte(record, token - 3, 0x01) ||
      !one_byte(record, token - 2, 0x00) ||
      record.encoded_tokens[token - 1].width != 1) {
    return false;
  }
  const auto marker_width = record.encoded_tokens[token - 1].value;
  return marker_width >= 0x0b && marker_width <= 0x20 &&
         token + 1 < record.tokens.size() && !record.tokens[token + 1].empty();
}

bool exact_spaces(const TokenWords& words, std::size_t count) {
  return words.size() == count &&
         std::all_of(words.begin(), words.end(),
                     [](std::uint16_t word) { return word == ' '; });
}

bool structural_marker_token(const TokenWords& words) {
  auto saw_marker = false;
  for (const auto word : words) {
    if (word < 0x20 || word == ' ') {
      continue;
    }
    if (word == '(' || word == ')' || word == '<' || word == '>' ||
        word == '/' || word == '=' || word == 0x2500) {
      saw_marker = true;
      continue;
    }
    return false;
  }
  return saw_marker;
}

std::size_t continuation_prefix(const DecodedLogicalRecordSource& record,
                                std::size_t token) {
  auto prefix = token;
  while (prefix > 0 &&
         !record.tokens[prefix - 1].empty() &&
         std::all_of(record.tokens[prefix - 1].begin(),
                     record.tokens[prefix - 1].end(),
                     [](std::uint16_t word) { return word == ' '; })) {
    --prefix;
  }
  if (prefix > 0 && structural_marker_token(record.tokens[prefix - 1])) {
    --prefix;
  }
  return prefix;
}

std::string visible_slice(const DecodedLogicalRecordSource& record,
                          std::size_t begin,
                          std::size_t end) {
  std::vector<TokenWords> tokens(record.tokens.begin() + begin,
                                 record.tokens.begin() + end);
  const auto assembled = assemble_logical_record(tokens);
  TokenWords visible;
  visible.reserve(assembled.size());
  for (const auto word : assembled) {
    if (word >= 0x20 && word != 0x2666) {
      visible.push_back(word);
    }
  }
  return token_words_to_ascii(visible);
}

std::string trim(std::string value) {
  const auto visible = [](unsigned char ch) { return std::isspace(ch) == 0; };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), visible));
  value.erase(std::find_if(value.rbegin(), value.rend(), visible).base(),
              value.end());
  return value;
}

std::optional<std::pair<std::size_t, std::size_t>> header_columns(
    const std::vector<ImplicitGridHeaderSpan>& spans) {
  if (spans.size() < 3) {
    return std::nullopt;
  }
  std::vector<ImplicitGridHeaderSpan> sorted = spans;
  std::sort(sorted.begin(), sorted.end(), [](const auto& left,
                                             const auto& right) {
    return left.offset < right.offset;
  });
  std::vector<std::size_t> group_starts{0};
  for (std::size_t index = 1; index < sorted.size(); ++index) {
    const auto previous_end = sorted[index - 1].offset +
                              sorted[index - 1].length;
    if (sorted[index].offset >= previous_end + 2) {
      group_starts.push_back(index);
    }
  }
  if (group_starts.size() != 2 || group_starts[1] == 0) {
    return std::nullopt;
  }
  return std::pair{sorted.front().offset,
                   sorted[group_starts[1]].offset};
}

} // namespace

bool is_implicit_grid_header_geometry(
    const std::vector<ImplicitGridHeaderSpan>& header_spans) {
  return header_columns(header_spans).has_value();
}

std::optional<ImplicitGrid> extract_implicit_grid(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::vector<ImplicitGridHeaderSpan>& header_spans) {
  const auto columns = header_columns(header_spans);
  if (!columns || columns->first == 0 || columns->second <= columns->first) {
    return std::nullopt;
  }

  std::size_t a_count = 0;
  std::size_t b_count = 0;
  for (std::size_t record_index = 0; record_index < records.size();
       ++record_index) {
    const auto& record = records[record_index];
    for (std::size_t token = 0; token < record.tokens.size(); ++token) {
      a_count += signature_a(record, token);
      b_count += signature_b(record, token);
    }
  }
  const auto mode = a_count >= 4 ? std::optional{Signature::a}
                                 : b_count >= 6
                                       ? std::optional{Signature::b}
                                       : std::nullopt;
  if (!mode) {
    return std::nullopt;
  }

  ImplicitGrid grid;
  grid.key_origin = columns->first;
  grid.value_origin = columns->second;
  for (std::size_t record_index = 0; record_index < records.size();
       ++record_index) {
    const auto& record = records[record_index];
    std::vector<Boundary> boundaries;
    std::vector<bool> keyed_tokens(record.tokens.size(), false);
    for (std::size_t token = 0; token < record.tokens.size(); ++token) {
      keyed_tokens[token] = *mode == Signature::a
                                ? signature_a(record, token)
                                : signature_b(record, token);
    }
    if (*mode == Signature::b) {
      const auto first = std::find(keyed_tokens.begin(), keyed_tokens.end(),
                                   true);
      if (first != keyed_tokens.end()) {
        const auto first_token =
            static_cast<std::size_t>(first - keyed_tokens.begin());
        // The first physical row in a logical record can use the preceding
        // record's row-control state and therefore omit the repeated B
        // prefix. Admit only the nearest source-owned col-3 origin before the
        // first fully evidenced row; this excludes the styled header and
        // earlier prose origins.
        for (auto token = first_token; token-- > 0;) {
          if (exact_spaces(record.tokens[token], grid.key_origin) &&
              token + 1 < record.tokens.size() &&
              !record.tokens[token + 1].empty()) {
            keyed_tokens[token] = true;
            break;
          }
        }
      }
    }
    for (std::size_t token = 0; token < record.tokens.size(); ++token) {
      const auto keyed = keyed_tokens[token];
      if (keyed && exact_spaces(record.tokens[token], grid.key_origin)) {
        boundaries.push_back(
            {token, token >= (signature_b(record, token) ? 3u : 2u)
                        ? token - (signature_b(record, token) ? 3u : 2u)
                        : token,
             false});
      } else if (exact_spaces(record.tokens[token], grid.value_origin)) {
        boundaries.push_back(
            {token, continuation_prefix(record, token), true});
      }
    }
    if (boundaries.empty()) {
      continue;
    }
    std::sort(boundaries.begin(), boundaries.end(),
              [](const auto& left, const auto& right) {
      return left.token < right.token;
    });
    for (std::size_t index = 0; index < boundaries.size(); ++index) {
      const auto& boundary = boundaries[index];
      const auto end = index + 1 < boundaries.size()
                           ? boundaries[index + 1].prefix
                           : record.tokens.size();
      if (end <= boundary.token) {
        continue;
      }
      auto text = visible_slice(record, boundary.token, end);
      ImplicitGridRow row;
      row.continuation = boundary.continuation;
      if (boundary.continuation) {
        row.value = trim(text.substr(std::min(grid.value_origin,
                                             text.size())));
      } else {
        auto cursor = std::min(grid.key_origin, text.size());
        const auto key_end = text.find(' ', cursor);
        if (key_end == std::string::npos) {
          row.key = trim(text.substr(cursor));
        } else {
          row.key = trim(text.substr(cursor, key_end - cursor));
          const auto value_begin = text.find_first_not_of(' ', key_end);
          if (value_begin != std::string::npos &&
              value_begin >= grid.value_origin) {
            row.value = trim(text.substr(value_begin));
          }
        }
      }
      if (!row.key.empty() || !row.value.empty()) {
        grid.physical_rows.push_back(std::move(row));
        if (record_index + 1 == records.size() &&
            index + 1 == boundaries.size() &&
            end == record.tokens.size()) {
          grid.owns_source_tail = true;
        }
      }
    }
  }

  std::vector<std::string>* pending = nullptr;
  for (const auto& physical : grid.physical_rows) {
    if (!physical.continuation && !physical.key.empty()) {
      grid.semantic_rows.push_back({physical.key, physical.value});
      pending = &grid.semantic_rows.back();
    } else if (pending != nullptr && !physical.value.empty()) {
      if (!(*pending)[1].empty()) {
        (*pending)[1] += "<br>";
      }
      (*pending)[1] += physical.value;
    }
  }
  if (grid.semantic_rows.size() < 4 || !grid.owns_source_tail) {
    return std::nullopt;
  }
  return grid;
}

} // namespace geist::detail
