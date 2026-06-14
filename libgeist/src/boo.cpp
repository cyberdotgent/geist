#include "geist/boo.hpp"

#include <algorithm>
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

namespace geist {
namespace {

using TokenWords = std::vector<std::uint16_t>;

struct TopicData {
  std::string id;
  std::string title;
  std::string heading_level;
  std::uint32_t topic_number = 0;
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  std::vector<std::string> raw_records;
};

constexpr std::array<std::uint16_t, 256> cp500_byte_to_token_word = {
    0xFFFF, 0x0001, 0xFFFF, 0xFFFF, 0x2666, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0x25CB, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0x25BA, 0xFFFF, 0xFFFF, 0xFFFF, 0x2191, 0x2193, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0x2510, 0x250C, 0xFFFF, 0x2514, 0x2518,
    0x00D7, 0x2551, 0x2550, 0x2554, 0x2566, 0xFFFF, 0x2557, 0xFFFF,
    0x2584, 0x2580, 0x2192, 0x2190, 0x253C, 0x2500, 0x2265, 0x2264,
    0x2560, 0x256C, 0x2563, 0x255A, 0x2569, 0x255D, 0xE936, 0x258C,
    0x2588, 0x2590, 0x25A0, 0x252C, 0xFFFF, 0x251C, 0x2534, 0x2524,
    0x0020, 0x00A0, 0x00E2, 0x00E4, 0x00E0, 0x00E1, 0x00E3, 0x00E5,
    0x00E7, 0x00F1, 0x005B, 0x002E, 0x003C, 0x0028, 0x002B, 0x0021,
    0x0026, 0x00E9, 0x00EA, 0x00EB, 0x00E8, 0x00ED, 0x00EE, 0x00EF,
    0x00EC, 0x00DF, 0x005D, 0x0024, 0x002A, 0x0029, 0x003B, 0x005E,
    0x002D, 0x002F, 0x00C2, 0x00C4, 0x00C0, 0x00C1, 0x00C3, 0x00C5,
    0x00C7, 0x00D1, 0x00A6, 0x002C, 0x0025, 0x005F, 0x003E, 0x003F,
    0x00F8, 0x00C9, 0x00CA, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF,
    0x00CC, 0x0060, 0x003A, 0x0023, 0x0040, 0x0027, 0x003D, 0x0022,
    0x00D8, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x00AB, 0x00BB, 0x00F0, 0x00FD, 0x00FE, 0x00B1,
    0x00B0, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F, 0x0070,
    0x0071, 0x0072, 0x00AA, 0x00BA, 0x00E6, 0x00B8, 0x00C6, 0x00A4,
    0x00B5, 0x007E, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078,
    0x0079, 0x007A, 0x00A1, 0x00BF, 0x00D0, 0x00DD, 0x00DE, 0x00AE,
    0x00A2, 0x00A3, 0x00A5, 0x00B7, 0x00A9, 0x00A7, 0x00B6, 0x00BC,
    0x00BD, 0x00BE, 0x00AC, 0x007C, 0x00AF, 0x00A8, 0x00B4, 0x2502,
    0x007B, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x00AD, 0x00F4, 0x00F6, 0x00F2, 0x00F3, 0x00F5,
    0x007D, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F, 0x0050,
    0x0051, 0x0052, 0x00B9, 0x00FB, 0x00FC, 0x00F9, 0x00FA, 0x00FF,
    0x005C, 0x00F7, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058,
    0x0059, 0x005A, 0x00B2, 0x00D4, 0x00D6, 0x00D2, 0x00D3, 0x00D5,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x00B3, 0x00DB, 0x00DC, 0x00D9, 0x00DA, 0xFFFF};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open BOO file: " + path.string());
  }

  input.seekg(0, std::ios::end);
  const auto end = input.tellg();
  if (end < 0) {
    throw std::runtime_error("failed to determine BOO file size: " +
                             path.string());
  }

  input.seekg(0, std::ios::beg);

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
      throw std::runtime_error("failed to read complete BOO file: " +
                               path.string());
    }
  }

  return bytes;
}

std::uint16_t read_be16(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset) {
  if (offset + 2 > bytes.size()) {
    throw std::runtime_error("unexpected end of BOO file while reading u16");
  }

  return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

std::uint32_t read_be24(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset) {
  if (offset + 3 > bytes.size()) {
    throw std::runtime_error("unexpected end of BOO file while reading u24");
  }

  return (static_cast<std::uint32_t>(bytes[offset]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         static_cast<std::uint32_t>(bytes[offset + 2]);
}

std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset) {
  if (offset + 4 > bytes.size()) {
    throw std::runtime_error("unexpected end of BOO file while reading u32");
  }

  return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
         static_cast<std::uint32_t>(bytes[offset + 3]);
}

char decode_cp037_byte(std::uint8_t byte) {
  if (byte >= 0xF0 && byte <= 0xF9) {
    return static_cast<char>('0' + (byte - 0xF0));
  }

  switch (byte) {
  case 0x40:
    return ' ';
  case 0x4B:
    return '.';
  case 0x5C:
    return '*';
  case 0x61:
    return '/';
  case 0x6B:
    return ',';
  case 0x7A:
    return ':';
  case 0x7E:
    return '=';
  case 0x81:
    return 'a';
  case 0x82:
    return 'b';
  case 0x83:
    return 'c';
  case 0x84:
    return 'd';
  case 0x85:
    return 'e';
  case 0x86:
    return 'f';
  case 0x87:
    return 'g';
  case 0x88:
    return 'h';
  case 0x89:
    return 'i';
  case 0x91:
    return 'j';
  case 0x92:
    return 'k';
  case 0x93:
    return 'l';
  case 0x94:
    return 'm';
  case 0x95:
    return 'n';
  case 0x96:
    return 'o';
  case 0x97:
    return 'p';
  case 0x98:
    return 'q';
  case 0x99:
    return 'r';
  case 0xA2:
    return 's';
  case 0xA3:
    return 't';
  case 0xA4:
    return 'u';
  case 0xA5:
    return 'v';
  case 0xA6:
    return 'w';
  case 0xA7:
    return 'x';
  case 0xA8:
    return 'y';
  case 0xA9:
    return 'z';
  case 0xB4:
    return 'c';
  case 0xC1:
    return 'A';
  case 0xC2:
    return 'B';
  case 0xC3:
    return 'C';
  case 0xC4:
    return 'D';
  case 0xC5:
    return 'E';
  case 0xC6:
    return 'F';
  case 0xC7:
    return 'G';
  case 0xC8:
    return 'H';
  case 0xC9:
    return 'I';
  case 0xD1:
    return 'J';
  case 0xD2:
    return 'K';
  case 0xD3:
    return 'L';
  case 0xD4:
    return 'M';
  case 0xD5:
    return 'N';
  case 0xD6:
    return 'O';
  case 0xD7:
    return 'P';
  case 0xD8:
    return 'Q';
  case 0xD9:
    return 'R';
  case 0xE2:
    return 'S';
  case 0xE3:
    return 'T';
  case 0xE4:
    return 'U';
  case 0xE5:
    return 'V';
  case 0xE6:
    return 'W';
  case 0xE7:
    return 'X';
  case 0xE8:
    return 'Y';
  case 0xE9:
    return 'Z';
  default:
    return '?';
  }
}

std::string decode_cp037(const std::vector<std::uint8_t>& bytes,
                         std::size_t offset,
                         std::size_t count) {
  if (offset + count > bytes.size()) {
    throw std::runtime_error("unexpected end of BOO file while reading text");
  }

  std::string output;
  output.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    output.push_back(decode_cp037_byte(bytes[offset + i]));
  }
  return output;
}

std::string trim_right_spaces(std::string value) {
  while (!value.empty() && value.back() == ' ') {
    value.pop_back();
  }
  return value;
}

std::string trim_ascii(std::string value) {
  while (!value.empty() &&
         (std::isspace(static_cast<unsigned char>(value.back())) != 0 ||
          static_cast<unsigned char>(value.back()) < 0x20 ||
          value.back() == '?')) {
    value.pop_back();
  }
  std::size_t first = 0;
  while (first < value.size() &&
         (std::isspace(static_cast<unsigned char>(value[first])) != 0 ||
          static_cast<unsigned char>(value[first]) < 0x20 ||
          value[first] == '?')) {
    ++first;
  }
  if (first != 0) {
    value.erase(0, first);
  }
  return value;
}

std::string ascii_lower(std::string value) {
  for (auto& ch : value) {
    ch = static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

bool ascii_equals_case_insensitive(const std::string& left,
                                   const std::string& right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(left[i])) !=
        std::tolower(static_cast<unsigned char>(right[i]))) {
      return false;
    }
  }
  return true;
}

