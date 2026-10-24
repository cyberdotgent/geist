// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/export.hpp"

#include <cstdint>
#include <string>

namespace geist {

enum class ResourceLayout {
  legacy_v12,
  legacy_v13,
  converted_v14,
  unknown,
};

struct ResourceEntry {
  std::string id;
  std::string name;
  std::string stored_format;
  std::string kind;
  std::string description;
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
  std::uint64_t description_offset = 0;
  std::uint64_t description_size = 0;
  ResourceLayout layout = ResourceLayout::unknown;
  // The size, in CSS pixels, an image resource is meant to be displayed at;
  // both are 0 when the stored header does not state it (or the resource is
  // not an image).
  //
  // `BooDocument::read_resource_png()` keeps every pixel the book stores and
  // never resamples, so its PNG can be larger than this: a legacy fax bitmap
  // (`legacy-mmr`) is scanned at 240 pixels per inch and is meant to be shown
  // at 96, two fifths of its pixel size, which is the size hosted BookServer
  // serves it at.  Display the PNG at this size -- HTML `width`/`height`, a
  // point size on a HiDPI toolkit -- and let the graphics stack scale it, so a
  // dense screen still gets the stored detail.  For every other format the
  // two sizes agree.
  std::uint32_t display_width = 0;
  std::uint32_t display_height = 0;
};

GEIST_API const char* to_string(ResourceLayout layout) noexcept;

} // namespace geist
