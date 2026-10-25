// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "img/gdf.hpp"
#include "img/gdf_font.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace geist::detail {

namespace {

void set_pixel(RgbaImage& image, int x, int y, Color color) {
  if (x < 0 || y < 0 || x >= static_cast<int>(image.width) ||
      y >= static_cast<int>(image.height)) {
    return;
  }
  const auto offset =
      (static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) *
      4;
  image.rgba[offset] = color.r;
  image.rgba[offset + 1] = color.g;
  image.rgba[offset + 2] = color.b;
  image.rgba[offset + 3] = color.a;
}

// On/off run lengths, in pixels, of each dash pattern.  The pattern runs on
// along a polyline across its vertices.
const std::vector<int>& dash_pattern(GdfLineStyle style) {
  static const std::vector<int> solid{};
  static const std::vector<int> dash{18, 6};
  static const std::vector<int> dot{3, 3};
  static const std::vector<int> dash_dot{9, 6, 3, 6};
  static const std::vector<int> dash_dot_dot{9, 3, 3, 3, 3, 3};
  switch (style) {
  case GdfLineStyle::dash:
    return dash;
  case GdfLineStyle::dot:
    return dot;
  case GdfLineStyle::dash_dot:
    return dash_dot;
  case GdfLineStyle::dash_dot_dot:
    return dash_dot_dot;
  case GdfLineStyle::solid:
    break;
  }
  return solid;
}

struct Pen {
  Color color;
  const std::vector<int>* pattern = nullptr;
  int period = 0;
  int phase = 0;

