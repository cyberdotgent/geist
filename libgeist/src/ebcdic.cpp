#include "geist/detail/internal.hpp"

#include "cp/cp037.hpp"
#include "cp/cp500.hpp"

#include <stdexcept>

namespace geist::detail {

namespace {

const std::array<std::uint16_t, 256>& table_for(EbcdicCodePage code_page)
    noexcept {
  switch (code_page) {
  case EbcdicCodePage::cp037:
    return cp::cp037_to_unicode;
  case EbcdicCodePage::cp500:
    return cp::cp500_to_token_word;
  }
  return cp::cp037_to_unicode;
}

char unicode_to_ascii(std::uint16_t word, char replacement) noexcept {
  if (word >= 0x20 && word <= 0x7E) {
    return static_cast<char>(word);
  }
  if (word == 0x00A0) {
    return ' ';
  }
  if (word == 0x00A9) {
    return 'c';
  }
  return replacement;
}

} // namespace

EbcdicCodec::EbcdicCodec(EbcdicCodePage code_page) noexcept
    : table_(&table_for(code_page)) {}

const EbcdicCodec& EbcdicCodec::cp037() noexcept {
  static const EbcdicCodec codec(EbcdicCodePage::cp037);
  return codec;
}

const EbcdicCodec& EbcdicCodec::cp500() noexcept {
  static const EbcdicCodec codec(EbcdicCodePage::cp500);
  return codec;
}

std::uint16_t EbcdicCodec::decode_word(std::uint8_t byte) const noexcept {
  return (*table_)[byte];
}

char EbcdicCodec::decode_ascii_byte(std::uint8_t byte,
                                    char replacement) const noexcept {
  return unicode_to_ascii(decode_word(byte), replacement);
}

std::string EbcdicCodec::decode_ascii(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::size_t count,
    const char* range_error) const {
  if (offset + count > bytes.size()) {
    throw std::runtime_error(range_error);
  }

  std::string output;
  output.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    output.push_back(decode_ascii_byte(bytes[offset + i]));
  }
  return output;
}

std::uint16_t map_token_word_to_upper_ascii(std::uint16_t word) {
  if (word >= 'a' && word <= 'z') {
    return static_cast<std::uint16_t>(word - 32);
  }
  return word;
}

std::uint16_t map_token_word_to_lower_ascii(std::uint16_t word) {
  if (word >= 'A' && word <= 'Z') {
    return static_cast<std::uint16_t>(word + 32);
  }
  return word;
}

std::string token_words_to_ascii(const TokenWords& words) {
  std::string output;
  output.reserve(words.size());
  for (const auto word : words) {
    if (word >= 0x20 && word <= 0x7E) {
      output.push_back(static_cast<char>(word));
    } else if (word == 0x00A0) {
      output.push_back(' ');
    } else if (word >= 0x00A1 && word <= 0x00FF) {
      output.push_back(static_cast<char>(0xC0 | (word >> 6)));
      output.push_back(static_cast<char>(0x80 | (word & 0x3F)));
    } else {
      output.push_back('?');
    }
  }
  return output;
}

} // namespace geist::detail
