// The HTML render entry points: one topic, and the whole book as its topics.
//
// The sibling of `topic_render.cpp`, which does the same for Markdown.  Both
// read the *same* typed lowering, and the diagnostic that explains a topic is
// computed once by `TocEntry::render()` and consumed here, so the two formats
// can never disagree about how well a topic rendered.
//
// Nothing here converts Markdown.  A topic that no typed family claims is
// rendered from its own verbatim rows, exactly as the Markdown route renders
// them, and a topic that could not be recovered at all becomes a labelled
// diagnostic rather than silence.
#include "geist/detail/core/internal.hpp"

#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/render_diagnostic_ir.hpp"
#include "geist/detail/lowering/topic_lowering_outcome.hpp"
#include "geist/html.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace geist {
namespace {

// The heading level the source proved for a topic, spelled either as the
// source writes it (`:H3`) or as the typed families normalise it (`h3`).
// Only the first word is the level: on a topic whose metadata run does not
// parse, the recorded value is the whole flattened remainder of the record.
// This mirrors `render_best_effort_markdown` deliberately -- the two routes
// must give one topic the same heading level.
std::size_t heading_level_of(const std::string& heading_level) {
  auto level = detail::ascii_lower(heading_level);
  level = level.substr(0, level.find_first_of(" \t"));
  if (!level.empty() && level.front() == ':')
    level.erase(0, 1);
  if (level.size() == 2 && level.front() == 'h' && level.back() >= '1' &&
      level.back() <= '6')
    return static_cast<std::size_t>(level.back() - '0');
  return 1;
}

// The topic's own heading, as every route emits it: the level the source
// proved, then the public topic identity, then the title.  Without it a
// verbatim topic would arrive with no heading at all.
std::string topic_heading_html(const detail::TopicIdentityIR& topic) {
  const auto level = std::to_string(heading_level_of(topic.heading_level));
  std::string out = "<h" + level + " class=\"geist-heading\">";
  if (!topic.id.empty())
    out += detail::escape_html_text(topic.id) + " ";
  out += detail::escape_html_text(topic.title);
  out += "</h" + level + ">";
  return out;
}

// A topic reproduced verbatim still *names* the objects other topics
// reference, so their ids are emitted even though no structure is claimed
// around them.  Without them every cross reference elsewhere in the book that
// points here would dangle.
std::string verbatim_anchors_html(const std::vector<std::string>& anchors,
                                  const HtmlRenderOptions& options) {
  std::string out;
  for (const auto& id : anchors) {
    out += "<span class=\"geist-anchor geist-anchor--cross-reference\" id=\"";
    out += detail::escape_html_attribute(detail::html_emitted_id(id, options));
    out += "\"></span>\n";
  }
  return out;
}

// The `failed` route: a placeholder naming the topic, its source record range
// and the reason.  Deliberately visible and deliberately labelled as a Geist
// diagnostic rather than as book text, so an absent topic is debuggable
// rather than invisible.
std::string failed_topic_html(const detail::TopicIdentityIR& topic,
                              const RenderDiagnostic& diagnostic) {
  std::string out = "<aside class=\"geist-diagnostic\"";
  out += " data-geist-reason=\"" +
         detail::escape_html_attribute(diagnostic.reason) + "\">";
  out += "<p class=\"geist-diagnostic-message\">Geist render diagnostic: "
         "topic <code>" +
         detail::escape_html_text(topic.id) +
         "</code> could not be rendered.</p>";
  out += "<p class=\"geist-diagnostic-source\">Source logical records " +
         std::to_string(diagnostic.source.start_logical_record) + "-" +
         std::to_string(diagnostic.source.end_logical_record) + ".</p>";
  out += "<p class=\"geist-diagnostic-reason\">Reason: <code>" +
         detail::escape_html_text(diagnostic.reason) + "</code>";
  if (!diagnostic.detail.empty())
    out += " - " + detail::escape_html_text(diagnostic.detail);
  out += "</p></aside>";
  return out;
}

void append_section(std::string& out, const std::string& section) {
  if (section.empty())
    return;
  if (!out.empty() && out.back() != '\n')
    out += '\n';
  out += section;
}

} // namespace

std::string TocEntry::html_fragment(const HtmlRenderOptions& options) const {
  detail::TopicIdentityIR identity;
  identity.id = id;
  identity.title = title;
  identity.heading_level = heading_level;
  identity.topic_number = topic_number;
  identity.start_logical_record = start_logical_record;
  identity.end_logical_record = end_logical_record;

  // One pass already decided how well this topic renders and by which route.
  // Reusing it is what keeps the two formats from disagreeing; it is cached,
  // so asking again costs nothing.
  const auto& diagnostic = render_diagnostic();

  const auto& outcome = lowered().outcome;
  const detail::DocumentIR* document =
      outcome && outcome->document ? &*outcome->document : nullptr;

  std::string inner;
  if (document != nullptr)
    inner = detail::render_document_html_blocks(*document, options);

  if (diagnostic.severity == RenderSeverity::best_effort) {
    const auto verbatim = best_effort_loader_ ? best_effort_loader_()
                                              : detail::TopicBestEffortIR{};
    append_section(inner, topic_heading_html(identity));
    auto emitted = verbatim.anchors;
    emitted.insert(emitted.end(), verbatim.footnote_anchors.begin(),
                   verbatim.footnote_anchors.end());
    append_section(inner, verbatim_anchors_html(emitted, options));
    append_section(inner,
                   detail::render_verbatim_rows_html(verbatim.rows, options));
  } else if (diagnostic.severity == RenderSeverity::failed) {
    append_section(inner, failed_topic_html(identity, diagnostic));
  }

  std::vector<std::string> destinations;
  if (document != nullptr)
    destinations = document->named_destinations;
  return detail::render_html_topic_root(id, destinations, diagnostic.severity,
                                        options, inner);
}

std::string TocEntry::html_document(
    const HtmlRenderOptions& options,
    const HtmlDocumentOptions& document_options) const {
  auto page = document_options;
  if (page.title.empty())
    page.title = title;
  return detail::render_html_document(html_fragment(options), page);
}

std::string BooDocument::html_fragment(const HtmlRenderOptions& options) const {
  // The whole book is the concatenation of its topics, each rendered by the
  // same route `TocEntry::html_fragment()` uses.
  std::string out = "<div class=\"geist-book\">\n";
  for (const auto& entry : toc_) {
    const auto topic = entry.html_fragment(options);
    if (topic.empty())
      continue;
    out += topic;
    if (out.back() != '\n')
      out += '\n';
  }
  out += "</div>\n";
  return out;
}

std::string BooDocument::html_document(
    const HtmlRenderOptions& options,
    const HtmlDocumentOptions& document_options) const {
  auto page = document_options;
  if (page.title.empty())
    page.title = book_properties_.title;
  return detail::render_html_document(html_fragment(options), page);
}

std::string BooDocument::topic_html_fragment(
    const std::string& topic_id, const HtmlRenderOptions& options) const {
  if (const auto* entry = find_toc_entry(topic_id))
    return entry->html_fragment(options);
  return synthesize_topic_entry(topic_id).html_fragment(options);
}

std::string BooDocument::topic_html_document(
    const std::string& topic_id, const HtmlRenderOptions& options,
    const HtmlDocumentOptions& document_options) const {
  if (const auto* entry = find_toc_entry(topic_id))
    return entry->html_document(options, document_options);
  return synthesize_topic_entry(topic_id).html_document(options,
                                                        document_options);
}

} // namespace geist
