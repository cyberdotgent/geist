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
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  std::size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }
  if (first != 0) {
    value.erase(0, first);
  }
  return value;
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

std::string decode_dictionary_bytes(const std::vector<std::uint8_t>& bytes,
                                    std::size_t offset,
                                    std::size_t count) {
  std::string output;
  output.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    output.push_back(decode_cp037_byte(bytes[offset + i]));
  }
  return output;
}

void lowercase_positions(std::string& value,
                         const std::vector<std::uint8_t>& positions) {
  for (const auto position : positions) {
    if (position < value.size()) {
      value[position] = static_cast<char>(
          std::tolower(static_cast<unsigned char>(value[position])));
    }
  }
}

void uppercase_positions(std::string& value,
                         const std::vector<std::uint8_t>& positions) {
  for (const auto position : positions) {
    if (position < value.size()) {
      value[position] = static_cast<char>(
          std::toupper(static_cast<unsigned char>(value[position])));
    }
  }
}

void decode_dictionary_delta_range(
    std::map<std::uint16_t, std::string>& token_strings,
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

  auto value = decode_dictionary_bytes(bytes, cursor, base_count);
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
      value += decode_dictionary_bytes(bytes, cursor, literal_count);
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
      value += decode_dictionary_bytes(bytes, cursor, count);
      cursor += count;
    }

    token_strings[key++] = value;
  }
}

std::map<std::uint16_t, std::string> decode_experimental_dictionary(
    const std::vector<std::uint8_t>& bytes) {
  std::map<std::uint16_t, std::string> token_strings;
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

std::string resolve_experimental_token(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    const std::map<std::uint16_t, std::string>& token_strings,
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

std::vector<BooLogicalControl> extract_logical_controls(
    const std::string& decoded_record) {
  static const std::array<const char*, 11> keys = {
      "CLANGUAGE=", "CVERSION=",  "CBLDVERS=", "CREFLOW=", "CTITLE=",
      "CSTITLE=",   "CCOPYRIGHT=", "CSECURITY=", "CDATE=",   "CAUTHOR=",
      "CDOCNUM="};

  std::vector<BooLogicalControl> controls;
  for (const auto* key : keys) {
    const std::string key_text(key);
    const auto found = decoded_record.find(key_text);
    if (found == std::string::npos) {
      continue;
    }

    auto value_begin = found + key_text.size();
    auto value_end = decoded_record.size();
    for (const auto* next_key : keys) {
      const auto next = decoded_record.find(next_key, value_begin);
      if (next != std::string::npos) {
        value_end = std::min(value_end, next);
      }
    }

    controls.push_back(
        {key_text.substr(0, key_text.size() - 1),
         trim_ascii(decoded_record.substr(value_begin,
                                          value_end - value_begin))});
  }
  return controls;
}

std::vector<BooLogicalControl> decode_experimental_logical_controls(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory) {
  std::vector<BooLogicalControl> controls;
  const auto token_strings = decode_experimental_dictionary(bytes);
  if (token_strings.empty()) {
    return controls;
  }

  const auto page_count = bytes.size() / boo_page_size;
  for (std::size_t page = 0; page < page_count; ++page) {
    const auto page_base = page * boo_page_size;
    if (read_be16(bytes, page_base) != 0x0001) {
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
      std::string decoded;
      for (auto cursor = length_offset; cursor < payload_end;) {
        const auto first = bytes[cursor++];
        if (first >= directory.token_threshold && cursor < payload_end) {
          const auto second = bytes[cursor++];
          decoded += resolve_experimental_token(bytes,
                                                directory,
                                                token_strings,
                                                first,
                                                second);
        } else {
          decoded += resolve_experimental_token(bytes,
                                                directory,
                                                token_strings,
                                                first,
                                                std::nullopt);
        }
      }

      auto record_controls = extract_logical_controls(decoded);
      controls.insert(controls.end(),
                      record_controls.begin(),
                      record_controls.end());
      record_offset = payload_end;
    }
  }

  return controls;
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
  document.logical_controls_ =
      decode_experimental_logical_controls(document.bytes_,
                                           document.directory_);
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
