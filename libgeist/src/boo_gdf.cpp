#include "geist/detail/boo_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace geist::detail {

namespace {

struct Point {
  double x = 0.0;
  double y = 0.0;
};

struct Polyline {
  std::size_t offset = 0;
  std::size_t length = 0;
  std::vector<Point> points;
};

double read_ibm_hfp_float(const std::vector<std::uint8_t>& bytes,
                          std::size_t offset) {
  const auto value =
      (static_cast<std::uint32_t>(bytes[offset]) << 24) |
      (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
      (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
      static_cast<std::uint32_t>(bytes[offset + 3]);
  if (value == 0) {
    return 0.0;
  }

  const double sign = (value & 0x80000000u) != 0 ? -1.0 : 1.0;
  const int exponent = static_cast<int>((value >> 24) & 0x7fu) - 64;
  const auto fraction = static_cast<double>(value & 0x00ffffffu) /
                        static_cast<double>(0x01000000u);
  return sign * std::ldexp(fraction, exponent * 4);
}

bool is_plausible_coordinate(double value) {
  return std::isfinite(value) && value >= -10000.0 && value <= 10000.0 &&
         (value == 0.0 || std::abs(value) >= 0.001);
}

bool read_point(const std::vector<std::uint8_t>& bytes,
                std::size_t offset,
                Point& point) {
  if (offset + 8 > bytes.size()) {
    return false;
  }

  const auto x = read_ibm_hfp_float(bytes, offset);
  const auto y = read_ibm_hfp_float(bytes, offset + 4);
  if (!is_plausible_coordinate(x) || !is_plausible_coordinate(y)) {
    return false;
  }

  point = Point{x, y};
  return true;
}

std::vector<Polyline> find_coordinate_runs(
    const std::vector<std::uint8_t>& bytes) {
  std::vector<Polyline> candidates;
  for (std::size_t offset = 0; offset + 16 <= bytes.size(); ++offset) {
    Polyline line;
    line.offset = offset;
    for (std::size_t pos = offset; pos + 8 <= bytes.size(); pos += 8) {
      Point point;
      if (!read_point(bytes, pos, point)) {
        break;
      }
      line.points.push_back(point);
    }
    if (line.points.size() >= 2) {
      line.length = line.points.size() * 8;
      candidates.push_back(std::move(line));
    }
  }

  std::sort(candidates.begin(),
            candidates.end(),
            [](const Polyline& left, const Polyline& right) {
              if (left.length != right.length) {
                return left.length > right.length;
              }
              return left.offset < right.offset;
            });

  std::vector<bool> used(bytes.size(), false);
  std::vector<Polyline> runs;
  for (auto& candidate : candidates) {
    bool overlaps = false;
    for (std::size_t i = 0; i < candidate.length; ++i) {
      if (used[candidate.offset + i]) {
        overlaps = true;
        break;
      }
    }
    if (overlaps) {
      continue;
    }
    for (std::size_t i = 0; i < candidate.length; ++i) {
      used[candidate.offset + i] = true;
    }
    runs.push_back(std::move(candidate));
  }

  std::sort(runs.begin(),
            runs.end(),
            [](const Polyline& left, const Polyline& right) {
              return left.offset < right.offset;
            });
  return runs;
}

void set_pixel(RgbaImage& image, int x, int y) {
  if (x < 0 || y < 0 || x >= static_cast<int>(image.width) ||
      y >= static_cast<int>(image.height)) {
    return;
  }
  const auto offset =
      (static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) *
      4;
  image.rgba[offset] = 0;
  image.rgba[offset + 1] = 0;
  image.rgba[offset + 2] = 0;
  image.rgba[offset + 3] = 255;
}

void draw_line(RgbaImage& image, int x0, int y0, int x1, int y1) {
  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  for (;;) {
    set_pixel(image, x0, y0);
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

} // namespace

RgbaImage decode_gdf_to_rgba(const std::vector<std::uint8_t>& bytes) {
  const auto lines = find_coordinate_runs(bytes);
  if (lines.empty()) {
    throw std::runtime_error(
        "GDF asset has no supported IBM hexadecimal-float coordinate runs");
  }

  double min_x = std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();
  for (const auto& line : lines) {
    for (const auto& point : line.points) {
      min_x = std::min(min_x, point.x);
      min_y = std::min(min_y, point.y);
      max_x = std::max(max_x, point.x);
      max_y = std::max(max_y, point.y);
    }
  }

  if (min_x >= max_x || min_y >= max_y) {
    throw std::runtime_error("GDF asset has an empty drawable coordinate range");
  }

  constexpr std::uint32_t max_dimension = 1600;
  constexpr std::uint32_t margin = 12;
  const auto width_units = max_x - min_x;
  const auto height_units = max_y - min_y;
  const auto scale = std::min(
      static_cast<double>(max_dimension - (margin * 2)) / width_units,
      static_cast<double>(max_dimension - (margin * 2)) / height_units);

  RgbaImage image;
  image.width = std::max<std::uint32_t>(
      1, static_cast<std::uint32_t>(std::ceil(width_units * scale)) +
             (margin * 2));
  image.height = std::max<std::uint32_t>(
      1, static_cast<std::uint32_t>(std::ceil(height_units * scale)) +
             (margin * 2));
  image.rgba.assign(static_cast<std::size_t>(image.width) * image.height * 4,
                    255);

  auto map_x = [&](double x) {
    return static_cast<int>(std::lround((x - min_x) * scale)) +
           static_cast<int>(margin);
  };
  auto map_y = [&](double y) {
    return static_cast<int>(image.height) - static_cast<int>(margin) - 1 -
           static_cast<int>(std::lround((y - min_y) * scale));
  };

  for (const auto& line : lines) {
    for (std::size_t i = 1; i < line.points.size(); ++i) {
      draw_line(image,
                map_x(line.points[i - 1].x),
                map_y(line.points[i - 1].y),
                map_x(line.points[i].x),
                map_y(line.points[i].y));
    }
  }

  return image;
}

} // namespace geist::detail
