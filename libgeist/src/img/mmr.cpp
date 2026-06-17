#include "geist/detail/internal.hpp"

#include "img/mmr.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace geist::detail {

namespace {

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

int decode_run_code(BitReader& reader, bool black) {
  const auto* codes = black ? black_codes : white_codes;
  const auto code_count = black ? black_code_count : white_code_count;

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
              long_makeup_code_count,
              bits)) {
        run += code->run;
        break;
      }
      if (bits.size() > 13 ||
          (!has_prefix_match(codes, code_count, bits) &&
           !has_prefix_match(long_makeup_codes,
                             long_makeup_code_count,
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
