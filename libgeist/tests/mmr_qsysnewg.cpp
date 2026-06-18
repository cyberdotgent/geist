#include "geist/document.hpp"

#include <png.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

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
  const auto png = document.read_resource_png("1");

  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  if (png_image_begin_read_from_memory(&image, png.data(), png.size()) == 0) {
    std::cerr << "failed to read rendered MMR PNG: "
              << png_message(image, "invalid PNG") << "\n";
    png_image_free(&image);
    return 1;
  }

  image.format = PNG_FORMAT_RGBA;
  std::vector<std::uint8_t> pixels(PNG_IMAGE_SIZE(image));
  if (png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr) == 0) {
    std::cerr << "failed to decode rendered MMR PNG: "
              << png_message(image, "invalid pixels") << "\n";
    png_image_free(&image);
    return 1;
  }

  const auto width = image.width;
  const auto height = image.height;
  png_image_free(&image);

  if (width != 82 || height != 165) {
    std::cerr << "unexpected QSYSNEWG resource 1 dimensions: " << width << "x"
              << height << "\n";
    return 1;
  }

  constexpr std::uint64_t expected_hash = 0x9491199eae92882eull;
  const auto actual_hash = fnv1a64(pixels);
  if (actual_hash != expected_hash) {
    std::cerr << "unexpected QSYSNEWG resource 1 pixel hash: 0x" << std::hex
              << actual_hash << "\n";
    return 1;
  }
}
