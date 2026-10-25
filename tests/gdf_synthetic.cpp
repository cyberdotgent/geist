// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/core/internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void be32(Bytes& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 24));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
  out.push_back(static_cast<std::uint8_t>(value & 0xff));
}

// IBM System/370 short hexadecimal floating point.
void hfp(Bytes& out, double value) {
  if (value == 0.0) {
    be32(out, 0);
    return;
  }
  const std::uint32_t sign = value < 0 ? 0x80000000u : 0u;
  double magnitude = std::abs(value);
  int exponent = 0;
  while (magnitude >= 1.0) {
    magnitude /= 16.0;
    ++exponent;
  }
  while (magnitude < 1.0 / 16.0) {
    magnitude *= 16.0;
    --exponent;
  }
  const auto fraction =
      static_cast<std::uint32_t>(std::lround(magnitude * 16777216.0));
  be32(out, sign | (static_cast<std::uint32_t>(exponent + 64) << 24) |
                (fraction & 0x00ffffffu));
}

Bytes points(std::initializer_list<double> coordinates) {
  Bytes out;
  for (const double value : coordinates) {
    hfp(out, value);
  }
  return out;
}

void normal(Bytes& out, std::uint8_t opcode, const Bytes& payload) {
  out.push_back(opcode);
  out.push_back(static_cast<std::uint8_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}

void short_order(Bytes& out, std::uint8_t opcode, std::uint8_t payload) {
  out.push_back(opcode);
  out.push_back(payload);
}

// The initial comment: data type 4 (System/370 floating point), window
// 0..100 by 0..100.
Bytes header() {
  Bytes gdf{0x01, 0x12, 0x00, 0x04};
  hfp(gdf, 0.0);
  hfp(gdf, 100.0);
  hfp(gdf, 0.0);
  hfp(gdf, 100.0);
  return gdf;
}

// The picture-attribute prolog every observed kind G payload carries, with
// the aspect record `02 20 08 <width> <height>` of a 0.3776 x 0.5155 page.
void attribute_prolog(Bytes& gdf) {
  normal(gdf, 0x02, {0x02, 0x7e});
  normal(gdf, 0x02, {0x02, 0x0e, 0x80});
  Bytes aspect{0x02, 0x20, 0x08};
  be32(aspect, 0x4060aab0u);
  be32(aspect, 0x4083f7feu);
  normal(gdf, 0x02, aspect);
  gdf.push_back(0x00);
  normal(gdf, 0x02, {0x02, 0x7f});
}

// The segment preamble the observed producer writes before every drawing.
void segment(Bytes& gdf) {
  normal(gdf, 0x70, {0x00, 0x11, 0x9f, 0x0b, 0x74, 0x18, 0, 0, 0, 0, 0, 0});
  normal(gdf, 0x04, {0x80, 0x00, 0x04, 0x08, 0x41, 0x6a, 0xb3, 0x84, 0, 0, 0, 0});
  short_order(gdf, 0x3e, 0x00);
  normal(gdf, 0x23, {0, 0, 0, 1});
  normal(gdf, 0x51, {0x02, 0x00});
  short_order(gdf, 0x58, 0x00);
}

struct Rgb {
  int r;
  int g;
  int b;
};

Rgb pixel(const geist::detail::RgbaImage& image, int x, int y) {
  const auto offset = (static_cast<std::size_t>(y) * image.width +
                       static_cast<std::size_t>(x)) *
                      4u;
  return Rgb{image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2]};
}

void require_pixel(const geist::detail::RgbaImage& image,
                   int x,
                   int y,
                   Rgb expected,
                   const std::string& label) {
  const auto got = pixel(image, x, y);
  if (got.r != expected.r || got.g != expected.g || got.b != expected.b) {
    std::ostringstream message;
    message << label << ": pixel (" << x << ',' << y << ") is " << got.r << ','
            << got.g << ',' << got.b << ", expected " << expected.r << ','
            << expected.g << ',' << expected.b;
    throw std::runtime_error(message.str());
  }
}

constexpr Rgb white{255, 255, 255};
constexpr Rgb black{0, 0, 0};

