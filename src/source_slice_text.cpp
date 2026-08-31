#include "geist/detail/source_slice_text.hpp"

#include "geist/detail/internal.hpp"

#include <cctype>

namespace geist::detail {
namespace {

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

} // namespace

std::optional<std::string> decode_source_slice_text(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    const std::map<std::uint16_t, TokenWords>& token_strings,
    const DocumentSourceSliceIR& slice,
    std::string* error) {
  if (error != nullptr) error->clear();
  if (slice.byte_begin > slice.byte_end) {
    fail(error, "source slice has a reversed byte range");
    return std::nullopt;
  }
  const auto tokens = decode_source_byte_range_tokens(
      bytes, directory, token_strings, slice.byte_begin, slice.byte_end);
  if (!tokens) {
    fail(error, "source slice bytes do not tile into whole tokens");
    return std::nullopt;
  }
  // A whole-token slice states its own token count; a disagreement means the
  // slice's byte range and token range describe different runs.
  const auto expected = slice.token_end - slice.token_begin;
  if (expected != 0 && tokens->size() != expected) {
    fail(error, "source slice byte range holds a different token count");
    return std::nullopt;
  }
  std::string text;
  for (const auto& token : *tokens) {
    // The leading spacing-control word is layout, not a display character.
    auto word = static_cast<std::size_t>(token.has_spacing_control);
    TokenWords body(token.decoded_words.begin() +
                        static_cast<std::ptrdiff_t>(word),
                    token.decoded_words.end());
    text += token_words_to_ascii(body);
  }
  if (!slice_is_partial(slice)) return text;
  if (slice.character_begin >= slice.character_end ||
      slice.character_end > text.size()) {
    fail(error, "sub-token character range is outside the decoded word");
    return std::nullopt;
  }
  return text.substr(slice.character_begin,
                     slice.character_end - slice.character_begin);
}


std::optional<std::string> project_source_slice_text(
    const LogicalRecordIR& record, const DocumentSourceSliceIR& slice,
    std::string* error) {
  if (error != nullptr) error->clear();
  if (slice.byte_begin > slice.byte_end) {
    fail(error, "source slice has a reversed byte range");
    return std::nullopt;
  }
  std::string text;
  std::uint32_t cursor = slice.byte_begin;
  std::size_t tokens = 0;
  for (const auto& token : record.tokens) {
    if (token.byte_range.begin < slice.byte_begin ||
        token.byte_range.end > slice.byte_end)
      continue;
    // The named window has to tile into whole tokens of this record: a slice
    // whose bounds fall inside a token names bytes no element ever owned.
    if (token.byte_range.begin != cursor) {
      fail(error, "source slice byte range does not tile into whole tokens");
      return std::nullopt;
    }
    cursor = token.byte_range.end;
    ++tokens;
    auto word = static_cast<std::size_t>(token.has_spacing_control);
    TokenWords body(token.decoded_words.begin() +
                        static_cast<std::ptrdiff_t>(word),
                    token.decoded_words.end());
    text += token_words_to_ascii(body);
  }
  if (cursor != slice.byte_end) {
    fail(error, "source slice byte range does not tile into whole tokens");
    return std::nullopt;
  }
  const auto expected = slice.token_end - slice.token_begin;
  if (expected != 0 && tokens != expected) {
    fail(error, "source slice byte range holds a different token count");
    return std::nullopt;
  }
  if (!slice_is_partial(slice)) return text;
  if (slice.character_begin >= slice.character_end ||
      slice.character_end > text.size()) {
    fail(error, "sub-token character range is outside the decoded word");
    return std::nullopt;
  }
  return text.substr(slice.character_begin,
                     slice.character_end - slice.character_begin);
}


} // namespace geist::detail
