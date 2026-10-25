// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

namespace geist::detail {

// The single-stroke Roman simplex face of the Hershey fonts, which sets the
// text of GDF pictures.  Glyph coordinates are Hershey units: x to the right
// of the glyph's centre line, y downwards, with the baseline at y = 9 and
// capitals rising to y = -12.  See gdf_font.cpp for the data's provenance
// and licence.
struct StrokeVertex {
  std::int8_t x = 0;
  std::int8_t y = 0;
};

// A vertex with this x lifts the pen between two strokes.
constexpr std::int8_t stroke_pen_up = -128;

struct StrokeGlyph {
  // Side bearings: the glyph's cell runs from `left` to `right`.
  std::int8_t left = 0;
  std::int8_t right = 0;
  const StrokeVertex* vertices = nullptr;
  std::size_t count = 0;
};

constexpr int stroke_baseline = 9;
constexpr int stroke_cap_height = 21;

// The glyph for a printable ASCII character (0x20..0x7e); nullptr otherwise.
const StrokeGlyph* stroke_glyph(char ch);

} // namespace geist::detail