void require_rejected(const Bytes& gdf,
                      const std::string& fragment,
                      const std::string& label) {
  try {
    (void)geist::detail::decode_gdf_to_rgba(gdf);
  } catch (const std::runtime_error& error) {
    if (std::string(error.what()).find(fragment) != std::string::npos) {
      return;
    }
    throw std::runtime_error(label + ": rejected for the wrong reason: " +
                             error.what());
  }
  throw std::runtime_error(label + ": was rendered instead of rejected");
}

// `66 ff 07` selects the background color (8, black) and `66 ff 08` neutral
// (7, white): the swap every observed producer relies on.  The window maps
// onto the box x 2..998, y 733..2 whatever the aspect record says.
void test_extended_color_swap_and_window() {
  Bytes gdf = header();
  attribute_prolog(gdf);
  segment(gdf);
  normal(gdf, 0x66, {0xff, 0x07});
  normal(gdf, 0xc1, points({0.0, 0.0, 50.0, 0.0}));
  normal(gdf, 0xc1, points({0.0, 100.0, 50.0, 100.0}));
  normal(gdf, 0xc1, points({100.0, 50.0, 100.0, 60.0}));
  normal(gdf, 0x71, {});
  const auto image = geist::detail::decode_gdf_to_rgba(gdf);
  require_pixel(image, 2, 733, black, "window origin");
  require_pixel(image, 400, 733, black, "bottom line");
  require_pixel(image, 400, 2, black, "top line");
  require_pixel(image, 998, 330, black, "right edge");
  require_pixel(image, 400, 734, white, "below the box");
  require_pixel(image, 999, 330, white, "right of the box");
}

// A picture that draws only in neutral white on the white canvas renders
// nothing: that is a failure, never a blank image.
void test_blank_render_fails_closed() {
  Bytes gdf = header();
  attribute_prolog(gdf);
  segment(gdf);
  normal(gdf, 0x66, {0xff, 0x08});
  normal(gdf, 0xc1, points({10.0, 10.0, 90.0, 90.0}));
  require_rejected(gdf, "draws nothing visible", "neutral-only picture");
}

// Areas fill in the color current when they begin, and everything draws in
// stream order: a white area later in the stream covers an earlier line.
void test_area_color_and_stream_order() {
  Bytes gdf = header();
  segment(gdf);
  normal(gdf, 0x66, {0xff, 0x07});
  normal(gdf, 0xc1, points({0.0, 50.0, 100.0, 50.0}));
  normal(gdf, 0xc1, points({0.0, 20.0, 100.0, 20.0}));
  normal(gdf, 0x66, {0x00, 0x07});
  short_order(gdf, 0x68, 0x80);
  normal(gdf, 0x66, {0x00, 0x02});
  normal(gdf, 0xc1, points({40.0, 40.0, 60.0, 40.0, 60.0, 60.0, 40.0, 60.0,
                            40.0, 40.0}));
  normal(gdf, 0x60, {});
  short_order(gdf, 0x68, 0x80);
  normal(gdf, 0xc1, points({70.0, 10.0, 90.0, 10.0, 90.0, 30.0, 70.0, 30.0}));
  normal(gdf, 0x60, {});
  const auto image = geist::detail::decode_gdf_to_rgba(gdf);
  require_pixel(image, 500, 368, white, "line under the white area");
  require_pixel(image, 200, 368, black, "line outside the area");
  require_pixel(image, 800, 587, Rgb{255, 0, 0}, "red area");
  require_pixel(image, 200, 587, black, "second line");
}

// Line type 5 is a 3-on, 3-off dot; 8 hides lines.
void test_line_types() {
  Bytes gdf = header();
  segment(gdf);
  normal(gdf, 0x66, {0xff, 0x07});
  short_order(gdf, 0x58, 0x05);
  normal(gdf, 0xc1, points({0.0, 50.0, 100.0, 50.0}));
  short_order(gdf, 0x58, 0x08);
  normal(gdf, 0xc1, points({0.0, 20.0, 100.0, 20.0}));
  const auto image = geist::detail::decode_gdf_to_rgba(gdf);
  const int y = 368;
  require_pixel(image, 2, y, black, "dot run 1");
  require_pixel(image, 4, y, black, "dot run 1 end");
  require_pixel(image, 5, y, white, "dot gap");
  require_pixel(image, 7, y, white, "dot gap end");
  require_pixel(image, 8, y, black, "dot run 2");
  require_pixel(image, 500, 587, white, "invisible line");
}

