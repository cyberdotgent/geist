#include "geist/detail/book_ir.hpp"

#include <limits>

namespace geist::detail {

std::vector<TokenWords> project_token_words(const LogicalRecordIR& record) {
  std::vector<TokenWords> result;
  result.reserve(record.tokens.size());
  for (const auto& token : record.tokens) {
    result.push_back(token.decoded_words);
  }
  return result;
}

std::vector<EncodedLogicalToken>
project_encoded_tokens(const LogicalRecordIR& record) {
  std::vector<EncodedLogicalToken> result;
  result.reserve(record.tokens.size());
  for (const auto& token : record.tokens) {
    result.push_back(token.encoded);
  }
  return result;
}

bool verify_token_ir(const LogicalRecordIR& record, std::string* error) {
  const auto fail = [&](const char* message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (record.payload_range.begin > record.payload_range.end) {
    return fail("logical-record payload range is reversed");
  }
  auto expected = record.payload_range.begin;
  for (std::size_t index = 0; index < record.tokens.size(); ++index) {
    const auto& token = record.tokens[index];
    if (token.token_index != index) {
      return fail("logical token ordinal is not contiguous");
    }
    if (token.encoded.width != 1 && token.encoded.width != 2) {
      return fail("logical token encoded width is invalid");
    }
    if (token.byte_range.begin != expected ||
        token.byte_range.end < token.byte_range.begin ||
        token.byte_range.end - token.byte_range.begin != token.encoded.width) {
      return fail("logical token byte range does not exactly cover payload");
    }
    if (token.byte_range.end > record.payload_range.end) {
      return fail("logical token byte range exceeds payload");
    }
    if (token.encoded.width == 1 &&
        token.encoded.value > std::numeric_limits<std::uint8_t>::max()) {
      return fail("one-byte logical token has a wide encoded value");
    }
    const auto has_control = !token.decoded_words.empty() &&
                             token.decoded_words.front() < 4;
    const auto spacing = has_control ? token.decoded_words.front()
                                     : std::uint16_t{3};
    if (token.has_spacing_control != has_control ||
        token.spacing_control != spacing) {
      return fail("logical token spacing metadata differs from decoded words");
    }
    expected = token.byte_range.end;
  }
  if (expected != record.payload_range.end) {
    return fail("logical token IR leaves an unconsumed payload suffix");
  }
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

} // namespace geist::detail
