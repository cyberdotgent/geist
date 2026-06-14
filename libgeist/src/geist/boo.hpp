#pragma once

#include "geist/export.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace geist {

constexpr std::uint32_t boo_page_size = 4096;

enum class BooPageRole {
  file_header,
  directory,
  dictionary,
  content,
  logical_records,
  unknown,
};

enum class ResourceLayout {
  legacy_v12,
  legacy_v13,
  converted_v14,
  unknown,
};

struct BooPage0Header {
  std::uint16_t directory_page_number = 0;
  std::uint16_t unknown_0002 = 0;
  std::uint32_t unknown_0004 = 0;
  std::string copyright_text;
  std::optional<std::array<std::uint8_t, 4>> unknown_0102;
};

// Parsed fields from the physical directory page. Names stay neutral where the
// reverse-engineered format meaning is not settled yet.
struct BooDirectory {
  std::uint32_t page_number = 0;
  std::string version_text;
  std::uint8_t version_variant = 0;
  std::uint8_t token_threshold = 0;
  std::uint16_t last_page_number = 0;
  std::uint16_t token_map_offset = 0;
  std::uint16_t dictionary_start_page = 0;
  std::uint16_t dictionary_page_count = 0;
  std::uint16_t content_start_page = 0;
  std::uint16_t content_page_count = 0;
  std::uint16_t stream_table_offset = 0;
  std::uint16_t stream_table_count = 0;
  std::uint16_t secondary_table_offset = 0;
  std::string date;
  std::string time;
};

// Contiguous run of physical pages with the same first 16-bit page-class word.
// The role is a best-effort classification derived from verified directory
// fields and should be treated as advisory.
struct BooPageRun {
  std::uint32_t start_page = 0;
  std::uint32_t page_count = 0;
  std::uint16_t page_class = 0;
  BooPageRole role = BooPageRole::unknown;
};

// Book-level properties decoded from logical header controls. Unknown or absent
// controls are represented by empty strings/vectors.
struct BooBookProperties {
  std::string language;
  std::string version;
  std::string build_version;
  bool reflow = false;
  std::string title;
  std::string short_title;
  std::string copyright;
  std::string security;
  std::string date;
  std::vector<std::string> authors;
  std::string document_number;
};

// Decoded logical metadata controls. The current decoder is experimental and
// only covers the version-2 dictionary/token paths documented from the bundled
// fixtures.
struct BooLogicalControl {
  std::string key;
  std::string value;
};

// File-level physical metadata that is available immediately after open().
struct BooMetadata {
  std::filesystem::path path;
  std::uint64_t file_size = 0;
  std::uint32_t page_size = boo_page_size;
  std::uint32_t page_count = 0;
};

struct TocEntry {
  std::string id;
  std::string title;
  std::uint32_t level = 0;
  std::uint32_t style = 0;
  std::string heading_level;
  std::uint32_t topic_number = 0;
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  // GML-style raw projection of the decoded BookManager topic records.
  std::vector<std::string> raw_records;
};

struct ResourceEntry {
  std::string id;
  std::string name;
  std::string stored_format;
  std::string kind;
  std::string description;
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
  std::uint64_t description_offset = 0;
  std::uint64_t description_size = 0;
  ResourceLayout layout = ResourceLayout::unknown;
};

class BooDocument {
public:
  // Opens and validates a BOO file, then parses the verified physical header
  // and directory fields. Throws std::runtime_error for invalid or unsupported
  // physical structure.
  static GEIST_API BooDocument open(const std::filesystem::path& path);

  GEIST_API const BooMetadata& metadata() const noexcept;
  GEIST_API const BooPage0Header& file_header() const noexcept;
  GEIST_API const BooDirectory& directory() const noexcept;
  GEIST_API const BooBookProperties& book_properties() const noexcept;
  GEIST_API const std::vector<BooPageRun>& page_runs() const noexcept;
  GEIST_API const std::vector<BooLogicalControl>& logical_controls()
      const noexcept;
  GEIST_API const std::vector<TocEntry>& table_of_contents() const noexcept;
  GEIST_API const std::vector<std::string>& raw_gml_records() const noexcept;
  GEIST_API const std::vector<ResourceEntry>& resources() const noexcept;
  GEIST_API const TocEntry* find_toc_entry(const std::string& topic_id)
      const noexcept;

  // Returns an exact 4096-byte physical page payload.
  GEIST_API std::vector<std::uint8_t> read_page(std::uint32_t page_number)
      const;
  GEIST_API std::vector<std::uint8_t> read_resource_data(
      const std::string& resource_id) const;
  GEIST_API std::vector<std::uint8_t> read_resource_png(
      const std::string& resource_id) const;

private:
  BooMetadata metadata_;
  BooPage0Header file_header_;
  BooDirectory directory_;
  BooBookProperties book_properties_;
  std::vector<BooPageRun> page_runs_;
  std::vector<BooLogicalControl> logical_controls_;
  std::vector<TocEntry> toc_;
  std::vector<std::string> raw_gml_records_;
  std::vector<ResourceEntry> resources_;
  std::vector<std::uint8_t> bytes_;
};

GEIST_API std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes);
GEIST_API const char* to_string(BooPageRole role) noexcept;
GEIST_API const char* to_string(ResourceLayout layout) noexcept;

} // namespace geist