// A pending boundary figure (`68 c0`) continues across `81` orders: one
// contour, closed once, so a ring drawn as c1 + 81 fills solid.
void test_figure_contour_continues_at_current_position() {
  Bytes gdf = header();
  segment(gdf);
  normal(gdf, 0x66, {0xff, 0x07});
  short_order(gdf, 0x68, 0xc0);
  normal(gdf, 0xc1, points({20.0, 20.0, 80.0, 20.0}));
  normal(gdf, 0x81, points({80.0, 80.0}));
  normal(gdf, 0x81, points({20.0, 80.0}));
  normal(gdf, 0x60, {});
  const auto image = geist::detail::decode_gdf_to_rgba(gdf);
  require_pixel(image, 500, 368, black, "figure interior");
  require_pixel(image, 100, 368, white, "outside the figure");
}

// Model transform `24`: mode 0 replaces the matrix; mask bits 3 and 2 of the
// second mask byte carry the translation.
void test_model_transform() {
  Bytes gdf = header();
  segment(gdf);
  normal(gdf, 0x66, {0xff, 0x07});
  Bytes transform{0x00, 0x00, 0x00, 0x0c};
  hfp(transform, 50.0);
  hfp(transform, 0.0);
  normal(gdf, 0x24, transform);
  normal(gdf, 0xc1, points({0.0, 10.0, 10.0, 10.0}));
  const auto image = geist::detail::decode_gdf_to_rgba(gdf);
  require_pixel(image, 550, 660, black, "translated line");
  require_pixel(image, 50, 660, white, "untranslated position");
}

// Pattern 7 is the sparsest dot pattern: black pixels on every fourth row
// and column of the canvas, whatever the fill color; the rest of the
// interior stays as it was.  Pattern 11 is a rising one-pixel hatch every
// eight pixels, in the fill color.
void test_patterns_and_hatches() {
  Bytes gdf = header();
  segment(gdf);
  normal(gdf, 0x66, {0x00, 0x04});
  short_order(gdf, 0x09, 0x07);
  short_order(gdf, 0x68, 0x80);
  normal(gdf, 0xc1, points({10.0, 10.0, 40.0, 10.0, 40.0, 40.0, 10.0, 40.0}));
  normal(gdf, 0x60, {});
  normal(gdf, 0x66, {0x00, 0x02});
  short_order(gdf, 0x09, 0x0b);
  short_order(gdf, 0x68, 0x80);
  normal(gdf, 0xc1, points({60.0, 10.0, 90.0, 10.0, 90.0, 40.0, 60.0, 40.0}));
  normal(gdf, 0x60, {});
  const auto image = geist::detail::decode_gdf_to_rgba(gdf);
  require_pixel(image, 200, 600, black, "dot");
  require_pixel(image, 204, 600, black, "next dot");
  require_pixel(image, 201, 600, white, "between dots");
  require_pixel(image, 200, 601, white, "row without dots");
  require_pixel(image, 801, 598, Rgb{255, 0, 0}, "hatch pixel");
  require_pixel(image, 800, 598, white, "beside the hatch");
}

// Push-and-set color, then pop, restores the previous color.
void test_push_pop_color() {
  Bytes gdf = header();
  segment(gdf);
  normal(gdf, 0x66, {0xff, 0x07});
  normal(gdf, 0x66, {0x00, 0x02});
  normal(gdf, 0x3f, {});
  normal(gdf, 0xc1, points({0.0, 50.0, 100.0, 50.0}));
  const auto image = geist::detail::decode_gdf_to_rgba(gdf);
  require_pixel(image, 500, 368, black, "restored color");
}

// --- picture text ------------------------------------------------------------