bool byte_range_is_valid(const std::vector<std::uint8_t>& bytes,
                         std::uint64_t offset,
                         std::uint64_t size) {
  return offset <= bytes.size() && size <= bytes.size() - offset;
}

std::string decode_asset_id(const std::vector<std::uint8_t>& bytes,
                            std::size_t offset) {
  auto id = trim_right_spaces(decode_cp037(bytes, offset, 8));
  id.erase(std::remove(id.begin(), id.end(), '?'), id.end());
  return id;
}

std::string legacy_kind_name(std::uint8_t kind) {
  return std::string(1, decode_cp037_byte(kind));
}

std::string stored_format_for_legacy_kind(std::uint8_t kind) {
  switch (decode_cp037_byte(kind)) {
  case 'G':
    return "legacy-gdf";
  case 'I':
    return "legacy-image";
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

bool ascii_starts_with_case_insensitive(const std::string& value,
                                        std::size_t offset,
                                        const std::string& prefix) {
  if (offset + prefix.size() > value.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(value[offset + i])) !=
        std::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

bool ascii_starts_with_case_insensitive(const std::string& value,
                                        const std::string& prefix) {
  return ascii_starts_with_case_insensitive(value, 0, prefix);
}

void replace_all_case_insensitive(std::string& value,
                                  const std::string& needle,
                                  const std::string& replacement) {
  std::size_t offset = 0;
  while (offset < value.size()) {
    if (ascii_starts_with_case_insensitive(value, offset, needle)) {
      value.replace(offset, needle.size(), replacement);
      offset += replacement.size();
    } else {
      ++offset;
    }
  }
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

std::string capitalize_bookmanager_words(std::string value) {
  bool capitalize_next = true;
  for (auto& ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isalpha(byte) != 0) {
      if (capitalize_next) {
        ch = static_cast<char>(std::toupper(byte));
      }
      capitalize_next = false;
    } else {
      capitalize_next = (ch == ' ' || ch == ':' || ch == '(' || ch == '-');
    }
  }
  return value;
}

std::string normalize_logical_control_value(const std::string& key,
                                            std::string value) {
  if (key == "CDOCNUM") {
    for (auto& ch : value) {
      ch = static_cast<char>(
          std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
  }

  if (key == "CTITLE" || key == "CSTITLE") {
    value = capitalize_bookmanager_words(value);
    replace_all_case_insensitive(value, "AS/400", "AS/400");
    replace_all_case_insensitive(value, "(TM)", "(TM)");
    replace_all_case_insensitive(value, "Officevision", "OfficeVision");
    replace_all_case_insensitive(value, "Cross-Reference", "Cross-Reference");
    return value;
  }

  if (key == "CCOPYRIGHT") {
    replace_all_case_insensitive(value, "IBM", "IBM");
    return value;
  }

  if (key == "CDATE") {
    return capitalize_bookmanager_words(value);
  }

  return value;
}

std::string normalize_toc_title(std::string value) {
  value = capitalize_bookmanager_words(value);
  std::string normalized;
  normalized.reserve(value.size());
  bool first_word = true;
  for (std::size_t cursor = 0; cursor < value.size();) {
    if (std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
      normalized.push_back(value[cursor++]);
      continue;
    }

    const auto word_begin = cursor;
    while (cursor < value.size() &&
           std::isspace(static_cast<unsigned char>(value[cursor])) == 0) {
      ++cursor;
    }
    const auto word = value.substr(word_begin, cursor - word_begin);
    std::string output_word;
    std::size_t part_begin = 0;
    bool first_part = true;
    while (part_begin <= word.size()) {
      const auto part_end = word.find('-', part_begin);
      auto part = word.substr(part_begin,
                              part_end == std::string::npos
                                  ? std::string::npos
                                  : part_end - part_begin);
      const auto lower_part = ascii_lower(part);
      const bool is_minor =
          lower_part == "a" || lower_part == "an" || lower_part == "and" ||
          lower_part == "for" || lower_part == "in" || lower_part == "of" ||
          lower_part == "on" || lower_part == "or" || lower_part == "the" ||
          lower_part == "to" || lower_part == "with";
      if (!(first_word && first_part) && is_minor) {
        part = lower_part;
      }
      if (!first_part) {
        output_word.push_back('-');
      }
      output_word += part;

      if (part_end == std::string::npos) {
        break;
      }
      part_begin = part_end + 1;
      first_part = false;
    }

    normalized += output_word;
    first_word = false;
  }
  value = normalized;
  replace_all_case_insensitive(value, "AS/400", "AS/400");
  replace_all_case_insensitive(value, "(TM)", "(TM)");
  replace_all_case_insensitive(value, "Officevision", "OfficeVision");
  replace_all_case_insensitive(value, "Cross-Reference", "Cross-Reference");
  replace_all_case_insensitive(value, "Ocl", "OCL");
  replace_all_case_insensitive(value, "Dbcs", "DBCS");
  replace_all_case_insensitive(value, "User Id", "User ID");
  return value;
}

std::string normalize_toc_id(std::string value) {
  for (auto& ch : value) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::size_t skip_decoded_separators(const std::string& value) {
  std::size_t cursor = 0;
  while (cursor < value.size()) {
    const auto ch = static_cast<unsigned char>(value[cursor]);
    if (std::isspace(ch) != 0 || ch < 0x20 || value[cursor] == '?' ||
        value[cursor] == ',') {
      ++cursor;
      continue;
    }
    break;
  }
  return cursor;
}

bool is_topic_id_char(char ch) {
  const auto byte = static_cast<unsigned char>(ch);
  return std::isalnum(byte) != 0 || ch == '.' || ch == '_' || ch == '-';
}

std::string extract_topic_header_id(const std::string& decoded_record) {
  auto record = trim_ascii(decoded_record);
  const auto start = skip_decoded_separators(record);
  if (start + 3 > record.size() ||
      std::tolower(static_cast<unsigned char>(record[start])) != 's' ||
      std::tolower(static_cast<unsigned char>(record[start + 1])) != 'h') {
    return {};
  }

  std::size_t cursor = start + 2;
  while (cursor < record.size() && is_topic_id_char(record[cursor])) {
    ++cursor;
  }

  return normalize_toc_id(record.substr(start + 2, cursor - (start + 2)));
}

std::string extract_control_value_until_boundary(const std::string& record,
                                                 const std::string& marker) {
  const auto lower_record = ascii_lower(record);
  const auto lower_marker = ascii_lower(marker);
  const auto found = lower_record.find(lower_marker);
  if (found == std::string::npos) {
    return {};
  }

  const auto value_begin = found + marker.size();
  auto value_end = record.size();
  static const std::array<const char*, 8> boundaries = {
      "?c", ", c", "?s", ", s", "?e", ", e", "?cz", ", cz"};
  for (const auto* boundary : boundaries) {
    const auto next = lower_record.find(boundary, value_begin);
    if (next != std::string::npos) {
      value_end = std::min(value_end, next);
    }
  }
  return trim_ascii(record.substr(value_begin, value_end - value_begin));
}

std::uint32_t extract_uint_control_value(const std::string& record,
                                         const std::string& marker) {
  const auto value = extract_control_value_until_boundary(record, marker);
  std::istringstream input(value);
  std::uint32_t number = 0;
  if (input >> number) {
    return number;
  }
  return 0;
}

std::string collapse_ascii_whitespace(std::string value) {
  std::string output;
  output.reserve(value.size());
  bool in_space = false;
  for (const auto ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isspace(byte) != 0 || byte < 0x20) {
      in_space = true;
      continue;
    }
    if (in_space && !output.empty()) {
      output.push_back(' ');
    }
    output.push_back(ch);
    in_space = false;
  }
  return trim_ascii(output);
}

std::string escape_gml_attr(std::string value) {
  for (auto& ch : value) {
    if (ch == '\'') {
      ch = '"';
    }
  }
  return value;
}

std::string dot_text(std::string value) {
  value = collapse_ascii_whitespace(std::move(value));
  if (value.empty()) {
    return {};
  }
  return value;
}

std::string trim_control_operand(std::string value) {
  value = dot_text(std::move(value));
  while (!value.empty() && (value.back() == '.' || value.back() == ',')) {
    value.pop_back();
  }
  return value;
}

bool looks_like_gml_control_at(const std::string& value, std::size_t offset) {
  while (offset < value.size() &&
         std::isspace(static_cast<unsigned char>(value[offset])) != 0) {
    ++offset;
  }

  static const std::array<const char*, 47> prefixes = {
      "sh",          "ctopicn",    "cparent",    "cforwardlevel",
      "cbacklevel",  "csummary",   "chdlevel",   "csourcefn",
      "st",          "ctocdef",    "ctoce",      "etoc",
      "cfontdef",    "cfont",      "cselect",    "cmenu",
      "cmitem",      "cemenu",     "srfig",      "srefig",
      "srtbl",       "sretbl",     "sr",         "cz",
      "si",
      "citerm",      "cgpsep",     "clanguage",  "cversion",
      "cbldvers",    "creflow",    "ctitle",     "cstitle",
      "ccopyright",  "csecurity",  "cdate",      "cauthor",
      "cdocnum",     "ctopics",    "cbasenum",   "cdoclevel",
      "cfront",      "ccontents",  "cfigures",   "ctables",
      "cindex",      "cpicture"};
  for (const auto* prefix : prefixes) {
    if (!ascii_starts_with_case_insensitive(value, offset, prefix)) {
      continue;
    }
    const auto prefix_text = std::string(prefix);
    const auto end = offset + prefix_text.size();
    if (end == value.size()) {
      return true;
    }
    const auto next = value[end];
    if (std::isspace(static_cast<unsigned char>(next)) != 0 || next == '=' ||
        next == ',' || next == '.') {
      return true;
    }
    if ((prefix_text == "sh" || prefix_text == "srfig" ||
         prefix_text == "srtbl" || prefix_text == "sr") &&
        is_topic_id_char(next)) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> split_decoded_markup_segments(
    const std::string& decoded_record) {
  std::vector<std::string> segments;
  std::size_t begin = 0;
  for (std::size_t cursor = 0; cursor < decoded_record.size(); ++cursor) {
    auto split = false;
    auto split_before = false;
    if (cursor > begin && looks_like_gml_control_at(decoded_record, cursor) &&
        !ascii_starts_with_case_insensitive(decoded_record, cursor, "sh") &&
        (std::isspace(static_cast<unsigned char>(decoded_record[cursor - 1])) !=
             0 ||
         decoded_record[cursor - 1] == '.' ||
         decoded_record[cursor - 1] == ',')) {
      split = true;
      split_before = true;
    } else if (decoded_record[cursor] == '?') {
      split = true;
    } else if (decoded_record[cursor] == ',' &&
               looks_like_gml_control_at(decoded_record, cursor + 1)) {
      split = true;
    }
    if (!split) {
      continue;
    }

    const auto end = split_before ? cursor : cursor;
    auto segment = trim_ascii(decoded_record.substr(begin, end - begin));
    if (!segment.empty()) {
      segments.push_back(std::move(segment));
    }
    begin = split_before ? cursor : cursor + 1;
  }

  auto segment = trim_ascii(decoded_record.substr(begin));
  if (!segment.empty()) {
    segments.push_back(std::move(segment));
  }
  return segments;
}

std::string first_word(std::string value) {
  value = trim_ascii(std::move(value));
  const auto end = value.find_first_of(" \t\r\n,");
  auto word = end == std::string::npos ? value : value.substr(0, end);
  while (!word.empty() && (word.back() == '.' || word.back() == ',')) {
    word.pop_back();
  }
  return word;
}

std::string rest_after_first_word(std::string value) {
  value = trim_ascii(std::move(value));
  const auto end = value.find_first_of(" \t\r\n,");
  if (end == std::string::npos) {
    return {};
  }
  return trim_ascii(value.substr(end + 1));
}

std::string render_simple_gml_control(const std::string& tag,
                                      std::string value) {
  value = dot_text(std::move(value));
  if (value.empty()) {
    return ":" + tag + ".";
  }
  return ":" + tag + "." + value;
}

std::string render_keyed_gml_control(const std::string& tag,
                                     const std::string& attr,
                                     std::string value) {
  value = trim_control_operand(std::move(value));
  if (value.empty()) {
    return ":" + tag + ".";
  }
  return ":" + tag + " " + attr + "='" + escape_gml_attr(value) + "'.";
}

std::string render_toc_entry_gml(std::string value) {
  std::istringstream input(value);
  std::uint32_t level = 0;
  std::uint32_t style = 0;
  std::string id;
  if (!(input >> level >> style >> id)) {
    return render_simple_gml_control("tocentry", std::move(value));
  }
  std::string title;
  std::getline(input, title);
  return ":tocentry level='" + std::to_string(level) + "' style='" +
         std::to_string(style) + "' refid='" + escape_gml_attr(id) + "'." +
         dot_text(title);
}

std::string render_tocdef_gml(std::string value) {
  const auto equals = value.find('=');
  if (equals != std::string::npos) {
    value = value.substr(equals + 1);
  }
  std::istringstream input(value);
  std::string style;
  if (!(input >> style)) {
    return render_simple_gml_control("tocdef", std::move(value));
  }
  std::string rest;
  std::getline(input, rest);
  return ":tocdef style='" + escape_gml_attr(style) + "' values='" +
         escape_gml_attr(dot_text(rest)) + "'.";
}

std::string render_fontdef_gml(std::string value) {
  const auto equals = value.find('=');
  if (equals != std::string::npos) {
    value = value.substr(equals + 1);
  }
  std::istringstream input(value);
  std::string code;
  if (!(input >> code)) {
    return render_simple_gml_control("fontdef", std::move(value));
  }
  std::string style;
  std::getline(input, style);
  return ":fontdef code='" + escape_gml_attr(code) + "' style='" +
         escape_gml_attr(dot_text(style)) + "'.";
}

std::string render_link_gml(std::string value) {
  std::istringstream input(value);
  std::string column;
  std::string length;
  std::string target;
  if (!(input >> column >> length >> target)) {
    return render_simple_gml_control("link", std::move(value));
  }
  std::string text;
  std::getline(input, text);
  auto output = ":link col='" + escape_gml_attr(column) + "' len='" +
                escape_gml_attr(length) + "' refid='" +
                escape_gml_attr(target) + "'.";
  text = dot_text(text);
  if (!text.empty()) {
    output += text;
  }
  return output;
}

std::string render_menu_item_gml(std::string value) {
  std::istringstream input(value);
  std::string target;
  if (!(input >> target)) {
    return render_simple_gml_control("mi", std::move(value));
  }
  std::string text;
  std::getline(input, text);
  return ":mi refid='" + escape_gml_attr(target) + "'." + dot_text(text);
}

std::string render_layout_gml(std::string value) {
  std::istringstream input(value);
  std::string mode;
  if (!(input >> mode)) {
    return render_simple_gml_control("layout", std::move(value));
  }
  std::string rest;
  std::getline(input, rest);
  return ":layout mode='" + escape_gml_attr(mode) + "' values='" +
         escape_gml_attr(dot_text(rest)) + "'.";
}

std::string render_anchor_gml(std::string value) {
  value = trim_ascii(std::move(value));
  std::size_t cursor = 0;
  while (cursor < value.size() && is_topic_id_char(value[cursor])) {
    ++cursor;
  }
  const auto id = value.substr(0, cursor);
  auto rest = dot_text(value.substr(cursor));
  auto output = render_keyed_gml_control("anchor", "id", id);
  if (!rest.empty()) {
    output += rest;
  }
  return output;
}

std::string render_gml_segment(std::string segment, bool allow_topic_header) {
  segment = trim_ascii(std::move(segment));
  while (!segment.empty() && segment.front() == ',') {
    segment.erase(segment.begin());
    segment = trim_ascii(std::move(segment));
  }
  const auto lower = ascii_lower(segment);
  if (allow_topic_header && ascii_starts_with_case_insensitive(lower, "sh")) {
    return render_keyed_gml_control("topic", "id",
                                    extract_topic_header_id(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "ctopicn")) {
    return render_keyed_gml_control("topicn", "number",
                                    rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cparent")) {
    return render_keyed_gml_control("parent", "refid",
                                    rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cforwardlevel")) {
    return render_keyed_gml_control("next", "refid",
                                    rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cbacklevel")) {
    return render_keyed_gml_control("prev", "refid",
                                    rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "csummary")) {
    return render_keyed_gml_control("summary", "values",
                                    rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "chdlevel")) {
    return render_keyed_gml_control("hlevel", "tag",
                                    rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "csourcefn")) {
    return render_keyed_gml_control("source", "file",
                                    rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "st")) {
    return render_simple_gml_control("st", rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "ctocdef")) {
    return render_tocdef_gml(std::move(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "ctoce")) {
    return render_toc_entry_gml(rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "etoc")) {
    return ":etoc.";
  }
  if (ascii_starts_with_case_insensitive(lower, "cfontdef")) {
    return render_fontdef_gml(std::move(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cfont")) {
    return render_keyed_gml_control("font", "spans",
                                    rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cselect")) {
    return render_link_gml(rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cmenu")) {
    return ":menu.";
  }
  if (ascii_starts_with_case_insensitive(lower, "cmitem")) {
    return render_menu_item_gml(rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cemenu")) {
    return ":emenu.";
  }
  if (ascii_starts_with_case_insensitive(lower, "srfig")) {
    return render_keyed_gml_control("fig", "id", segment.substr(5));
  }
  if (ascii_starts_with_case_insensitive(lower, "srefig")) {
    return ":efig.";
  }
  if (ascii_starts_with_case_insensitive(lower, "srtbl")) {
    return render_keyed_gml_control("table", "id", segment.substr(5));
  }
  if (ascii_starts_with_case_insensitive(lower, "sretbl")) {
    return ":etable.";
  }
  if (ascii_starts_with_case_insensitive(lower, "sr")) {
    return render_anchor_gml(segment.substr(2));
  }
  if (ascii_starts_with_case_insensitive(lower, "cz")) {
    return render_layout_gml(rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "si")) {
    return render_simple_gml_control("index", rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "citerm")) {
    return render_simple_gml_control("iterm", rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cgpsep")) {
    return render_simple_gml_control("indexsep", rest_after_first_word(segment));
  }
  if (!segment.empty() &&
      std::tolower(static_cast<unsigned char>(segment.front())) == 'c') {
    return render_keyed_gml_control("control", "name", first_word(segment) +
                                                      " " +
                                                      rest_after_first_word(
                                                          segment));
  }
  return render_simple_gml_control("p", std::move(segment));
}

std::vector<std::string> render_gml_records(
    const std::vector<std::string>& decoded_records) {
  std::vector<std::string> rendered;
  for (std::size_t record_index = 0; record_index < decoded_records.size();
       ++record_index) {
    auto segments = split_decoded_markup_segments(decoded_records[record_index]);
    for (std::size_t segment_index = 0; segment_index < segments.size();
         ++segment_index) {
      const auto allow_topic_header = record_index == 0 && segment_index == 0;
      auto line = render_gml_segment(std::move(segments[segment_index]),
                                     allow_topic_header);
      if (!line.empty() && line != ":p.") {
        rendered.push_back(std::move(line));
      }
    }
  }
  return rendered;
}

bool looks_like_control_boundary(const std::string& decoded_record,
                                 const std::string& lower_record,
                                 std::size_t offset) {
  std::size_t key_start = std::string::npos;
  if (offset + 3 < decoded_record.size() &&
      decoded_record[offset] == '?' &&
      decoded_record[offset + 1] == ',') {
    key_start = offset + 2;
  } else if (offset + 3 < decoded_record.size() &&
             decoded_record[offset] == ',' &&
             decoded_record[offset + 1] == ' ') {
    key_start = offset + 2;
  } else if (offset + 2 < decoded_record.size() &&
             decoded_record[offset] == '?' &&
             decoded_record[offset + 1] == ' ') {
    key_start = offset + 2;
  } else {
    return false;
  }

  if (lower_record[key_start] != 'c') {
    return false;
  }

  const auto max_key_end =
      std::min(decoded_record.size(), key_start + std::size_t{20});
  for (auto cursor = key_start + 1; cursor < max_key_end; ++cursor) {
    const auto ch = static_cast<unsigned char>(lower_record[cursor]);
    if (decoded_record[cursor] == '=') {
      return cursor > key_start + 1;
    }
    if (std::isalnum(ch) == 0 && decoded_record[cursor] != '_') {
      return false;
    }
  }
  return false;
}

std::optional<std::uint16_t> read_compact_length(
    const std::vector<std::uint8_t>& bytes,
    std::size_t& offset,
    std::size_t end) {
  if (offset >= end) {
    return std::nullopt;
  }

  const auto first = bytes[offset++];
  if (first <= 0xEF) {
    return first;
  }
  if (offset >= end) {
    return std::nullopt;
  }

  const auto second = bytes[offset++];
  return static_cast<std::uint16_t>(((first - 0xF0) << 8) + second);
}

std::uint32_t physical_page_for_logical(const BooDirectory& directory,
                                        std::uint32_t logical_page) {
  if (logical_page == 0) {
    throw std::runtime_error("BOO logical page numbers are 1-based");
  }
  return directory.page_number + logical_page - 1;
}

TokenWords decode_dictionary_words(const std::vector<std::uint8_t>& bytes,
                                   std::size_t offset,
                                   std::size_t count) {
  TokenWords output;
  output.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    output.push_back(cp500_byte_to_token_word[bytes[offset + i]]);
  }
  return output;
}

void lowercase_positions(TokenWords& value,
                         const std::vector<std::uint8_t>& positions) {
  for (const auto position : positions) {
    if (position < value.size() && value[position] >= 'A' &&
        value[position] <= 'Z') {
      value[position] = static_cast<std::uint16_t>(value[position] + 32);
    }
  }
}

void uppercase_positions(TokenWords& value,
                         const std::vector<std::uint8_t>& positions) {
  for (const auto position : positions) {
    if (position < value.size() && value[position] >= 'a' &&
        value[position] <= 'z') {
      value[position] = static_cast<std::uint16_t>(value[position] - 32);
    }
  }
}

std::uint16_t map_token_word_to_upper_ascii(std::uint16_t word) {
  if (word >= 'a' && word <= 'z') {
    return static_cast<std::uint16_t>(word - 32);
  }
  return word;
}

void decode_dictionary_delta_range(
    std::map<std::uint16_t, TokenWords>& token_strings,
    std::uint16_t first_key,
    const std::vector<std::uint8_t>& bytes,
    std::size_t begin,
    std::size_t end) {
  if (begin >= end) {
    return;
  }

  auto cursor = begin;
  const auto base_count = bytes[cursor++];
  if (cursor + base_count > end) {
    return;
  }

  auto value = decode_dictionary_words(bytes, cursor, base_count);
  cursor += base_count;
  auto key = first_key;
  token_strings[key++] = value;

  while (cursor < end) {
    const auto op = bytes[cursor++];
    const auto mode = static_cast<std::uint8_t>(op >> 6);
    const auto count = static_cast<std::uint8_t>(op & 0x3F);

    if (mode == 0) {
      if (cursor + count > end) {
        break;
      }
      const std::vector<std::uint8_t> positions(bytes.begin() + cursor,
                                                bytes.begin() + cursor + count);
      cursor += count;
      lowercase_positions(value, positions);
    } else if (mode == 1) {
      if (cursor >= end) {
        break;
      }
      const auto literal_count = static_cast<std::uint8_t>(bytes[cursor++] &
                                                           0x3F);
      if (cursor + literal_count > end) {
        break;
      }
      value.resize(std::min<std::size_t>(count, value.size()));
      const auto literal_words =
          decode_dictionary_words(bytes, cursor, literal_count);
      value.insert(value.end(), literal_words.begin(), literal_words.end());
      cursor += literal_count;
    } else if (mode == 2) {
      if (cursor + count > end) {
        break;
      }
      const std::vector<std::uint8_t> positions(bytes.begin() + cursor,
                                                bytes.begin() + cursor + count);
      cursor += count;
      uppercase_positions(value, positions);
    } else {
      if (cursor + count > end) {
        break;
      }
      const auto literal_words = decode_dictionary_words(bytes, cursor, count);
      value.insert(value.end(), literal_words.begin(), literal_words.end());
      cursor += count;
    }

    token_strings[key++] = value;
  }
}

std::map<std::uint16_t, TokenWords> decode_experimental_dictionary(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory) {
  std::map<std::uint16_t, TokenWords> token_strings;
  const auto page_count = bytes.size() / boo_page_size;
  const auto dictionary_page_end =
      directory.dictionary_start_page +
      static_cast<std::uint32_t>(directory.dictionary_page_count);

  for (std::uint32_t logical_page = directory.dictionary_start_page;
       logical_page < dictionary_page_end;
       ++logical_page) {
    const auto page = physical_page_for_logical(directory, logical_page);
    if (page >= page_count) {
      continue;
    }
    const auto page_base = page * boo_page_size;
    if (read_be16(bytes, page_base) != 0x0100) {
      continue;
    }

    const auto used_end = read_be16(bytes, page_base + 2);
    std::size_t top_offset = page_base + 4;
    const auto page_end = page_base + std::min<std::size_t>(used_end,
                                                            boo_page_size);

    while (top_offset < page_end) {
      auto top_length_offset = top_offset;
      const auto top_length =
          read_compact_length(bytes, top_length_offset, page_end);
      if (!top_length || *top_length == 0 ||
          top_length_offset + *top_length > page_end) {
        break;
      }

      const auto top_payload = top_length_offset;
      const auto top_end = top_payload + *top_length;
      if (top_payload + 3 <= top_end) {
        const auto prefix_length = bytes[top_payload + 2];
        const auto nested_begin =
            top_payload + 3 + static_cast<std::size_t>(prefix_length);
        if (nested_begin < top_end) {
          std::size_t nested_offset = nested_begin;
          while (nested_offset < top_end) {
            auto nested_length_offset = nested_offset;
            const auto nested_length =
                read_compact_length(bytes, nested_length_offset, top_end);
            if (!nested_length || *nested_length == 0 ||
                nested_length_offset + *nested_length > top_end) {
              break;
            }

            const auto nested_payload = nested_length_offset;
            const auto nested_end = nested_payload + *nested_length;
            if (nested_payload + 2 < nested_end) {
              const auto key = read_be16(bytes, nested_payload);
              decode_dictionary_delta_range(token_strings,
                                            key,
                                            bytes,
                                            nested_payload + 2,
                                            nested_end);
            }
            nested_offset = nested_end;
          }
        }
      }

      top_offset = top_end;
    }
  }

  return token_strings;
}

TokenWords resolve_experimental_token(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    const std::map<std::uint16_t, TokenWords>& token_strings,
    std::uint8_t first,
    std::optional<std::uint8_t> second) {
  std::uint16_t key = 0;
  if (second) {
    key = static_cast<std::uint16_t>((first << 8) | *second);
  } else {
    const auto token_map_entry =
        static_cast<std::size_t>(directory.page_number) * boo_page_size +
        directory.token_map_offset + static_cast<std::size_t>(first) * 2;
    if (token_map_entry + 2 > bytes.size()) {
      return {};
    }
    key = read_be16(bytes, token_map_entry);
  }

  const auto found = token_strings.find(key);
  if (found == token_strings.end()) {
    return {};
  }
  return found->second;
}

std::string token_words_to_ascii(const TokenWords& words) {
  std::string output;
  output.reserve(words.size());
  for (const auto word : words) {
    if (word >= 0x20 && word <= 0x7E) {
      output.push_back(static_cast<char>(word));
    } else if (word == 0x00A0) {
      output.push_back(' ');
    } else {
      output.push_back('?');
    }
  }
  return output;
}

TokenWords assemble_logical_record(const std::vector<TokenWords>& tokens) {
  TokenWords output;
  std::uint16_t spacing_control = 2;

  for (const auto& token : tokens) {
    TokenWords words = token;
    spacing_control = words.empty() ? 3 : words.front();

    if (!words.empty() && words.front() < 4) {
      words.erase(words.begin());
      if (!output.empty()) {
        if (spacing_control == 1) {
          output.pop_back();
          if (words.empty()) {
            spacing_control = 2;
          }
        } else if (spacing_control == 0) {
          output.pop_back();
          spacing_control = 2;
        }
      }
    }

    output.insert(output.end(), words.begin(), words.end());

    if (!words.empty() && words.back() == ' ') {
      spacing_control = 2;
    }
    if (spacing_control != 2) {
      output.push_back(' ');
    }
  }

  if (output.size() > 1 && spacing_control != 2) {
    output.pop_back();
  }

  if (!output.empty() && output.front() != ' ' && output.front() != 'S') {
    for (auto& word : output) {
      if (word == ' ' || word == '=' || word == 0) {
        break;
      }
      word = map_token_word_to_upper_ascii(word);
    }
  }

  return output;
}

std::vector<BooLogicalControl> extract_logical_controls(
    const std::string& decoded_record) {
  struct ControlKey {
    const char* canonical;
    const char* lower;
  };
  static const std::array<ControlKey, 11> keys = {{
      {"CLANGUAGE", "clanguage="},
      {"CVERSION", "cversion="},
      {"CBLDVERS", "cbldvers="},
      {"CREFLOW", "creflow="},
      {"CTITLE", "ctitle="},
      {"CSTITLE", "cstitle="},
      {"CCOPYRIGHT", "ccopyright="},
      {"CSECURITY", "csecurity="},
      {"CDATE", "cdate="},
      {"CAUTHOR", "cauthor="},
      {"CDOCNUM", "cdocnum="},
  }};

  std::vector<BooLogicalControl> controls;
  const auto lower_record = ascii_lower(decoded_record);
  for (const auto& key : keys) {
    const std::string key_text(key.lower);
    const auto found = lower_record.find(key_text);
    if (found == std::string::npos) {
      continue;
    }

    auto value_begin = found + key_text.size();
    auto value_end = decoded_record.size();
    for (const auto& next_key : keys) {
      const auto next = lower_record.find(next_key.lower, value_begin);
      if (next != std::string::npos) {
        value_end = std::min(value_end, next);
      }
    }
    for (auto cursor = value_begin; cursor + 3 < decoded_record.size();
         ++cursor) {
      if (looks_like_control_boundary(decoded_record, lower_record, cursor)) {
        value_end = std::min(value_end, cursor);
        break;
      }
    }
    static const std::array<const char*, 8> auxiliary_boundaries = {
        "?csource=", "?cbasenum=", "?cdoclevel=", "?cfront=",
        "?ccontents=", "?cfigures=", "?ctables=", "?cindex="};
    for (const auto* boundary : auxiliary_boundaries) {
      const auto next = lower_record.find(boundary, value_begin);
      if (next != std::string::npos) {
        value_end = std::min(value_end, next);
      }
    }

    auto value =
        trim_ascii(decoded_record.substr(value_begin, value_end - value_begin));
    controls.push_back({key.canonical,
                        normalize_logical_control_value(key.canonical,
                                                        value)});
  }
  return controls;
}

bool looks_like_toc_entry_boundary(const std::string& lower_record,
                                   std::size_t offset) {
  static const std::array<const char*, 5> boundaries = {
      "?ctoce ", ", ctoce ", "?ctocdef=", ", ctocdef=", "?sh"};
  for (const auto* boundary : boundaries) {
    const std::string boundary_text(boundary);
    if (offset + boundary_text.size() <= lower_record.size() &&
        lower_record.compare(offset, boundary_text.size(), boundary_text) ==
            0) {
      return true;
    }
  }
  return false;
}

std::size_t find_toc_end_marker(const std::string& lower_record,
                                std::size_t offset,
                                std::size_t limit) {
  std::size_t end_marker = std::string::npos;
  const auto cz_off_etoc = lower_record.find("cz off etoc", offset);
  if (cz_off_etoc != std::string::npos && cz_off_etoc < limit) {
    end_marker = std::min(end_marker, cz_off_etoc);
  }
  const auto etoc = lower_record.find("etoc", offset);
  if (etoc != std::string::npos && etoc < limit) {
    end_marker = std::min(end_marker, etoc);
  }
  return end_marker;
}

std::vector<TocEntry> extract_toc_entries(const std::string& decoded_record) {
  std::vector<TocEntry> entries;
  const auto lower_record = ascii_lower(decoded_record);
  std::size_t search_offset = 0;

  while (search_offset < decoded_record.size()) {
    const auto found = lower_record.find("ctoce ", search_offset);
    if (found == std::string::npos) {
      break;
    }
    const auto record_toc_end =
        find_toc_end_marker(lower_record, search_offset, found + 1);
    if (record_toc_end != std::string::npos && record_toc_end < found) {
      break;
    }

    const auto marker_size = std::string("ctoce ").size();
    auto value_begin = found + marker_size;
    auto value_end = decoded_record.size();
    const auto next_entry = lower_record.find("ctoce ", value_begin);
    const auto toc_end =
        find_toc_end_marker(lower_record, value_begin, value_end);
    if (toc_end != std::string::npos &&
        (next_entry == std::string::npos || toc_end < next_entry)) {
      value_end = toc_end;
    }
    if (next_entry != std::string::npos) {
      value_end = std::min(value_end, next_entry);
    }
    for (auto cursor = value_begin; cursor < value_end; ++cursor) {
      if (looks_like_toc_entry_boundary(lower_record, cursor)) {
        value_end = cursor;
        break;
      }
    }

    const auto value =
        trim_ascii(decoded_record.substr(value_begin, value_end - value_begin));
    std::istringstream input(value);
    std::uint32_t level = 0;
    std::uint32_t style = 0;
    std::string id;
    if (input >> level >> style >> id) {
      if (style == 0) {
        search_offset = found + marker_size;
        continue;
      }
      std::string title;
      std::getline(input, title);
      title = normalize_toc_title(trim_ascii(title));
      if (!id.empty() && !title.empty()) {
        entries.push_back(
            {normalize_toc_id(id), title, level, style});
      }
    }

    search_offset = found + marker_size;
  }

  return entries;
}

bool is_contents_topic_record(const std::string& decoded_record) {
  const auto lower_record = ascii_lower(decoded_record);
  return lower_record.find("shcontents") != std::string::npos ||
         lower_record.find("chdlevel :toc") != std::string::npos ||
         lower_record.find("ctocdef=") != std::string::npos;
}

bool is_topic_header_record(const std::string& decoded_record) {
  auto lower_record = ascii_lower(trim_ascii(decoded_record));
  lower_record.erase(0, skip_decoded_separators(lower_record));
  return lower_record.rfind("sh", 0) == 0 &&
         lower_record.find("ctopicn") != std::string::npos;
}

std::vector<std::string> decode_experimental_logical_records(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory) {
  std::vector<std::string> records;
  const auto token_strings = decode_experimental_dictionary(bytes, directory);
  if (token_strings.empty()) {
    return records;
  }

  std::vector<std::size_t> candidate_pages;
  const auto content_page_end =
      directory.content_start_page +
      static_cast<std::uint32_t>(directory.content_page_count);
  for (std::uint32_t logical_page = directory.content_start_page;
       logical_page < content_page_end;
       ++logical_page) {
    candidate_pages.push_back(physical_page_for_logical(directory,
                                                        logical_page));
  }

  const auto page_count = bytes.size() / boo_page_size;
  for (std::size_t page = 0; page < page_count; ++page) {
    const auto page_base = page * boo_page_size;
    if (read_be16(bytes, page_base) == 0x0001) {
      candidate_pages.push_back(page);
    }
  }

  for (const auto page : candidate_pages) {
    if (page >= page_count) {
      continue;
    }

    const auto page_base = page * boo_page_size;
    const auto page_class = read_be16(bytes, page_base);
    if (page_class != 0x0000 && page_class != 0x0001) {
      continue;
    }

    const auto used_end = read_be16(bytes, page_base + 2);
    const auto page_end = page_base + std::min<std::size_t>(used_end,
                                                            boo_page_size);
    std::size_t record_offset = page_base + 4;
    while (record_offset < page_end) {
      auto length_offset = record_offset;
      const auto record_length =
          read_compact_length(bytes, length_offset, page_end);
      if (!record_length || length_offset + *record_length > page_end) {
        break;
      }

      const auto payload_end = length_offset + *record_length;
      std::vector<TokenWords> record_tokens;
      for (auto cursor = length_offset; cursor < payload_end;) {
        const auto first = bytes[cursor++];
        if (first >= directory.token_threshold && cursor < payload_end) {
          const auto second = bytes[cursor++];
          const auto token_words = resolve_experimental_token(bytes,
                                                              directory,
                                                              token_strings,
                                                              first,
                                                              second);
          record_tokens.push_back(token_words);
        } else {
          const auto token_words = resolve_experimental_token(bytes,
                                                              directory,
                                                              token_strings,
                                                              first,
                                                              std::nullopt);
          record_tokens.push_back(token_words);
        }
      }

      const auto decoded_words = assemble_logical_record(record_tokens);
      records.push_back(token_words_to_ascii(decoded_words));
      record_offset = payload_end;
    }
  }

  return records;
}

std::vector<BooLogicalControl> extract_book_logical_controls(
    const std::vector<std::string>& decoded_records) {
  std::vector<BooLogicalControl> controls;
  for (const auto& decoded : decoded_records) {
    auto record_controls = extract_logical_controls(decoded);
    const auto has_docnum =
        std::any_of(record_controls.begin(),
                    record_controls.end(),
                    [](const BooLogicalControl& control) {
                      return control.key == "CDOCNUM";
                    });
    controls.insert(controls.end(),
                    record_controls.begin(),
                    record_controls.end());
    if (has_docnum) {
      return controls;
    }
  }
  return controls;
}

const TopicData* find_topic_data(const std::vector<TopicData>& topics,
                                 const std::string& topic_id) {
  const auto normalized_id = normalize_toc_id(topic_id);
  const auto found = std::find_if(topics.begin(), topics.end(),
                                  [&](const TopicData& topic) {
                                    return topic.id == normalized_id ||
                                           ascii_equals_case_insensitive(
                                               topic.id, topic_id);
                                  });
  if (found == topics.end()) {
    return nullptr;
  }
  return &*found;
}

void attach_topic_data(TocEntry& entry, const TopicData& topic) {
  entry.heading_level = topic.heading_level;
  entry.topic_number = topic.topic_number;
  entry.start_logical_record = topic.start_logical_record;
  entry.end_logical_record = topic.end_logical_record;
  entry.raw_records = render_gml_records(topic.raw_records);
}

std::vector<TocEntry> build_table_of_contents(
    const std::vector<std::string>& decoded_records,
    const std::vector<TopicData>& topics) {
  std::vector<TocEntry> toc;
  bool in_contents_topic = false;
  for (const auto& decoded : decoded_records) {
    if (!in_contents_topic) {
      if (!is_contents_topic_record(decoded)) {
        continue;
      }
      in_contents_topic = true;
    } else if (is_topic_header_record(decoded)) {
      break;
    }

    auto entries = extract_toc_entries(decoded);
    if (entries.empty()) {
      continue;
    }
    for (auto& entry : entries) {
      if (const auto* topic = find_topic_data(topics, entry.id)) {
        attach_topic_data(entry, *topic);
      }
    }
    toc.insert(toc.end(), entries.begin(), entries.end());
  }
  return toc;
}

std::vector<std::string> build_raw_gml_records(
    const std::vector<TopicData>& topics) {
  std::vector<std::string> records;
  for (const auto& topic : topics) {
    auto topic_records = render_gml_records(topic.raw_records);
    records.insert(records.end(),
                   std::make_move_iterator(topic_records.begin()),
                   std::make_move_iterator(topic_records.end()));
  }
  return records;
}

std::vector<TopicData> build_topics(
    const std::vector<std::string>& decoded_records) {
  std::vector<TopicData> topics;

  std::vector<std::size_t> header_indexes;
  for (std::size_t index = 0; index < decoded_records.size(); ++index) {
    if (is_topic_header_record(decoded_records[index]) &&
        !extract_topic_header_id(decoded_records[index]).empty()) {
      header_indexes.push_back(index);
    }
  }
  if (header_indexes.empty()) {
    return topics;
  }

  topics.reserve(header_indexes.size());
  std::set<std::string> seen_topic_ids;
  for (std::size_t index = 0; index < header_indexes.size(); ++index) {
    const auto record_begin = header_indexes[index];
    const auto record_end =
        (index + 1 < header_indexes.size())
            ? header_indexes[index + 1]
            : decoded_records.size();
    if (record_begin >= record_end) {
      continue;
    }

    TopicData topic;
    const auto& header = decoded_records[record_begin];
    topic.topic_number = extract_uint_control_value(header, "ctopicn ");
    topic.start_logical_record =
        static_cast<std::uint32_t>(record_begin + 1);
    topic.end_logical_record = static_cast<std::uint32_t>(record_end + 1);
    topic.raw_records.assign(decoded_records.begin() +
                                 static_cast<std::ptrdiff_t>(record_begin),
                             decoded_records.begin() +
                                 static_cast<std::ptrdiff_t>(record_end));

    topic.id = extract_topic_header_id(header);
    topic.heading_level =
        extract_control_value_until_boundary(header, "chdlevel ");
    topic.title =
        normalize_toc_title(extract_control_value_until_boundary(header,
                                                                 "st "));
    if (!topic.id.empty() && seen_topic_ids.insert(topic.id).second) {
      topics.push_back(std::move(topic));
    }
  }
  return topics;
}

BooBookProperties build_book_properties(
    const std::vector<BooLogicalControl>& controls) {
  BooBookProperties properties;
  for (const auto& control : controls) {
    if (control.key == "CLANGUAGE") {
      properties.language = control.value;
    } else if (control.key == "CVERSION") {
      properties.version = control.value;
    } else if (control.key == "CBLDVERS") {
      properties.build_version = control.value;
    } else if (control.key == "CREFLOW") {
      properties.reflow = ascii_lower(control.value) == "on";
    } else if (control.key == "CTITLE") {
      properties.title = control.value;
    } else if (control.key == "CSTITLE") {
      properties.short_title = control.value;
    } else if (control.key == "CCOPYRIGHT") {
      properties.copyright = control.value;
    } else if (control.key == "CSECURITY") {
      properties.security = control.value;
    } else if (control.key == "CDATE") {
      properties.date = control.value;
    } else if (control.key == "CAUTHOR") {
      if (!control.value.empty()) {
        properties.authors.push_back(control.value);
      }
    } else if (control.key == "CDOCNUM") {
      properties.document_number = control.value;
    }
  }
  return properties;
}

BooPageRole classify_run(std::uint32_t start_page,
                         std::uint16_t page_class,
                         const BooDirectory& directory) {
  if (start_page == 0) {
    return BooPageRole::file_header;
  }
  if (start_page == directory.page_number) {
    return BooPageRole::directory;
  }
  if (start_page == physical_page_for_logical(directory,
                                              directory.dictionary_start_page) &&
      page_class == 0x0100) {
    return BooPageRole::dictionary;
  }
  if (start_page == physical_page_for_logical(directory,
                                              directory.content_start_page) &&
      page_class == 0x0000) {
    return BooPageRole::content;
  }
  if (page_class == 0x0001) {
    return BooPageRole::logical_records;
  }
  return BooPageRole::unknown;
}

std::vector<BooPageRun> build_page_runs(const std::vector<std::uint8_t>& bytes,
                                        const BooDirectory& directory) {
  std::vector<BooPageRun> runs;
  const auto page_count = static_cast<std::uint32_t>(bytes.size() /
                                                    boo_page_size);
  if (page_count == 0) {
    return runs;
  }

  std::uint32_t run_start = 0;
  std::uint16_t run_class = read_be16(bytes, 0);

  for (std::uint32_t page = 1; page < page_count; ++page) {
    const auto page_class = read_be16(bytes, page * boo_page_size);
    if (page_class == run_class) {
      continue;
    }

    runs.push_back({run_start,
                    page - run_start,
                    run_class,
                    classify_run(run_start, run_class, directory)});
    run_start = page;
    run_class = page_class;
  }

  runs.push_back({run_start,
                  page_count - run_start,
                  run_class,
                  classify_run(run_start, run_class, directory)});
  return runs;
}

} // namespace

BooDocument BooDocument::open(const std::filesystem::path& path) {
  BooDocument document;
  document.bytes_ = read_file(path);

  if (document.bytes_.size() < boo_page_size) {
    throw std::runtime_error("BOO file is smaller than one 4096-byte page: " +
                             path.string());
  }
  if (document.bytes_.size() % boo_page_size != 0) {
    throw std::runtime_error("BOO file size is not a multiple of 4096 bytes: " +
                             path.string());
  }

  document.metadata_.path = path;
  document.metadata_.file_size =
      static_cast<std::uint64_t>(document.bytes_.size());
  document.metadata_.page_count =
      static_cast<std::uint32_t>(document.bytes_.size() / boo_page_size);

  document.file_header_.directory_page_number = read_be16(document.bytes_, 0);
  document.file_header_.unknown_0002 = read_be16(document.bytes_, 2);
  document.file_header_.unknown_0004 = read_be32(document.bytes_, 4);
  document.file_header_.copyright_text =
      trim_right_spaces(decode_cp037(document.bytes_, 0x000C, 128));
  if (document.bytes_.size() >= 0x0106) {
    document.file_header_.unknown_0102 =
        std::array<std::uint8_t, 4>{document.bytes_[0x0102],
                                    document.bytes_[0x0103],
                                    document.bytes_[0x0104],
                                    document.bytes_[0x0105]};
  }

  const auto directory_page = document.file_header_.directory_page_number;
  if (directory_page >= document.metadata_.page_count) {
    throw std::runtime_error("BOO directory page is outside the file");
  }

  const std::size_t directory_base =
      static_cast<std::size_t>(directory_page) * boo_page_size;
  document.directory_.page_number = directory_page;
  document.directory_.version_text =
      decode_cp037(document.bytes_, directory_base + 0x0010, 4);
  document.directory_.version_variant = document.bytes_[directory_base + 0x0013];
  document.directory_.token_threshold = document.bytes_[directory_base + 0x0014];
  document.directory_.last_page_number =
      read_be16(document.bytes_, directory_base + 0x0016);
  document.directory_.token_map_offset =
      read_be16(document.bytes_, directory_base + 0x0022);
  document.directory_.dictionary_start_page =
      read_be16(document.bytes_, directory_base + 0x0028);
  document.directory_.dictionary_page_count =
      read_be16(document.bytes_, directory_base + 0x002E);
  document.directory_.content_page_count =
      read_be16(document.bytes_, directory_base + 0x0038);
  document.directory_.content_start_page =
      read_be16(document.bytes_, directory_base + 0x003A);
  document.directory_.stream_table_offset =
      read_be16(document.bytes_, directory_base + 0x003C);
  document.directory_.stream_table_count =
      read_be16(document.bytes_, directory_base + 0x003E);
  document.directory_.secondary_table_offset =
      read_be16(document.bytes_, directory_base + 0x0040);
  document.directory_.date =
      decode_cp037(document.bytes_, directory_base + 0x0044, 8);
  document.directory_.time =
      decode_cp037(document.bytes_, directory_base + 0x004E, 8);

  const auto last_physical_page =
      physical_page_for_logical(document.directory_,
                                document.directory_.last_page_number);
  if (last_physical_page >= document.metadata_.page_count) {
    throw std::runtime_error("BOO directory last-page field points outside the "
                             "file");
  }

  document.page_runs_ = build_page_runs(document.bytes_, document.directory_);
  const auto decoded_records =
      decode_experimental_logical_records(document.bytes_, document.directory_);
  document.logical_controls_ = extract_book_logical_controls(decoded_records);
  document.book_properties_ =
      build_book_properties(document.logical_controls_);
  const auto topics = build_topics(decoded_records);
  document.toc_ = build_table_of_contents(decoded_records, topics);
  document.raw_gml_records_ = build_raw_gml_records(topics);
  document.resources_ = build_resources(document.bytes_, document.directory_);
  return document;
}

const BooMetadata& BooDocument::metadata() const noexcept {
  return metadata_;
}

const BooPage0Header& BooDocument::file_header() const noexcept {
  return file_header_;
}

const BooDirectory& BooDocument::directory() const noexcept {
  return directory_;
}

const BooBookProperties& BooDocument::book_properties() const noexcept {
  return book_properties_;
}

const std::vector<BooPageRun>& BooDocument::page_runs() const noexcept {
  return page_runs_;
}

const std::vector<BooLogicalControl>& BooDocument::logical_controls()
    const noexcept {
  return logical_controls_;
}

const std::vector<TocEntry>& BooDocument::table_of_contents() const noexcept {
  return toc_;
}

const std::vector<std::string>& BooDocument::raw_gml_records() const noexcept {
  return raw_gml_records_;
}

const std::vector<ResourceEntry>& BooDocument::resources() const noexcept {
  return resources_;
}

const TocEntry* BooDocument::find_toc_entry(const std::string& topic_id)
    const noexcept {
  const auto normalized_id = normalize_toc_id(topic_id);
  const auto found = std::find_if(toc_.begin(), toc_.end(),
                                  [&](const TocEntry& entry) {
                                    return entry.id == normalized_id ||
                                           ascii_equals_case_insensitive(
                                               entry.id, topic_id);
                                  });
  if (found == toc_.end()) {
    return nullptr;
  }
  return &*found;
}

std::vector<std::uint8_t> BooDocument::read_page(
    std::uint32_t page_number) const {
  if (page_number >= metadata_.page_count) {
    throw std::out_of_range("BOO page number is outside the file");
  }

  const auto begin = bytes_.begin() +
                     static_cast<std::ptrdiff_t>(page_number * boo_page_size);
  return {begin, begin + boo_page_size};
}

std::vector<std::uint8_t> BooDocument::read_resource_data(
    const std::string& resource_id) const {
  auto found = std::find_if(resources_.begin(), resources_.end(),
                            [&](const ResourceEntry& resource) {
                              return resource.id == resource_id ||
                                     ascii_equals_case_insensitive(
                                         resource.id, resource_id);
                            });
  if (found == resources_.end()) {
    throw std::out_of_range("BOO resource id was not found: " + resource_id);
  }
  if (!byte_range_is_valid(bytes_, found->offset, found->size)) {
    throw std::runtime_error("BOO resource byte range is outside the file");
  }

  const auto begin = bytes_.begin() +
                     static_cast<std::ptrdiff_t>(found->offset);
  return {begin, begin + static_cast<std::ptrdiff_t>(found->size)};
}

std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');

  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i != 0) {
      output << ' ';
    }
    output << std::setw(2) << static_cast<unsigned>(bytes[i]);
  }

  return output.str();
}

const char* to_string(BooPageRole role) noexcept {
  switch (role) {
  case BooPageRole::file_header:
    return "file_header";
  case BooPageRole::directory:
    return "directory";
  case BooPageRole::dictionary:
    return "dictionary";
  case BooPageRole::content:
    return "content";
  case BooPageRole::logical_records:
    return "logical_records";
  case BooPageRole::unknown:
    return "unknown";
  }
  return "unknown";
}

const char* to_string(ResourceLayout layout) noexcept {
  switch (layout) {
  case ResourceLayout::legacy_v12:
    return "legacy_v12";
  case ResourceLayout::legacy_v13:
    return "legacy_v13";
  case ResourceLayout::converted_v14:
    return "converted_v14";
  case ResourceLayout::unknown:
    return "unknown";
  }
  return "unknown";
}

} // namespace geist
