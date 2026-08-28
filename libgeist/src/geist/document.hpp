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
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace geist {

namespace detail {
struct BookTopicCatalogIR;
struct LogicalDecodeContext;
struct TypedRouteInventoryIR;
}

struct BooFontTrace {
  std::uint32_t logical_record = 0;
  std::uint32_t segment_index = 0;
  std::uint32_t span_index = 0;
  std::uint32_t offset = 0;
  std::uint32_t length = 0;
  std::string code;
  std::string style;
  std::string text;
  std::string projected_gml;
};

struct BooLogicalRecordTrace {
  std::uint32_t logical_record = 0;
  std::string decoded_record;
  std::vector<std::string> segments;
  std::vector<std::string> normalized_gml_records;
  std::vector<BooFontTrace> font_spans;
  std::vector<std::string> ir_control_segments;
  std::vector<std::string> ir_physical_rows;
  std::vector<std::string> ir_ownership_cells;
  std::vector<std::string> ir_semantic_blocks;
};

class BooDocument {
public:
  // Opens and validates a BOO file, builds its lightweight topic/TOC indexes,
  // and defers topic GML and Markdown rendering until requested.
  static GEIST_API BooDocument open(const std::filesystem::path& path);

  GEIST_API const BooMetadata& metadata() const noexcept;
  GEIST_API const BooPage0Header& file_header() const noexcept;
  GEIST_API const BooDirectory& directory() const noexcept;
  GEIST_API const BooBookProperties& book_properties() const noexcept;
  GEIST_API const std::vector<BooPageRun>& page_runs() const noexcept;
  GEIST_API const std::vector<BooLogicalControl>& logical_controls()
      const noexcept;
  GEIST_API const std::vector<std::string>& decoded_logical_records()
      const;
  GEIST_API const std::map<std::string, std::string>& font_definitions()
      const;
  GEIST_API const std::vector<TocEntry>& table_of_contents() const noexcept;
  GEIST_API const std::vector<TopicInfo>& topics() const noexcept;
  GEIST_API const std::vector<std::string>& raw_gml_records() const;
  GEIST_API std::string markdown() const;
  GEIST_API std::string topic_markdown(const std::string& topic_id) const;
  GEIST_API const std::vector<ResourceEntry>& resources() const noexcept;
  GEIST_API const TocEntry* find_toc_entry(const std::string& topic_id)
      const noexcept;
  GEIST_API std::vector<BooLogicalRecordTrace> trace_logical_records(
      const std::string& topic_id) const;
  // Reports, for every TOC topic, whether typed Document IR lowering claims
  // it (and which family) or the legacy string pipeline renders it. Lowering
  // only; no Markdown is rendered. See geist/detail/typed_route_inventory.hpp.
  GEIST_API detail::TypedRouteInventoryIR typed_route_inventory() const;

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
  mutable std::map<std::string, std::string> font_definitions_;
  mutable bool font_definitions_loaded_ = false;
  std::vector<TocEntry> toc_;
  std::vector<TopicInfo> topics_;
  std::map<std::string, std::string> topic_titles_;
  mutable std::vector<std::string> raw_gml_records_;
  mutable bool raw_gml_records_loaded_ = false;
  std::vector<ResourceEntry> resources_;
  std::shared_ptr<const detail::BookTopicCatalogIR> topic_catalog_ir_;
  std::shared_ptr<detail::LogicalDecodeContext> decode_context_;
};

} // namespace geist
