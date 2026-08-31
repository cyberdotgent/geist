#include "geist/detail/core/internal.hpp"

#include "img/mmr.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
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

  std::size_t bit_offset() const noexcept {
    return bit_offset_;
  }

  std::string peek_bits(std::size_t count) const {
    std::string bits;
    bits.reserve(count);
    auto bit_offset = bit_offset_;
    while (bits.size() < count && bit_offset < end_bit_) {
      bits.push_back(bit_at(bit_offset++) == 0 ? '0' : '1');
    }
    return bits;
  }

  bool try_read_bit(int& bit) {
    if (bit_offset_ >= end_bit_) {
      return false;
    }
    bit = bit_at(bit_offset_);
    ++bit_offset_;
    return true;
  }

  bool starts_with(const char* bits) const {
    return starts_with_at(bit_offset_, bits);
  }

private:
  bool starts_with_at(std::size_t bit_offset, const char* bits) const {
    for (const char* cursor = bits; *cursor != '\0'; ++cursor) {
      if (bit_offset >= end_bit_) {
        return false;
      }
      const int bit = bit_at(bit_offset++);
      if (bit != (*cursor == '1' ? 1 : 0)) {
        return false;
      }
    }
    return true;
  }

public:
  void skip_bits(std::size_t count) {
    bit_offset_ = std::min(bit_offset_ + count, end_bit_);
  }

  bool find_and_skip(const char* bits) {
    while (!empty()) {
      if (starts_with(bits)) {
        skip_bits(std::char_traits<char>::length(bits));
        return true;
      }
      skip_bits(1);
    }
    return false;
  }

private:
  int bit_at(std::size_t bit_offset) const {
    const auto byte = bytes_[bit_offset / 8];
    const auto shift = 7 - static_cast<int>(bit_offset % 8);
    return (byte >> shift) & 1;
  }

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
      throw std::runtime_error(
          "MMR bitstream contains an invalid 2D mode at bit " +
          std::to_string(reader.bit_offset() - bits.size()) + " near " +
          reader.peek_bits(24));
    }
  }
}

enum class LineKind {
  two_dimensional,
  one_dimensional
};

LineKind read_line_kind(BitReader& reader) {
  if (!reader.starts_with("000000000001")) {
    return LineKind::two_dimensional;
  }
  reader.skip_bits(12);
  int tag = 0;
  if (!reader.try_read_bit(tag)) {
    throw std::runtime_error("MMR bitstream ended inside a line tag");
  }
  return tag == 0 ? LineKind::two_dimensional : LineKind::one_dimensional;
}

