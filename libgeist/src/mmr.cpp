#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace geist::detail {

namespace {

struct Code {
  const char* bits;
  int run;
  bool terminating;
};

class BitReader {
public:
  BitReader(const std::vector<std::uint8_t>& bytes,
            std::size_t offset,
            std::size_t size)
      : bytes_(bytes), bit_offset_(offset * 8), end_bit_((offset + size) * 8) {}

  bool empty() const noexcept {
    return bit_offset_ >= end_bit_;
  }

  bool try_read_bit(int& bit) {
    if (bit_offset_ >= end_bit_) {
      return false;
    }
    const auto byte = bytes_[bit_offset_ / 8];
    const auto shift = 7 - static_cast<int>(bit_offset_ % 8);
    bit = (byte >> shift) & 1;
    ++bit_offset_;
    return true;
  }

private:
  const std::vector<std::uint8_t>& bytes_;
  std::size_t bit_offset_ = 0;
  std::size_t end_bit_ = 0;
};

const Code white_codes[] = {
    {"00110101", 0, true},      {"000111", 1, true},
    {"0111", 2, true},          {"1000", 3, true},
    {"1011", 4, true},          {"1100", 5, true},
    {"1110", 6, true},          {"1111", 7, true},
    {"10011", 8, true},         {"10100", 9, true},
    {"00111", 10, true},        {"01000", 11, true},
    {"001000", 12, true},       {"000011", 13, true},
    {"110100", 14, true},       {"110101", 15, true},
    {"101010", 16, true},       {"101011", 17, true},
    {"0100111", 18, true},      {"0001100", 19, true},
    {"0001000", 20, true},      {"0010111", 21, true},
    {"0000011", 22, true},      {"0000100", 23, true},
    {"0101000", 24, true},      {"0101011", 25, true},
    {"0010011", 26, true},      {"0100100", 27, true},
    {"0011000", 28, true},      {"00000010", 29, true},
    {"00000011", 30, true},     {"00011010", 31, true},
    {"00011011", 32, true},     {"00010010", 33, true},
    {"00010011", 34, true},     {"00010100", 35, true},
    {"00010101", 36, true},     {"00010110", 37, true},
    {"00010111", 38, true},     {"00101000", 39, true},
    {"00101001", 40, true},     {"00101010", 41, true},
    {"00101011", 42, true},     {"00101100", 43, true},
    {"00101101", 44, true},     {"00000100", 45, true},
    {"00000101", 46, true},     {"00001010", 47, true},
    {"00001011", 48, true},     {"01010010", 49, true},
    {"01010011", 50, true},     {"01010100", 51, true},
    {"01010101", 52, true},     {"00100100", 53, true},
    {"00100101", 54, true},     {"01011000", 55, true},
    {"01011001", 56, true},     {"01011010", 57, true},
    {"01011011", 58, true},     {"01001010", 59, true},
    {"01001011", 60, true},     {"00110010", 61, true},
    {"00110011", 62, true},     {"00110100", 63, true},
    {"11011", 64, false},       {"10010", 128, false},
    {"010111", 192, false},     {"0110111", 256, false},
    {"00110110", 320, false},   {"00110111", 384, false},
    {"01100100", 448, false},   {"01100101", 512, false},
    {"01101000", 576, false},   {"01100111", 640, false},
    {"011001100", 704, false},  {"011001101", 768, false},
    {"011010010", 832, false},  {"011010011", 896, false},
    {"011010100", 960, false},  {"011010101", 1024, false},
    {"011010110", 1088, false}, {"011010111", 1152, false},
    {"011011000", 1216, false}, {"011011001", 1280, false},
    {"011011010", 1344, false}, {"011011011", 1408, false},
    {"010011000", 1472, false}, {"010011001", 1536, false},
    {"010011010", 1600, false}, {"011000", 1664, false},
    {"010011011", 1728, false},
};

const Code black_codes[] = {
    {"0000110111", 0, true},      {"010", 1, true},
    {"11", 2, true},              {"10", 3, true},
    {"011", 4, true},             {"0011", 5, true},
    {"0010", 6, true},            {"00011", 7, true},
    {"000101", 8, true},          {"000100", 9, true},
    {"0000100", 10, true},        {"0000101", 11, true},
    {"0000111", 12, true},        {"00000100", 13, true},
    {"00000111", 14, true},       {"000011000", 15, true},
    {"0000010111", 16, true},     {"0000011000", 17, true},
    {"0000001000", 18, true},     {"00001100111", 19, true},
    {"00001101000", 20, true},    {"00001101100", 21, true},
    {"00000110111", 22, true},    {"00000101000", 23, true},
    {"00000010111", 24, true},    {"00000011000", 25, true},
    {"000011001010", 26, true},   {"000011001011", 27, true},
    {"000011001100", 28, true},   {"000011001101", 29, true},
    {"000001101000", 30, true},   {"000001101001", 31, true},
    {"000001101010", 32, true},   {"000001101011", 33, true},
    {"000011010010", 34, true},   {"000011010011", 35, true},
    {"000011010100", 36, true},   {"000011010101", 37, true},
    {"000011010110", 38, true},   {"000011010111", 39, true},
    {"000001101100", 40, true},   {"000001101101", 41, true},
    {"000011011010", 42, true},   {"000011011011", 43, true},
    {"000001010100", 44, true},   {"000001010101", 45, true},
    {"000001010110", 46, true},   {"000001010111", 47, true},
    {"000001100100", 48, true},   {"000001100101", 49, true},
    {"000001010010", 50, true},   {"000001010011", 51, true},
    {"000000100100", 52, true},   {"000000110111", 53, true},
    {"000000111000", 54, true},   {"000000100111", 55, true},
    {"000000101000", 56, true},   {"000001011000", 57, true},
    {"000001011001", 58, true},   {"000000101011", 59, true},
    {"000000101100", 60, true},   {"000001011010", 61, true},
    {"000001100110", 62, true},   {"000001100111", 63, true},
    {"0000001111", 64, false},    {"000011001000", 128, false},
    {"000011001001", 192, false}, {"000001011011", 256, false},
    {"000000110011", 320, false}, {"000000110100", 384, false},
    {"000000110101", 448, false}, {"0000001101100", 512, false},
    {"0000001101101", 576, false},
    {"0000001001010", 640, false},
    {"0000001001011", 704, false},
    {"0000001001100", 768, false},
    {"0000001001101", 832, false},
    {"0000001110010", 896, false},
    {"0000001110011", 960, false},
    {"0000001110100", 1024, false},
    {"0000001110101", 1088, false},
    {"0000001110110", 1152, false},
    {"0000001110111", 1216, false},
    {"0000001010010", 1280, false},
    {"0000001010011", 1344, false},
    {"0000001010100", 1408, false},
    {"0000001010101", 1472, false},
    {"0000001011010", 1536, false},
    {"0000001011011", 1600, false},
    {"0000001100100", 1664, false},
    {"0000001100101", 1728, false},
};

const Code long_makeup_codes[] = {
    {"00000001000", 1792, false},  {"00000001100", 1856, false},
    {"00000001101", 1920, false},  {"000000010010", 1984, false},
    {"000000010011", 2048, false}, {"000000010100", 2112, false},
    {"000000010101", 2176, false}, {"000000010110", 2240, false},
    {"000000010111", 2304, false}, {"000000011100", 2368, false},
    {"000000011101", 2432, false}, {"000000011110", 2496, false},
    {"000000011111", 2560, false},
};

bool matches_prefix(const char* code, const std::string& prefix) {
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (code[i] == '\0' || code[i] != prefix[i]) {
      return false;
    }
  }
  return true;
}

