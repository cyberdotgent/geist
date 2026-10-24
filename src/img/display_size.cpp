// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The size a stored image is meant to be displayed at, read from its header.
//
// The rendered PNG never resamples what the book stores: a 240 dpi fax bitmap
// becomes a PNG with every one of its pixels, and the display size below is
// how a consumer scales it back to the size hosted BookServer presents, so a
// high-density screen still gets the stored detail.

#include "img/image.hpp"

#include "geist/detail/core/internal.hpp"

#include <algorithm>
#include <cstring>

namespace geist::detail {
namespace {

std::uint32_t be16_at(const std::uint8_t* bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 8) | bytes[offset + 1];
}

std::uint32_t le16_at(const std::uint8_t* bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) | bytes[offset];
}

std::uint32_t be32_at(const std::uint8_t* bytes, std::size_t offset) {
  return (be16_at(bytes, offset) << 16) | be16_at(bytes, offset + 2);
}

std::optional<ImageSize> sized(std::uint32_t width, std::uint32_t height) {
  if (!decoded_image_dimensions_are_safe(width, height))
    return std::nullopt;
  return ImageSize{width, height};
}

// The kind `I` payload's image input descriptor states the bitmap's pixel
// width and height at payload offsets 0x42 and 0x44.  Hosted BookServer's
// GIF of it is (pixels * 96) / 240 on each axis.
std::optional<ImageSize> mmr_display_size(const std::uint8_t* bytes,
                                          std::size_t size) {
  static constexpr std::uint8_t descriptor_id[] = {0xd3, 0xa6, 0x7b};
  if (size < 0x48 || std::memcmp(bytes + 0x2a, descriptor_id, 3) != 0)
    return std::nullopt;
  const auto width = be16_at(bytes, 0x42);
  const auto height = be16_at(bytes, 0x44);
  if (width == 0 || height == 0)
    return std::nullopt;
  const auto scale = [](std::uint32_t pixels) {
    return std::max<std::uint32_t>(
        1, pixels * mmr_display_pixels_per_inch / mmr_source_pixels_per_inch);
  };
  return sized(scale(width), scale(height));
}

std::optional<ImageSize> gif_size(const std::uint8_t* bytes,
                                  std::size_t size) {
  if (size < 10 || (std::memcmp(bytes, "GIF87a", 6) != 0 &&
                    std::memcmp(bytes, "GIF89a", 6) != 0))
    return std::nullopt;
  return sized(le16_at(bytes, 6), le16_at(bytes, 8));
}

std::optional<ImageSize> png_size(const std::uint8_t* bytes,
                                  std::size_t size) {
  static constexpr std::uint8_t signature[] = {0x89, 'P',  'N',  'G',
                                               0x0d, 0x0a, 0x1a, 0x0a};
  if (size < 24 || std::memcmp(bytes, signature, 8) != 0 ||
      std::memcmp(bytes + 12, "IHDR", 4) != 0)
    return std::nullopt;
  return sized(be32_at(bytes, 16), be32_at(bytes, 20));
}

} // namespace

std::optional<ImageSize> stored_image_display_size(
    const ResourceEntry& resource,
    const std::uint8_t* stored_bytes,
    std::size_t stored_size) {
  if (stored_bytes == nullptr)
    return std::nullopt;
  const auto format = ascii_lower(resource.stored_format);
  if (format == "legacy-mmr")
    return mmr_display_size(stored_bytes, stored_size);
  if (format == "legacy-gdf")
    return ImageSize{gdf_canvas_width, gdf_canvas_height};
  if (const auto gif = gif_size(stored_bytes, stored_size))
    return gif;
  return png_size(stored_bytes, stored_size);
}

} // namespace geist::detail
