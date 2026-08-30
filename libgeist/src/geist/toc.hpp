#pragma once

#include "geist/export.hpp"
#include "geist/render_diagnostic.hpp"
#include "geist/trace.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace geist {

namespace detail {
struct TopicLoweringOutcomeIR;
struct TopicBestEffortIR;
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
  // How well this topic rendered, by which route, and why. Computed by the
  // same single pass that produces `markdown()`, so the two can never
  // disagree; both are cached after the first call.
  GEIST_API const RenderDiagnostic& render_diagnostic() const;
  // Renders exactly the same bytes as `markdown()` and fills `trace` with the
  // map from rendered output ranges back to the nodes, and thus to the BOO
  // file bytes, that produced them.
  GEIST_API std::string markdown(RenderTrace& trace) const;

private:
  void render() const;
  mutable std::vector<std::string> cached_raw_records_;
  std::function<std::vector<std::string>()> raw_record_loader_;
  mutable std::shared_ptr<const detail::TopicLoweringOutcomeIR>
      cached_lowering_;
  mutable bool document_load_attempted_ = false;
  std::function<std::shared_ptr<const detail::TopicLoweringOutcomeIR>()>
      document_ir_loader_;
  // The topic's own display rows, verbatim: the last-resort content when no
  // route produced any. Loaded only when that happens.
  std::function<detail::TopicBestEffortIR()> best_effort_loader_;
  mutable std::string cached_markdown_;
  mutable RenderDiagnostic cached_diagnostic_;
  mutable bool rendered_ = false;
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
