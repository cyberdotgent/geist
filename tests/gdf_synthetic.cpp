#include "geist/detail/core/internal.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void be16(std::vector<std::uint8_t>& out, int value) {
  const auto v = static_cast<std::uint16_t>(static_cast<std::int16_t>(value));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
  out.push_back(static_cast<std::uint8_t>(v & 0xff));
}

void be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 24));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
  out.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void point(std::vector<std::uint8_t>& out, int x, int y) {
  be16(out, x);
  be16(out, y);
}

void normal(std::vector<std::uint8_t>& out,
            std::uint8_t opcode,
            const std::vector<std::uint8_t>& payload) {
  out.push_back(opcode);
  out.push_back(static_cast<std::uint8_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}

void short_order(std::vector<std::uint8_t>& out,
                 std::uint8_t opcode,
                 std::uint8_t payload) {
  out.push_back(opcode);
  out.push_back(payload);
}

std::vector<std::uint8_t> payload_point(int x, int y) {
  std::vector<std::uint8_t> out;
  point(out, x, y);
  return out;
}

std::vector<std::uint8_t> synthetic_gdf_all_opcodes() {
  std::vector<std::uint8_t> gdf;
  gdf.push_back(0x01);
  gdf.push_back(0x0a);
  be16(gdf, 2);
  be16(gdf, 0);
  be16(gdf, 200);
  be16(gdf, 0);
  be16(gdf, 200);

  normal(gdf, 0x03, payload_point(0, 12));
  normal(gdf, 0x07, {});
  short_order(gdf, 0x09, 3);
  short_order(gdf, 0x0a, 3);
  short_order(gdf, 0x0c, 1);
  short_order(gdf, 0x0d, 2);
  normal(gdf, 0x10, {1, 2});
  normal(gdf, 0x11, {0, 16});
  short_order(gdf, 0x18, 1);
  short_order(gdf, 0x19, 2);
  normal(gdf, 0x21, payload_point(20, 20));
  {
    std::vector<std::uint8_t> p;
    point(p, 20, 0);
    point(p, 0, 20);
    normal(gdf, 0x22, p);
  }
  normal(gdf, 0x24, {0, 0, 0, 0});
  normal(gdf, 0x26, {0xff, 5});
  normal(gdf, 0x27, {0, 0});
  short_order(gdf, 0x28, 5);
  short_order(gdf, 0x29, 2);
  normal(gdf, 0x33, payload_point(0, 14));
  normal(gdf, 0x34, payload_point(1, 0));
  {
    std::vector<std::uint8_t> p{0, 0};
    point(p, 2, 10);
    normal(gdf, 0x36, p);
  }
  short_order(gdf, 0x38, 0x41);
  normal(gdf, 0x3f, {});
  short_order(gdf, 0x4a, 4);
  short_order(gdf, 0x4c, 1);
  short_order(gdf, 0x4d, 1);
  normal(gdf, 0x50, {1, 1});
  normal(gdf, 0x51, {0, 20});
  short_order(gdf, 0x58, 2);
  short_order(gdf, 0x59, 1);
  normal(gdf, 0x61, payload_point(30, 30));
  {
    std::vector<std::uint8_t> p;
    point(p, 15, 0);
    point(p, 0, 15);
    normal(gdf, 0x62, p);
  }
  normal(gdf, 0x64, {0, 0, 0, 0});
  normal(gdf, 0x66, {0xff, 6});
  normal(gdf, 0x67, {0, 0});

  short_order(gdf, 0x68, 1);
  {
    std::vector<std::uint8_t> p;
    point(p, 20, 20);
    point(p, 60, 20);
    point(p, 60, 60);
    point(p, 20, 60);
    point(p, 20, 20);
    normal(gdf, 0xc1, p);
  }
  short_order(gdf, 0x68, 0x40);
  normal(gdf, 0x60, {});

  short_order(gdf, 0x69, 3);
  {
    std::vector<std::uint8_t> p;
    be32(p, 0x01020304);
    normal(gdf, 0x70, p);
  }
  normal(gdf, 0x71, {});
  normal(gdf, 0x81, payload_point(80, 30));
  normal(gdf, 0x82, payload_point(85, 35));
  normal(gdf, 0x83, {0xc1, 0xe3});
  {
    std::vector<std::uint8_t> p;
    point(p, 90, 40);
    point(p, 110, 70);
    normal(gdf, 0x85, p);
  }
  {
    std::vector<std::uint8_t> p;
    point(p, 100, 50);
    point(p, 130, 75);
    normal(gdf, 0x86, p);
  }
  normal(gdf, 0x87, {});
  {
    std::vector<std::uint8_t> p;
    be16(p, 12);
    be16(p, 12);
    normal(gdf, 0x91, p);
  }
  normal(gdf, 0x92, {1, 0, 1, 0, 1, 0, 1, 0});
  normal(gdf, 0x93, {});
  normal(gdf, 0xa1, {10, 0, 0, 10});
  {
    std::vector<std::uint8_t> p;
    point(p, 10, 150);
    point(p, 80, 150);
    normal(gdf, 0xc1, p);
  }
  normal(gdf, 0xc2, payload_point(90, 150));
  {
    std::vector<std::uint8_t> p = payload_point(95, 150);
    p.push_back(0xc3);
    p.push_back(0xc8);
    normal(gdf, 0xc3, p);
  }
  {
    std::vector<std::uint8_t> p;
    point(p, 105, 150);
    point(p, 125, 170);
    point(p, 145, 150);
    normal(gdf, 0xc5, p);
    normal(gdf, 0xc6, p);
  }
  normal(gdf, 0xc7, payload_point(160, 150));
  {
    std::vector<std::uint8_t> p = payload_point(150, 20);
    be16(p, 10);
    be16(p, 10);
    normal(gdf, 0xd1, p);
  }
  normal(gdf, 0x92, {1, 1, 0, 0, 1, 1, 0, 0});
  normal(gdf, 0x93, {});
  {
    std::vector<std::uint8_t> p = payload_point(150, 80);
    p.push_back(10);
    p.push_back(10);
    p.push_back(10);
    p.push_back(static_cast<std::uint8_t>(-10));
    normal(gdf, 0xe1, p);
  }
  return gdf;
}

void rectangle(std::vector<std::uint8_t>& out,
               int left,
               int bottom,
               int right,
               int top) {
  std::vector<std::uint8_t> p;
  point(p, left, bottom);
  point(p, right, bottom);
  point(p, right, top);
  point(p, left, top);
  point(p, left, bottom);
  normal(out, 0xc1, p);
}

std::vector<std::uint8_t> synthetic_packet_frame_gdf() {
  std::vector<std::uint8_t> gdf;
  gdf.push_back(0x01);
  gdf.push_back(0x0a);
  be16(gdf, 2);
  be16(gdf, 0);
  be16(gdf, 100);
  be16(gdf, 0);
  be16(gdf, 100);

  auto filled_box = [&](std::uint8_t fill,
                        int left,
                        int bottom,
                        int right,
                        int top) {
    normal(gdf, 0x26, {0x00, fill});
    short_order(gdf, 0x68, 0x80);
    normal(gdf, 0x26, {0x00, 0x08});
    rectangle(gdf, left, bottom, right, top);
    normal(gdf, 0x60, {});
  };

  filled_box(0x04, 0, 60, 20, 80);
  filled_box(0x06, 20, 60, 40, 80);
  filled_box(0x02, 40, 60, 50, 80);
  filled_box(0x01, 50, 60, 100, 80);
  filled_box(0x08, 0, 40, 10, 50);
  filled_box(0x0a, 70, 20, 80, 30);
  return gdf;
}

void require_pixel(const geist::detail::RgbaImage& image,
                   std::uint32_t x,
                   std::uint32_t y,
                   std::uint8_t r,
                   std::uint8_t g,
                   std::uint8_t b,
                   const char* label) {
  const auto offset =
      (static_cast<std::size_t>(y) * image.width + x) * 4u;
  if (image.rgba[offset] != r || image.rgba[offset + 1] != g ||
      image.rgba[offset + 2] != b) {
    std::ostringstream message;
    message << label << " had unexpected color "
            << static_cast<int>(image.rgba[offset]) << ','
            << static_cast<int>(image.rgba[offset + 1]) << ','
            << static_cast<int>(image.rgba[offset + 2]);
    throw std::runtime_error(message.str());
  }
}

void write_le16(std::ofstream& out, std::uint16_t value) {
  out.put(static_cast<char>(value & 0xff));
  out.put(static_cast<char>(value >> 8));
}

void write_le32(std::ofstream& out, std::uint32_t value) {
  out.put(static_cast<char>(value & 0xff));
  out.put(static_cast<char>((value >> 8) & 0xff));
  out.put(static_cast<char>((value >> 16) & 0xff));
  out.put(static_cast<char>((value >> 24) & 0xff));
}

void write_bmp(const std::filesystem::path& path,
               const geist::detail::RgbaImage& image) {
  std::ofstream out(path, std::ios::binary);
  const auto row_stride = ((image.width * 3u) + 3u) & ~3u;
  const auto pixel_bytes = row_stride * image.height;
  constexpr std::uint32_t header_bytes = 14 + 40;

  out.put('B');
  out.put('M');
  write_le32(out, header_bytes + pixel_bytes);
  write_le16(out, 0);
  write_le16(out, 0);
  write_le32(out, header_bytes);

  write_le32(out, 40);
  write_le32(out, image.width);
  write_le32(out, image.height);
  write_le16(out, 1);
  write_le16(out, 24);
  write_le32(out, 0);
  write_le32(out, pixel_bytes);
  write_le32(out, 2835);
  write_le32(out, 2835);
  write_le32(out, 0);
  write_le32(out, 0);

  std::vector<std::uint8_t> row(row_stride, 0);
  for (std::uint32_t source_y = image.height; source_y > 0; --source_y) {
    std::fill(row.begin(), row.end(), 0);
    const auto y = source_y - 1;
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const auto source =
          (static_cast<std::size_t>(y) * image.width + x) * 4u;
      const auto target = static_cast<std::size_t>(x) * 3u;
      row[target] = image.rgba[source + 2];
      row[target + 1] = image.rgba[source + 1];
      row[target + 2] = image.rgba[source];
    }
    out.write(reinterpret_cast<const char*>(row.data()),
              static_cast<std::streamsize>(row.size()));
  }
}

} // namespace

