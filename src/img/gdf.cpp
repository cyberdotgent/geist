// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/core/internal.hpp"

#include "img/gdf.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace geist::detail {

namespace {

constexpr double kPi = 3.14159265358979323846;

// A row-vector affine transform: x' = x*m[0] + y*m[3] + m[6],
// y' = x*m[1] + y*m[4] + m[7].  m[2], m[5] and m[8] complete the 3x3 matrix
// that model-transform orders compose.
using Matrix = std::array<double, 9>;

constexpr Matrix identity_matrix() {
  return {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
}

Matrix multiply(const Matrix& left, const Matrix& right) {
  Matrix out{};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      double sum = 0.0;
      for (int k = 0; k < 3; ++k) {
        sum += left[static_cast<std::size_t>(i * 3 + k)] *
               right[static_cast<std::size_t>(k * 3 + j)];
      }
      out[static_cast<std::size_t>(i * 3 + j)] = sum;
    }
  }
  return out;
}

// Arc parameters, already transformed into virtual device coordinates.  The
// unit circle maps onto the arc ellipse by x' = a*x + c*y, y' = d*x + b*y.
struct ArcParameters {
  double a = 1.0;
  double b = 1.0;
  double c = 0.0;
  double d = 0.0;
};

enum class FillStyle : std::uint8_t { solid, empty, pattern, hatch };

// The attribute state a push-and-set order saves and a pop order restores.
struct Attributes {
  // The character box height in virtual device units; unset until a
  // character-box order sets it.
  double char_height = 0.0;
  bool char_box_set = false;
  // The baseline direction in quarter turns counter-clockwise from +x.
  int quarter_turns = 0;
  // Whether a character-set order (or the pop of one) has selected a font.
  bool font_selected = false;
  // The marker type byte of the last marker-type order, or -1 before one.
  int marker_type = -1;
  FillStyle fill_style = FillStyle::solid;
  int pattern_density = 0;
  GdfHatch hatch = GdfHatch::rising;
  // Background mix 2 paints the background pixels of patterns and hatches;
  // every other value leaves them transparent.
  bool opaque_background = false;
  std::uint8_t basic_color = 7;
  std::uint8_t extended_color = 7;
  GdfLineStyle line_style = GdfLineStyle::solid;
  Point current;
  ArcParameters arc;
  Matrix matrix = identity_matrix();
};

struct SavedAttribute {
  std::uint8_t opcode = 0;
  Attributes value;
};

struct ParserState {
  GdfPicture picture;
  Attributes attr;
  std::vector<SavedAttribute> saved;
  // The color index lines, edges and text are drawn in, and the one a figure
  // begun now is filled with.
  std::uint8_t line_color = 7;
  std::uint8_t fill_color = 7;
  bool lines_visible = true;
  bool edges_visible = true;
  bool in_figure = false;
  // Entries in the font-list prolog.
  std::size_t font_list_entries = 0;
  // A pop restored a saved marker scale, which sets the marker size.
  bool marker_size_restored = false;
  // An area order that asked for boundary lines begins its figure at the next
  // line order, not at once.
  bool figure_pending = false;
  FilledArea figure;
  // Virtual device extent, for the arc plausibility test.
  double vdc_x0 = 0.0;
  double vdc_y0 = 0.0;
  double vdc_x1 = 100.0;
  double vdc_y1 = 100.0;
};

[[noreturn]] void unsupported(std::uint8_t opcode, const char* what) {
  char code[8];
  std::snprintf(code, sizeof(code), "0x%02x", opcode);
  throw std::runtime_error(std::string("GDF order ") + code + " (" + what +
                           ") is not supported");
}

// GDDM color numbers as hosted BookServer's GIFs show them.  Indexes 9..16
// are the GIF palette entries nearest to the reference color table.
Color palette(std::uint8_t index) {
  static constexpr std::array<Color, 17> colors{{
      {0, 0, 0, 255},       // 0 default
      {0, 0, 255, 255},     // 1 blue
      {255, 0, 0, 255},     // 2 red
      {255, 0, 255, 255},   // 3 pink / magenta
      {0, 255, 0, 255},     // 4 green
      {0, 255, 255, 255},   // 5 turquoise
      {255, 255, 0, 255},   // 6 yellow
      {255, 255, 255, 255}, // 7 neutral
      {0, 0, 0, 255},       // 8 background
      {0, 0, 128, 255},     // 9 dark blue
      {224, 128, 0, 255},   // 10 orange
      {128, 0, 128, 255},   // 11 purple
      {0, 128, 0, 255},     // 12 dark green
      {0, 128, 128, 255},   // 13 dark turquoise
      {160, 160, 0, 255},   // 14 mustard
      {128, 128, 128, 255}, // 15 gray
      {128, 128, 0, 255},   // 16 brown
  }};
  return colors[std::min<std::size_t>(index, colors.size() - 1)];
}

// Color order 0x0a: indexes 0..8 select themselves, anything higher selects
// neutral (7).
std::uint8_t basic_color_index(std::uint8_t value) {
  return value < 9 ? value : 7;
}

