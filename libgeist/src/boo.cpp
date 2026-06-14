#include "geist/boo.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
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
