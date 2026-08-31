#pragma once

#include "geist/export.hpp"
#include "geist/link_target.hpp"
#include "geist/render_diagnostic.hpp"
#include "geist/trace.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace geist {

namespace detail {
struct TopicLoweringOutcomeIR;
struct TopicBestEffortIR;
}

// Thread safety: once `BooDocument::open()` has returned, every `const`
// operation on a TOC entry -- and on the document that owns it -- may be
// called concurrently from any number of threads without external
// synchronisation. Rendering a topic is a pure function of state that is
// immutable from `open()` onward, so the lazy caches below only ever publish
// an already-finished value; see geist/detail/atomic_cache.hpp. Two threads
// that race to render one topic produce the same bytes and one result is
// discarded.
//
// A returned reference stays valid for the lifetime of the entry, including
// across concurrent calls: a published cache value is never replaced.
// Modifying an entry, or the document, is not concurrency-safe; that includes
// copying a document while another thread reads it.
struct TocEntry {
  std::string id;
  std::string title;
  std::uint32_t level = 0;
  std::uint32_t style = 0;
  std::string heading_level;
  std::uint32_t topic_number = 0;
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;

  GEIST_API std::string markdown() const;
  // How well this topic rendered, by which route, and why. Computed by the
  // same single pass that produces `markdown()`, so the two can never
  // disagree; both are cached after the first call.
  GEIST_API const RenderDiagnostic& render_diagnostic() const;
  // Renders exactly the same bytes as `markdown()` and fills `trace` with the
  // map from rendered output ranges back to the nodes, and thus to the BOO
  // file bytes, that produced them.
  GEIST_API std::string markdown(RenderTrace& trace) const;
  // The ids cross references may use to reach this topic.  Answered from the
  // typed Document IR when the topic renders through it, and from the anchors
  // its own structural controls name when it is reproduced verbatim.
  GEIST_API const std::vector<LinkTarget>& link_targets() const;

private:
  // The typed lowering the loader produced, wrapped so that "the loader ran
  // and declined" is a published value rather than an empty slot. `outcome`
  // is null exactly when there was no loader or it produced nothing, which is
  // what the old `document_load_attempted_` flag distinguished.
  struct LoweredTopic {
    std::shared_ptr<const detail::TopicLoweringOutcomeIR> outcome;
  };
  // One render pass produces the Markdown, the diagnostic that explains it,
  // and the anchors a verbatim topic named, so the three can never disagree.
  struct RenderedTopic {
    std::string markdown;
    RenderDiagnostic diagnostic;
    // The anchor ids a verbatim-rendered topic named; empty for a typed
    // topic, whose Document IR carries them instead.
    std::vector<std::string> best_effort_anchors;
  };
  // Each slot is filled at most once and published atomically; see
  // geist/detail/atomic_cache.hpp. The state is held behind a shared_ptr for
  // two reasons: a TocEntry is stored by value in `std::vector<TocEntry>` and
  // must stay copyable, which an atomic member is not; and copies of an entry
  // then share one cache rather than each recomputing the same topic.
  struct CacheState {
    std::shared_ptr<const LoweredTopic> lowering;
    std::shared_ptr<const RenderedTopic> rendered;
    std::shared_ptr<const std::vector<LinkTarget>> link_targets;
  };
  const LoweredTopic& lowered() const;
  const RenderedTopic& render() const;
  // Attaching loaders defines what the caches would hold, so it starts a
  // fresh cache: an entry copied before its loaders were attached must not
  // inherit -- or share -- results computed without them.
  void attach_loaders(
      std::function<std::shared_ptr<const detail::TopicLoweringOutcomeIR>()>
          document,
      std::function<detail::TopicBestEffortIR()> best_effort) {
    document_ir_loader_ = std::move(document);
    best_effort_loader_ = std::move(best_effort);
    cache_ = std::make_shared<CacheState>();
  }
  std::function<std::shared_ptr<const detail::TopicLoweringOutcomeIR>()>
      document_ir_loader_;
  // The topic's own display rows, verbatim: the last-resort content when no
  // route produced any. Loaded only when that happens.
  std::function<detail::TopicBestEffortIR()> best_effort_loader_;
  std::shared_ptr<CacheState> cache_ = std::make_shared<CacheState>();
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
