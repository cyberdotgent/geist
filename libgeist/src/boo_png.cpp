#include "geist/detail/boo_detail.hpp"

#include <gif_lib.h>
#include <png.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace geist::detail {

namespace {

struct GifMemoryInput {
  const std::uint8_t* bytes = nullptr;
  std::size_t size = 0;
  std::size_t offset = 0;
};

bool has_png_signature(const std::vector<std::uint8_t>& bytes) {
  return bytes.size() >= 8 && png_sig_cmp(bytes.data(), 0, 8) == 0;
}

bool has_gif_signature(const std::vector<std::uint8_t>& bytes) {
  return bytes.size() >= 6 &&
         (std::string(bytes.begin(), bytes.begin() + 6) == "GIF87a" ||
          std::string(bytes.begin(), bytes.begin() + 6) == "GIF89a");
}

std::string png_error_message(const png_image& image,
                              const std::string& fallback) {
  if (image.message[0] != '\0') {
    return image.message;
  }
  return fallback;
}

int read_gif_memory(GifFileType* gif, GifByteType* output, int length) {
  auto* input = static_cast<GifMemoryInput*>(gif->UserData);
  const auto remaining = input->size - input->offset;
  const auto count = std::min<std::size_t>(remaining,
                                           static_cast<std::size_t>(length));
  std::copy(input->bytes + input->offset,
            input->bytes + input->offset + count,
            output);
  input->offset += count;
  return static_cast<int>(count);
}

int transparent_color_index(const SavedImage& image) {
  for (int index = 0; index < image.ExtensionBlockCount; ++index) {
    const auto& block = image.ExtensionBlocks[index];
    if (block.Function == GRAPHICS_EXT_FUNC_CODE && block.ByteCount >= 4 &&
        (block.Bytes[0] & 0x01) != 0) {
      return static_cast<unsigned char>(block.Bytes[3]);
    }
  }
  return -1;
}

RgbaImage decode_gif_to_rgba(const std::vector<std::uint8_t>& bytes) {
  GifMemoryInput input{bytes.data(), bytes.size(), 0};
  int error = 0;
  GifFileType* gif = DGifOpen(&input, read_gif_memory, &error);
  if (gif == nullptr) {
    throw std::runtime_error("failed to open GIF asset with giflib");
  }

  if (DGifSlurp(gif) != GIF_OK) {
    DGifCloseFile(gif, &error);
    throw std::runtime_error("failed to decode GIF asset with giflib");
  }
  if (gif->SWidth <= 0 || gif->SHeight <= 0 || gif->ImageCount <= 0) {
    DGifCloseFile(gif, &error);
    throw std::runtime_error("GIF asset has no image frame");
  }

  const auto& frame = gif->SavedImages[0];
  const auto& desc = frame.ImageDesc;
  const ColorMapObject* color_map =
      desc.ColorMap != nullptr ? desc.ColorMap : gif->SColorMap;
  if (color_map == nullptr || color_map->ColorCount <= 0) {
    DGifCloseFile(gif, &error);
    throw std::runtime_error("GIF asset has no color table");
  }

  RgbaImage image;
  image.width = static_cast<std::uint32_t>(gif->SWidth);
  image.height = static_cast<std::uint32_t>(gif->SHeight);
  image.rgba.assign(static_cast<std::size_t>(image.width) * image.height * 4,
                    0);

  const auto transparent_index = transparent_color_index(frame);
  for (int y = 0; y < desc.Height; ++y) {
    for (int x = 0; x < desc.Width; ++x) {
      const auto source = static_cast<std::size_t>(y) * desc.Width + x;
      const int color_index = frame.RasterBits[source];
      if (color_index < 0 || color_index >= color_map->ColorCount) {
        DGifCloseFile(gif, &error);
        throw std::runtime_error("GIF color index is outside the color table");
      }

      const int target_x = desc.Left + x;
      const int target_y = desc.Top + y;
      if (target_x < 0 || target_y < 0 ||
          target_x >= static_cast<int>(image.width) ||
          target_y >= static_cast<int>(image.height)) {
        continue;
      }

      const auto target =
          (static_cast<std::size_t>(target_y) * image.width +
           static_cast<std::size_t>(target_x)) *
          4;
      const auto& color = color_map->Colors[color_index];
      image.rgba[target] = color.Red;
      image.rgba[target + 1] = color.Green;
      image.rgba[target + 2] = color.Blue;
      image.rgba[target + 3] = color_index == transparent_index ? 0 : 255;
    }
  }

  DGifCloseFile(gif, &error);
  return image;
}

std::vector<std::uint8_t> encode_rgba_png(const RgbaImage& rgba_image) {
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = rgba_image.width;
  image.height = rgba_image.height;
  image.format = PNG_FORMAT_RGBA;

  png_alloc_size_t output_size = 0;
  if (png_image_write_to_memory(&image,
                                nullptr,
                                &output_size,
                                0,
                                rgba_image.rgba.data(),
                                0,
                                nullptr) == 0) {
    const auto message =
        png_error_message(image, "failed to size rendered PNG output");
    png_image_free(&image);
    throw std::runtime_error("failed to render PNG asset: " + message);
  }

  std::vector<std::uint8_t> output(output_size);
  if (png_image_write_to_memory(&image,
                                output.data(),
                                &output_size,
                                0,
                                rgba_image.rgba.data(),
                                0,
                                nullptr) == 0) {
    const auto message =
        png_error_message(image, "failed to write rendered PNG output");
    png_image_free(&image);
    throw std::runtime_error("failed to render PNG asset: " + message);
  }
  png_image_free(&image);
  output.resize(output_size);
  return output;
}

} // namespace

std::vector<std::uint8_t> render_resource_png(
    const ResourceEntry& resource,
    const std::vector<std::uint8_t>& stored_bytes) {
  const auto stored_format = ascii_lower(resource.stored_format);
  if (stored_format == "image/gif" || has_gif_signature(stored_bytes)) {
    return encode_rgba_png(decode_gif_to_rgba(stored_bytes));
  }

  if (stored_format == "legacy-gdf") {
    return encode_rgba_png(decode_gdf_to_rgba(stored_bytes));
  }

  if (stored_format != "image/png" && !has_png_signature(stored_bytes)) {
    throw std::runtime_error(
        "asset cannot be rendered to PNG yet: legacy BookManager MMR/MET, "
        "JPEG, TIFF, and CGM payload decoding is not implemented");
  }

  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  if (png_image_begin_read_from_memory(&image,
                                       stored_bytes.data(),
                                       stored_bytes.size()) == 0) {
    const auto message = png_error_message(image, "invalid PNG asset data");
    png_image_free(&image);
    throw std::runtime_error("failed to read PNG asset: " + message);
  }

  image.format = PNG_FORMAT_RGBA;
  std::vector<std::uint8_t> pixels(PNG_IMAGE_SIZE(image));
  if (png_image_finish_read(&image,
                            nullptr,
                            pixels.data(),
                            0,
                            nullptr) == 0) {
    const auto message = png_error_message(image, "invalid PNG pixel data");
    png_image_free(&image);
    throw std::runtime_error("failed to decode PNG asset: " + message);
  }

  RgbaImage rgba;
  rgba.width = image.width;
  rgba.height = image.height;
  rgba.rgba = std::move(pixels);
  png_image_free(&image);
  return encode_rgba_png(rgba);
}

} // namespace geist::detail
