#include "geist/detail/internal.hpp"

#include "img/gdf.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace geist::detail {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct ParserState {
  GdfPicture picture;
  Point current;
  Color line_color{0, 0, 0, 255};
  Color fill_color{220, 220, 220, 255};
  Color text_color{0, 0, 0, 255};
  Color marker_color{0, 0, 0, 255};
  double line_width = 1.0;
  double marker_size = 2.0;
  double char_height = 3.0;
  std::uint8_t character_set = 0;
  std::uint8_t marker_type = 1;
  std::uint8_t pattern = 0;
  std::uint8_t color_index = 0;
  std::uint8_t draw_mode = 0;
  bool in_area = false;
  bool area_draw_boundary = false;
  std::vector<std::vector<Point>> area_contours;
  std::map<std::uint32_t, std::size_t> segments;
  std::vector<std::uint8_t> transform_payload;
  std::vector<std::uint8_t> saved_attributes;
  CellImage pending_image;
  bool has_pending_image = false;
  int coordinate_type = 4;
  std::size_t coordinate_size = 4;
};

Color palette(std::uint8_t index) {
  static constexpr std::array<Color, 17> colors{{
      {0, 0, 0, 255},
      {0, 0, 255, 255},
      {255, 0, 0, 255},
      {255, 0, 255, 255},
      {0, 255, 0, 255},
      {0, 255, 255, 255},
      {255, 255, 0, 255},
      {255, 255, 255, 255},
      {0, 0, 0, 255},
      {128, 0, 0, 255},
      {0, 128, 0, 255},
      {0, 0, 128, 255},
      {128, 128, 0, 255},
      {128, 0, 128, 255},
      {0, 128, 128, 255},
      {64, 64, 64, 255},
  }};
  return colors[index < colors.size() ? index : 0];
}

struct GdfFontStyle {
  const char* gddm_name;
  const char* imagemark_name;
  bool bold;
  bool italic;
  bool monospaced;
};

