// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/core/internal.hpp"

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
                     normal(g, 0x34, points({0.0, 1.0}));
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
    test_unsupported_orders_fail_closed();
    test_inert_orders_are_skipped();
    test_other_formats();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "gdf_synthetic: " << error.what() << "\n";
    return 1;
  }
}