// A font-list prolog naming one symbol set, `ADMUUDRP`, under local id 0x41.
void font_prolog(Bytes& gdf) {
  normal(gdf, 0x02, {0x01, 0x7e});
  Bytes entry{0x01, 0x40, 0x02, 0x00, 0x41,
              0xc1, 0xc4, 0xd4, 0xe4, 0xe4, 0xc4, 0xd9, 0xd7};
  entry.insert(entry.end(), 8, 0x40);
  normal(gdf, 0x02, entry);
  gdf.push_back(0x00);
  normal(gdf, 0x02, {0x01, 0x7f});
}

// A picture that draws `text` (EBCDIC) at (10, 50) in black with a character
// box `box_height` high, after the given character-angle vector and, when
// `select_set`, a character-set order.
Bytes text_picture(const Bytes& text,
                   bool with_font_list,
                   double box_height = 10.0,
                   double angle_x = 1.0,
                   double angle_y = 0.0,
                   bool select_set = true) {
  Bytes gdf = header();
  if (with_font_list) {
    font_prolog(gdf);
  }
  attribute_prolog(gdf);
  segment(gdf);
  normal(gdf, 0x66, {0xff, 0x07});
  if (select_set) {
    short_order(gdf, 0x78, 0x41);
  }
  normal(gdf, 0x33, points({5.0, box_height}));
  normal(gdf, 0x74, points({angle_x, angle_y}));
  Bytes order = points({10.0, 50.0});
  order.insert(order.end(), text.begin(), text.end());
  normal(gdf, 0xc3, order);
  return gdf;
}

struct InkBox {
  int left = 1 << 20;
  int top = 1 << 20;
  int right = -1;
  int bottom = -1;
  int width() const { return right - left + 1; }
  int height() const { return bottom - top + 1; }
};

InkBox ink_box(const geist::detail::RgbaImage& image) {
  InkBox box;
  for (int y = 0; y < static_cast<int>(image.height); ++y) {
    for (int x = 0; x < static_cast<int>(image.width); ++x) {
      const auto p = pixel(image, x, y);
      if (p.r != 255 || p.g != 255 || p.b != 255) {
        box.left = std::min(box.left, x);
        box.right = std::max(box.right, x);
        box.top = std::min(box.top, y);
        box.bottom = std::max(box.bottom, y);
      }
    }
  }
  if (box.right < 0) {
    throw std::runtime_error("text picture has no ink");
  }
  return box;
}

InkBox text_ink(const Bytes& text,
                bool with_font_list,
                double box_height = 10.0,
                double angle_x = 1.0,
                double angle_y = 0.0,
                bool select_set = true) {
  return ink_box(geist::detail::decode_gdf_to_rgba(text_picture(
      text, with_font_list, box_height, angle_x, angle_y, select_set)));
}

void require_near(int got, int expected, int tolerance, const std::string& label) {
  if (std::abs(got - expected) > tolerance) {
    throw std::runtime_error(label + ": got " + std::to_string(got) +
                             ", expected " + std::to_string(expected));
  }
}

// Once a character-set order selects a font in a picture without a font
// list, strings are set in the reference's fixed-pitch face, so "Wi" and
// " i" end on the same column.  With a font list, or before any
// character-set order, they are set in its proportional face, where a W is
// wider than a space.
void test_text_pitch_follows_the_font_list() {
  const Bytes wide{0xe6, 0x89};
  const Bytes narrow{0x40, 0x89};
  if (text_ink(wide, false).right != text_ink(narrow, false).right) {
    throw std::runtime_error("fixed-pitch text: W and space advance differ");
  }
  if (text_ink(wide, true).right <= text_ink(narrow, true).right) {
    throw std::runtime_error("proportional text: W advances no more than a space");
  }
  if (text_ink(wide, false, 10.0, 1.0, 0.0, false).right <=
      text_ink(narrow, false, 10.0, 1.0, 0.0, false).right) {
    throw std::runtime_error(
        "text before any character-set order is not proportional");
  }
}

