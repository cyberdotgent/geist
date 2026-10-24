// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A legacy kind `I` (MMR) picture keeps every pixel the book stores, and
// carries the size it is meant to be shown at separately.
//
// The book scans these pictures at 240 pixels per inch.  Hosted BookServer
// serves them at 96 per inch by keeping two of every five rows and columns,
// which drops one-pixel strokes (box edges, arrows, diagonals) outright.  The
// rendered PNG therefore keeps the stored resolution, and the display size
// -- (pixels * 96) / 240 on each axis, hosted BookServer's own GIF size -- is
// stated alongside it for the consumer's graphics stack to scale with.

#include "geist/detail/core/internal.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

int failures = 0;

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "mmr_display_size_synthetic: " << message << '\n';
    ++failures;
  }
}

void be16(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
  out.push_back(static_cast<std::uint8_t>(value & 0xff));
}

class BitWriter {
public:
  void bits(const std::string& value) {
    for (const auto bit : value) {
      current_ = static_cast<std::uint8_t>((current_ << 1) | (bit == '1'));
      if (++count_ == 8) {
        bytes_.push_back(current_);
        current_ = 0;
        count_ = 0;
      }
    }
  }
  std::vector<std::uint8_t> finish() {
    while (count_ != 0)
      bits("0");
    return bytes_;
  }

private:
  std::vector<std::uint8_t> bytes_;
  std::uint8_t current_ = 0;
  int count_ = 0;
};

// The kind `I` structured-field chain (doc/boo-spec/mmr.adoc): begin image
// object, image output control, image input descriptor with the bitmap
// dimensions at 0x42/0x44, one picture-data field, end image object.
std::vector<std::uint8_t> mmr_payload(std::uint32_t width,
                                      std::uint32_t height,
                                      const std::vector<std::uint8_t>& fax) {
  std::vector<std::uint8_t> out{0x00, 0x08, 0xd3, 0xa8, 0x7b, 0x00, 0x00,
                                0x00, 0x00, 0x20, 0xd3, 0xa7, 0x7b, 0x00,
                                0x00, 0x00};
  out.resize(0x18, 0x00);
  out.push_back(0x2d);
  out.resize(0x22, 0x00);
  out.insert(out.end(), {0x03, 0xe8, 0x03, 0xe8, 0xff, 0xff});
  out.insert(out.end(), {0x00, 0x20, 0xd3, 0xa6, 0x7b, 0x00, 0x00, 0x00,
                         0x01, 0x01, 0x00, 0x64, 0x00, 0x64});
  out.resize(0x3e, 0x00);
  out.insert(out.end(), {0x09, 0x60, 0x09, 0x60});
  be16(out, width);
  be16(out, height);
  out.insert(out.end(), {0x01, 0x00});
  be16(out, static_cast<std::uint32_t>(fax.size() + 8));
  out.insert(out.end(), {0xd3, 0xee, 0x7b, 0x40, 0x00, 0x00});
  out.insert(out.end(), fax.begin(), fax.end());
  out.insert(out.end(), {0x00, 0x08, 0xd3, 0xa9, 0x7b, 0x00, 0x00, 0x00});
  return out;
}

geist::ResourceEntry mmr_resource() {
  geist::ResourceEntry resource;
  resource.id = "5";
  resource.kind = "I";
  resource.stored_format = "legacy-mmr";
  return resource;
}

std::optional<geist::detail::ImageSize> display_size(
    const geist::ResourceEntry& resource,
    const std::vector<std::uint8_t>& bytes) {
  return geist::detail::stored_image_display_size(resource, bytes.data(),
                                                  bytes.size());
}

bool is_black(const geist::detail::RgbaImage& image, std::uint32_t x,
              std::uint32_t y) {
  const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
  return image.rgba[offset] == 0 && image.rgba[offset + 1] == 0 &&
         image.rgba[offset + 2] == 0;
}