  bool on() const {
    if (pattern == nullptr || pattern->empty()) {
      return true;
    }
    int at = phase % period;
    for (std::size_t i = 0; i < pattern->size(); ++i) {
      const int run = (*pattern)[i];
      if (at < run) {
        return i % 2 == 0;
      }
      at -= run;
    }
    return true;
  }
};

Pen make_pen(Color color, GdfLineStyle style) {
  Pen pen;
  pen.color = color;
  pen.pattern = &dash_pattern(style);
  for (const int run : *pen.pattern) {
    pen.period += run;
  }
  return pen;
}

// One segment, Bresenham, without its final pixel: a polyline's next
// segment starts there, and the last point of a polyline is not drawn.
void draw_segment(RgbaImage& image, int x0, int y0, int x1, int y1, Pen& pen) {
  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (x0 != x1 || y0 != y1) {
    if (pen.on()) {
      set_pixel(image, x0, y0, pen.color);
    }
    ++pen.phase;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void draw_polyline(RgbaImage& image,
                   const std::vector<std::pair<int, int>>& points,
                   Pen pen) {
  for (std::size_t i = 1; i < points.size(); ++i) {
    draw_segment(image, points[i - 1].first, points[i - 1].second,
                 points[i].first, points[i].second, pen);
  }
}

// --- picture text ------------------------------------------------------------
//
// Text is set in the Hershey Roman simplex stroke face, sized and spaced to
// stand in for the reference's two faces; the em is the character box height
// in device pixels.  The proportions were fitted on hosted renders of
// BLGR8MST, GC24-5673-01 and packet.boo:
// - fixed pitch: capitals round(0.631 em) pixels tall, every character
//   advancing round(0.566 em), strokes 0.05 em wide (at least one pixel);
// - proportional: capitals 0.716 em tall, strokes 0.08 em wide, a space and
//   a period or comma advancing 0.278 em, a digit 0.556 em, a hyphen
//   0.333 em, a capital 1.1 times its own width, a small letter 0.95 and
//   anything else 0.97 times; each glyph is scaled across to its advance.

struct TextMetrics {
  double em = 0.0;
  // Device pixels per Hershey unit.
  double unit = 1.0;
  // The advance of every character in the fixed-pitch face; 0 in the
  // proportional face.
  double pitch = 0.0;
  // The width of the square pen, in pixels.
  int pen = 1;
};

TextMetrics text_metrics(const TextRun& text, int em) {
  TextMetrics metrics;
  metrics.em = em;
  if (text.face == GdfTextFace::fixed_pitch) {
    metrics.pen = std::max(1, static_cast<int>(std::lround(em * 0.05)));
    metrics.unit = std::max(0.0, std::round(em * 0.631) - metrics.pen) /
                   stroke_cap_height;
    metrics.pitch = std::round(em * 0.566);
  } else {
    metrics.pen = std::max(1, static_cast<int>(std::lround(em * 0.08)));
    metrics.unit = std::max(0.0, em * 0.716 - metrics.pen) / stroke_cap_height;
  }
  return metrics;
}

double glyph_width(const StrokeGlyph& glyph, const TextMetrics& metrics) {
  return (glyph.right - glyph.left) * metrics.unit;
}

double glyph_advance(char ch, const StrokeGlyph& glyph, const TextMetrics& metrics) {
  if (metrics.pitch > 0.0) {
    return metrics.pitch;
  }
  if (ch == ' ' || ch == '.' || ch == ',') {
    return 0.278 * metrics.em;
  }
  if (ch >= '0' && ch <= '9') {
    return 0.556 * metrics.em;
  }
  if (ch == '-') {
    return 0.333 * metrics.em;
  }
  if (ch >= 'A' && ch <= 'Z') {
    return 1.1 * glyph_width(glyph, metrics);
  }
  if (ch >= 'a' && ch <= 'z') {
    return 0.95 * glyph_width(glyph, metrics);
  }
  return 0.97 * glyph_width(glyph, metrics);
}

void stamp(RgbaImage& image, int x, int y, int pen, Color color) {
  const int from = -(pen - 1) / 2;
  for (int dy = 0; dy < pen; ++dy) {
    for (int dx = 0; dx < pen; ++dx) {
      set_pixel(image, x + from + dx, y + from + dy, color);
    }
  }
}

void draw_stroke_segment(RgbaImage& image,
                         std::pair<int, int> from,
                         std::pair<int, int> to,
                         int pen,
                         Color color) {
  auto [x0, y0] = from;
  const auto [x1, y1] = to;
  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    stamp(image, x0, y0, pen, color);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

// The total stroke length of a run in device pixels, for the raster work
// charge.
double text_stroke_length(const TextRun& text, const TextMetrics& metrics) {
  double length = 0.0;
  for (const char ch : text.text) {
    const auto* glyph = stroke_glyph(ch);
    for (std::size_t i = 1; glyph != nullptr && i < glyph->count; ++i) {
      const auto& a = glyph->vertices[i - 1];
      const auto& b = glyph->vertices[i];
      if (a.x != stroke_pen_up && b.x != stroke_pen_up) {
        length += (std::abs(a.x - b.x) + std::abs(a.y - b.y)) * metrics.unit + 1.0;
      }
    }
  }
  return length;
}

// Draws a run whose baseline starts at device pixel (x, y).  Glyphs stand on
// the row above that pixel; a turned run turns about it.
void draw_text(RgbaImage& image,
               const TextRun& text,
               int x,
               int y,
               const TextMetrics& metrics) {
  // A wide pen is centred on the stroke, so the strokes move in by half its
  // width to keep the ink on the glyph's side of the baseline.
  const double half_pen = (metrics.pen - 1) / 2;
  // Glyph-local coordinates: `along` the baseline, `up` above the row the
  // glyphs stand on.
  const auto place = [&](double along, double up) {
    const double ux = along + half_pen;
    const double uy = up + 1.0 + half_pen;
    double dx = ux;
    double dy = -uy;
    switch (text.quarter_turns & 3) {
    case 1:
      dx = -uy;
      dy = -ux;
      break;
    case 2:
      dx = -ux;
      dy = uy;
      break;
    case 3:
      dx = uy;
      dy = ux;
      break;
    default:
      break;
    }
    return std::pair<int, int>{x + static_cast<int>(std::lround(dx)),
                               y + static_cast<int>(std::lround(dy))};
  };

  double pen_x = 0.0;
  for (const char ch : text.text) {
    const auto* glyph = stroke_glyph(ch);
    if (glyph == nullptr) {
      throw std::runtime_error("GDF text has a character without a glyph");
    }
    const double width = glyph_width(*glyph, metrics);
    const double advance = glyph_advance(ch, *glyph, metrics);
    // Fixed pitch centres each glyph in its cell; proportional text scales
    // each glyph across to its advance.
    const double inset = metrics.pitch > 0.0 ? (metrics.pitch - width) / 2.0 : 0.0;
    const double squeeze =
        metrics.pitch == 0.0 && width > 0.0 ? advance / width : 1.0;
    bool have_previous = false;
    std::pair<int, int> previous;
    for (std::size_t i = 0; i < glyph->count; ++i) {
      const auto& v = glyph->vertices[i];
      if (v.x == stroke_pen_up) {
        have_previous = false;
        continue;
      }
      const auto point =
          place(pen_x + inset + (v.x - glyph->left) * metrics.unit * squeeze,
                (stroke_baseline - v.y) * metrics.unit);
      if (have_previous) {
        draw_stroke_segment(image, previous, point, metrics.pen, text.color);
      }
      previous = point;
      have_previous = true;
    }
    pen_x += advance;
  }
}

// Fills a figure: all its contours together, each implicitly closed.  An edge
// and its exact reverse cancel (stroke-font construction edges run out and
// back), and the rest fill by the non-zero winding rule.
// The dot patterns, densities 1..7: four rows of eight pixels, most
// significant bit leftmost, repeating every 4 rows and 8 columns of the
// canvas.  Canvas row y uses row 3 - (y mod 4).
constexpr std::array<std::array<std::uint8_t, 4>, 7> dot_patterns{{
    {0x00, 0x00, 0x00, 0x88},
    {0x00, 0xaa, 0x44, 0xaa},
    {0x00, 0x22, 0x00, 0xaa},
    {0x11, 0xaa, 0x55, 0xaa},
    {0x55, 0xaa, 0x55, 0xee},
    {0x55, 0xbb, 0x55, 0xff},
    {0x55, 0xff, 0xdd, 0xff},
}};

bool interior_inked(const FilledArea& area, int x, int y) {
  switch (area.interior) {
  case GdfInterior::pattern: {
    const auto& rows =
        dot_patterns[static_cast<std::size_t>(std::clamp(area.pattern_density, 1, 7) - 1)];
    const auto row = rows[static_cast<std::size_t>(3 - (y & 3))];
    return (row & (0x80u >> (x & 7))) != 0;
  }
  case GdfInterior::hatch:
    // One-pixel lines eight pixels apart, aligned to the canvas.
    switch (area.hatch) {
    case GdfHatch::rising:
      return ((x + y) & 7) == 7;
    case GdfHatch::falling:
      return ((x - y) & 7) == 0;
    case GdfHatch::horizontal:
      return (y & 7) == 3;
    case GdfHatch::vertical:
      return (x & 7) == 4;
    }
    return true;
  default:
    return true;
  }
}

void fill_area(RgbaImage& image,
               const std::vector<std::vector<std::pair<int, int>>>& contours,
               const FilledArea& area) {
  const Color fill = area.interior == GdfInterior::pattern
                         ? Color{0, 0, 0, 255}
                         : area.fill;
  struct Edge {
    int x0;
    int y0;
    int x1;
    int y1;
  };

  std::vector<Edge> edges;
  std::map<std::array<int, 4>, std::vector<std::size_t>> pending_edges;
  int min_y = static_cast<int>(image.height);
  int max_y = 0;
  for (const auto& pts : contours) {
    for (std::size_t i = 0; i < pts.size(); ++i) {
      const auto [x0, y0] = pts[i];
      const auto [x1, y1] = pts[(i + 1) % pts.size()];
      min_y = std::min(min_y, y0);
      max_y = std::max(max_y, y0);
      if (x0 == x1 && y0 == y1) {
        continue;
      }
      const std::array<int, 4> reverse_key{x1, y1, x0, y0};
      const auto reverse = pending_edges.find(reverse_key);
      if (reverse != pending_edges.end() && !reverse->second.empty()) {
        edges[reverse->second.back()] = {0, 0, 0, 0};
        reverse->second.pop_back();
        if (reverse->second.empty()) {
          pending_edges.erase(reverse);
        }
        continue;
      }

      const auto index = edges.size();
      edges.push_back(Edge{x0, y0, x1, y1});
      pending_edges[{x0, y0, x1, y1}].push_back(index);
    }
  }

  min_y = std::max(0, min_y);
  max_y = std::min(static_cast<int>(image.height) - 1, max_y);
  for (int y = min_y; y <= max_y; ++y) {
    std::vector<std::pair<int, int>> nodes;
    for (const auto& edge : edges) {
      if (edge.x0 == edge.x1 && edge.y0 == edge.y1) {
        continue;
      }
      if ((edge.y0 < y && edge.y1 >= y) ||
          (edge.y1 < y && edge.y0 >= y)) {
        nodes.emplace_back(edge.x0 + (y - edge.y0) * (edge.x1 - edge.x0) /
                                         (edge.y1 - edge.y0),
                           edge.y1 > edge.y0 ? 1 : -1);
      }
    }
    std::sort(nodes.begin(), nodes.end());
    int winding = 0;
    int begin = 0;
    for (const auto& [x, direction] : nodes) {
      if (winding == 0) {
        begin = x;
      }
      winding += direction;
      if (winding == 0) {
        const int fill_begin = std::max(0, begin);
        const int fill_end = std::min(static_cast<int>(image.width) - 1, x);
        for (int xx = fill_begin; xx <= fill_end; ++xx) {
          if (interior_inked(area, xx, y)) {
            set_pixel(image, xx, y, fill);
          }
        }
      }
    }
  }
}

} // namespace

RgbaImage decode_gdf_to_rgba(const std::vector<std::uint8_t>& bytes) {
  const auto picture = parse_gdf_picture(bytes);

  RgbaImage image;
  image.width = gdf_canvas_width;
  image.height = gdf_canvas_height;
  image.rgba.assign(static_cast<std::size_t>(image.width) * image.height * 4,
                    255);

  constexpr std::size_t max_raster_work = 64u * 1024u * 1024u;
  std::size_t raster_work = 0;
  const auto charge = [&](std::size_t count, std::size_t cost) {
    if (count != 0 &&
        (cost > max_raster_work ||
         count > (max_raster_work - raster_work) / cost)) {
      throw std::runtime_error("GDF asset exceeds the raster work limit");
    }
    raster_work += count * cost;
  };

  // The picture window maps onto the drawing box; y grows upwards.
  const double scale_x = static_cast<double>(gdf_box_right - gdf_box_left) /
                         ((picture.max_x - picture.min_x) * picture.aspect_x);
  const double scale_y = static_cast<double>(gdf_box_bottom - gdf_box_top) /
                         ((picture.max_y - picture.min_y) * picture.aspect_y);
  const double origin_x = picture.min_x * picture.aspect_x;
  const double origin_y = picture.min_y * picture.aspect_y;
  const auto to_pixel = [](double value) {
    if (!std::isfinite(value)) {
      throw std::runtime_error("GDF asset maps a coordinate outside the raster");
    }
    return static_cast<int>(std::floor(std::clamp(value, -100000.0, 100000.0) + 0.5));
  };
  const auto map = [&](const Point& p) {
    return std::pair<int, int>{
        to_pixel(gdf_box_left + (p.x - origin_x) * scale_x),
        to_pixel(gdf_box_bottom - (p.y - origin_y) * scale_y)};
  };
  // A stroke costs the pixels it steps through.
  const auto charge_stroke = [&](const std::vector<std::pair<int, int>>& pts) {
    for (std::size_t i = 1; i < pts.size(); ++i) {
      const auto dx = std::abs(static_cast<long long>(pts[i].first) - pts[i - 1].first);
      const auto dy = std::abs(static_cast<long long>(pts[i].second) - pts[i - 1].second);
      charge(1, static_cast<std::size_t>(std::max(dx, dy)) + 1);
    }
  };
  const auto map_all = [&](const std::vector<Point>& points) {
    std::vector<std::pair<int, int>> out;
    out.reserve(points.size());
    for (const auto& point : points) {
      out.push_back(map(point));
    }
    return out;
  };

  for (const auto& step : picture.steps) {
    switch (step.kind) {
    case GdfPrimitive::line: {
      const auto& line = picture.lines[step.index];
      const auto mapped = map_all(line.points);
      charge_stroke(mapped);
      draw_polyline(image, mapped, make_pen(line.color, line.style));
      break;
    }
    case GdfPrimitive::area: {
      const auto& area = picture.areas[step.index];
      std::vector<std::vector<std::pair<int, int>>> contours;
      std::size_t points = 1;
      int top = static_cast<int>(image.height);
      int bottom = -1;
      for (const auto& contour : area.contours) {
        contours.push_back(map_all(contour));
        points += contour.size();
        for (const auto& point : contours.back()) {
          top = std::min(top, point.second);
          bottom = std::max(bottom, point.second);
        }
      }
      if (area.filled) {
        // Scan conversion visits each covered row once per edge.
        const auto rows = static_cast<std::size_t>(
            std::max(0, std::min(bottom, static_cast<int>(image.height) - 1) -
                            std::max(top, 0) + 1));
        charge(rows, image.width);
        charge(points, rows + 1);
        fill_area(image, contours, area);
      }
      if (area.draw_boundary) {
        for (auto contour : contours) {
          contour.push_back(contour.front());
          charge_stroke(contour);
          draw_polyline(image, contour, make_pen(area.edge, area.edge_style));
        }
      }
      break;
    }
    case GdfPrimitive::text: {
      const auto& text = picture.texts[step.index];
      const double em_size = text.height * scale_y;
      if (!std::isfinite(em_size) || em_size > 4096.0) {
        throw std::runtime_error("GDF asset has an invalid text size");
      }
      const int em = std::max(1, static_cast<int>(std::lround(em_size)));
      const auto metrics = text_metrics(text, em);
      const auto strokes = static_cast<std::size_t>(text_stroke_length(text, metrics)) + 1;
      charge(strokes, static_cast<std::size_t>(metrics.pen) * metrics.pen);
      const auto [x0, y0] = map(text.point);
      draw_text(image, text, x0, y0, metrics);
      break;
    }
    }
  }

  // A picture with drawing orders that leaves the canvas blank lost them.
  bool inked = false;
  for (std::size_t i = 0; i < image.rgba.size() && !inked; i += 4) {
    inked = image.rgba[i] != 255 || image.rgba[i + 1] != 255 ||
            image.rgba[i + 2] != 255;
  }
  if (!inked) {
    throw std::runtime_error("GDF asset draws nothing visible on the canvas");
  }
  return image;
}

} // namespace geist::detail
