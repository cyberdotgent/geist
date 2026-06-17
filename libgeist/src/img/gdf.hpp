#pragma once

#include "img/image.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace geist::detail {

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

struct Polyline {
  std::vector<Point> points;
  Color color;
  double width = 1.0;
};

struct Marker {
  Point point;
  Color color;
  double size = 1.0;
  std::uint8_t type = 1;
};

struct TextRun {
  Point point;
  std::string text;
  Color color;
  double height = 3.0;
  std::uint8_t character_set = 0;
  bool bold = false;
  bool italic = false;
  bool monospaced = false;
};

struct Polygon {
  std::vector<Point> points;
  Color fill;
  Color edge;
};

struct CellImage {
  Point lower_left;
  Point upper_right;
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> bits;
};

struct GdfPicture {
  double min_x = 0.0;
  double min_y = 0.0;
  double max_x = 100.0;
  double max_y = 100.0;
  std::vector<Polyline> lines;
  std::vector<Marker> markers;
  std::vector<TextRun> texts;
  std::vector<Polygon> polygons;
  std::vector<CellImage> images;
};

GdfPicture parse_gdf_picture(const std::vector<std::uint8_t>& bytes);

} // namespace geist::detail
