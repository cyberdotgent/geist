// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/directory.hpp"
#include "geist/export.hpp"
#include "geist/html.hpp"
#include "geist/metadata.hpp"
#include "geist/page.hpp"
#include "geist/properties.hpp"
#include "geist/render_diagnostic.hpp"
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
struct DrawnWordConservationIR;
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
  std::vector<BooFontTrace> font_spans;
  std::vector<std::string> ir_control_segments;
  std::vector<std::string> ir_physical_rows;
  std::vector<std::string> ir_ownership_cells;
  std::vector<std::string> ir_semantic_blocks;
  std::vector<std::string> ir_tokens;
  std::vector<std::string> ir_display_lines;
};

// Thread safety: an opened document is immutable apart from caches that are
// filled once and published atomically, so every `const` member below --
// metadata and property access, TOC traversal, topic lookup, decoded logical
// records, whole-book and per-topic rendering and resource reads -- may be
// called concurrently from any number of threads on one `BooDocument` with no
// external synchronisation, and references it returns stay valid across those
// calls. See geist/toc.hpp for the same contract on the TOC entries, and
// geist/detail/atomic_cache.hpp for how it is kept.
//
// Reading a trace slice back to its source bytes needs a per-caller memo, so
// it is not a member here: construct a `geist::TraceSourceReader` per thread
// (geist/trace.hpp).
//
// Non-`const` use is not covered: opening, assigning, copying or destroying a
// document must not overlap with another thread's use of it.
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
  GEIST_API const std::vector<std::string>& decoded_logical_records()
      const;
  GEIST_API const std::map<std::string, std::string>& font_definitions()
      const;
  GEIST_API const std::vector<TocEntry>& table_of_contents() const noexcept;
  GEIST_API const std::vector<TopicInfo>& topics() const noexcept;
  GEIST_API std::string markdown() const;
  GEIST_API std::string topic_markdown(const std::string& topic_id) const;
  // Native HTML output (issue #46), rendered from the same typed Document IR
  // as the Markdown above rather than converted from it. The fragment forms
  // carry semantic content only and no page chrome; the document forms wrap
  // exactly those bytes in a minimal standalone page. See geist/html.hpp for
  // the resolver hooks and libgeist/doc/html-styling.md for the class, id and
  // data-attribute scheme a consumer styles against.
  GEIST_API std::string html_fragment(
      const HtmlRenderOptions& options = {}) const;
  GEIST_API std::string html_document(
      const HtmlRenderOptions& options = {},
      const HtmlDocumentOptions& document_options = {}) const;
  GEIST_API std::string topic_html_fragment(
      const std::string& topic_id,
      const HtmlRenderOptions& options = {}) const;
  GEIST_API std::string topic_html_document(
      const std::string& topic_id, const HtmlRenderOptions& options = {},
      const HtmlDocumentOptions& document_options = {}) const;
  // Render provenance for every TOC topic, parallel to table_of_contents():
  // how well each topic was rendered, by which route, and why. Renders every
  // topic, caching the Markdown on the TOC entries as it goes. See
  // geist/render_diagnostic.hpp.
  GEIST_API std::vector<RenderDiagnostic> render_diagnostics() const;
  GEIST_API const std::vector<ResourceEntry>& resources() const noexcept;
  GEIST_API const TocEntry* find_toc_entry(const std::string& topic_id)
      const noexcept;
  GEIST_API std::vector<BooLogicalRecordTrace> trace_logical_records(
      const std::string& topic_id) const;
  // Reports, for every TOC topic, whether typed Document IR lowering claims
  // it (and which family) or the legacy string pipeline renders it. Lowering
  // only; no Markdown is rendered. See geist/detail/typed_route_inventory.hpp.
  GEIST_API detail::TypedRouteInventoryIR typed_route_inventory() const;
  // Source-side conservation for every TOC topic: the words the topic's
  // records draw, per their own display-line framing, against the words its
  // Markdown emits. Renders every topic. See
  // geist/detail/drawn_word_conservation.hpp.
  GEIST_API detail::DrawnWordConservationIR drawn_word_conservation() const;

  // Returns an exact 4096-byte physical page payload.
  GEIST_API std::vector<std::uint8_t> read_page(std::uint32_t page_number)
      const;
  GEIST_API std::vector<std::uint8_t> read_resource_data(
      const std::string& resource_id) const;
  GEIST_API std::vector<std::uint8_t> read_resource_png(
      const std::string& resource_id) const;

private:
  // Reads `decode_context_` to build its own source reader; see
  // geist/trace.hpp.
  friend class TraceSourceReader;

  // A topic the TOC does not list, built as a standalone entry with the same
  // loaders a listed one gets, so every per-topic render entry point reaches
  // the same topics. Throws `std::out_of_range` for an unknown id.
  TocEntry synthesize_topic_entry(const std::string& topic_id) const;

  BooMetadata metadata_;
  BooPage0Header file_header_;
  BooDirectory directory_;
  BooBookProperties book_properties_;
  std::vector<BooPageRun> page_runs_;
  // Filled once and published atomically; see the thread-safety note above
  // and geist/detail/atomic_cache.hpp.
  mutable std::shared_ptr<const std::map<std::string, std::string>>
      font_definitions_;
  std::vector<TocEntry> toc_;
  std::vector<TopicInfo> topics_;
  std::map<std::string, std::string> topic_titles_;
  std::vector<ResourceEntry> resources_;
  std::shared_ptr<const detail::BookTopicCatalogIR> topic_catalog_ir_;
  std::shared_ptr<detail::LogicalDecodeContext> decode_context_;
};

} // namespace geist
