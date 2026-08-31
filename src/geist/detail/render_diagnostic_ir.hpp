#pragma once

#include "geist/detail/display_lines.hpp"
#include "geist/detail/document_ir.hpp"
#include "geist/detail/topic_document_lowering.hpp"
#include "geist/detail/verbatim_cross_references.hpp"
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

// The same rows, each carrying the source display line it came from and the
// byte offset of every one of that line's display columns
// (verbatim_cross_references.hpp).  A consumer that has to place something on
// a column range -- a selector's covered span -- reads the map instead of
// re-deriving the walk over the flattened row.
std::vector<BestEffortLineIR> best_effort_display_lines(
    const std::vector<DecodedLogicalRecordSource> &sources,
    const std::string &title);

// The anchor ids the topic's structural controls name, in source order and
// without duplicates.  The id is the control opcode without its `SR` prefix,
// which is the same evidence the typed families read.
std::vector<std::string>
best_effort_anchors(const std::vector<DecodedLogicalRecordSource> &sources);

// The footnote destinations the topic *prints*, in source order and without
// duplicates.  `SRFTN<id>` is deliberately absent from `best_effort_anchors`
// because a footnote is not a destination the book at large may reference --
// but the topic that prints it does reference it, from the selector that
// marks the footnote's marker in the text, so the anchor has to exist in the
// file.  This is the same split the typed route makes: the renderer emits
// `<a id="FTN...">` while `document_link_targets` publishes nothing for it.
std::vector<std::string> best_effort_footnote_anchors(
    const std::vector<DecodedLogicalRecordSource> &sources);

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

// Markdown for the `best_effort` route: the verbatim rows in a raw HTML
// `<pre>` block, appended to whatever the higher route managed to produce.
//
// Raw HTML rather than a fence because the rows carry links.  A fence
// preserves every column but renders Markdown link syntax as inert text, and
// hosted BookServer keeps its anchors inside the `<pre>` of the very same
// rows -- so `<pre>` is the one form that holds both halves, and it is the
// form the HTML backend (#46) wants anyway.  Markdown passes a raw HTML
// block through untouched, and a `<pre>` block runs to its closing tag, so
// blank rows inside it stay inside it.
//
// A row's bytes are the row's bytes: only `&`, `<` and `>` are escaped, as
// HTML requires, and the anchor markup is placed at the column offsets the
// selector named.  Nothing is inserted that occupies a column.
std::string render_best_effort_markdown(
    const TopicIdentityIR &topic,
    const std::vector<VerbatimRowIR> &rows,
    const std::vector<std::string> &anchors);

// Markdown for the `failed` route: a placeholder naming the topic, its source
// record range and the reason, so an absent topic is debuggable rather than
// invisible.
std::string render_failed_markdown(const TopicIdentityIR &topic,
                                   const RenderDiagnostic &diagnostic);

// Tab-separated one-line rendering of a diagnostic's degradation list, for
// the sidecar manifest and `bootrace --coverage`.
std::string format_render_degradations(const RenderDiagnostic &diagnostic);

} // namespace geist::detail