const Code* find_exact_code(const Code* codes,
                            std::size_t count,
                            const std::string& bits) {
  for (std::size_t i = 0; i < count; ++i) {
    if (bits == codes[i].bits) {
      return &codes[i];
    }
  }
  return nullptr;
}

bool has_prefix_match(const Code* codes,
                      std::size_t count,
                      const std::string& bits) {
  for (std::size_t i = 0; i < count; ++i) {
    if (matches_prefix(codes[i].bits, bits)) {
      return true;
    }
  }
  return false;
}

int decode_run_code(BitReader& reader, bool black) {
  const auto* codes = black ? black_codes : white_codes;
  const auto code_count =
      black ? (sizeof(black_codes) / sizeof(black_codes[0]))
            : (sizeof(white_codes) / sizeof(white_codes[0]));

  int run = 0;
  for (;;) {
    std::string bits;
    for (;;) {
      int bit = 0;
      if (!reader.try_read_bit(bit)) {
        throw std::runtime_error("MMR bitstream ended inside a run code");
      }
      bits.push_back(bit == 0 ? '0' : '1');

      if (const auto* code = find_exact_code(codes, code_count, bits)) {
        run += code->run;
        if (code->terminating) {
          return run;
        }
        break;
      }
      if (const auto* code = find_exact_code(
              long_makeup_codes,
              sizeof(long_makeup_codes) / sizeof(long_makeup_codes[0]),
              bits)) {
        run += code->run;
        break;
      }
      if (bits.size() > 13 ||
          (!has_prefix_match(codes, code_count, bits) &&
           !has_prefix_match(long_makeup_codes,
                             sizeof(long_makeup_codes) /
                                 sizeof(long_makeup_codes[0]),
                             bits))) {
        throw std::runtime_error("MMR bitstream contains an invalid run code");
      }
    }
  }
}

enum class Mode {
  pass,
  horizontal,
  vertical
};

struct ModeCode {
  Mode mode;
  int vertical_delta = 0;
};

ModeCode decode_mode(BitReader& reader) {
  std::string bits;
  for (;;) {
    int bit = 0;
    if (!reader.try_read_bit(bit)) {
      throw std::runtime_error("MMR bitstream ended inside a 2D mode code");
    }
    bits.push_back(bit == 0 ? '0' : '1');

    if (bits == "1") {
      return {Mode::vertical, 0};
    }
    if (bits == "011") {
      return {Mode::vertical, 1};
    }
    if (bits == "010") {
      return {Mode::vertical, -1};
    }
    if (bits == "000011") {
      return {Mode::vertical, 2};
    }
    if (bits == "000010") {
      return {Mode::vertical, -2};
    }
    if (bits == "0000011") {
      return {Mode::vertical, 3};
    }
    if (bits == "0000010") {
      return {Mode::vertical, -3};
    }
    if (bits == "001") {
      return {Mode::horizontal, 0};
    }
    if (bits == "0001") {
      return {Mode::pass, 0};
    }
    if (bits.size() > 7) {
      throw std::runtime_error("MMR bitstream contains an invalid 2D mode");
    }
  }
}

