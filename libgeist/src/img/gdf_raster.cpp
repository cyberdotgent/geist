#include "img/gdf.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
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

void draw_line(RgbaImage& image, int x0, int y0, int x1, int y1, Color color) {
  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  for (;;) {
    set_pixel(image, x0, y0, color);
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

template <typename MapX, typename MapY>
void fill_polygon(RgbaImage& image,
                  const Polygon& polygon,
                  MapX map_x,
                  MapY map_y) {
  if (polygon.points.size() < 3) {
    return;
  }
  std::vector<std::pair<int, int>> pts;
  pts.reserve(polygon.points.size());
  for (const auto& point : polygon.points) {
    pts.emplace_back(map_x(point.x), map_y(point.y));
  }
  int min_y = pts.front().second;
  int max_y = pts.front().second;
  for (const auto& [x, y] : pts) {
    (void)x;
    min_y = std::min(min_y, y);
    max_y = std::max(max_y, y);
  }
  for (int y = min_y; y <= max_y; ++y) {
    std::vector<int> nodes;
    for (std::size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
      const auto [xi, yi] = pts[i];
      const auto [xj, yj] = pts[j];
      if ((yi < y && yj >= y) || (yj < y && yi >= y)) {
        nodes.push_back(xi + (y - yi) * (xj - xi) / (yj - yi));
      }
    }
    std::sort(nodes.begin(), nodes.end());
    for (std::size_t i = 0; i + 1 < nodes.size(); i += 2) {
      for (int x = nodes[i]; x <= nodes[i + 1]; ++x) {
        set_pixel(image, x, y, polygon.fill);
      }
    }
  }
  for (std::size_t i = 0; i < pts.size(); ++i) {
    const auto [x0, y0] = pts[i];
    const auto [x1, y1] = pts[(i + 1) % pts.size()];
    draw_line(image, x0, y0, x1, y1, polygon.edge);
  }
}

template <typename MapX, typename MapY>
void fill_area(RgbaImage& image,
               const FilledArea& area,
               MapX map_x,
               MapY map_y) {
  std::vector<std::vector<std::pair<int, int>>> contours;
  std::vector<double> segment_lengths;
  int min_y = static_cast<int>(image.height);
  int max_y = 0;
  for (const auto& contour : area.contours) {
    if (contour.size() < 2) {
      continue;
    }
    auto& pts = contours.emplace_back();
    pts.reserve(contour.size());
    for (const auto& point : contour) {
      pts.emplace_back(map_x(point.x), map_y(point.y));
      min_y = std::min(min_y, pts.back().second);
      max_y = std::max(max_y, pts.back().second);
    }
    for (std::size_t i = 1; i < contour.size(); ++i) {
      const double dx = contour[i].x - contour[i - 1].x;
      const double dy = contour[i].y - contour[i - 1].y;
      segment_lengths.push_back(std::sqrt(dx * dx + dy * dy));
    }
  }
  if (contours.empty()) {
    return;
  }

  double connector_threshold = std::numeric_limits<double>::infinity();
  if (segment_lengths.size() > 20) {
    auto sorted_lengths = segment_lengths;
    std::sort(sorted_lengths.begin(), sorted_lengths.end());
    const double median = sorted_lengths[sorted_lengths.size() / 2];
    if (median < 1.0) {
      // Packet vector-font areas contain long pen-up connectors among tiny
      // outline segments; ImageMark does not fill across those connectors.
      connector_threshold = std::max(3.4, median * 8.0);
    }
  }

  auto is_connector = [&](const std::pair<int, int>& p0,
                          const std::pair<int, int>& p1) {
    if (!std::isfinite(connector_threshold)) {
      return false;
    }
    const double dx = static_cast<double>(p1.first - p0.first);
    const double dy = static_cast<double>(p1.second - p0.second);
    return std::sqrt(dx * dx + dy * dy) >
           connector_threshold * std::min(map_x(1.0) - map_x(0.0),
                                          map_y(0.0) - map_y(1.0));
  };

  min_y = std::max(0, min_y);
  max_y = std::min(static_cast<int>(image.height) - 1, max_y);
  for (int y = min_y; y <= max_y; ++y) {
    std::vector<std::pair<int, int>> nodes;
    for (const auto& pts : contours) {
      for (std::size_t i = 1; i < pts.size(); ++i) {
        const auto [x0, y0] = pts[i - 1];
        const auto [x1, y1] = pts[i];
        if (is_connector(pts[i - 1], pts[i])) {
          continue;
        }
        if ((y0 < y && y1 >= y) || (y1 < y && y0 >= y)) {
          nodes.emplace_back(x0 + (y - y0) * (x1 - x0) / (y1 - y0),
                             y1 > y0 ? 1 : -1);
        }
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
          set_pixel(image, xx, y, area.fill);
        }
      }
    }
  }

  if (area.draw_boundary) {
    for (const auto& pts : contours) {
      for (std::size_t i = 1; i < pts.size(); ++i) {
        if (is_connector(pts[i - 1], pts[i])) {
          continue;
        }
        draw_line(image,
                  pts[i - 1].first,
                  pts[i - 1].second,
                  pts[i].first,
                  pts[i].second,
                  area.edge);
      }
    }
  }
}

} // namespace