// The character box height is the em of the face in device pixels (10
// units = 73 px here): capitals stand about 0.72 em in the proportional face
// and 0.63 em in the fixed-pitch one.  Lower case stays lower case.
void test_text_size_and_case() {
  const Bytes capital_h{0xc8};
  require_near(text_ink(capital_h, true).height(), 52, 1, "proportional cap height");
  require_near(text_ink(capital_h, false).height(), 46, 1, "fixed-pitch cap height");
  const Bytes capital_o{0xd6};
  const Bytes small_o{0x96};
  if (text_ink(small_o, true).height() * 4 > text_ink(capital_o, true).height() * 3) {
    throw std::runtime_error("lower-case o is drawn as a capital");
  }
}

// A character angle of (0, 1) turns the baseline a quarter turn
// counter-clockwise: the string runs up the page from its start point.
void test_text_quarter_turn() {
  const Bytes text{0xc8, 0xc8, 0xc8, 0xc8};
  const auto flat = text_ink(text, true);
  const auto turned = text_ink(text, true, 10.0, 0.0, 1.0);
  require_near(turned.height(), flat.width(), 1, "turned run length");
  require_near(turned.width(), flat.height(), 1, "turned cap height");
  // The text point is (10, 50): device column 102, row 368.  The run climbs
  // from it, its letters' tops to the left.
  if (turned.bottom > 368 || turned.bottom < 345 || turned.top > 368 - 150) {
    throw std::runtime_error("turned text does not climb from its start point: rows " +
                             std::to_string(turned.top) + ".." +
                             std::to_string(turned.bottom));
  }
  if (turned.right > 102 || turned.left < 102 - 60) {
    throw std::runtime_error("turned text stands on the wrong side of its point");
  }
  // The angle is truncated to tenths of a degree first, so a vector a hair
  // off vertical (BLGR8MST picture 12) is still a quarter turn.
  const auto nearly = geist::detail::decode_gdf_to_rgba(
      text_picture(text, true, 10.0, 0.0000447, 1.0));
  const auto exactly =
      geist::detail::decode_gdf_to_rgba(text_picture(text, true, 10.0, 0.0, 1.0));
  if (nearly.rgba != exactly.rgba) {
    throw std::runtime_error("a near-vertical angle is not a quarter turn");
  }
}

// Bytes below 0x40 show as spaces; a zero byte ends the string.
void test_text_control_bytes() {
  const auto spaced =
      geist::detail::decode_gdf_to_rgba(text_picture({0xc1, 0x40, 0xc2}, true));
  const auto controlled =
      geist::detail::decode_gdf_to_rgba(text_picture({0xc1, 0x15, 0xc2}, true));
  if (spaced.rgba != controlled.rgba) {
    throw std::runtime_error("a control byte does not show as a space");
  }
  const auto single = geist::detail::decode_gdf_to_rgba(text_picture({0xc1}, true));
  const auto terminated =
      geist::detail::decode_gdf_to_rgba(text_picture({0xc1, 0x00, 0xc2}, true));
  if (single.rgba != terminated.rgba) {
    throw std::runtime_error("a zero byte does not end the string");
  }
}

// Text this decoder cannot set as the reference does fails closed.
void test_text_fails_closed() {
  require_rejected(text_picture({0xc1, 0xb0}, true), "character",
                   "character outside the stroke font");
  require_rejected(text_picture({0xc1}, true, 0.0), "character box",
                   "zero character box");
  require_rejected(text_picture({0xc1}, true, 10.0, 1.0, 1.0),
                   "rotated characters", "diagonal character angle");
  Bytes spaced = header();
  segment(spaced);
  normal(spaced, 0x66, {0xff, 0x07});
  Bytes spacing{0x00, 0x00};
  hfp(spacing, 1.0);
  hfp(spacing, 0.0);
  normal(spaced, 0x76, spacing);
  Bytes order = points({10.0, 50.0});
  order.push_back(0xc1);
  normal(spaced, 0xc3, order);
  require_rejected(spaced, "character spacing", "character spacing");
}

// --- markers -----------------------------------------------------------------

Bytes marker_picture(const std::function<void(Bytes&)>& add) {
  Bytes gdf = header();
  segment(gdf);
  normal(gdf, 0x66, {0xff, 0x07});
  add(gdf);
  return gdf;
}

