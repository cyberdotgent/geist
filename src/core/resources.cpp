#include "geist/detail/core/internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace geist::detail {

std::string decode_asset_id(const std::vector<std::uint8_t>& bytes,
                            std::size_t offset) {
  auto id = trim_right_spaces(EbcdicCodec::cp037().decode_ascii(
      bytes, offset, 8, "unexpected end of BOO file while reading text"));
  id.erase(std::remove(id.begin(), id.end(), '?'), id.end());
  return id;
}

std::string legacy_kind_name(std::uint8_t kind) {
  return std::string(1, EbcdicCodec::cp037().decode_ascii_byte(kind));
}

std::string stored_format_for_legacy_kind(std::uint8_t kind) {
  switch (EbcdicCodec::cp037().decode_ascii_byte(kind)) {
  case 'G':
    return "legacy-gdf";
  case 'I':
    return "legacy-mmr";
  case 'M':
    return "legacy-met";
  default:
    return "legacy-unknown";
  }
}

std::string sanitize_resource_filename(std::string id) {
  if (id.empty()) {
    return "resource";
  }
  for (auto& ch : id) {
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte) == 0 && ch != '-' && ch != '_') {
      ch = '_';
    }
  }
  return id;
}

std::string extension_for_resource(const ResourceEntry& resource) {
  const auto lower_format = ascii_lower(resource.stored_format);
  if (lower_format == "image/jpeg" || lower_format == "image/jpg") {
    return ".jpg";
  }
  if (lower_format == "image/png") {
    return ".png";
  }
  if (lower_format == "image/tiff" || lower_format == "image/tif") {
    return ".tif";
  }
  if (lower_format == "image/gif") {
    return ".gif";
  }
  if (lower_format == "image/cgm") {
    return ".cgm";
  }
  return ".bin";
}

std::string decode_version14_description(const std::vector<std::uint8_t>& bytes,
                                         std::uint64_t offset,
                                         std::uint64_t size) {
  if (!byte_range_is_valid(bytes, offset, size) || size == 0) {
    return {};
  }

  std::string description;
  const auto begin = static_cast<std::size_t>(offset);
  const auto count = static_cast<std::size_t>(size);
  if (bytes[begin] == 0 && count >= 2) {
    description.reserve(count / 2);
    for (std::size_t i = 1; i < count; i += 2) {
      description.push_back(static_cast<char>(bytes[begin + i]));
    }
  } else {
    description.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      description.push_back(static_cast<char>(bytes[begin + i]));
    }
  }
  return trim_ascii(description);
}

std::string extract_description_type(const std::string& description) {
  const auto lower = ascii_lower(description);
  for (const auto* marker : {"type=\"", "type='"}) {
    const std::string marker_text(marker);
    const auto found = lower.find(marker_text);
    if (found == std::string::npos) {
      continue;
    }

    const auto value_begin = found + marker_text.size();
    const auto quote = marker_text.back();
    const auto value_end = lower.find(quote, value_begin);
    if (value_end != std::string::npos && value_end > value_begin) {
      return lower.substr(value_begin, value_end - value_begin);
    }
  }
  return {};
}

