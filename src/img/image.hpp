// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/resource.hpp"

#include <cstddef>
#include <cstdint>
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

std::vector<std::uint8_t> render_resource_png(
    const ResourceEntry& resource,
    const std::vector<std::uint8_t>& stored_bytes);
RgbaImage decode_gdf_to_rgba(const std::vector<std::uint8_t>& bytes);
RgbaImage decode_mmr_to_rgba(const std::vector<std::uint8_t>& bytes);

} // namespace geist::detail
