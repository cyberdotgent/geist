// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "img/gdf.hpp"

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

std::array<std::uint8_t, 7> glyph_rows(char ch) {
  if (ch >= 'a' && ch <= 'z') {
    ch = static_cast<char>(ch - 'a' + 'A');
  }

  switch (ch) {
  case 'A': return {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
  case 'B': return {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
  case 'C': return {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e};
  case 'D': return {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
  case 'E': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
  case 'F': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
  case 'G': return {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f};
  case 'H': return {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
  case 'I': return {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e};
  case 'J': return {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c};
  case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
  case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
  case 'M': return {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11};
  case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
  case 'O': return {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
  case 'P': return {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
  case 'Q': return {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d};
  case 'R': return {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
  case 'S': return {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
  case 'T': return {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
  case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
  case 'V': return {0x11, 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04};
  case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11};
  case 'X': return {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11};
  case 'Y': return {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04};
  case 'Z': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f};
  case '0': return {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e};
  case '1': return {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e};
  case '2': return {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f};
  case '3': return {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e};
  case '4': return {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02};
  case '5': return {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e};
  case '6': return {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e};
  case '7': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
  case '8': return {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e};
  case '9': return {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c};
  case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c};
  case ',': return {0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08};
  case '-': return {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00};
  case '+': return {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00};
  case '/': return {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
  case ':': return {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00};
  case ';': return {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x04, 0x08};
  case '%': return {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03};
  case '\'': return {0x0c, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00};
  case '"': return {0x0a, 0x0a, 0x14, 0x00, 0x00, 0x00, 0x00};
  case '(': return {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
  case ')': return {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
  case '[': return {0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e};
  case ']': return {0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e};
  case ' ': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  default: return {0x1f, 0x11, 0x02, 0x04, 0x04, 0x00, 0x04};
  }
}

void set_block(RgbaImage& image, int x, int y, int size, Color color) {
  for (int yy = 0; yy < size; ++yy) {
    for (int xx = 0; xx < size; ++xx) {
      set_pixel(image, x + xx, y + yy, color);
    }
  }
}

int glyph_advance(char ch, const TextRun& text, int pixel_size) {
  if (text.monospaced || ch == ' ') {
    return 6 * pixel_size;
  }
  switch (ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch) {
  case 'I':
  case '1':
  case '.':
  case ',':
  case ':':
  case ';':
  case '\'':
    return 4 * pixel_size;
  case 'M':
  case 'W':
    return 7 * pixel_size;
  default:
    return 6 * pixel_size;
  }
}

void draw_glyph(RgbaImage& image,
                int origin_x,
                int baseline_y,
                int pixel_size,
                char ch,
                const TextRun& text) {
  const auto rows = glyph_rows(ch);
  for (int row = 0; row < 7; ++row) {
    const int slant = text.italic ? (6 - row) * pixel_size / 3 : 0;
    for (int col = 0; col < 5; ++col) {
      if ((rows[static_cast<std::size_t>(row)] & (1u << (4 - col))) == 0) {
        continue;
      }
      const int x = origin_x + slant + col * pixel_size;
      const int y = baseline_y - (7 - row) * pixel_size;
      set_block(image, x, y, pixel_size, text.color);
      if (text.bold) {
        set_block(image, x + std::max(1, pixel_size / 3), y, pixel_size, text.color);
      }
    }
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
      const auto [x0, y0] = map(text.point);
      const auto scaled_size = text.height * scale_y / 8.0;
      if (!std::isfinite(scaled_size)) {
        throw std::runtime_error("GDF asset has an invalid text size");
      }
      const int pixel_size = std::clamp(
          static_cast<int>(std::lround(std::min(scaled_size, 64.0))), 1, 64);
      charge(text.text.size(), static_cast<std::size_t>(pixel_size) *
                                   pixel_size * 70);
      int x = x0;
      for (std::size_t i = 0; i < text.text.size(); ++i) {
        const char ch = text.text[i];
        draw_glyph(image, x, y0, pixel_size, ch, text);
        x = std::clamp(x + glyph_advance(ch, text, pixel_size), -1,
                       static_cast<int>(image.width));
      }
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