int main(int argc, char** argv) {
  try {
    const auto image = geist::detail::decode_gdf_to_rgba(synthetic_gdf_all_opcodes());
    if (image.width == 0 || image.height == 0 || image.rgba.empty()) {
      throw std::runtime_error("synthetic GDF rendered an empty image");
    }

    std::size_t non_white = 0;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
      if (image.rgba[i] != 255 || image.rgba[i + 1] != 255 ||
          image.rgba[i + 2] != 255) {
        ++non_white;
      }
    }
    if (non_white < 100) {
      throw std::runtime_error("synthetic GDF rendered too few non-white pixels");
    }

    const auto packet_frame = geist::detail::decode_gdf_to_rgba(
        synthetic_packet_frame_gdf());
    require_pixel(packet_frame, 100, 220, 0, 255, 0, "green fill");
    require_pixel(packet_frame, 300, 220, 255, 255, 0, "yellow fill");
    require_pixel(packet_frame, 450, 220, 255, 0, 0, "red fill");
    require_pixel(packet_frame, 750, 220, 0, 0, 255, "blue fill");
    require_pixel(packet_frame, 50, 405, 0, 0, 0, "black fill");
    require_pixel(packet_frame, 750, 550, 224, 128, 0, "orange fill");

    if (argc > 1) {
      write_bmp(argv[1], image);
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "gdf_synthetic: " << error.what() << "\n";
    return 1;
  }
}
