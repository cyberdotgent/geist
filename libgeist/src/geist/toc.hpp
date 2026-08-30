#pragma once

#include "geist/export.hpp"
#include "geist/trace.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace geist {

namespace detail {
struct DocumentIR;
}

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

  GEIST_API const std::vector<std::string>& gml_records() const;
  GEIST_API std::string markdown() const;
  // Renders exactly the same bytes as `markdown()` and fills `trace` with the
  // map from rendered output ranges back to the nodes, and thus to the BOO
  // file bytes, that produced them.
  GEIST_API std::string markdown(RenderTrace& trace) const;

private:
  mutable std::vector<std::string> cached_raw_records_;
  std::function<std::vector<std::string>()> raw_record_loader_;
  mutable std::shared_ptr<const detail::DocumentIR> cached_document_ir_;
  mutable bool document_load_attempted_ = false;
  std::function<std::shared_ptr<const detail::DocumentIR>()>
      document_ir_loader_;
  friend class BooDocument;
};

struct TopicInfo {
  std::string id;
  std::string title;
  std::string heading_level;
  std::uint32_t topic_number = 0;
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;
};

} // namespace geist