// Marker type 2 is an upright cross and type 1 a diagonal one, ten pixels
// across around the point, each stroke without its final pixel.  `82` marks
// the current position too, and markers move the current position to their
// last point.
void test_markers() {
  // (50, 50) maps to (500, 368); (20, 20) to (201, 587).
  const auto plus = geist::detail::decode_gdf_to_rgba(marker_picture([](Bytes& g) {
    short_order(g, 0x29, 0x02);
    normal(g, 0xc2, points({50.0, 50.0}));
  }));
  require_pixel(plus, 500, 363, black, "plus top");
  require_pixel(plus, 500, 372, black, "plus bottom");
  require_pixel(plus, 500, 373, white, "plus end excluded");
  require_pixel(plus, 495, 368, black, "plus left");
  require_pixel(plus, 504, 368, black, "plus right");
  require_pixel(plus, 498, 366, white, "plus has no diagonal");
  const auto cross = geist::detail::decode_gdf_to_rgba(marker_picture([](Bytes& g) {
    short_order(g, 0x29, 0x01);
    normal(g, 0x21, points({20.0, 20.0}));
    normal(g, 0x82, points({50.0, 50.0}));
    normal(g, 0x81, points({50.0, 20.0}));
  }));
  require_pixel(cross, 495, 363, black, "cross corner");
  require_pixel(cross, 504, 372, black, "cross far corner");
  require_pixel(cross, 495, 373, black, "cross other corner");
  require_pixel(cross, 500, 363, white, "cross has no upright");
  require_pixel(cross, 196, 582, black, "current position marked");
  require_pixel(cross, 500, 450, black, "line from the last marker");
}

void test_markers_fail_closed() {
  require_rejected(marker_picture([](Bytes& g) {
                     normal(g, 0xc2, points({50.0, 50.0}));
                   }),
                   "marker type", "marker before a marker-type order");
  require_rejected(marker_picture([](Bytes& g) {
                     short_order(g, 0x29, 0x03);
                     normal(g, 0xc2, points({50.0, 50.0}));
                   }),
                   "marker type", "marker type 3");
  require_rejected(marker_picture([](Bytes& g) {
                     short_order(g, 0x29, 0x02);
                     normal(g, 0x77, points({2.0, 2.0}));
                     normal(g, 0x3f, {});
                     normal(g, 0xc2, points({50.0, 50.0}));
                   }),
                   "marker size", "marker after a restored marker scale");
}

// Orders the reference renderer draws but this decoder cannot reproduce
// reject the whole picture.
void test_unsupported_orders_fail_closed() {
  const auto with = [](const std::function<void(Bytes&)>& add) {
    Bytes gdf = header();
    segment(gdf);
    normal(gdf, 0x66, {0xff, 0x07});
    normal(gdf, 0xc1, points({0.0, 50.0, 100.0, 50.0}));
    add(gdf);
    return gdf;
  };
  require_rejected(with([](Bytes& g) { normal(g, 0x07, {0, 0, 0, 1}); }),
                   "segment call", "segment call");
  require_rejected(with([](Bytes& g) { normal(g, 0xc2, points({1.0, 1.0})); }),
                   "marker", "marker");
  require_rejected(with([](Bytes& g) { short_order(g, 0x09, 0x45); }),
                   "pattern", "color pattern");
  require_rejected(with([](Bytes& g) {
                     short_order(g, 0x0d, 0x02);
                     short_order(g, 0x09, 0x07);
                     short_order(g, 0x68, 0x80);
                   }),
                   "opaque pattern background", "opaque pattern");
  require_rejected(with([](Bytes& g) { short_order(g, 0x19, 0x02); }),
                   "line width", "line width");
  require_rejected(with([](Bytes& g) { normal(g, 0x27, {0, 0}); }),
                   "clipping", "clip");
  require_rejected(with([](Bytes& g) { normal(g, 0x91, {0, 8, 0, 8}); }),
                   "graphics image", "image");
  require_rejected(with([](Bytes& g) {
                     normal(g, 0x34, points({1.0, -1.0}));
                   }),
                   "rotated characters", "character angle");
  require_rejected(with([](Bytes& g) { short_order(g, 0x68, 0x80); }),
                   "inside an area", "unterminated area");
  Bytes two_byte{0x01, 0x0a, 0x00, 0x02, 0, 0, 0, 100, 0, 0, 0, 100};
  require_rejected(two_byte, "coordinate format", "two-byte coordinates");
}

