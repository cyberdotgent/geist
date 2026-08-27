#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace geist::detail {

using TokenWords = std::vector<std::uint16_t>;

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
};

struct LogicalRecordIR {
  std::uint32_t logical_record = 0;
  SourceByteRange payload_range;
  std::vector<LogicalTokenIR> tokens;
};

std::vector<TokenWords> project_token_words(const LogicalRecordIR& record);
std::vector<EncodedLogicalToken>
project_encoded_tokens(const LogicalRecordIR& record);

// Checks exact, ordered payload coverage and consistency between encoded
// widths, source byte ranges, token ordinals, and spacing metadata.
bool verify_token_ir(const LogicalRecordIR& record,
                     std::string* error = nullptr);

} // namespace geist::detail
