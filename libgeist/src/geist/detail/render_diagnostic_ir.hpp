#pragma once

#include "geist/detail/display_lines.hpp"
#include "geist/detail/document_ir.hpp"
#include "geist/detail/topic_document_lowering.hpp"
#include "geist/render_diagnostic.hpp"

#include <string>
#include <vector>

namespace geist::detail {

// Single source of truth for render provenance.
//
// `TocEntry::markdown()`, `TocEntry::render_diagnostic()` and
// `BooDocument::typed_route_inventory()` all classify a topic through these
// functions, so the exported Markdown, the sidecar manifest and
// `bootrace --coverage` cannot drift apart the way the coverage metric and
// the renderer once drifted on topic identity.

// Stage 1: classify the outcome of typed lowering alone. `document` is null
// when no typed family claimed the topic. This never renders Markdown.
RenderDiagnostic classify_typed_lowering(const TopicIdentityIR &topic,
                                         const DocumentIR *document,
                                         const std::string &typed_rejection,
                                         const TypedLoweringTraceIR &trace);

// True when the rendered Markdown carries something a reader would call
// content: a line that is neither blank, nor a heading, nor an anchor.
bool markdown_has_content(const std::string &markdown);

// The topic's own decoded display lines, verbatim, with control opcodes and
// operands removed and the topic-envelope metadata controls dropped whole
// (the renderer already emits their content as the heading).  This is the
// last-resort content and it sits *below* every structural pass: it needs
// only the record decoder, so it still works for a book the layout and
// semantic passes cannot make sense of -- and some real BOO files are
// malformed in ways the IBM reader itself mishandles.  Reading a book badly
// beats not reading it at all.
std::vector<std::string>
best_effort_lines(const std::vector<DecodedLogicalRecordSource> &sources,
                  const std::string &title);

// Stage 2: the render escalation.  Fail-closed is a rule about *claiming
// structure*; it must never mean withholding content, so this is the guard
// that turns a silently empty topic into an explicit lower-fidelity
// rendering.  A topic whose chosen route produced no content is demoted to
// `best_effort` when verbatim source lines exist; when they do not, it is
// `failed` only if the record decoder produced nothing at all -- a topic that
// decodes cleanly and simply has no body is correctly rendered as its heading
// alone, and is reported with the `empty-topic-body` reason instead.
void escalate_render_diagnostic(RenderDiagnostic &diagnostic,
                                bool markdown_has_content,
                                bool best_effort_available,
                                bool source_decoded);

// Markdown for the `best_effort` route: the verbatim rows in a fenced block,
// appended to whatever the higher route managed to produce.
std::string render_best_effort_markdown(const std::vector<std::string> &lines);

// Markdown for the `failed` route: a placeholder naming the topic, its source
// record range and the reason, so an absent topic is debuggable rather than
// invisible.
std::string render_failed_markdown(const TopicIdentityIR &topic,
                                   const RenderDiagnostic &diagnostic);

// Tab-separated one-line rendering of a diagnostic's degradation list, for
// the sidecar manifest and `bootrace --coverage`.
std::string format_render_degradations(const RenderDiagnostic &diagnostic);

} // namespace geist::detail