// Color Set Extended order 0x26: the second byte is the color index. With a
// first byte of 0xff the indexes 7 (neutral) and 8 (background) swap, so
// `ff 07` draws in the background color and `ff 08` in neutral. Index 0 and
// indexes past 16 select neutral.
std::uint8_t extended_color_index(std::uint8_t high, std::uint8_t low) {
  if (high == 0xff && low == 7) {
    return 8;
  }
  if (high == 0xff && low == 8) {
    return 7;
  }
  if (low == 0 || low > 16) {
    return 7;
  }
  return low;
}

// Line type order 0x18 values 0..7.  8 and above draw invisible lines.
GdfLineStyle line_style_for(std::uint8_t value) {
  static constexpr std::array<GdfLineStyle, 8> styles{{
      GdfLineStyle::solid,
      GdfLineStyle::dot,
      GdfLineStyle::dash,
      GdfLineStyle::dash_dot,
      GdfLineStyle::dash_dot,
      GdfLineStyle::dot,
      GdfLineStyle::dash_dot_dot,
      GdfLineStyle::solid,
  }};
  return styles[std::min<std::size_t>(value, 7)];
}

double read_ibm_hfp_float(const std::vector<std::uint8_t>& bytes,
                          std::size_t offset) {
  const auto value =
      (static_cast<std::uint32_t>(bytes[offset]) << 24) |
      (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
      (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
      static_cast<std::uint32_t>(bytes[offset + 3]);
  if ((value & 0x00ffffffu) == 0) {
    return 0.0;
  }

  const double sign = (value & 0x80000000u) != 0 ? -1.0 : 1.0;
  const int exponent = static_cast<int>((value >> 24) & 0x7fu) - 64;
  const auto fraction = static_cast<double>(value & 0x00ffffffu) /
                        static_cast<double>(0x01000000u);
  return sign * std::ldexp(fraction, exponent * 4);
}

constexpr std::size_t coordinate_size = 4;
constexpr std::size_t point_size = coordinate_size * 2;

double read_coord(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  const auto value = read_ibm_hfp_float(bytes, offset);
  if (!std::isfinite(value) || value < -1000000.0 || value > 1000000.0) {
    throw std::runtime_error("GDF asset contains an implausible coordinate");
  }
  return value;
}

Point read_raw_point(const std::vector<std::uint8_t>& bytes,
                     std::size_t offset) {
  return Point{read_coord(bytes, offset),
               read_coord(bytes, offset + coordinate_size)};
}

Point transform(const Matrix& m, Point p) {
  return Point{p.x * m[0] + p.y * m[3] + m[6], p.x * m[1] + p.y * m[4] + m[7]};
}

Point read_point(const ParserState& state,
                 const std::vector<std::uint8_t>& bytes,
                 std::size_t offset) {
  return transform(state.attr.matrix, read_raw_point(bytes, offset));
}

std::vector<Point> read_points(const ParserState& state,
                               const std::vector<std::uint8_t>& bytes,
                               std::size_t offset,
                               std::size_t length) {
  std::vector<Point> points;
  points.reserve(length / point_size);
  for (std::size_t i = 0; i + point_size <= length; i += point_size) {
    points.push_back(read_point(state, bytes, offset + i));
  }
  return points;
}

std::size_t gdf_record_payload_length(std::uint8_t opcode,
                                      const std::vector<std::uint8_t>& bytes,
                                      std::size_t& offset) {
  if (opcode == 0) {
    return 0;
  }

  const auto high = static_cast<std::uint8_t>(opcode >> 4);
  const auto low = static_cast<std::uint8_t>(opcode & 0x0f);
  if (high < 8 && low >= 8) {
    return 1;
  }

  if (offset >= bytes.size()) {
    throw std::runtime_error("GDF asset ends inside a record header");
  }
  return bytes[offset++];
}

void require_length(std::uint8_t opcode,
                    std::size_t length,
                    std::size_t needed) {
  if (length < needed) {
    char code[8];
    std::snprintf(code, sizeof(code), "0x%02x", opcode);
    throw std::runtime_error(std::string("GDF order ") + code +
                             " is truncated");
  }
}

// --- color and figure state ------------------------------------------------

void apply_color(ParserState& state, std::uint8_t index) {
  if (!state.figure_pending) {
    state.fill_color = index;
  }
  state.line_color = index;
}

void begin_figure(ParserState& state) {
  state.figure = FilledArea{};
  state.figure.filled = state.attr.fill_style != FillStyle::empty;
  switch (state.attr.fill_style) {
  case FillStyle::solid:
    state.figure.interior = GdfInterior::solid;
    break;
  case FillStyle::empty:
    state.figure.interior = GdfInterior::empty;
    break;
  case FillStyle::pattern:
    state.figure.interior = GdfInterior::pattern;
    break;
  case FillStyle::hatch:
    state.figure.interior = GdfInterior::hatch;
    break;
  }
  if (state.attr.opaque_background &&
      (state.attr.fill_style == FillStyle::pattern ||
       state.attr.fill_style == FillStyle::hatch)) {
    unsupported(0x0d, "opaque pattern background");
  }
  state.figure.pattern_density = state.attr.pattern_density;
  state.figure.hatch = state.attr.hatch;
  state.figure.fill = palette(state.fill_color);
  state.figure.draw_boundary = state.edges_visible;
  state.figure.edge = palette(state.line_color);
  state.figure.edge_style = state.attr.line_style;
  state.in_figure = true;
}

// Line orders that start a figure region (absolute lines and arcs) close the
// figure's current contour first, and every line order except the relative
// one at current position begins a pending figure.
void prepare_figure(ParserState& state, bool new_region) {
  if (state.in_figure && new_region) {
    state.figure.contours.emplace_back();
  }
  if (state.figure_pending) {
    begin_figure(state);
    state.figure_pending = false;
  }
}

void end_figure(ParserState& state) {
  if (state.in_figure) {
    auto& contours = state.figure.contours;
    contours.erase(std::remove_if(contours.begin(), contours.end(),
                                  [](const std::vector<Point>& contour) {
                                    return contour.size() < 2;
                                  }),
                   contours.end());
    if (!contours.empty() &&
        (state.figure.filled || state.figure.draw_boundary)) {
      state.picture.steps.push_back(
          GdfDrawStep{GdfPrimitive::area, state.picture.areas.size()});
      state.picture.areas.push_back(std::move(state.figure));
    }
  }
  state.figure = FilledArea{};
  state.in_figure = false;
  state.edges_visible = true;
}

// Draws a polyline: inside a figure it extends the current contour, outside
// one it is stroked in the current line color and type.
void draw_polyline(ParserState& state,
                   std::vector<Point> points,
                   bool stroke_outside_figure) {
  if (points.size() < 2) {
    return;
  }
  if (state.in_figure) {
    if (state.figure.contours.empty()) {
      state.figure.contours.emplace_back();
    }
    auto& contour = state.figure.contours.back();
    for (const auto& point : points) {
      if (contour.empty() || contour.back().x != point.x ||
          contour.back().y != point.y) {
        contour.push_back(point);
      }
    }
    return;
  }
  if (!stroke_outside_figure) {
    return;
  }
  state.picture.steps.push_back(
      GdfDrawStep{GdfPrimitive::line, state.picture.lines.size()});
  state.picture.lines.push_back(Polyline{std::move(points),
                                         palette(state.line_color),
                                         state.attr.line_style});
}

void set_current(ParserState& state, const std::vector<Point>& points) {
  if (!points.empty()) {
    state.attr.current = points.back();
  }
}

// --- drawing orders ----------------------------------------------------------

void handle_line(ParserState& state,
                 const std::vector<std::uint8_t>& bytes,
                 std::size_t offset,
                 std::size_t length,
                 bool at_current) {
  prepare_figure(state, !at_current);
  std::vector<Point> points;
  if (at_current) {
    points.push_back(state.attr.current);
  }
  const auto read = read_points(state, bytes, offset, length);
  points.insert(points.end(), read.begin(), read.end());
  draw_polyline(state, points, state.lines_visible);
  set_current(state, points);
}

void handle_relative_line(ParserState& state,
                          const std::vector<std::uint8_t>& bytes,
                          std::size_t offset,
                          std::size_t length,
                          bool at_current) {
  std::vector<Point> points;
  std::size_t consumed = 0;
  if (at_current) {
    // The deltas accumulate from the current position, which is already in
    // virtual device coordinates; each accumulated point is then transformed
    // again.  Under the identity transform this is the plain delta walk.
    Point raw = state.attr.current;
    points.push_back(raw);
    while (consumed + 1 < length) {
      raw.x += static_cast<std::int8_t>(bytes[offset + consumed]);
      raw.y += static_cast<std::int8_t>(bytes[offset + consumed + 1]);
      points.push_back(transform(state.attr.matrix, raw));
      consumed += 2;
    }
  } else {
    prepare_figure(state, true);
    require_length(0xe1, length, point_size);
    Point raw = read_raw_point(bytes, offset);
    std::vector<Point> raw_points{raw};
    consumed = point_size;
    while (consumed + 1 < length) {
      raw.x += static_cast<std::int8_t>(bytes[offset + consumed]);
      raw.y += static_cast<std::int8_t>(bytes[offset + consumed + 1]);
      raw_points.push_back(raw);
      consumed += 2;
    }
    for (const auto& point : raw_points) {
      points.push_back(transform(state.attr.matrix, point));
    }
  }
  draw_polyline(state, points, state.lines_visible);
  set_current(state, points);
}

double normalized_degrees(double radians, bool lift_zero) {
  double degrees = radians * 57.295827908797776;
  if (degrees < 0.0) {
    degrees += 360.0;
  }
  if (lift_zero && degrees <= 0.0001) {
    degrees += 360.0;
  }
  return degrees;
}

// The three-point arc: the ellipse of the current arc parameters through p0,
// p1 and p2, from p0 through p1 to p2.  Degenerate and implausible arcs are
// drawn as the polyline through the three points.
std::vector<Point> arc_points(const ParserState& state,
                              Point p0,
                              Point p1,
                              Point p2) {
  const std::vector<Point> fallback{p0, p1, p2};
  const auto& arc = state.attr.arc;
  const double det = arc.b * arc.a - arc.d * arc.c;
  if (std::abs(det) < 1e-5) {
    return fallback;
  }

  // Into the space where the arc ellipse is a circle (scaled by det).
  const auto unmap = [&](Point p) {
    const double x = p.x - p0.x;
    const double y = p.y - p0.y;
    return Point{x * arc.b - y * arc.c, y * arc.a - x * arc.d};
  };
  const Point q1 = unmap(p1);
  const Point q2 = unmap(p2);
  const double denominator = (q2.x * q1.y - q1.x * q2.y) * 2.0;
  if (std::abs(denominator) < 1e-5) {
    return fallback;
  }
  const double s1 = -(q1.x * q1.x + q1.y * q1.y);
  const double s2 = -(q2.x * q2.x + q2.y * q2.y);
  const double cx = (s1 * q2.y - q1.y * s2) / denominator;
  const double cy = (q1.x * s2 - q2.x * s1) / denominator;

  const double start = normalized_degrees(std::atan2(-cy, -cx), true);
  const double end = normalized_degrees(std::atan2(q2.y - cy, q2.x - cx), true);
  const double mid = normalized_degrees(std::atan2(q1.y - cy, q1.x - cx), false);
  const bool clockwise = (mid < start && end < mid && end < start) ||
                         (mid < start && mid < end && start < end) ||
                         (start < mid && end < mid && start < end);
  if (std::abs(start - end) < 1.0) {
    return fallback;
  }

  const Point center{(arc.a * cx + arc.c * cy) / det + p0.x,
                     (arc.b * cy + arc.d * cx) / det + p0.y};
  if (!(std::abs(center.x) < 50000.0 && std::abs(center.y) < 50000.0)) {
    return fallback;
  }
  const double dx = p0.x - center.x;
  const double dy = p0.y - center.y;
  const double squared =
      dy * dy * (arc.a * arc.a + arc.c * arc.c) +
      dy * dx * (-(arc.c * arc.b) - arc.d * arc.a) * 2.0 +
      dx * dx * (arc.d * arc.d + arc.b * arc.b);
  const double radius = std::sqrt(std::max(0.0, squared)) / det;
  Point first{arc.a * radius + center.x, arc.d * radius + center.y};
  Point second{arc.c * radius + center.x, arc.b * radius + center.y};
  if (clockwise) {
    std::swap(first, second);
  }
  const double limit_x = (state.vdc_x1 - state.vdc_x0) * 2.0;
  const double limit_y = (state.vdc_y1 - state.vdc_y0) * 2.0;
  if (!(first.x - state.vdc_x0 <= limit_x && first.y - state.vdc_y0 <= limit_y &&
        second.x - state.vdc_x0 <= limit_x &&
        second.y - state.vdc_y0 <= limit_y)) {
    return fallback;
  }

  // Elliptical arc from the start vector to the end vector, sweeping from the
  // first conjugate diameter point towards the second.
  const double ux = first.x - center.x;
  const double uy = first.y - center.y;
  const double vx = second.x - center.x;
  const double vy = second.y - center.y;
  const double basis = ux * vy - uy * vx;
  if (std::abs(basis) < 1e-12) {
    return fallback;
  }
  const auto parameter = [&](Point p) {
    const double px = p.x - center.x;
    const double py = p.y - center.y;
    const double cos_t = (px * vy - py * vx) / basis;
    const double sin_t = (ux * py - uy * px) / basis;
    return std::atan2(sin_t, cos_t);
  };
  const double t0 = parameter(p0);
  double t1 = parameter(p2);
  while (t1 <= t0) {
    t1 += 2.0 * kPi;
  }
  const int steps = std::max(
      8, static_cast<int>(std::ceil((t1 - t0) / (2.0 * kPi) * 720.0)));
  std::vector<Point> points;
  points.reserve(static_cast<std::size_t>(steps) + 1);
  for (int i = 0; i <= steps; ++i) {
    const double t = t0 + (t1 - t0) * static_cast<double>(i) / steps;
    points.push_back(Point{center.x + ux * std::cos(t) + vx * std::sin(t),
                           center.y + uy * std::cos(t) + vy * std::sin(t)});
  }
  points.front() = p0;
  points.back() = p2;
  return points;
}

void handle_arc(ParserState& state,
                const std::vector<std::uint8_t>& bytes,
                std::size_t offset,
                std::size_t length,
                bool at_current) {
  Point p0 = state.attr.current;
  std::size_t cursor = 0;
  if (!at_current) {
    prepare_figure(state, true);
    require_length(0xc6, length, point_size * 3);
    p0 = read_point(state, bytes, offset);
    cursor = point_size;
  } else {
    require_length(0x86, length, point_size * 2);
  }
  const Point p1 = read_point(state, bytes, offset + cursor);
  const Point p2 = read_point(state, bytes, offset + cursor + point_size);
  // Arcs are drawn whatever the line type.
  draw_polyline(state, arc_points(state, p0, p1, p2), true);
  state.attr.current = p2;
}

// A text byte as the reference shows it: bytes below 0x40, 0x41, 0x8f and
// 0xff are spaces, 0x8a is G, and the rest follow code page 500.  A
// character outside printable ASCII has no glyph in the stroke font.
char text_character(std::uint8_t byte) {
  if (byte < 0x42 || byte == 0x8f || byte == 0xff) {
    return ' ';
  }
  if (byte == 0x8a) {
    return 'G';
  }
  const auto decoded = EbcdicCodec::cp500().decode_word(byte);
  if (decoded < 0x20 || decoded > 0x7e) {
    char code[8];
    std::snprintf(code, sizeof(code), "0x%02x", byte);
    throw std::runtime_error(std::string("GDF text character ") + code +
                             " has no glyph in the stroke font");
  }
  return static_cast<char>(decoded);
}

void handle_text(ParserState& state,
                 const std::vector<std::uint8_t>& bytes,
                 std::size_t offset,
                 std::size_t length,
                 bool at_current) {
  Point point = state.attr.current;
  std::size_t text_offset = offset;
  if (!at_current) {
    require_length(0xc3, length, point_size);
    point = read_point(state, bytes, offset);
    text_offset += point_size;
  }

  std::string text;
  for (std::size_t i = text_offset; i < offset + length && bytes[i] != 0; ++i) {
    text.push_back(text_character(bytes[i]));
  }
  if (text.empty()) {
    return;
  }
  if (!state.attr.char_box_set || !(state.attr.char_height > 0.0)) {
    throw std::runtime_error(
        "GDF text has no positive character box height");
  }
  state.picture.steps.push_back(
      GdfDrawStep{GdfPrimitive::text, state.picture.texts.size()});
  state.picture.texts.push_back(
      TextRun{point, text, palette(state.line_color), state.attr.char_height,
              state.attr.quarter_turns,
              state.font_list_entries == 0 && state.attr.font_selected
                  ? GdfTextFace::fixed_pitch
                  : GdfTextFace::proportional});
}

// Marker orders: `c2` marks every point; `82` marks the current position and
// then every point.  The current position moves to the last point.  Marker
// types 1 (a diagonal cross) and 2 (an upright cross) are reproduced; other
// types, a marker before any marker-type order and a marker size restored by
// a pop decline.
void handle_markers(ParserState& state,
                    const std::vector<std::uint8_t>& bytes,
                    std::size_t offset,
                    std::size_t length,
                    bool at_current) {
  const std::uint8_t opcode = at_current ? 0x82 : 0xc2;
  GdfMarkerShape shape = GdfMarkerShape::cross;
  if (state.attr.marker_type == 1) {
    shape = GdfMarkerShape::cross;
  } else if (state.attr.marker_type == 2) {
    shape = GdfMarkerShape::plus;
  } else {
    unsupported(opcode, "marker type");
  }
  if (state.marker_size_restored) {
    unsupported(opcode, "marker size");
  }
  auto points = read_points(state, bytes, offset, length);
  if (at_current) {
    points.insert(points.begin(), state.attr.current);
  } else if (points.empty()) {
    require_length(opcode, length, point_size);
  }
  for (const auto& point : points) {
    state.picture.steps.push_back(
        GdfDrawStep{GdfPrimitive::marker, state.picture.markers.size()});
    state.picture.markers.push_back(Marker{point, palette(state.line_color), shape});
  }
  state.attr.current = points.back();
}

// --- attribute orders --------------------------------------------------------

void handle_pattern(ParserState& state, std::uint8_t opcode, std::uint8_t value) {
  // 0 and 16 fill solid in the current color, 15 leaves the interior empty.
  // 1..8 are dot patterns of density 7 down to 1 (8 repeats 1), 9..14 line
  // hatches, 0x41..0xfe color patterns.
  if (value == 0 || value == 16) {
    state.attr.fill_style = FillStyle::solid;
  } else if (value == 15) {
    state.attr.fill_style = FillStyle::empty;
  } else if (value <= 8) {
    state.attr.fill_style = FillStyle::pattern;
    state.attr.pattern_density = value == 8 ? 1 : 8 - value;
  } else if (value == 9) {
    state.attr.fill_style = FillStyle::hatch;
    state.attr.hatch = GdfHatch::vertical;
  } else if (value == 10) {
    state.attr.fill_style = FillStyle::hatch;
    state.attr.hatch = GdfHatch::horizontal;
  } else if (value == 11 || value == 12) {
    state.attr.fill_style = FillStyle::hatch;
    state.attr.hatch = GdfHatch::rising;
  } else if (value == 13 || value == 14) {
    state.attr.fill_style = FillStyle::hatch;
    state.attr.hatch = GdfHatch::falling;
  } else if (value < 0x11 || (value >= 0x41 && value != 0xff)) {
    unsupported(opcode, "pattern fill");
  } else {
    // Values 0x11..0x40 and 0xff fall back to a solid interior.
    state.attr.fill_style = FillStyle::solid;
    return;
  }
  apply_color(state, state.line_color);
}

void handle_line_type(ParserState& state, std::uint8_t value) {
  if (value < 8) {
    state.attr.line_style = line_style_for(value);
    state.lines_visible = true;
  } else {
    state.attr.line_style = GdfLineStyle::solid;
    state.lines_visible = false;
    state.edges_visible = false;
  }
}

void handle_model_transform(ParserState& state,
                            const std::vector<std::uint8_t>& bytes,
                            std::size_t offset,
                            std::size_t length) {
  require_length(0x24, length, 4);
  Matrix given = identity_matrix();
  std::size_t cursor = 4;
  const auto read_masked = [&](std::uint8_t mask,
                               const std::array<int, 8>& slot_for_bit) {
    for (int bit = 7; bit >= 0; --bit) {
      if ((mask & (1u << bit)) == 0) {
        continue;
      }
      require_length(0x24, length, cursor + coordinate_size);
      const double value = read_coord(bytes, offset + cursor);
      cursor += coordinate_size;
      const int slot = slot_for_bit[static_cast<std::size_t>(bit)];
      if (slot >= 0) {
        given[static_cast<std::size_t>(slot)] = value;
      }
    }
  };
  // Bit -> matrix element, -1 for values that are read and discarded.
  read_masked(bytes[offset + 2], {5, -1, 4, 3, 2, -1, 1, 0});
  read_masked(bytes[offset + 3], {8, -1, 7, 6, -1, -1, -1, -1});

  switch (bytes[offset + 1] & 3u) {
  case 0:
    given[0] *= state.picture.aspect_x;
    given[3] *= state.picture.aspect_x;
    given[1] *= state.picture.aspect_y;
    given[4] *= state.picture.aspect_y;
    state.attr.matrix = given;
    break;
  case 1:
    state.attr.matrix = multiply(state.attr.matrix, given);
    break;
  case 2:
    state.attr.matrix = multiply(given, state.attr.matrix);
    break;
  default:
    break;
  }
}

void handle_arc_parameters(ParserState& state,
                           const std::vector<std::uint8_t>& bytes,
                           std::size_t offset,
                           std::size_t length) {
  // Operands P, Q, R, S; transformed as the points (P, S) and (R, Q).
  require_length(0x22, length, coordinate_size * 4);
  const double p = read_coord(bytes, offset);
  const double q = read_coord(bytes, offset + coordinate_size);
  const double r = read_coord(bytes, offset + coordinate_size * 2);
  const double s = read_coord(bytes, offset + coordinate_size * 3);
  const Point first = transform(state.attr.matrix, Point{p, s});
  const Point second = transform(state.attr.matrix, Point{r, q});
  state.attr.arc = ArcParameters{first.x, second.y, second.x, first.y};
}

// Saves the attribute a push-and-set order is about to change.
void push_attribute(ParserState& state, std::uint8_t opcode) {
  switch (opcode) {
  case 0x03:
  case 0x09:
  case 0x23:
  case 0x4a:
  case 0x4c:
  case 0x4d:
  case 0x50:
  case 0x51:
  case 0x58:
  case 0x59:
  case 0x61:
  case 0x62:
  case 0x64:
  case 0x66:
  case 0x67:
  case 0x69:
  case 0x74:
  case 0x75:
  case 0x76:
  case 0x77:
  case 0x78:
  case 0x79:
  case 0x7a:
    state.saved.push_back(SavedAttribute{opcode, state.attr});
    break;
  default:
    break;
  }
}

void pop_attribute(ParserState& state) {
  if (state.saved.empty()) {
    return;
  }
  const auto saved = state.saved.back();
  state.saved.pop_back();
  const auto& value = saved.value;
  switch (saved.opcode) {
  case 0x03:
    state.attr.char_height = value.char_height;
    state.attr.char_box_set = value.char_box_set;
    break;
  case 0x09:
    state.attr.fill_style = value.fill_style;
    state.attr.pattern_density = value.pattern_density;
    state.attr.hatch = value.hatch;
    break;
  case 0x4a:
    state.attr.basic_color = value.basic_color;
    apply_color(state, state.attr.basic_color);
    break;
  case 0x4d:
    state.attr.opaque_background = value.opaque_background;
    break;
  case 0x58:
    // Restores the type only; whether lines are visible stays as it is.
    state.attr.line_style = value.line_style;
    break;
  case 0x61:
    state.attr.current = value.current;
    break;
  case 0x62:
    state.attr.arc = value.arc;
    break;
  case 0x64:
    state.attr.matrix = value.matrix;
    break;
  case 0x66:
    state.attr.extended_color = value.extended_color;
    apply_color(state, state.attr.extended_color);
    break;
  case 0x69:
    state.attr.marker_type = value.marker_type;
    break;
  case 0x74:
    state.attr.quarter_turns = value.quarter_turns;
    break;
  case 0x77:
    // The marker scale order itself has no effect, but its pop applies the
    // saved scale as the marker size.
    state.marker_size_restored = true;
    break;
  case 0x78:
    // The pop selects the saved font again, whatever it was.
    state.attr.font_selected = true;
    break;
  default:
    break;
  }
}

// --- stream structure --------------------------------------------------------

void parse_header(ParserState& state, const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 2 || bytes[0] != 0x01) {
    throw std::runtime_error("GDF asset does not start with a supported header");
  }
  // The initial comment carries a two-byte data type and the window as four
  // coordinates.  Length 0x12 selects four-byte System/370 floating point.
  if (bytes[1] != 0x12 || bytes.size() < 0x14) {
    throw std::runtime_error("GDF asset uses an unsupported coordinate format");
  }
  state.picture.min_x = read_coord(bytes, 4);
  state.picture.max_x = read_coord(bytes, 8);
  state.picture.min_y = read_coord(bytes, 12);
  state.picture.max_y = read_coord(bytes, 16);
  if (!(state.picture.min_x < state.picture.max_x) ||
      !(state.picture.min_y < state.picture.max_y)) {
    throw std::runtime_error("GDF asset has an empty declared extent");
  }
  state.vdc_x0 = state.picture.min_x;
  state.vdc_x1 = state.picture.max_x;
  state.vdc_y0 = state.picture.min_y;
  state.vdc_y1 = state.picture.max_y;
}

// A prolog is a run of `02` records opened by `02 02 xx 7e`, where xx is 01
// for the font list and 02 for picture attributes, and closed by a two-byte
// record whose second payload byte is 7f.  A zero byte may follow a record.
std::size_t parse_prolog(ParserState& state,
                         const std::vector<std::uint8_t>& bytes,
                         std::size_t offset,
                         std::uint8_t kind) {
  if (offset + 3 > bytes.size() || bytes[offset] != 0x02 ||
      bytes[offset + 1] != 0x02 || bytes[offset + 2] != kind) {
    return offset;
  }
  for (;;) {
    if (offset + 2 > bytes.size()) {
      throw std::runtime_error("GDF prolog is truncated");
    }
    const std::size_t length = bytes[offset + 1];
    const std::size_t payload = offset + 2;
    if (payload + length > bytes.size()) {
      throw std::runtime_error("GDF prolog is truncated");
    }
    offset = payload + length;
    if (length == 2) {
      if (bytes[payload + 1] == 0x7f) {
        return offset;
      }
      continue;
    }
    if (length < 2) {
      throw std::runtime_error("GDF prolog record is truncated");
    }
    if (kind == 0x01) {
      // A font-list entry: local id at payload offset 4, symbol-set name
      // after it.  Their presence alone selects the proportional face.
      ++state.font_list_entries;
    }
    if (kind == 0x02) {
      const auto attribute = bytes[payload + 1];
      switch (attribute) {
      case 0x20: {
        // Picture aspect: width and height.  Coordinates scale so that the
        // larger of the two becomes 1, and the window grows to match.
        if (length < 3 + 2 * coordinate_size) {
          throw std::runtime_error("GDF aspect prolog record is truncated");
        }
        const double width = read_coord(bytes, payload + 3);
        const double height = read_coord(bytes, payload + 3 + coordinate_size);
        if (!(width > 0.0) || !(height > 0.0)) {
          throw std::runtime_error("GDF aspect prolog record is not positive");
        }
        const double larger = std::max(width, height);
        state.picture.aspect_x = larger / width;
        state.picture.aspect_y = larger / height;
        state.attr.matrix[0] *= state.picture.aspect_x;
        state.attr.matrix[3] *= state.picture.aspect_x;
        state.attr.matrix[1] *= state.picture.aspect_y;
        state.attr.matrix[4] *= state.picture.aspect_y;
        state.vdc_x1 = std::floor(state.vdc_x0 + (state.vdc_x1 - state.vdc_x0) *
                                                     state.picture.aspect_x +
                                  0.5);
        state.vdc_y1 = std::floor(state.vdc_y0 + (state.vdc_y1 - state.vdc_y0) *
                                                     state.picture.aspect_y +
                                  0.5);
        break;
      }
      case 0x01:
      case 0x0c:
      case 0x0d:
      case 0x0e:
      case 0x10:
      case 0x11:
      case 0x18:
      case 0x22:
      case 0x26:
      case 0x27:
      case 0x28:
      case 0x29:
      case 0x32:
      case 0x33:
      case 0x34:
      case 0x35:
      case 0x36:
      case 0x37:
      case 0x38:
      case 0x39:
      case 0x3a:
      case 0x43:
        break;
      default: {
        char code[8];
        std::snprintf(code, sizeof(code), "0x%02x", attribute);
        throw std::runtime_error(std::string("GDF picture-attribute prolog "
                                             "record ") +
                                 code + " is not supported");
      }
      }
    }
    if (offset < bytes.size() && bytes[offset] == 0x00) {
      ++offset;
    }
  }
}

} // namespace