RgbaImage decode_gdf_to_rgba(const std::vector<std::uint8_t>& bytes) {
  const auto picture = parse_gdf_picture(bytes);

  constexpr std::uint32_t output_width = 1004;
  constexpr std::uint32_t output_height = 735;
  const auto width_units = std::max(1.0, picture.max_x - picture.min_x);
  const auto height_units = std::max(1.0, picture.max_y - picture.min_y);
  const auto scale_x = static_cast<double>(output_width) / width_units;
  const auto scale_y = static_cast<double>(output_height) / height_units;

  RgbaImage image;
  image.width = output_width;
  image.height = output_height;
  image.rgba.assign(static_cast<std::size_t>(image.width) * image.height * 4,
                    255);

  auto map_x = [&](double x) {
    return static_cast<int>(std::lround((x - picture.min_x) * scale_x));
  };
  auto map_y = [&](double y) {
    return static_cast<int>(image.height) - 1 -
           static_cast<int>(std::lround((y - picture.min_y) * scale_y));
  };

  for (const auto& polygon : picture.polygons) {
    fill_polygon(image, polygon, map_x, map_y);
  }

  for (const auto& area : picture.areas) {
    fill_area(image, area, map_x, map_y);
  }

  for (const auto& cell : picture.images) {
    const int x0 = map_x(cell.lower_left.x);
    const int y0 = map_y(cell.upper_right.y);
    const int x1 = map_x(cell.upper_right.x);
    const int y1 = map_y(cell.lower_left.y);
    const int w = std::max(1, x1 - x0);
    const int h = std::max(1, y1 - y0);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const auto bit_index =
            static_cast<std::size_t>(y * std::max(1, cell.width) / h) *
                static_cast<std::size_t>(std::max(1, cell.width)) +
            static_cast<std::size_t>(x * std::max(1, cell.width) / w);
        const bool on = cell.bits.empty() || (cell.bits[bit_index % cell.bits.size()] & 1u);
        set_pixel(image, x0 + x, y0 + y, on ? Color{0, 0, 0, 255}
                                            : Color{230, 230, 230, 255});
      }
    }
  }

  for (const auto& line : picture.lines) {
    for (std::size_t i = 1; i < line.points.size(); ++i) {
      draw_line(image,
                map_x(line.points[i - 1].x),
                map_y(line.points[i - 1].y),
                map_x(line.points[i].x),
                map_y(line.points[i].y),
                line.color);
    }
  }

  for (const auto& marker : picture.markers) {
    const int x = map_x(marker.point.x);
    const int y = map_y(marker.point.y);
    const int size = std::max(
        2,
        static_cast<int>(
            std::lround(marker.size * std::min(scale_x, scale_y) / 25.0)));
    draw_line(image, x - size, y, x + size, y, marker.color);
    draw_line(image, x, y - size, x, y + size, marker.color);
    if ((marker.type & 1u) != 0) {
      draw_line(image, x - size, y - size, x + size, y + size, marker.color);
      draw_line(image, x - size, y + size, x + size, y - size, marker.color);
    }
  }

  for (const auto& text : picture.texts) {
    const int x0 = map_x(text.point.x);
    const int y0 = map_y(text.point.y);
    const int pixel_size =
        std::max(1, static_cast<int>(std::lround(text.height * scale_y / 8.0)));
    int x = x0;
    for (std::size_t i = 0; i < text.text.size(); ++i) {
      const char ch = text.text[i];
      draw_glyph(image, x, y0, pixel_size, ch, text);
      x += glyph_advance(ch, text, pixel_size);
    }
  }

  return image;
}

} // namespace geist::detail
