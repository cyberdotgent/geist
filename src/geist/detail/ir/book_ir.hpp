// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace geist::detail {

using TokenWords = std::vector<std::uint16_t>;

// Two space-run predicates that several IR builders need. They used to be
// re-derived per file, four times for the first and three for the second, and
// -- because five of those copies were all called `exact_spaces` -- one name
// stood for both. They disagree on the empty token, so keep the names apart:
// an empty token holds no space at all, which makes it not `all_space_words`,
// while it is trivially a space run of width zero.

// A token made of nothing but spaces.
inline bool all_space_words(const TokenWords& words) {
  return !words.empty() &&
         std::all_of(words.begin(), words.end(),
                     [](const std::uint16_t word) { return word == ' '; });
}

// A space run of exactly `width` spaces.
inline bool space_run_of_width(const TokenWords& words, std::size_t width) {
  return words.size() == width &&
         std::all_of(words.begin(), words.end(),
                     [](const std::uint16_t word) { return word == ' '; });
}

struct SourceByteRange {
  std::uint32_t begin = 0;
  std::uint32_t end = 0;
};

struct EncodedLogicalToken {
  std::uint16_t value = 0;
  std::uint8_t width = 0;
};

inline bool operator==(const EncodedLogicalToken& left,
                       const EncodedLogicalToken& right) noexcept {
  return left.value == right.value && left.width == right.width;
}

// Where one token stands in the record's display-line framing.
//
// A record payload is a sequence of `<length byte><that many bytes of
// tokens>` display lines (doc/boo-spec/logical-controls.adoc, "Display Lines Inside
// A Record Payload").  The length byte is a raw byte, but a byte below the
// book's token threshold is resolved through the dictionary like any other
// token, so a length byte routinely expands to a control-shaped word --
// `cparent`, `cfont`, `SRCFILE`, `.`, `are`.  Nothing local separates the
// two roles; only the walk from the record start does.  Recording the role
// on the token is what stops every consumer from having to redo that walk
// (and getting it wrong).
enum class TokenFramingRole : std::uint8_t {
  // The record's payload does not tile into whole display lines, so no token
  // has a decided role.  A consumer must not assume either role here.
  unframed = 0,
  // The token is a display line's length byte.  It is never display text and
  // never opens a control, whatever word the dictionary spells for it.
  line_length,
  // The token is content of a display line.
  line_content,
};

// Lossless token-level IR for one encoded BOO logical-record fragment.
// decoded_words retain the dictionary expansion, including an optional 0-3
// spacing prefix. byte_range always addresses the original BOO payload.
struct LogicalTokenIR {
  std::size_t token_index = 0;
  EncodedLogicalToken encoded;
  TokenWords decoded_words;
  SourceByteRange byte_range;
  bool has_spacing_control = false;
  std::uint16_t spacing_control = 3;
  // Word ordinals which the code-page decoder could not map.  Keep this as
  // typed decoder provenance so semantic consumers never need to infer an
  // artifact from its rendered replacement character.
  std::vector<std::size_t> unmapped_word_indices;
  // Display-line framing role, decided once by the record decoder.
  TokenFramingRole framing = TokenFramingRole::unframed;
};

// One display line of a record payload: the length byte's token and the
// exclusive end of the line's tokens.
struct DisplayLineIR {
  std::size_t prefix_token = 0;  // the length byte
  std::size_t token_end = 0;     // exclusive end of the line's tokens
};

struct LogicalRecordIR {
  std::uint32_t logical_record = 0;
  SourceByteRange payload_range;
  std::vector<LogicalTokenIR> tokens;
  // The record's display-line framing, computed once at decode time by
  // `assign_display_line_framing` (display_lines.hpp).  Empty and
  // `display_lines_parse == false` when the payload does not tile into whole
  // display lines.  Never re-derive this: read it.
  std::vector<DisplayLineIR> display_lines;
  bool display_lines_parse = false;
};

std::vector<TokenWords> project_token_words(const LogicalRecordIR& record);
std::vector<EncodedLogicalToken>
project_encoded_tokens(const LogicalRecordIR& record);

// Checks exact, ordered payload coverage and consistency between encoded
// widths, source byte ranges, token ordinals, and spacing metadata.
bool verify_token_ir(const LogicalRecordIR& record,
                     std::string* error = nullptr);

// Readable one-line dump of one token: ordinal, encoded value/width, spacing
// prefix, payload byte range and the decoded words.
std::string format_logical_token_ir(const LogicalTokenIR& token);

} // namespace geist::detail
