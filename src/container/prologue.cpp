// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The container prologue, shared by `BooDocument::open` and `probe_book`.
//
// It was lifted verbatim out of `BooDocument::open`, which is the point: a
// shelf listing that read these fields through a second implementation would
// be free to drift from what a opened document reports, and a divergence
// between two readers of the same bytes is a defect class this project has
// paid for before.

#include "geist/detail/core/internal.hpp"

#include <stdexcept>

namespace geist::detail {

ContainerPrologue read_container_prologue(
    const std::vector<std::uint8_t>& bytes,
    const std::filesystem::path& path) {
  ContainerPrologue result;
  if (bytes.size() < boo_page_size) {
    throw std::runtime_error("BOO file is smaller than one 4096-byte page: " +
                             path.string());
  }
  if (bytes.size() % boo_page_size != 0) {
    throw std::runtime_error("BOO file size is not a multiple of 4096 bytes: " +
                             path.string());
  }

  result.metadata.path = path;
  result.metadata.file_size =
      static_cast<std::uint64_t>(bytes.size());
  result.metadata.page_count =
      static_cast<std::uint32_t>(bytes.size() / boo_page_size);

  result.file_header.directory_page_number = read_be16(bytes, 0);
  result.file_header.unknown_0002 = read_be16(bytes, 2);
  result.file_header.unknown_0004 = read_be32(bytes, 4);
  result.file_header.copyright_text =
      trim_right_spaces(EbcdicCodec::cp037().decode_ascii(
          bytes,
          0x000C,
          128,
          "unexpected end of BOO file while reading text"));
  if (bytes.size() >= 0x0106) {
    result.file_header.unknown_0102 =
        std::array<std::uint8_t, 4>{bytes[0x0102],
                                    bytes[0x0103],
                                    bytes[0x0104],
                                    bytes[0x0105]};
  }

  const auto directory_page = result.file_header.directory_page_number;
  if (directory_page >= result.metadata.page_count) {
    throw std::runtime_error("BOO directory page is outside the file");
  }

  const std::size_t directory_base =
      static_cast<std::size_t>(directory_page) * boo_page_size;
  result.directory.page_number = directory_page;
  result.directory.version_text =
      EbcdicCodec::cp037().decode_ascii(
          bytes,
          directory_base + 0x0010,
          4,
          "unexpected end of BOO file while reading text");
  result.directory.version_variant = bytes[directory_base + 0x0013];
  result.directory.token_threshold = bytes[directory_base + 0x0014];
  result.directory.last_page_number =
      read_be16(bytes, directory_base + 0x0016);
  result.directory.token_map_offset =
      read_be16(bytes, directory_base + 0x0022);
  result.directory.dictionary_start_page =
      read_be16(bytes, directory_base + 0x0028);
  result.directory.dictionary_page_count =
      read_be16(bytes, directory_base + 0x002E);
  result.directory.content_page_index_offset =
      read_be16(bytes, directory_base + 0x0034);
  result.directory.logical_record_count =
      read_be16(bytes, directory_base + 0x0036);
  result.directory.content_page_count =
      read_be16(bytes, directory_base + 0x0038);
  result.directory.content_start_page =
      read_be16(bytes, directory_base + 0x003A);
  result.directory.stream_table_offset =
      read_be16(bytes, directory_base + 0x003C);
  result.directory.stream_table_count =
      read_be16(bytes, directory_base + 0x003E);
  result.directory.secondary_table_offset =
      read_be16(bytes, directory_base + 0x0040);
  result.directory.date =
      EbcdicCodec::cp037().decode_ascii(
          bytes,
          directory_base + 0x0044,
          8,
          "unexpected end of BOO file while reading text");
  result.directory.time =
      EbcdicCodec::cp037().decode_ascii(
          bytes,
          directory_base + 0x004E,
          8,
          "unexpected end of BOO file while reading text");

  const auto last_physical_page =
      physical_page_for_logical(result.directory,
                                result.directory.last_page_number);
  if (last_physical_page >= result.metadata.page_count) {
    throw std::runtime_error("BOO directory last-page field points outside the "
                             "file");
  }


  return result;
}

} // namespace geist::detail
