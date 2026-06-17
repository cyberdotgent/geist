#include "geist/detail/boo_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace geist::detail {

namespace {

struct Point {
  double x = 0.0;
  double y = 0.0;
};

struct Polyline {
  std::vector<Point> points;
};

struct GdfPicture {
  double min_x = 0.0;
  double min_y = 0.0;
  double max_x = 0.0;
  double max_y = 0.0;
  std::vector<Polyline> lines;
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
  return std::isfinite(value) && value >= -100000.0 && value <= 100000.0;
}

Point read_point(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  const Point point{read_ibm_hfp_float(bytes, offset),
                    read_ibm_hfp_float(bytes, offset + 4)};
  if (!is_plausible_coordinate(point.x) ||
      !is_plausible_coordinate(point.y)) {
    throw std::runtime_error("GDF asset contains an implausible coordinate");
  }
  return point;
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

std::vector<Point> read_absolute_points(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::size_t length) {
  std::vector<Point> points;
  points.reserve(length / 8);
  for (std::size_t i = 0; i + 7 < length; i += 8) {
    points.push_back(read_point(bytes, offset + i));
  }
  return points;
}

GdfPicture parse_gdf_picture(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 20 || bytes[0] != 0x01) {
    throw std::runtime_error("GDF asset does not start with a supported header");
  }

  const auto header_length = static_cast<std::size_t>(bytes[1]);
  if (header_length < 18 || 2 + header_length > bytes.size()) {
    throw std::runtime_error("GDF asset has an invalid header length");
  }

  GdfPicture picture;
  picture.min_x = read_ibm_hfp_float(bytes, 4);
  picture.max_x = read_ibm_hfp_float(bytes, 8);
  picture.min_y = read_ibm_hfp_float(bytes, 12);
  picture.max_y = read_ibm_hfp_float(bytes, 16);
  if (!(picture.min_x < picture.max_x) || !(picture.min_y < picture.max_y)) {
    throw std::runtime_error("GDF asset has an empty declared extent");
  }

  Point current;
  std::size_t offset = 2 + header_length;
  while (offset < bytes.size()) {
    const auto opcode = bytes[offset++];
    const auto length = gdf_record_payload_length(opcode, bytes, offset);
    if (offset + length > bytes.size()) {
      throw std::runtime_error("GDF asset has a truncated record payload");
    }

    switch (opcode) {
    case 0x21:
    case 0x61:
      if (length >= 8) {
        current = read_point(bytes, offset);
      }
      break;

    case 0x81: {
      if (length >= 8) {
        Polyline line;
        line.points.push_back(current);
        auto points = read_absolute_points(bytes, offset, length);
        line.points.insert(line.points.end(), points.begin(), points.end());
        if (line.points.size() > 1) {
          picture.lines.push_back(std::move(line));
          current = picture.lines.back().points.back();
        }
      }
      break;
    }

    case 0xc1: {
      auto points = read_absolute_points(bytes, offset, length);
      if (points.size() > 1) {
        picture.lines.push_back(Polyline{points});
      }
      if (!points.empty()) {
        current = points.back();
      }
      break;
    }

    case 0xe1: {
      if (length >= 8) {
        Polyline line;
        line.points.push_back(read_point(bytes, offset));
        for (std::size_t i = 8; i + 1 < length; i += 2) {
          const auto dx = static_cast<std::int8_t>(bytes[offset + i]);
          const auto dy = static_cast<std::int8_t>(bytes[offset + i + 1]);
          const auto prev = line.points.back();
          line.points.push_back(Point{prev.x + dx, prev.y + dy});
        }
        if (line.points.size() > 1) {
          picture.lines.push_back(std::move(line));
          current = picture.lines.back().points.back();
        }
      }
      break;
    }

    default:
      break;
    }

    offset += length;
  }

  if (picture.lines.empty()) {
    throw std::runtime_error("GDF asset has no supported drawable records");
  }
  return picture;
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
  const auto picture = parse_gdf_picture(bytes);

  constexpr std::uint32_t max_dimension = 1600;
  constexpr std::uint32_t margin = 12;
  const auto width_units = picture.max_x - picture.min_x;
  const auto height_units = picture.max_y - picture.min_y;
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
    return static_cast<int>(std::lround((x - picture.min_x) * scale)) +
           static_cast<int>(margin);
  };
  auto map_y = [&](double y) {
    return static_cast<int>(image.height) - static_cast<int>(margin) - 1 -
           static_cast<int>(std::lround((y - picture.min_y) * scale));
  };

  for (const auto& line : picture.lines) {
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