// Orders with no effect on the reference canvas are skipped: fillets,
// comments and undefined codes.
void test_inert_orders_are_skipped() {
  Bytes gdf = header();
  segment(gdf);
  normal(gdf, 0x66, {0xff, 0x07});
  normal(gdf, 0xc5, points({0.0, 0.0, 50.0, 50.0, 100.0, 0.0}));
  normal(gdf, 0x01, {0x01, 0x02});
  normal(gdf, 0xf4, {0x01, 0x02});
  normal(gdf, 0xc1, points({0.0, 50.0, 100.0, 50.0}));
  const auto image = geist::detail::decode_gdf_to_rgba(gdf);
  require_pixel(image, 500, 368, black, "line after inert orders");
  require_pixel(image, 500, 368 - 20, white, "no fillet stroke");
}

Bytes oversized_gif_header() {
  Bytes gif{'G', 'I', 'F', '8', '9', 'a'};
  gif.insert(gif.end(), {0xff, 0xff, 0xff, 0xff, 0, 0, 0});
  return gif;
}

Bytes one_pixel_gif() {
  return {'G',  'I',  'F',  '8',  '9',  'a',  0x01, 0x00, 0x01, 0x00,
          0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x2c,
          0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02,
          0x02, 0x44, 0x01, 0x00, 0x3b};
}

Bytes oversized_png_header() {
  Bytes png{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  be32(png, 13);
  png.insert(png.end(), {'I', 'H', 'D', 'R'});
  be32(png, 8192);
  be32(png, 8192);
  return png;
}

Bytes oversized_mmr_header() {
  Bytes mmr(0x50, 0);
  mmr[0x42] = 0x20;
  mmr[0x44] = 0x20;
  return mmr;
}

void require_image_limit_rejection(const geist::ResourceEntry& resource,
                                   const Bytes& bytes,
                                   const char* label) {
  try {
    (void)geist::detail::render_resource_png(resource, bytes);
  } catch (const std::runtime_error& error) {
    if (std::string(error.what()).find("pixel limit") != std::string::npos ||
        std::string(error.what()).find("unsupported dimensions") !=
            std::string::npos) {
      return;
    }
  }
  throw std::runtime_error(std::string(label) + " image limit was not enforced");
}

void test_other_formats() {
  geist::ResourceEntry resource;
  resource.stored_format = "image/gif";
  const auto converted_gif =
      geist::detail::render_resource_png(resource, one_pixel_gif());
  if (converted_gif.size() < 8 || converted_gif[0] != 0x89 ||
      converted_gif[1] != 'P' || converted_gif[2] != 'N' ||
      converted_gif[3] != 'G') {
    throw std::runtime_error("valid GIF did not convert to PNG");
  }
  require_image_limit_rejection(resource, oversized_gif_header(), "GIF");
  resource.stored_format = "image/png";
  require_image_limit_rejection(resource, oversized_png_header(), "PNG");
  resource.stored_format = "legacy-mmr";
  require_image_limit_rejection(resource, oversized_mmr_header(), "MMR");
}

} // namespace

int main() {
  try {
    test_extended_color_swap_and_window();
    test_blank_render_fails_closed();
    test_area_color_and_stream_order();
    test_line_types();
    test_figure_contour_continues_at_current_position();
    test_model_transform();
    test_push_pop_color();
    test_patterns_and_hatches();
    test_text_pitch_follows_the_font_list();
    test_text_size_and_case();
    test_text_quarter_turn();
    test_text_control_bytes();
    test_text_fails_closed();
    test_markers();
    test_markers_fail_closed();
    test_unsupported_orders_fail_closed();
    test_inert_orders_are_skipped();
    test_other_formats();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "gdf_synthetic: " << error.what() << "\n";
    return 1;
  }
}
