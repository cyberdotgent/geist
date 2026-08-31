#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/topic_document_lowering.hpp"
#include "geist/detail/verbatim_cross_references.hpp"

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
  // The verbatim rows, each with the cross references the source proves on
  // it as column ranges.  A row's text is the drawn row; a link is a pair of
  // offsets into it, never a byte inserted into it.
  std::vector<VerbatimRowIR> rows;
  // The anchor ids the topic's own structural controls name (`SRFIG...`,
  // `SRTBL...`, `SRSPT...`).  A topic that renders verbatim still *names*
  // these objects, and cross references elsewhere in the book point at them,
  // so they are emitted and reported even though no structure is claimed
  // around them.
  std::vector<std::string> anchors;
  // Footnote destinations a link in this same topic uses.  They are emitted
  // in the file but never published book-wide: a footnote is reachable only
  // from the page that prints it.
  std::vector<std::string> footnote_anchors;
};

} // namespace geist::detail
