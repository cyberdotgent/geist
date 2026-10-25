// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "img/image.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace geist::detail {

// Coordinates of a parsed GDF picture are virtual device coordinates: the
// picture's own coordinates after the model transform, with x and y scaled by
// the picture's aspect factors (see GdfPicture::aspect_x).
struct Point {
  double x = 0.0;
  double y = 0.0;
};

struct Color {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;
};

// The dash pattern a line is stroked with.  Lines are always one device
// pixel wide.
enum class GdfLineStyle : std::uint8_t {
  solid,
  dash,
  dot,
  dash_dot,
  dash_dot_dot,
};

struct Polyline {
  std::vector<Point> points;
  Color color;
  GdfLineStyle style = GdfLineStyle::solid;
};

// How a figure's interior is painted.  `solid` covers it in the fill color.
// `pattern` sets the black pixels of an 8x8 dot pattern (densities 1..7) and
// `hatch` the fill-colored pixels of an 8x8 line hatch; both leave the other
// pixels as they were.
enum class GdfInterior : std::uint8_t { solid, empty, pattern, hatch };

enum class GdfHatch : std::uint8_t { rising, falling, horizontal, vertical };

// A closed figure: its contours are filled together, and its boundary is
// stroked when `draw_boundary` is set.
struct FilledArea {
  std::vector<std::vector<Point>> contours;
  bool filled = true;
  GdfInterior interior = GdfInterior::solid;
  int pattern_density = 0;
  GdfHatch hatch = GdfHatch::rising;
  Color fill;
  bool draw_boundary = false;
  Color edge;
  GdfLineStyle edge_style = GdfLineStyle::solid;
};

// The face picture text is set in.  The reference renderer sets text in a
// fixed-pitch face once a character-set order has selected a font in a
// picture without a font list, and in a proportional sans-serif face
// otherwise: before any character-set order, and in every picture with a
// font list, whatever symbol set the text selects.
enum class GdfTextFace : std::uint8_t { fixed_pitch, proportional };

struct TextRun {
  // The start of the baseline, in virtual device coordinates.
  Point point;
  // Printable ASCII.
  std::string text;
  Color color;
  // The character box height in virtual device units: the face's em.
  double height = 0.0;
  // The baseline direction in quarter turns counter-clockwise from +x.
  int quarter_turns = 0;
  GdfTextFace face = GdfTextFace::fixed_pitch;
};

// A marker symbol, ten pixels across whatever the picture's scale.
enum class GdfMarkerShape : std::uint8_t { cross, plus };

struct Marker {
  Point point;
  Color color;
  GdfMarkerShape shape = GdfMarkerShape::cross;
};

enum class GdfPrimitive : std::uint8_t { line, area, text, marker };

struct GdfDrawStep {
  GdfPrimitive kind = GdfPrimitive::line;
  std::size_t index = 0;
};

struct GdfPicture {
  // The picture window from the stream's initial comment, in the picture's
  // own coordinates.  It fills the drawing box of the canvas.
  double min_x = 0.0;
  double min_y = 0.0;
  double max_x = 100.0;
  double max_y = 100.0;
  // Aspect factors from the picture-attribute prolog: virtual device
  // coordinates are picture coordinates times these.
  double aspect_x = 1.0;
  double aspect_y = 1.0;
  std::vector<Polyline> lines;
  std::vector<FilledArea> areas;
  std::vector<TextRun> texts;
  std::vector<Marker> markers;
  // Every primitive, in the order the stream draws it.
  std::vector<GdfDrawStep> steps;
};

// Parses a kind `G` payload.  Throws std::runtime_error for a malformed
// stream, and for any order the reference renderer draws or applies that
// this decoder cannot reproduce: a picture is rendered completely or not at
// all.
GdfPicture parse_gdf_picture(const std::vector<std::uint8_t>& bytes);

// The device box the picture window maps onto, in canvas pixels.
constexpr int gdf_box_left = 2;
constexpr int gdf_box_right = 998;
constexpr int gdf_box_top = 2;
constexpr int gdf_box_bottom = 733;

} // namespace geist::detail