GdfFontStyle font_style_for_character_set(std::uint8_t character_set) {
  static constexpr std::array<GdfFontStyle, 23> fonts{{
      {"ADMDVECP", "Modern:Modern", false, false, true},
      {"ADMUUARP", "Roman:Tms Rmn", false, false, false},
      {"ADMUUCIP", "Roman:Tms Rmn Italic", false, true, false},
      {"ADMUUCRP", "Roman:Tms Rmn", false, false, false},
      {"ADMUUCSP", "Script:Script", false, true, false},
      {"ADMUUDRP", "Swiss:Helvetica", false, false, false},
      {"ADMUUFSS", "Swiss:Helvetica", false, false, false},
      {"ADMUUGEP", "Roman:Tms Rmn", false, false, false},
      {"ADMUUGGP", "Roman:Tms Rmn", false, false, false},
      {"ADMUUGIP", "Roman:Tms Rmn", false, true, false},
      {"ADMUUKRF", "Swiss:Helvetica Bold", true, false, false},
      {"ADMUUKRO", "Swiss:Helvetica Bold", true, true, false},
      {"ADMUUKSF", "Swiss:Helvetica Bold", true, false, false},
      {"ADMUUKSO", "Swiss:Helvetica Bold", true, true, false},
      {"ADMUUMOD", "Modern:Modern", false, false, true},
      {"ADMUUNSF", "Swiss:Helvetica-Narrow", false, false, false},
      {"ADMUUNSO", "Swiss:Helvetica-Narrow", false, true, false},
      {"ADMUUORP", "Roman:Tms Rmn", false, false, false},
      {"ADMUUSHD", "Swiss:Helvetica", false, false, false},
      {"ADMUUSRP", "Modern:Modern", false, false, true},
      {"ADMUUTIP", "Roman:Tms Rmn Bold Italic", true, true, false},
      {"ADMUUTRP", "Roman:Tms Rmn Bold", true, false, false},
      {"ADMUUTSS", "Swiss:Helvetica Bold", true, false, false},
  }};

  if (character_set >= 0x41) {
    const auto index = static_cast<std::size_t>(character_set - 0x41);
    if (index < fonts.size()) {
      return fonts[index];
    }
  }
  return fonts[0];
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

std::int16_t read_be_i16(const std::vector<std::uint8_t>& bytes,
                         std::size_t offset) {
  return static_cast<std::int16_t>(
      (static_cast<std::uint16_t>(bytes[offset]) << 8) |
      static_cast<std::uint16_t>(bytes[offset + 1]));
}

std::uint32_t read_be_u32(const std::vector<std::uint8_t>& bytes,
                          std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
         static_cast<std::uint32_t>(bytes[offset + 3]);
}

double read_coord(const std::vector<std::uint8_t>& bytes,
                  std::size_t offset,
                  const ParserState& state) {
  if (state.coordinate_type == 2) {
    return static_cast<double>(read_be_i16(bytes, offset));
  }
  return read_ibm_hfp_float(bytes, offset);
}

bool is_plausible_coordinate(double value) {
  return std::isfinite(value) && value >= -1000000.0 && value <= 1000000.0;
}

Point read_point(const std::vector<std::uint8_t>& bytes,
                 std::size_t offset,
                 const ParserState& state) {
  const Point point{read_coord(bytes, offset, state),
                    read_coord(bytes, offset + state.coordinate_size, state)};
  if (!is_plausible_coordinate(point.x) ||
      !is_plausible_coordinate(point.y)) {
    throw std::runtime_error("GDF asset contains an implausible coordinate");
  }
  return point;
}

std::size_t point_size(const ParserState& state) {
  return state.coordinate_size * 2;
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

std::vector<Point> read_points(const std::vector<std::uint8_t>& bytes,
                               std::size_t offset,
                               std::size_t length,
                               const ParserState& state) {
  std::vector<Point> points;
  const auto stride = point_size(state);
  points.reserve(length / stride);
  for (std::size_t i = 0; i + stride <= length; i += stride) {
    points.push_back(read_point(bytes, offset + i, state));
  }
  return points;
}

void extend_bounds(GdfPicture& picture, const Point& point) {
  picture.min_x = std::min(picture.min_x, point.x);
  picture.max_x = std::max(picture.max_x, point.x);
  picture.min_y = std::min(picture.min_y, point.y);
  picture.max_y = std::max(picture.max_y, point.y);
}

void add_polyline(ParserState& state, std::vector<Point> points) {
  if (points.empty()) {
    return;
  }
  if (state.in_area && points.size() > 1) {
    state.area_contours.push_back(points);
  }
  for (const auto& point : points) {
    extend_bounds(state.picture, point);
  }
  if (state.in_area) {
    return;
  }
  if (points.size() > 1) {
    state.picture.lines.push_back(
        Polyline{std::move(points), state.line_color, state.line_width});
  }
}

void update_current_from(const std::vector<Point>& points, ParserState& state) {
  if (!points.empty()) {
    state.current = points.back();
  }
}

void set_color(ParserState& state, std::uint8_t index) {
  state.color_index = index;
  const auto color = palette(index);
  state.line_color = color;
  state.text_color = color;
  state.marker_color = color;
  if (!state.in_area) {
    state.fill_color = color;
  }
}

void handle_line(ParserState& state,
                 const std::vector<std::uint8_t>& bytes,
                 std::size_t offset,
                 std::size_t length,
                 bool at_current) {
  std::vector<Point> points;
  if (at_current) {
    points.push_back(state.current);
  }
  auto read = read_points(bytes, offset, length, state);
  points.insert(points.end(), read.begin(), read.end());
  add_polyline(state, points);
  update_current_from(read.empty() ? points : read, state);
}

void handle_relative_line(ParserState& state,
                          const std::vector<std::uint8_t>& bytes,
                          std::size_t offset,
                          std::size_t length,
                          bool at_current) {
  std::vector<Point> points;
  std::size_t consumed = 0;
  if (at_current) {
    points.push_back(state.current);
  } else if (length >= point_size(state)) {
    points.push_back(read_point(bytes, offset, state));
    consumed = point_size(state);
  }

  while (consumed + 1 < length && !points.empty()) {
    const auto prev = points.back();
    const auto dx = static_cast<std::int8_t>(bytes[offset + consumed]);
    const auto dy = static_cast<std::int8_t>(bytes[offset + consumed + 1]);
    points.push_back(Point{prev.x + dx, prev.y + dy});
    consumed += 2;
  }

  add_polyline(state, points);
  update_current_from(points, state);
}

void handle_marker(ParserState& state,
                   const std::vector<std::uint8_t>& bytes,
                   std::size_t offset,
                   std::size_t length,
                   bool at_current) {
  std::vector<Point> points;
  if (at_current) {
    points.push_back(state.current);
  }
  auto read = read_points(bytes, offset, length, state);
  points.insert(points.end(), read.begin(), read.end());
  for (const auto& point : points) {
    extend_bounds(state.picture, point);
    state.picture.markers.push_back(
        Marker{point, state.marker_color, state.marker_size, state.marker_type});
  }
  update_current_from(read.empty() ? points : read, state);
}

void handle_text(ParserState& state,
                 const std::vector<std::uint8_t>& bytes,
                 std::size_t offset,
                 std::size_t length,
                 bool at_current) {
  Point point = state.current;
  std::size_t text_offset = offset;
  if (!at_current && length >= point_size(state)) {
    point = read_point(bytes, offset, state);
    text_offset += point_size(state);
  }

  std::string text;
  for (std::size_t i = text_offset; i < offset + length; ++i) {
    const auto ch = bytes[i];
    if (ch >= 0x40) {
      const auto decoded = EbcdicCodec::cp037().decode_ascii_byte(ch);
      text.push_back(decoded >= 0x20 && decoded <= 0x7e ? decoded : '?');
    }
  }
  if (text.empty()) {
    text = "?";
  }
  const auto font = font_style_for_character_set(state.character_set);
  extend_bounds(state.picture, point);
  state.picture.texts.push_back(TextRun{point,
                                        text,
                                        state.text_color,
                                        std::max(1.0, state.char_height),
                                        state.character_set,
                                        font.bold,
                                        font.italic,
                                        font.monospaced});
}

std::vector<Point> arc_points(Point center,
                              double radius_x,
                              double radius_y,
                              double start,
                              double end) {
  std::vector<Point> points;
  constexpr int steps = 40;
  for (int i = 0; i <= steps; ++i) {
    const double t = start + (end - start) * static_cast<double>(i) / steps;
    points.push_back(
        Point{center.x + std::cos(t) * radius_x, center.y + std::sin(t) * radius_y});
  }
  return points;
}

void handle_arc(ParserState& state,
                const std::vector<std::uint8_t>& bytes,
                std::size_t offset,
                std::size_t length,
                bool at_current,
                bool full_arc) {
  std::vector<Point> points;
  if (at_current) {
    points.push_back(state.current);
  }
  auto read = read_points(bytes, offset, length, state);
  points.insert(points.end(), read.begin(), read.end());
  if (full_arc) {
    const Point center = points.empty() ? state.current : points.front();
    const double radius = std::max(1.0, length > point_size(state)
                                           ? std::abs(read_coord(bytes,
                                                                 offset + point_size(state),
                                                                 state))
                                           : state.marker_size * 4.0);
    add_polyline(state, arc_points(center, radius, radius, 0.0, kPi * 2.0));
  } else if (points.size() >= 3) {
    const Point center{(points.front().x + points.back().x) / 2.0,
                       (points.front().y + points.back().y) / 2.0};
    const double rx = std::max(1.0, std::abs(points.back().x - points.front().x) / 2.0);
    const double ry = std::max(1.0, std::abs(points[1].y - center.y));
    add_polyline(state, arc_points(center, rx, ry, 0.0, kPi));
  }
  update_current_from(points, state);
}

void finish_area(ParserState& state) {
  if (state.in_area && !state.area_contours.empty()) {
    state.picture.areas.push_back(FilledArea{state.area_contours,
                                             state.fill_color,
                                             state.line_color,
                                             state.area_draw_boundary});
  }
  state.in_area = false;
  state.area_draw_boundary = false;
  state.area_contours.clear();
}

void handle_area_order(ParserState& state, std::uint8_t flags) {
  if ((flags & 0x80u) == 0) {
    finish_area(state);
    return;
  }
  finish_area(state);
  state.in_area = true;
  state.area_draw_boundary = (flags & 0x40u) != 0;
}

void handle_image_begin(ParserState& state,
                        const std::vector<std::uint8_t>& bytes,
                        std::size_t offset,
                        std::size_t length,
                        bool at_current) {
  Point origin = state.current;
  std::size_t cursor = 0;
  if (!at_current && length >= point_size(state)) {
    origin = read_point(bytes, offset, state);
    cursor += point_size(state);
  }
  int width = 16;
  int height = 16;
  if (cursor + 3 < length) {
    width = std::max(1, static_cast<int>(read_be_i16(bytes, offset + cursor)));
    height = std::max(1, static_cast<int>(read_be_i16(bytes, offset + cursor + 2)));
  }
  state.pending_image = CellImage{origin,
                                  Point{origin.x + width, origin.y + height},
                                  width,
                                  height,
                                  {}};
  state.pending_image.bits.reserve(static_cast<std::size_t>(width) * height);
  state.has_pending_image = true;
}

void handle_image_data(ParserState& state,
                       const std::vector<std::uint8_t>& bytes,
                       std::size_t offset,
                       std::size_t length) {
  if (!state.has_pending_image) {
    return;
  }
  state.pending_image.bits.insert(state.pending_image.bits.end(),
                                  bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                  bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
}

void handle_image_end(ParserState& state) {
  if (!state.has_pending_image) {
    return;
  }
  extend_bounds(state.picture, state.pending_image.lower_left);
  extend_bounds(state.picture, state.pending_image.upper_right);
  state.picture.images.push_back(std::move(state.pending_image));
  state.pending_image = {};
  state.has_pending_image = false;
}

void handle_attribute(ParserState& state,
                      std::uint8_t opcode,
                      const std::vector<std::uint8_t>& bytes,
                      std::size_t offset,
                      std::size_t length) {
  switch (opcode) {
  case 0x03:
  case 0x33:
    if (length >= point_size(state)) {
      const auto p = read_point(bytes, offset, state);
      state.char_height = std::max(1.0, std::abs(p.y) * 0.727);
    }
    break;
  case 0x09:
  case 0x28:
    state.pattern = length ? bytes[offset] : 0;
    state.fill_color = palette(static_cast<std::uint8_t>((state.pattern % 14) + 2));
    break;
  case 0x0a:
  case 0x4a:
    set_color(state, length ? bytes[offset] : 0);
    break;
  case 0x0c:
  case 0x4c:
    state.draw_mode = length ? bytes[offset] : 0;
    break;
  case 0x0d:
  case 0x4d:
    state.draw_mode = length ? bytes[offset] : 0;
    break;
  case 0x10:
  case 0x50:
    break;
  case 0x11:
  case 0x51:
    if (length >= 2) {
      state.line_width =
          std::max(1.0, static_cast<double>((bytes[offset] << 8) | bytes[offset + 1]) / 10.0);
    }
    break;
  case 0x18:
  case 0x58:
    state.line_width = length && bytes[offset] == 0 ? 0.0 : std::max(1.0, state.line_width);
    break;
  case 0x19:
  case 0x59:
    state.line_width = std::max(1.0, static_cast<double>(length ? bytes[offset] : 1));
    break;
  case 0x22:
  case 0x62:
    break;
  case 0x24:
  case 0x64:
    state.transform_payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
    break;
  case 0x26:
  case 0x66:
    if (length >= 2) {
      set_color(state, bytes[offset + 1]);
    } else if (length) {
      set_color(state, bytes[offset]);
    }
    break;
  case 0x27:
  case 0x67:
    break;
  case 0x34:
  case 0x74:
    break;
  case 0x36:
  case 0x76:
    break;
  case 0x38:
  case 0x78:
    state.character_set = length ? bytes[offset] : 0;
    break;
  case 0x3f:
    if (!state.saved_attributes.empty()) {
      const auto saved = state.saved_attributes.back();
      state.saved_attributes.pop_back();
      set_color(state, saved);
    }
    break;
  case 0x61:
    if (length >= point_size(state)) {
      state.current = read_point(bytes, offset, state);
    }
    break;
  case 0x69:
    state.marker_type = length ? bytes[offset] : 1;
    break;
  default:
    break;
  }

  if ((opcode & 0x40u) != 0 && length) {
    state.saved_attributes.push_back(state.color_index);
  }
}

void parse_header(ParserState& state, const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 4 || bytes[0] != 0x01) {
    throw std::runtime_error("GDF asset does not start with a supported header");
  }
  const auto header_length = static_cast<std::size_t>(bytes[1]);
  if (2 + header_length > bytes.size() || header_length < 2) {
    throw std::runtime_error("GDF asset has an invalid header length");
  }
  state.coordinate_type =
      (static_cast<int>(bytes[2]) << 8) | static_cast<int>(bytes[3]);
  state.coordinate_size = state.coordinate_type == 2 ? 2 : 4;
  if (state.coordinate_type == 2 && header_length >= 10) {
    state.picture.min_x = read_be_i16(bytes, 4);
    state.picture.max_x = read_be_i16(bytes, 6);
    state.picture.min_y = read_be_i16(bytes, 8);
    state.picture.max_y = read_be_i16(bytes, 10);
  } else if (header_length >= 18) {
    state.coordinate_type = 4;
    state.coordinate_size = 4;
    state.picture.min_x = read_ibm_hfp_float(bytes, 4);
    state.picture.max_x = read_ibm_hfp_float(bytes, 8);
    state.picture.min_y = read_ibm_hfp_float(bytes, 12);
    state.picture.max_y = read_ibm_hfp_float(bytes, 16);
  } else {
    throw std::runtime_error("GDF asset header lacks a supported extent");
  }
  if (!(state.picture.min_x < state.picture.max_x) ||
      !(state.picture.min_y < state.picture.max_y)) {
    throw std::runtime_error("GDF asset has an empty declared extent");
  }
  state.current = Point{state.picture.min_x, state.picture.min_y};
}

} // namespace

GdfPicture parse_gdf_picture(const std::vector<std::uint8_t>& bytes) {
  ParserState state;
  parse_header(state, bytes);

  std::size_t offset = 2 + static_cast<std::size_t>(bytes[1]);
  while (offset < bytes.size()) {
    const auto record_start = offset;
    const auto opcode = bytes[offset++];
    const auto length = gdf_record_payload_length(opcode, bytes, offset);
    if (offset + length > bytes.size()) {
      throw std::runtime_error("GDF asset has a truncated record payload");
    }

    switch (opcode) {
    case 0x03:
    case 0x09:
    case 0x0a:
    case 0x0c:
    case 0x0d:
    case 0x10:
    case 0x11:
    case 0x18:
    case 0x19:
    case 0x22:
    case 0x24:
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x33:
    case 0x34:
    case 0x36:
    case 0x38:
    case 0x3f:
    case 0x4a:
    case 0x4c:
    case 0x4d:
    case 0x50:
    case 0x51:
    case 0x58:
    case 0x59:
    case 0x60:
    case 0x62:
    case 0x64:
    case 0x66:
    case 0x67:
    case 0x69:
    case 0x74:
    case 0x76:
    case 0x78:
      if (opcode == 0x60) {
        finish_area(state);
      } else {
        handle_attribute(state, opcode, bytes, offset, length);
      }
      break;
    case 0x07:
      break;
    case 0x21:
    case 0x61:
      if (length >= point_size(state)) {
        state.current = read_point(bytes, offset, state);
      }
      break;
    case 0x68:
      handle_area_order(state, length ? bytes[offset] : 0);
      break;
    case 0x70:
      if (length >= 4) {
        state.segments[read_be_u32(bytes, offset)] = record_start;
      }
      break;
    case 0x71:
      break;
    case 0x81:
      handle_line(state, bytes, offset, length, true);
      break;
    case 0x82:
      handle_marker(state, bytes, offset, length, true);
      break;
    case 0x83:
      handle_text(state, bytes, offset, length, true);
      break;
    case 0x85:
    case 0x86:
      handle_arc(state, bytes, offset, length, true, false);
      break;
    case 0x87:
      handle_arc(state, bytes, offset, length, true, true);
      break;
    case 0x91:
      handle_image_begin(state, bytes, offset, length, true);
      break;
    case 0x92:
      handle_image_data(state, bytes, offset, length);
      break;
    case 0x93:
      handle_image_end(state);
      break;
    case 0xa1:
      handle_relative_line(state, bytes, offset, length, true);
      break;
    case 0xc1:
      handle_line(state, bytes, offset, length, false);
      break;
    case 0xc2:
      handle_marker(state, bytes, offset, length, false);
      break;
    case 0xc3:
      handle_text(state, bytes, offset, length, false);
      break;
    case 0xc5:
    case 0xc6:
      handle_arc(state, bytes, offset, length, false, false);
      break;
    case 0xc7:
      handle_arc(state, bytes, offset, length, false, true);
      break;
    case 0xd1:
      handle_image_begin(state, bytes, offset, length, false);
      break;
    case 0xe1:
      handle_relative_line(state, bytes, offset, length, false);
      break;
    default:
      break;
    }

    offset += length;
  }

  finish_area(state);
  if (state.has_pending_image) {
    handle_image_end(state);
  }
  if (state.picture.lines.empty() && state.picture.markers.empty() &&
      state.picture.texts.empty() && state.picture.polygons.empty() &&
      state.picture.areas.empty() && state.picture.images.empty()) {
    throw std::runtime_error("GDF asset has no supported drawable records");
  }
  return state.picture;
}

} // namespace geist::detail