std::uint16_t read_be16_unchecked(const std::vector<std::uint8_t>& bytes,
                                  std::size_t offset) {
  return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

std::vector<std::uint8_t> read_segments(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 0x50) {
    throw std::runtime_error("MMR asset is too small for the observed header");
  }

  std::vector<std::uint8_t> compressed;
  std::size_t offset = 0x48;
  while (offset + 8 <= bytes.size()) {
    const auto segment_size = read_be16_unchecked(bytes, offset);
    if (segment_size < 8 || offset + segment_size > bytes.size()) {
      break;
    }
    const auto data_begin = offset + 8;
    const auto data_end = offset + segment_size;
    compressed.insert(compressed.end(),
                      bytes.begin() + static_cast<std::ptrdiff_t>(data_begin),
                      bytes.begin() + static_cast<std::ptrdiff_t>(data_end));
    offset += segment_size;
  }

  if (compressed.empty()) {
    compressed.insert(compressed.end(), bytes.begin() + 0x50, bytes.end());
  }
  return compressed;
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

void write_runs(RgbaImage& image,
                std::uint32_t y,
                const std::vector<std::size_t>& runs) {
  std::size_t a0 = 0;
  bool black = false;
  for (const auto run : runs) {
    const auto a1 = std::min<std::size_t>(a0 + run, image.width);
    write_run(image, y, a0, a1, black);
    a0 = a1;
    black = !black;
    if (a0 >= image.width) {
      return;
    }
  }
}

void set_run(std::vector<std::size_t>& runs,
             std::size_t& a0,
             std::size_t& pending_run,
             std::size_t run,
             std::uint32_t width) {
  const auto clamped =
      a0 + run > width ? static_cast<std::size_t>(width) - a0 : run;
  runs.push_back(pending_run + clamped);
  a0 += clamped;
  pending_run = 0;
}

void append_reference_tail(std::vector<std::size_t>& runs) {
  runs.push_back(0);
}

std::vector<std::size_t> decode_1d_line(BitReader& reader,
                                        std::uint32_t width) {
  std::vector<std::size_t> current;
  std::size_t a0 = 0;
  bool black = false;
  while (a0 < width) {
    const auto run = decode_run_code(reader, black);
    const auto clamped_run = std::min<std::size_t>(run, width - a0);
    current.push_back(clamped_run);
    a0 += clamped_run;
    black = !black;
  }
  append_reference_tail(current);
  return current;
}

void sync_mh_eol(BitReader& reader, std::uint32_t y) {
  if (reader.starts_with("000000000001")) {
    reader.skip_bits(12);
    return;
  }
  if (reader.find_and_skip("000000000001")) {
    return;
  }
  throw std::runtime_error("MMR line " + std::to_string(y) +
                           ": MH bitstream ended before EOL");
}

RgbaImage scale_2_to_5(const RgbaImage& image) {
  const auto scaled_width = std::max<std::uint32_t>(1, (image.width * 2) / 5);
  const auto scaled_height = std::max<std::uint32_t>(1, (image.height * 2) / 5);

  RgbaImage scaled;
  scaled.width = scaled_width;
  scaled.height = scaled_height;
  scaled.rgba.assign(static_cast<std::size_t>(scaled_width) * scaled_height * 4,
                     255);

  for (std::uint32_t y = 0; y < scaled_height; ++y) {
    const auto source_y =
        std::min<std::uint32_t>(image.height - 1, (y * 5) / 2);
    for (std::uint32_t x = 0; x < scaled_width; ++x) {
      const auto source_x =
          std::min<std::uint32_t>(image.width - 1, (x * 5) / 2);
      const auto source =
          (static_cast<std::size_t>(source_y) * image.width + source_x) * 4;
      const auto target =
          (static_cast<std::size_t>(y) * scaled_width + x) * 4;
      std::copy_n(image.rgba.begin() + static_cast<std::ptrdiff_t>(source),
                  4,
                  scaled.rgba.begin() + static_cast<std::ptrdiff_t>(target));
    }
  }

  return scaled;
}

RgbaImage decode_mh_image(const std::vector<std::uint8_t>& compressed,
                          std::uint32_t width,
                          std::uint32_t height) {
  RgbaImage image;
  image.width = width;
  image.height = height;
  image.rgba.assign(static_cast<std::size_t>(width) * height * 4, 255);

  BitReader reader(compressed, 0, compressed.size());
  for (std::uint32_t y = 0; y < height; ++y) {
    sync_mh_eol(reader, y);
    try {
      write_runs(image, y, decode_1d_line(reader, width));
    } catch (const std::runtime_error& error) {
      throw std::runtime_error("MMR line " + std::to_string(y) + ": " +
                               error.what());
    }
  }

  return image;
}

RgbaImage decode_t6_image(const std::vector<std::uint8_t>& compressed,
                          std::uint32_t width,
                          std::uint32_t height) {
  RgbaImage image;
  image.width = width;
  image.height = height;
  image.rgba.assign(static_cast<std::size_t>(width) * height * 4, 255);

  BitReader reader(compressed, 0, compressed.size());
  std::vector<std::size_t> reference{static_cast<std::size_t>(width), 0};
  for (std::uint32_t y = 0; y < height; ++y) {
    LineKind line_kind = LineKind::two_dimensional;
    try {
      line_kind = read_line_kind(reader);
    } catch (const std::runtime_error& error) {
      throw std::runtime_error("MMR line " + std::to_string(y) + ": " +
                               error.what());
    }
    if (line_kind == LineKind::one_dimensional) {
      try {
        reference = decode_1d_line(reader, width);
        write_runs(image, y, reference);
      } catch (const std::runtime_error& error) {
        throw std::runtime_error("MMR line " + std::to_string(y) + ": " +
                                 error.what());
      }
      continue;
    }

    std::vector<std::size_t> current;
    std::size_t a0 = 0;
    std::size_t pending_run = 0;
    std::size_t b1 = reference.empty() ? width : reference[0];
    std::size_t pb = 1;

    const auto check_b1 = [&]() {
      if (current.empty()) {
        return;
      }
      while (b1 <= a0 && b1 < width) {
        const auto first = pb < reference.size() ? reference[pb++] : 0;
        const auto second = pb < reference.size() ? reference[pb++] : 0;
        b1 += first + second;
      }
    };

    try {
      while (a0 < width && !reader.empty()) {
        ModeCode mode;
        mode = decode_mode(reader);
        if (mode.mode == Mode::pass) {
          check_b1();
          const auto span1 = pb < reference.size() ? reference[pb++] : 0;
          b1 += span1;
          pending_run += b1 - a0;
          a0 = b1;
          const auto span2 = pb < reference.size() ? reference[pb++] : 0;
          b1 += span2;
          continue;
        }

        if (mode.mode == Mode::horizontal) {
          const bool black_first = (current.size() % 2) != 0;
          const auto run1 = decode_run_code(reader, black_first);
          set_run(current, a0, pending_run, run1, width);

          const auto run2 = decode_run_code(reader, !black_first);
          set_run(current, a0, pending_run, run2, width);
          check_b1();
          continue;
        }

        check_b1();
        if (mode.vertical_delta < 0) {
          const auto delta = static_cast<std::size_t>(-mode.vertical_delta);
          if (b1 < a0 + delta) {
            throw std::runtime_error(
                "MMR bitstream contains an invalid left vertical mode");
          }
          set_run(current, a0, pending_run, b1 - a0 - delta, width);
          if (pb > 0) {
            --pb;
            b1 -= reference[pb];
          }
          continue;
        }

        set_run(current,
                a0,
                pending_run,
                b1 - a0 + static_cast<std::size_t>(mode.vertical_delta),
                width);
        if (pb < reference.size()) {
          b1 += reference[pb++];
        }
      }
    } catch (const std::runtime_error& error) {
      throw std::runtime_error("MMR line " + std::to_string(y) + ": " +
                               error.what());
    }

    if (pending_run != 0) {
      if (pending_run + a0 < width) {
        int final_v0 = 0;
        if (!reader.try_read_bit(final_v0) || final_v0 != 1) {
          throw std::runtime_error(
              "MMR bitstream is missing the final V0 mode");
        }
      }
      set_run(current, a0, pending_run, 0, width);
    }
    append_reference_tail(current);
    write_runs(image, y, current);
    reference = std::move(current);
  }

  return image;
}

} // namespace

RgbaImage decode_mmr_to_rgba(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 0x50) {
    throw std::runtime_error("MMR asset is too small for the observed header");
  }

  const auto width = read_be16_unchecked(bytes, 0x42);
  const auto height = read_be16_unchecked(bytes, 0x44);
  if (width == 0 || height == 0 || width > 8192 || height > 8192) {
    throw std::runtime_error("MMR asset has unsupported dimensions");
  }
  const auto compressed = read_segments(bytes);
  try {
    return scale_2_to_5(decode_t6_image(compressed, width, height));
  } catch (const std::runtime_error& t6_error) {
    if (compressed.size() >= 2 && compressed[0] == 0x00 &&
        (compressed[1] & 0xf0) == 0x10) {
      try {
        return scale_2_to_5(decode_mh_image(compressed, width, height));
      } catch (const std::runtime_error&) {
        throw std::runtime_error(t6_error.what());
      }
    }
    throw;
  }
}

} // namespace geist::detail
