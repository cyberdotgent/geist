#include "geist/boo.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace geist {
namespace {

using TokenWords = std::vector<std::uint16_t>;

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
          lower_part == "before" || lower_part == "between" ||
          lower_part == "by" || lower_part == "for" ||
          lower_part == "from" || lower_part == "in" ||
          lower_part == "into" || lower_part == "of" ||
          lower_part == "on" || lower_part == "or" ||
          lower_part == "the" || lower_part == "to" ||
          lower_part == "with" || lower_part == "without";
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
    const std::vector<std::uint8_t>& bytes) {
  std::map<std::uint16_t, TokenWords> token_strings;
  const auto page_count = bytes.size() / boo_page_size;

  for (std::size_t page = 0; page < page_count; ++page) {
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

std::vector<TocEntry> extract_toc_entries(const std::string& decoded_record) {
  std::vector<TocEntry> entries;
  const auto lower_record = ascii_lower(decoded_record);
  std::size_t search_offset = 0;

  while (search_offset < decoded_record.size()) {
    const auto found = lower_record.find("ctoce ", search_offset);
    if (found == std::string::npos) {
      break;
    }

    const auto marker_size = std::string("ctoce ").size();
    auto value_begin = found + marker_size;
    auto value_end = decoded_record.size();
    const auto next_entry = lower_record.find("ctoce ", value_begin);
    if (next_entry != std::string::npos) {
      value_end = next_entry;
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

std::vector<std::string> decode_experimental_logical_records(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory) {
  std::vector<std::string> records;
  const auto token_strings = decode_experimental_dictionary(bytes);
  if (token_strings.empty()) {
    return records;
  }

  std::vector<std::size_t> candidate_pages;
  const auto content_page_end =
      directory.content_start_page +
      static_cast<std::uint32_t>(directory.content_page_count);
  for (std::uint32_t page = directory.content_start_page;
       page < content_page_end;
       ++page) {
    candidate_pages.push_back(page);
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

std::vector<TocEntry> build_table_of_contents(
    const std::vector<std::string>& decoded_records) {
  std::vector<TocEntry> toc;
  for (const auto& decoded : decoded_records) {
    auto entries = extract_toc_entries(decoded);
    toc.insert(toc.end(), entries.begin(), entries.end());
  }
  return toc;
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
  if (start_page == directory.dictionary_start_page && page_class == 0x0100) {
    return BooPageRole::dictionary;
  }
  if (start_page == directory.content_start_page && page_class == 0x0000) {
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

  if (static_cast<std::uint32_t>(document.directory_.last_page_number) + 1U !=
      document.metadata_.page_count) {
    throw std::runtime_error("BOO directory last-page field does not match file "
                             "page count");
  }

  document.page_runs_ = build_page_runs(document.bytes_, document.directory_);
  const auto decoded_records =
      decode_experimental_logical_records(document.bytes_, document.directory_);
  document.logical_controls_ = extract_book_logical_controls(decoded_records);
  document.book_properties_ =
      build_book_properties(document.logical_controls_);
  document.toc_ = build_table_of_contents(decoded_records);
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

const std::vector<ResourceEntry>& BooDocument::resources() const noexcept {
  return resources_;
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

std::string BooDocument::render_chapter_markdown(
    const std::string& chapter_id) const {
  std::ostringstream output;
  output << "# " << (chapter_id.empty() ? "BOO chapter" : chapter_id) << "\n\n";
  output << "_Chapter rendering is not implemented yet._\n";
  return output.str();
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

} // namespace geist
