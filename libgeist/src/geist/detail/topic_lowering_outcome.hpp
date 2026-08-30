#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/topic_document_lowering.hpp"

#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

// One typed lowering attempt, kept whole so the render path can explain
// itself.  Before this existed, `TocEntry::markdown()` discarded the rejection
// string and the decline trace and silently fell through to the legacy
// renderer; `bootrace --coverage` computed the same information separately.
struct TopicLoweringOutcomeIR {
  std::optional<DocumentIR> document;
  std::string rejection;
  TypedLoweringTraceIR trace;
};

// The last-resort content for one topic, plus whether the record decoder
// produced anything at all.  The two are distinct outcomes: a topic with no
// body content in source is correctly rendered as a heading alone, while a
// topic whose records could not be decoded is a genuine failure.
struct TopicBestEffortIR {
  bool source_decoded = false;
  std::vector<std::string> lines;
  // The anchor ids the topic's own structural controls name (`SRFIG...`,
  // `SRTBL...`, `SRSPT...`).  A topic that renders verbatim still *names*
  // these objects, and cross references elsewhere in the book point at them,
  // so they are emitted and reported even though no structure is claimed
  // around them.
  std::vector<std::string> anchors;
};

} // namespace geist::detail