GdfPicture parse_gdf_picture(const std::vector<std::uint8_t>& bytes) {
  ParserState state;
  parse_header(state, bytes);

  std::size_t offset = 2 + static_cast<std::size_t>(bytes[1]);
  offset = parse_prolog(state, bytes, offset, 0x01);
  offset = parse_prolog(state, bytes, offset, 0x02);
  apply_color(state, state.attr.basic_color);

  while (offset < bytes.size()) {
    const auto opcode = bytes[offset++];
    const auto length = gdf_record_payload_length(opcode, bytes, offset);
    if (offset + length > bytes.size()) {
      throw std::runtime_error("GDF asset has a truncated record payload");
    }
    const auto byte0 = length > 0 ? bytes[offset] : std::uint8_t{0};

    push_attribute(state, opcode);
    switch (opcode) {
    case 0x03:
    case 0x33:
      require_length(opcode, length, point_size);
      // The box width is not used: the face sets its own advances.
      state.attr.char_height = read_coord(bytes, offset + coordinate_size);
      state.attr.char_box_set = true;
      break;
    case 0x07:
      unsupported(opcode, "segment call");
    case 0x09:
    case 0x28:
      handle_pattern(state, opcode, byte0);
      break;
    case 0x0a:
    case 0x4a:
      state.attr.basic_color = basic_color_index(byte0);
      apply_color(state, state.attr.basic_color);
      break;
    case 0x0d:
    case 0x4d:
      // 1, 3 and 4 select mixing draw modes; the others overpaint.
      if (byte0 == 1 || byte0 == 3 || byte0 == 4) {
        unsupported(opcode, "background mix");
      }
      state.attr.opaque_background = byte0 == 2;
      break;
    case 0x10:
    case 0x50:
      unsupported(opcode, "character alignment");
    case 0x18:
    case 0x58:
      handle_line_type(state, byte0);
      break;
    case 0x19:
    case 0x59:
      unsupported(opcode, "line width");
    case 0x21:
    case 0x61:
      require_length(opcode, length, point_size);
      state.attr.current = read_point(state, bytes, offset);
      break;
    case 0x22:
    case 0x62:
      handle_arc_parameters(state, bytes, offset, length);
      break;
    case 0x24:
    case 0x64:
      handle_model_transform(state, bytes, offset, length);
      break;
    case 0x26:
    case 0x66:
      require_length(opcode, length, 2);
      state.attr.extended_color =
          extended_color_index(bytes[offset], bytes[offset + 1]);
      apply_color(state, state.attr.extended_color);
      break;
    case 0x27:
    case 0x67:
      unsupported(opcode, "clipping");
    case 0x34:
    case 0x74: {
      // The character angle is a direction vector in picture coordinates,
      // untransformed.  The reference turns it into a baseline angle in
      // whole tenths of a degree, truncated, with pi taken as 3.141 and a
      // negative angle raised by 6.28.  Only quarter turns are reproduced.
      require_length(opcode, length, point_size);
      const auto angle = read_raw_point(bytes, offset);
      double radians = std::atan2(angle.y, angle.x);
      if (radians < 0.0) {
        radians += 6.28;
      }
      const auto tenths = static_cast<long>(radians * 1800.0 / 3.141);
      if (tenths % 900 != 0) {
        unsupported(opcode, "rotated characters");
      }
      state.attr.quarter_turns = static_cast<int>((tenths / 900) % 4);
      break;
    }
    case 0x36:
    case 0x76:
      // Character spacing: a first byte of zero sets the extra advance per
      // character to the x of the point that follows; anything else resets
      // it.  Only a zero extra advance is reproduced.
      if (byte0 == 0) {
        require_length(opcode, length, 2 + point_size);
        if (read_coord(bytes, offset + 2) != 0.0) {
          unsupported(opcode, "character spacing");
        }
      }
      break;
    case 0x38:
    case 0x78:
      // Character set: without a font list every set selects the first font
      // of the reference's built-in list, a fixed-pitch face.
      state.attr.font_selected = true;
      break;
    case 0x3f:
      pop_attribute(state);
      break;
    case 0x60:
      end_figure(state);
      break;
    case 0x68:
      if ((byte0 & 0x80u) == 0) {
        end_figure(state);
      } else if ((byte0 & 0x40u) == 0) {
        state.edges_visible = false;
        begin_figure(state);
      } else {
        state.edges_visible = true;
        state.figure_pending = true;
      }
      break;
    case 0x81:
      handle_line(state, bytes, offset, length, true);
      break;
    case 0xc1:
      handle_line(state, bytes, offset, length, false);
      break;
    case 0x29:
    case 0x69:
      state.attr.marker_type = byte0;
      break;
    case 0x82:
      handle_markers(state, bytes, offset, length, true);
      break;
    case 0xc2:
      handle_markers(state, bytes, offset, length, false);
      break;
    case 0x83:
      handle_text(state, bytes, offset, length, true);
      break;
    case 0xc3:
      handle_text(state, bytes, offset, length, false);
      break;
    case 0x86:
      handle_arc(state, bytes, offset, length, true);
      break;
    case 0xc6:
      handle_arc(state, bytes, offset, length, false);
      break;
    case 0x87:
    case 0xc7:
      unsupported(opcode, "full arc");
    case 0x91:
    case 0x92:
    case 0x93:
    case 0xd1:
      unsupported(opcode, "graphics image");
    case 0xa1:
      handle_relative_line(state, bytes, offset, length, true);
      break;
    case 0xe1:
      handle_relative_line(state, bytes, offset, length, false);
      break;
    default:
      // Every other order -- comments, segment starts and ends, the
      // attributes the reference renderer accepts without drawing effect
      // (mix, fractional line width, marker type, character shear, mode and
      // direction), fillets, and codes it does not define -- changes nothing
      // on the canvas.
      break;
    }

    offset += length;
  }

  if (state.in_figure) {
    throw std::runtime_error("GDF asset ends inside an area");
  }
  if (state.picture.steps.empty()) {
    throw std::runtime_error("GDF asset has no supported drawable records");
  }
  return state.picture;
}

} // namespace geist::detail