std::uint32_t png_be32(const std::vector<std::uint8_t>& png,
                       std::size_t offset) {
  return (static_cast<std::uint32_t>(png[offset]) << 24) |
         (static_cast<std::uint32_t>(png[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(png[offset + 2]) << 8) | png[offset + 3];
}

// A 10 x 5 bitmap with a one-pixel black column at x = 1, every line coded
// as a tagged one-dimensional T.4 line: EOL, tag 1, white 1, black 1,
// white 8.  Sampling two of every five columns reads columns 0 and 2 only,
// so the stroke is exactly the detail a resampled render loses.
void check_full_resolution_decode() {
  BitWriter fax;
  for (int line = 0; line < 5; ++line) {
    fax.bits("000000000001");
    fax.bits("1");
    fax.bits("000111");  // white 1
    fax.bits("010");     // black 1
    fax.bits("10011");   // white 8
  }
  const auto payload = mmr_payload(10, 5, fax.finish());

  const auto image = geist::detail::decode_mmr_to_rgba(payload);
  require(image.width == 10 && image.height == 5,
          "the MMR decode was resampled: " + std::to_string(image.width) +
              "x" + std::to_string(image.height));
  if (image.width == 10 && image.height == 5) {
    for (std::uint32_t y = 0; y < 5; ++y)
      for (std::uint32_t x = 0; x < 10; ++x)
        require(is_black(image, x, y) == (x == 1),
                "pixel (" + std::to_string(x) + "," + std::to_string(y) +
                    ") lost the one-pixel stroke");
  }

  const auto png =
      geist::detail::render_resource_png(mmr_resource(), payload);
  require(png.size() > 24 && png_be32(png, 16) == 10 && png_be32(png, 20) == 5,
          "the rendered PNG is not the stored 10x5 bitmap");

  const auto size = display_size(mmr_resource(), payload);
  require(size && size->width == 4 && size->height == 2,
          "a 10x5 bitmap is not displayed at hosted's 4x2");
}

// Hosted BookServer's GIF dimensions, from the descriptor alone: QSYSNEWG
// resource 1 (205 x 413 -> 82 x 165) and SC41-3735-00 picture 5
// (1216 x 1150 -> 486 x 460).
void check_hosted_display_sizes() {
  const struct {
    std::uint32_t width, height, display_width, display_height;
  } cases[] = {{205, 413, 82, 165}, {1216, 1150, 486, 460},
               {960, 832, 384, 332}, {2, 2, 1, 1}};
  for (const auto& sample : cases) {
    const auto size =
        display_size(mmr_resource(), mmr_payload(sample.width, sample.height,
                                                 {0x00}));
    require(size && size->width == sample.display_width &&
                size->height == sample.display_height,
            "bitmap " + std::to_string(sample.width) + "x" +
                std::to_string(sample.height) +
                " is not displayed at its hosted size");
  }

  auto truncated = mmr_payload(205, 413, {0x00});
  truncated.resize(0x40);
  require(!display_size(mmr_resource(), truncated),
          "a payload cut before its dimensions was given a size");
  auto foreign = mmr_payload(205, 413, {0x00});
  foreign[0x2b] = 0x00;
  require(!display_size(mmr_resource(), foreign),
          "a payload without an image input descriptor was given a size");
}

// The other image formats are shown at their pixel size: a GDF picture is
// drawn onto hosted's fixed canvas, and a GIF or PNG states its own size.
void check_other_formats() {
  geist::ResourceEntry gdf;
  gdf.stored_format = "legacy-gdf";
  const std::vector<std::uint8_t> any{0x01};
  const auto gdf_size = display_size(gdf, any);
  require(gdf_size && gdf_size->width == geist::detail::gdf_canvas_width &&
              gdf_size->height == geist::detail::gdf_canvas_height,
          "a GDF picture is not displayed at its canvas size");

  geist::ResourceEntry web;
  web.stored_format = "image/gif";
  const std::vector<std::uint8_t> gif{'G', 'I', 'F', '8', '9', 'a',
                                      0x6c, 0x02, 0xe0, 0x01};
  const auto gif_size = display_size(web, gif);
  require(gif_size && gif_size->width == 620 && gif_size->height == 480,
          "a GIF is not displayed at its logical screen size");

  web.stored_format = "image/png";
  std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
                                0x00, 0x00, 0x00, 0x0d, 'I', 'H', 'D', 'R',
                                0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x12};
  const auto png_size = display_size(web, png);
  require(png_size && png_size->width == 17 && png_size->height == 18,
          "a PNG is not displayed at its IHDR size");

  web.stored_format = "object";
  require(!display_size(web, std::vector<std::uint8_t>(32, 0x00)),
          "a non-image object was given a display size");
}

} // namespace

int main() {
  check_full_resolution_decode();
  check_hosted_display_sizes();
  check_other_formats();
  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "MMR display size synthetic checks passed\n";
  return 0;
}
