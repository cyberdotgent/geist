// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/container/revision_declarations.hpp"

#include <algorithm>

namespace geist::detail {

namespace {

// The decoder's word for a byte its code page does not map.
constexpr std::uint16_t unmapped_word = 0xFFFF;

// A token's drawn words: a leading spacing control (a word below 4) is not
// drawn.
TokenWords drawn_words(const LogicalTokenIR& token) {
  auto words = token.decoded_words;
  if (!words.empty() && words.front() < 4) words.erase(words.begin());
  return words;
}

bool blank(const TokenWords& words) {
  return std::all_of(words.begin(), words.end(),
                     [](const std::uint16_t word) { return word == ' '; });
}

bool opcode_is_crevdesc(const TokenWords& words) {
  static constexpr char opcode[] = "crevdesc";
  if (words.size() != sizeof(opcode) - 1) return false;
  for (std::size_t index = 0; index < words.size(); ++index) {
    auto word = words[index];
    if (word >= 'A' && word <= 'Z') word = static_cast<std::uint16_t>(word + 32);
    if (word != static_cast<unsigned char>(opcode[index])) return false;
  }
  return true;
}

} // namespace

DeclaredRevisionCharacters read_declared_revision_characters(
    const std::vector<LogicalRecordIR>& header_records) {
  DeclaredRevisionCharacters declared;
  for (const auto& record : header_records) {
    if (!record.display_lines_parse) continue;
    for (const auto& line : record.display_lines) {
      // The line's drawn tokens after its length byte, blanks skipped: the
      // opcode, then the operands.
      std::vector<TokenWords> drawn;
      for (auto token = line.prefix_token + 1;
           token < line.token_end && token < record.tokens.size(); ++token) {
        auto words = drawn_words(record.tokens[token]);
        if (words.empty() || blank(words)) continue;
        drawn.push_back(std::move(words));
        if (drawn.size() == 2) break;
      }
      if (drawn.size() != 2 || !opcode_is_crevdesc(drawn[0])) continue;
      const auto& operand = drawn[1];
      if (operand.size() != 1 || operand.front() <= ' ' ||
          operand.front() == unmapped_word)
        continue;
      declared.push_back(static_cast<char32_t>(operand.front()));
    }
  }
  std::sort(declared.begin(), declared.end());
  declared.erase(std::unique(declared.begin(), declared.end()),
                 declared.end());
  return declared;
}

DeclaredRevisionCharacters read_book_header_revision_characters(
    const LogicalDecodeContext& context,
    const std::uint32_t first_topic_record) {
  std::vector<LogicalRecordIR> header;
  const auto end = std::min<std::size_t>(
      first_topic_record, context.record_payload_ranges.size() + 1);
  if (end > 1) {
    const auto& dictionary = source_dictionary_for(context);
    for (std::size_t logical_record = 1; logical_record < end;
         ++logical_record) {
      const auto& range = context.record_payload_ranges[logical_record - 1];
      if (range.begin > range.end || range.end > context.bytes.size()) break;
      header.push_back(decode_record_payload_ir(
          context.bytes, context.directory, dictionary, range.begin, range.end,
          static_cast<std::uint32_t>(logical_record)));
    }
  }
  return read_declared_revision_characters(header);
}

} // namespace geist::detail