std::vector<ResourceEntry> build_legacy_resources(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    ResourceLayout layout,
    std::uint32_t count) {
  std::vector<ResourceEntry> resources;
  const auto directory_base =
      static_cast<std::uint64_t>(directory.page_number) * boo_page_size;
  const auto table_start =
      std::uint64_t{0x0118} +
      (layout == ResourceLayout::legacy_v13
           ? static_cast<std::uint64_t>(count) * 16
           : 0);
  const auto table_size = static_cast<std::uint64_t>(count) * 16;
  if (count == 0 || table_start >= directory_base ||
      table_size > directory_base - table_start ||
      !byte_range_is_valid(bytes, table_start, table_size)) {
    return resources;
  }

  resources.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto entry_offset =
        static_cast<std::size_t>(table_start + index * 16);
    const auto id = decode_asset_id(bytes, entry_offset);
    const auto kind = bytes[entry_offset + 8];
    const auto size = read_be24(bytes, entry_offset + 9);
    const auto offset = read_be32(bytes, entry_offset + 12);
    if (id.empty() || size == 0 ||
        !byte_range_is_valid(bytes, offset, size) ||
        static_cast<std::uint64_t>(offset) + size > directory_base) {
      continue;
    }

    ResourceEntry resource;
    resource.id = id;
    resource.name = sanitize_resource_filename(id) + ".bin";
    resource.stored_format = stored_format_for_legacy_kind(kind);
    resource.kind = legacy_kind_name(kind);
    resource.offset = offset;
    resource.size = size;
    resource.layout = layout;
    resources.push_back(resource);
  }

  return resources;
}

std::vector<ResourceEntry> build_version14_resources(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    std::uint32_t count) {
  std::vector<ResourceEntry> resources;
  const auto directory_base =
      static_cast<std::uint64_t>(directory.page_number) * boo_page_size;
  const auto group_size = static_cast<std::uint64_t>(count) * 16;
  const auto data_group = std::uint64_t{0x0118} + group_size;
  const auto description_group = std::uint64_t{0x0118} + (group_size * 2);
  if (count == 0 || data_group >= directory_base ||
      group_size > directory_base - data_group ||
      !byte_range_is_valid(bytes, description_group, group_size) ||
      !byte_range_is_valid(bytes, data_group, group_size)) {
    return resources;
  }

  resources.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto description_entry =
        static_cast<std::size_t>(description_group + index * 16);
    const auto data_entry = static_cast<std::size_t>(data_group + index * 16);
    auto id = decode_asset_id(bytes, data_entry);
    if (id.empty()) {
      id = decode_asset_id(bytes, description_entry);
    }

    const auto description_size = read_be32(bytes, description_entry + 8);
    const auto description_payload_offset =
        read_be32(bytes, description_entry + 12);
    const auto data_size = read_be32(bytes, data_entry + 8);
    const auto data_payload_offset = read_be32(bytes, data_entry + 12);
    if (id.empty() || data_size == 0 ||
        !byte_range_is_valid(bytes, data_payload_offset, data_size)) {
      continue;
    }

    ResourceEntry resource;
    resource.id = id;
    resource.description_offset = description_payload_offset;
    resource.description_size = description_size;
    if (description_size != 0 &&
        byte_range_is_valid(bytes, description_payload_offset,
                            description_size)) {
      resource.description =
          decode_version14_description(bytes,
                                       description_payload_offset,
                                       description_size);
    }
    resource.stored_format = extract_description_type(resource.description);
    if (resource.stored_format.empty()) {
      resource.stored_format = "object";
    }
    resource.offset = data_payload_offset;
    resource.size = data_size;
    resource.layout = ResourceLayout::converted_v14;
    resource.name = sanitize_resource_filename(id) +
                    extension_for_resource(resource);
    resources.push_back(resource);
  }

  return resources;
}

std::vector<ResourceEntry> build_resources(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory) {
  const auto count = read_be32(bytes, 4);
  const auto directory_base =
      static_cast<std::size_t>(directory.page_number) * boo_page_size;
  if (count == 0 || directory_base + 11 > bytes.size()) {
    return {};
  }

  const auto version_major = bytes[directory_base + 9];
  const auto version_minor = bytes[directory_base + 10];
  if (version_major == 0x01 && version_minor == 0x00) {
    return build_version14_resources(bytes, directory, count);
  }
  if (version_major == 0x01 && version_minor == 0x03) {
    return build_legacy_resources(bytes, directory, ResourceLayout::legacy_v13,
                                  count);
  }
  return build_legacy_resources(bytes, directory, ResourceLayout::legacy_v12,
                                count);
}

} // namespace geist::detail