std::uint16_t read_be16_unchecked(const std::vector<std::uint8_t>& bytes,
                                  std::size_t offset) {
  return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

std::size_t find_b1(const std::vector<std::size_t>& reference,
                    std::size_t a0,
                    bool black) {
  for (std::size_t i = 0; i < reference.size(); ++i) {
    if (reference[i] <= a0) {
      continue;
    }
    const bool color_after_change_is_black = (i % 2) == 0;
    if (color_after_change_is_black != black) {
      return i;
    }
  }
  return reference.size() - 1;
}

void write_run(RgbaImage& image,
               std::uint32_t y,
               std::size_t begin,
               std::size_t end,
               bool black) {
  if (!black) {
    return;
  }
  begin = std::min<std::size_t>(begin, image.width);
  end = std::min<std::size_t>(end, image.width);
  for (std::size_t x = begin; x < end; ++x) {
    const auto offset =
        (static_cast<std::size_t>(y) * image.width + x) * 4;
    image.rgba[offset] = 0;
    image.rgba[offset + 1] = 0;
    image.rgba[offset + 2] = 0;
    image.rgba[offset + 3] = 255;
  }
}

} // namespace

RgbaImage decode_mmr_to_rgba_experimental(
    const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 0x50) {
    throw std::runtime_error("MMR asset is too small for the observed header");
  }

  const auto width = read_be16_unchecked(bytes, 0x32);
  const auto height = read_be16_unchecked(bytes, 0x34);
  auto compressed_size = static_cast<std::size_t>(read_be16_unchecked(bytes, 0x38));
  if (width == 0 || height == 0 || width > 8192 || height > 8192) {
    throw std::runtime_error("MMR asset has unsupported dimensions");
  }
  // ephimage.dll process_mmr_pict starts the first compressed segment at
  // relative 0x50. The big-endian word at 0x48 is the segment record length,
  // including the 8-byte segment header.
  constexpr std::size_t compressed_offset = 0x50;
  if (bytes.size() >= 0x4a) {
    const auto segment_size = read_be16_unchecked(bytes, 0x48);
    if (segment_size >= 8) {
      compressed_size = static_cast<std::size_t>(segment_size - 8);
    }
  }
  if (compressed_size == 0 ||
      compressed_offset + compressed_size > bytes.size()) {
    compressed_size = bytes.size() - compressed_offset;
  }

  RgbaImage image;
  image.width = width;
  image.height = height;
  image.rgba.assign(static_cast<std::size_t>(width) * height * 4, 255);

  BitReader reader(bytes, compressed_offset, compressed_size);
  std::vector<std::size_t> reference{static_cast<std::size_t>(width)};
  for (std::uint32_t y = 0; y < height; ++y) {
    std::vector<std::size_t> current;
    std::size_t a0 = 0;
    bool black = false;

    while (a0 < width && !reader.empty()) {
      const auto mode = decode_mode(reader);
      if (mode.mode == Mode::pass) {
        const auto b1_index = find_b1(reference, a0, black);
        const auto b2 = reference[std::min(b1_index + 1, reference.size() - 1)];
        write_run(image, y, a0, b2, black);
        a0 = b2;
        continue;
      }

      if (mode.mode == Mode::horizontal) {
        const auto run1 = decode_run_code(reader, black);
        const auto a1 = std::min<std::size_t>(a0 + run1, width);
        write_run(image, y, a0, a1, black);
        if (a1 < width) {
          current.push_back(a1);
        }
        black = !black;

        const auto run2 = decode_run_code(reader, black);
        const auto a2 = std::min<std::size_t>(a1 + run2, width);
        write_run(image, y, a1, a2, black);
        if (a2 < width) {
          current.push_back(a2);
        }
        a0 = a2;
        black = !black;
        continue;
      }

      const auto b1_index = find_b1(reference, a0, black);
      const auto b1 = reference[b1_index];
      int a1_signed = static_cast<int>(b1) + mode.vertical_delta;
      a1_signed = std::max(0, std::min<int>(a1_signed, width));
      const auto a1 = static_cast<std::size_t>(a1_signed);
      write_run(image, y, a0, a1, black);
      if (a1 < width) {
        current.push_back(a1);
      }
      a0 = a1;
      black = !black;
    }

    current.push_back(width);
    reference = std::move(current);
  }

  return image;
}

RgbaImage decode_mmr_to_rgba(const std::vector<std::uint8_t>& bytes) {
  (void)bytes;
  throw std::runtime_error(
      "asset cannot be rendered to PNG yet: legacy BookManager MMR payload "
      "decoding is not implemented");
}

} // namespace geist::detail
