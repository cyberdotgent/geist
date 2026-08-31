#pragma once

#include "geist/export.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace geist {

// How well a topic was rendered.
//
// The ladder is ordered from most to least faithful.  It describes the
// *fidelity of the claim* the pipeline makes about a topic, never whether the
// consumer receives content: every severity except `failed` carries the
// topic's words, and `failed` still carries a diagnostic placeholder naming
// the topic and the reason.
enum class RenderSeverity {
  // Typed Document IR lowering claimed the topic and every block it produced
  // is a proven semantic structure. No known loss.
  typed,
  // Typed lowering claimed the topic, but at least one block could not prove
  // its structure and was emitted verbatim (preformatted) instead. The words
  // are present; the structure around them is not asserted. See
  // `RenderDiagnostic::degradations`.
  typed_degraded,
  // No typed family would claim the topic, so its own source display rows
  // were emitted verbatim. The reader gets the words in source order and
  // roughly the source shape, with no typed structure at all. `detail`
  // carries the exact typed rejection.
  best_effort,
  // Not even the source rows could be recovered. The topic renders as a
  // diagnostic placeholder naming itself, its source record range and the
  // reason. This is the only severity that does not carry book content, and
  // it exists so that a topic can never silently disappear.
  failed,
};

GEIST_API const char* to_string(RenderSeverity severity) noexcept;

// Source coordinates for a diagnostic. `start_logical_record` /
// `end_logical_record` are the topic's own record range and are always set;
// the remaining fields locate a specific block and are zero when unknown.
struct RenderSourceCoordinates {
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  std::size_t token_begin = 0;
  std::size_t token_end = 0;
  std::uint32_t byte_begin = 0;
  std::uint32_t byte_end = 0;
};

// One block inside a typed topic that took a lower-fidelity path.
struct RenderDegradation {
  // Stable lowerer/block name, e.g. "fixed table region: preformatted body".
  std::string block;
  // Machine-readable reason code, e.g. "fixed-table-verbatim".
  std::string reason;
  // Human-readable explanation, e.g. the message family's `fallback_reason`.
  std::string detail;
  RenderSourceCoordinates source;
};

// Render provenance for one topic: how well it was rendered, by which route,
// and why.
struct RenderDiagnostic {
  RenderSeverity severity = RenderSeverity::failed;
  // "typed", "legacy", "best-effort" or "none".
  std::string route;
  // Typed family that produced the document, empty for every other route.
  std::string family;
  // Machine-readable reason code. Stable across releases; see
  // render_diagnostic.cpp for the enumeration.
  std::string reason;
  // Human-readable detail. For `best_effort` this is the exact typed
  // rejection string that `bootrace --coverage` reports.
  std::string detail;
  RenderSourceCoordinates source;
  std::vector<RenderDegradation> degradations;
};

// The one-line HTML comment that marks a non-`typed` topic inside its own
// Markdown. Returns an empty string for `RenderSeverity::typed`, which is why
// a fully typed topic's Markdown is byte-identical to a pipeline without
// diagnostics at all.
GEIST_API std::string render_diagnostic_comment(
    const RenderDiagnostic& diagnostic);

} // namespace geist
