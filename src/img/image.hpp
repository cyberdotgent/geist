// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/resource.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

constexpr std::size_t max_encoded_image_bytes = 64u * 1024u * 1024u;
constexpr std::uint64_t max_decoded_image_pixels = 16u * 1024u * 1024u;

inline bool decoded_image_dimensions_are_safe(std::uint32_t width,
                                              std::uint32_t height) {
  return width != 0 && height != 0 &&
         static_cast<std::uint64_t>(width) * height <=
             max_decoded_image_pixels;
}

struct RgbaImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;
};

// A legacy kind `G` (GDF) picture is vector graphics.  It is drawn onto a
// canvas of this fixed size, the size hosted BookServer's GIF of it has.
constexpr std::uint32_t gdf_canvas_width = 1004;
constexpr std::uint32_t gdf_canvas_height = 735;

// A legacy kind `I` (MMR) bitmap is scanned at 240 pixels per inch.  Hosted
// BookServer presents it at 96 per inch: each dimension times 96, divided by
// 240, rounded down.  See the "Reference Render" section of the MMR chapter.
constexpr std::uint32_t mmr_source_pixels_per_inch = 240;
constexpr std::uint32_t mmr_display_pixels_per_inch = 96;

struct ImageSize {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

// The rendered PNG of a stored image keeps every pixel the book stores, so it
// can be larger than the image is meant to be seen.  This is the size, in CSS
// pixels, it is meant to be shown at, read from the stored bytes' own header
// without decoding them.  `std::nullopt` when the format or the header does
// not state it.  `stored_bytes` may be only a prefix of the payload.
std::optional<ImageSize> stored_image_display_size(
    const ResourceEntry& resource,
    const std::uint8_t* stored_bytes,
    std::size_t stored_size);

std::vector<std::uint8_t> render_resource_png(
    const ResourceEntry& resource,
    const std::vector<std::uint8_t>& stored_bytes);
RgbaImage decode_gdf_to_rgba(const std::vector<std::uint8_t>& bytes);
RgbaImage decode_mmr_to_rgba(const std::vector<std::uint8_t>& bytes);

} // namespace geist::detail
