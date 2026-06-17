#pragma once

#include "geist/directory.hpp"
#include "geist/export.hpp"
#include "geist/metadata.hpp"
#include "geist/page.hpp"
#include "geist/properties.hpp"
#include "geist/resource.hpp"
#include "geist/toc.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace geist {

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
  GEIST_API std::string markdown() const;
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

} // namespace geist
