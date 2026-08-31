// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/resource.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace geist::detail {

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
