// The render entry points: one topic, and the whole book as its topics.
// `TocEntry::render` is the single pass that produces a topic's Markdown and
// its render diagnostic together, so the two can never disagree.
#include "geist/detail/internal.hpp"

#include "geist/detail/document_markdown_renderer.hpp"
#include "geist/detail/render_diagnostic_ir.hpp"
#include "geist/detail/topic_lowering_outcome.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace geist {

namespace {

} // namespace

std::string TocEntry::markdown() const {
  render();
  return cached_markdown_;
}

const RenderDiagnostic& TocEntry::render_diagnostic() const {
  render();
  return cached_diagnostic_;
}

// One pass produces both the Markdown and the diagnostic that explains it.
// The routes are tried in descending fidelity and none of them is allowed to
// yield nothing: a route that produces no content is demoted rather than
// accepted, because declining to claim structure must never mean declining to
// emit the topic's words.
void TocEntry::render() const {
  if (rendered_)
    return;
  rendered_ = true;

  if (!document_load_attempted_ && document_ir_loader_) {
    cached_lowering_ = document_ir_loader_();
    document_load_attempted_ = true;
  }

  detail::TopicIdentityIR identity;
  identity.id = id;
  identity.title = title;
  identity.heading_level = heading_level;
  identity.topic_number = topic_number;
  identity.start_logical_record = start_logical_record;
  identity.end_logical_record = end_logical_record;

  const detail::DocumentIR* document = nullptr;
  std::string rejection;
  detail::TypedLoweringTraceIR trace;
  if (cached_lowering_) {
    document = cached_lowering_->document ? &*cached_lowering_->document
                                          : nullptr;
    rejection = cached_lowering_->rejection;
    trace = cached_lowering_->trace;
  }

  cached_diagnostic_ =
      detail::classify_typed_lowering(identity, document, rejection, trace);
  // A TocEntry a caller built by hand out of `raw_records` never met the
  // typed pipeline, so "the typed dispatcher declined it" would be a false
  // statement about a topic that has no book behind it. It still gets a
  // diagnostic; it just does not get the marker, and its Markdown stays
  // exactly what the compatibility renderer produces.
  const auto lowering_attempted = static_cast<bool>(document_ir_loader_);
  if (!lowering_attempted) {
    cached_diagnostic_.reason = "no-typed-lowering-attempted";
    cached_diagnostic_.detail.clear();
  }
  // A topic the typed dispatcher declines produces nothing here; the
  // verbatim route below is what renders it. There is no second renderer.
  cached_markdown_ = document != nullptr
                         ? detail::render_document_markdown(*document)
                         : std::string();

  const auto has_content = detail::markdown_has_content(cached_markdown_);
  detail::TopicBestEffortIR verbatim;
  if (!has_content && best_effort_loader_)
    verbatim = best_effort_loader_();
  detail::escalate_render_diagnostic(cached_diagnostic_, has_content,
                                     !verbatim.lines.empty(),
                                     verbatim.source_decoded);
  if (cached_diagnostic_.severity == RenderSeverity::best_effort) {
    if (!cached_markdown_.empty() && cached_markdown_.back() != '\n')
      cached_markdown_ += "\n";
    if (!cached_markdown_.empty())
      cached_markdown_ += "\n";
    cached_best_effort_anchors_ = verbatim.anchors;
    cached_markdown_ +=
        detail::render_best_effort_markdown(identity, verbatim.lines,
                                            verbatim.anchors);
  } else if (cached_diagnostic_.severity == RenderSeverity::failed) {
    if (!cached_markdown_.empty() && cached_markdown_.back() != '\n')
      cached_markdown_ += "\n";
    if (!cached_markdown_.empty())
      cached_markdown_ += "\n";
    cached_markdown_ += detail::render_failed_markdown(identity,
                                                       cached_diagnostic_);
  }

  const auto marker = lowering_attempted
                          ? render_diagnostic_comment(cached_diagnostic_)
                          : std::string();
  if (!marker.empty()) {
    if (!cached_markdown_.empty() && cached_markdown_.back() != '\n')
      cached_markdown_ += "\n";
    cached_markdown_ = marker + "\n" +
                       (cached_markdown_.empty() ? std::string()
                                                 : "\n" + cached_markdown_);
  }
}

namespace {


} // namespace

std::string BooDocument::markdown() const {
  // The whole book is the concatenation of its topics, each rendered by the
  // same route `TocEntry::markdown()` uses. Rendering the book as one
  // undifferentiated record stream, as this once did, discards the topic
  // boundaries the typed pipeline needs and is lossier for every topic that
  // the typed route handles.
  std::string out;
  for (const auto& entry : toc_) {
    const auto topic = entry.markdown();
    if (topic.empty()) {
      continue;
    }
    if (!out.empty()) {
      if (out.back() != '\n') {
        out.push_back('\n');
      }
      out.push_back('\n');
    }
    out += topic;
  }
  if (!out.empty() && out.back() != '\n') {
    out.push_back('\n');
  }
  return out;
}

} // namespace geist
