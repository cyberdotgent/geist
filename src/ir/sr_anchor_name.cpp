// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/sr_anchor_name.hpp"

#include "geist/detail/layout/display_lines.hpp"

namespace geist::detail {

std::optional<std::string>
sr_row_anchor_name(const DecodedLogicalRecordSource &record,
                   std::size_t opcode_token) {
  if (!record_framing_is_decided(record.ir))
    return std::nullopt;
  const auto *line = display_line_of_token(record, opcode_token);
  if (line == nullptr || line->prefix_token + 1 != opcode_token)
    return std::nullopt;
  TokenWords words;
  for (auto word : display_line_columns(record, *line)) {
    // A word the dictionary does not map is the reader's substitution byte
    // 0xFF in the served name (AFM41MST 1.3 `LEN GPCLWS -\xff  Close
    // Workstation`, SC31-6081-00 12.1.10 `MSG Code:\xff   102`).
    if (word == 0xFFFF)
      word = 0x00FF;
    const auto printable = (word >= 0x20 && word <= 0x7E) ||
                           (word >= 0xA0 && word <= 0xFF);
    if (!printable)
      return std::nullopt;
    words.push_back(word == 0xA0 ? std::uint16_t{' '} : word);
  }
  while (!words.empty() && words.back() == ' ')
    words.pop_back();
  if (words.size() < 2 || (words[0] != 'S' && words[0] != 's') ||
      (words[1] != 'R' && words[1] != 'r'))
    return std::nullopt;
  words.erase(words.begin(), words.begin() + 2);
  if (words.empty())
    return std::nullopt;
  return token_words_to_ascii(words);
}

std::optional<std::string>
message_row_anchor_name(const DecodedLogicalRecordSource &record,
                        const ControlSegmentIR &segment) {
  if (segment.source_tokens.empty())
    return std::nullopt;
  auto name = sr_row_anchor_name(record, segment.source_tokens.front());
  if (!name || name->size() < 3 ||
      !ascii_equals_case_insensitive(name->substr(0, 3), "msg") ||
      (name->size() > 3 && (*name)[3] != ' '))
    return std::nullopt;
  return "MSG" + name->substr(3);
}

} // namespace geist::detail
