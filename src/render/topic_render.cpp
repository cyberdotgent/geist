// The render entry points: one topic, and the whole book as its topics.
// `TocEntry::render` is the single pass that produces a topic's Markdown and
// its render diagnostic together, so the two can never disagree.
#include "geist/detail/core/internal.hpp"

#include "geist/detail/core/atomic_cache.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "geist/detail/render/render_diagnostic_ir.hpp"
#include "geist/detail/lowering/topic_lowering_outcome.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace geist {

const TocEntry::LoweredTopic& TocEntry::lowered() const {
  // Fill-once and published atomically (geist/detail/atomic_cache.hpp): the
  // lowering is a pure function of the topic's records, which never change
  // after `open()`.
  return *detail::publish_once(cache_->lowering, [this] {
    auto lowered = std::make_shared<LoweredTopic>();
    if (document_ir_loader_)
      lowered->outcome = document_ir_loader_();
    return std::shared_ptr<const LoweredTopic>(std::move(lowered));
  });
}

std::string TocEntry::markdown() const {
  return render().markdown;
}

const RenderDiagnostic& TocEntry::render_diagnostic() const {
  return render().diagnostic;
}

// One pass produces both the Markdown and the diagnostic that explains it.
// The routes are tried in descending fidelity and none of them is allowed to
// yield nothing: a route that produces no content is demoted rather than
// accepted, because declining to claim structure must never mean declining to
// emit the topic's words.
//
// The pass writes into a private `RenderedTopic` and only then publishes it,
// so a concurrent reader sees either no cache or a complete one, never a
// half-assembled string.
const TocEntry::RenderedTopic& TocEntry::render() const {
  return *detail::publish_once(cache_->rendered, [this] {
    auto out = std::make_shared<RenderedTopic>();

    detail::TopicIdentityIR identity;
    identity.id = id;
    identity.title = title;
    identity.heading_level = heading_level;
    identity.topic_number = topic_number;
    identity.start_logical_record = start_logical_record;
    identity.end_logical_record = end_logical_record;

    const auto& outcome = lowered().outcome;
    const detail::DocumentIR* document = nullptr;
    std::string rejection;
    detail::TypedLoweringTraceIR trace;
    if (outcome) {
      document = outcome->document ? &*outcome->document : nullptr;
      rejection = outcome->rejection;
      trace = outcome->trace;
    }

    out->diagnostic =
        detail::classify_typed_lowering(identity, document, rejection, trace);
    // A TocEntry a caller built by hand out of `raw_records` never met the
    // typed pipeline, so "the typed dispatcher declined it" would be a false
    // statement about a topic that has no book behind it. It still gets a
    // diagnostic; it just does not get the marker, and its Markdown stays
    // whatever the verbatim route below produces.
    const auto lowering_attempted = static_cast<bool>(document_ir_loader_);
    if (!lowering_attempted) {
      out->diagnostic.reason = "no-typed-lowering-attempted";
      out->diagnostic.detail.clear();
    }
    // A topic the typed dispatcher declines produces nothing here; the
    // verbatim route below is what renders it. There is no second renderer.
    out->markdown = document != nullptr
                        ? detail::render_document_markdown(*document)
                        : std::string();

    const auto has_content = detail::markdown_has_content(out->markdown);
    detail::TopicBestEffortIR verbatim;
    if (!has_content && best_effort_loader_)
      verbatim = best_effort_loader_();
    detail::escalate_render_diagnostic(out->diagnostic, has_content,
                                       !verbatim.rows.empty(),
                                       verbatim.source_decoded);
    if (out->diagnostic.severity == RenderSeverity::best_effort) {
      if (!out->markdown.empty() && out->markdown.back() != '\n')
        out->markdown += "\n";
      if (!out->markdown.empty())
        out->markdown += "\n";
      // The book-wide answer excludes the footnote destinations; the file the
      // reader opens still has to carry them.
      out->best_effort_anchors = verbatim.anchors;
      auto emitted = verbatim.anchors;
      emitted.insert(emitted.end(), verbatim.footnote_anchors.begin(),
                     verbatim.footnote_anchors.end());
      out->markdown +=
          detail::render_best_effort_markdown(identity, verbatim.rows, emitted);
    } else if (out->diagnostic.severity == RenderSeverity::failed) {
      if (!out->markdown.empty() && out->markdown.back() != '\n')
        out->markdown += "\n";
      if (!out->markdown.empty())
        out->markdown += "\n";
      out->markdown += detail::render_failed_markdown(identity,
                                                      out->diagnostic);
    }

    const auto marker = lowering_attempted
                            ? render_diagnostic_comment(out->diagnostic)
                            : std::string();
    if (!marker.empty()) {
      if (!out->markdown.empty() && out->markdown.back() != '\n')
        out->markdown += "\n";
      out->markdown = marker + "\n" +
                      (out->markdown.empty() ? std::string()
                                             : "\n" + out->markdown);
    }
    return std::shared_ptr<const RenderedTopic>(std::move(out));
  });
}

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
