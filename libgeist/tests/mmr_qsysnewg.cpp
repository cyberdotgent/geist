#include "geist/document.hpp"

#include <png.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct ExpectedRender {
  const char* resource_id;
  std::uint32_t width;
  std::uint32_t height;
  std::uint64_t pixel_hash;
};

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string png_message(const png_image& image, const char* fallback) {
  if (image.message[0] != '\0') {
    return image.message;
  }
  return fallback;
}

} // namespace

int main() {
  const auto book =
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "QSYSNEWG.BOO";
  const auto document = geist::BooDocument::open(book);

  const ExpectedRender expected[] = {
      {"1", 82, 165, 0x9491199eae92882eull},
      {"12", 340, 294, 0x6500956ed2a002ceull},
      {"56", 344, 385, 0x7e6db4a165ef27f3ull},
  };

  for (const auto& item : expected) {
    const auto png = document.read_resource_png(item.resource_id);

    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (png_image_begin_read_from_memory(&image, png.data(), png.size()) == 0) {
      std::cerr << "failed to read rendered MMR PNG for resource "
                << item.resource_id << ": "
                << png_message(image, "invalid PNG") << "\n";
      png_image_free(&image);
      return 1;
    }

    image.format = PNG_FORMAT_RGBA;
    std::vector<std::uint8_t> pixels(PNG_IMAGE_SIZE(image));
    if (png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr) ==
        0) {
      std::cerr << "failed to decode rendered MMR PNG for resource "
                << item.resource_id << ": "
                << png_message(image, "invalid pixels") << "\n";
      png_image_free(&image);
      return 1;
    }

    const auto width = image.width;
    const auto height = image.height;
    png_image_free(&image);

    if (width != item.width || height != item.height) {
      std::cerr << "unexpected QSYSNEWG resource " << item.resource_id
                << " dimensions: " << width << "x" << height << "\n";
      return 1;
    }

    const auto actual_hash = fnv1a64(pixels);
    if (actual_hash != item.pixel_hash) {
      std::cerr << "unexpected QSYSNEWG resource " << item.resource_id
                << " pixel hash: 0x" << std::hex << actual_hash << "\n";
      return 1;
    }
  }
}
