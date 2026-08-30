#include <cstdio>
#include <cstdlib>
#include "geist/detail/render_diagnostic_ir.hpp"

#include "geist/detail/figure_block_ir.hpp"
#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

// Render provenance.
//
// A topic that renders badly must exit the pipeline with the consumer knowing
// that it did, and why.  Everything the exported Markdown, the `boo2git`
// sidecar manifest and `bootrace --coverage` say about render quality is
// derived here, so those three channels cannot drift apart.
//
// Fail-closed, precisely: a family declining a topic means "I will not assert
// that this is a table/figure/list", never "this topic produces nothing".
// `escalate_render_diagnostic` is the guard that enforces the second half of
// that rule -- when a route yields no content, the topic drops to a lower
// fidelity that still carries its words, rather than disappearing.

namespace geist {

const char* to_string(RenderSeverity severity) noexcept {
  switch (severity) {
  case RenderSeverity::typed:
    return "typed";
  case RenderSeverity::typed_degraded:
    return "typed-degraded";
  case RenderSeverity::legacy_fallback:
    return "legacy-fallback";
  case RenderSeverity::best_effort:
    return "best-effort";
  case RenderSeverity::failed:
    return "failed";
  }
  return "failed";
}

namespace {

// An HTML comment ends at the first `-->`, so no attribute value may contain
// `--`. Newlines and control bytes are folded to spaces so the marker stays
// exactly one line and stays greppable.
std::string comment_safe(const std::string& value) {
  std::string output;
  output.reserve(value.size());
  for (const auto ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7f) {
      if (!output.empty() && output.back() != ' ')
        output.push_back(' ');
      continue;
    }
    if (ch == '-' && !output.empty() && output.back() == '-') {
      output.push_back(' ');
    }
    output.push_back(ch);
  }
  while (!output.empty() && output.back() == ' ')
    output.pop_back();
  return output;
}

std::string record_range(const RenderSourceCoordinates& source) {
  return std::to_string(source.start_logical_record) + "-" +
         std::to_string(source.end_logical_record);
}

} // namespace

std::string render_diagnostic_comment(const RenderDiagnostic& diagnostic) {
  // A fully typed topic carries no marker at all. That is what keeps the
  // 94.5% of the corpus that renders cleanly byte-identical to a pipeline
  // without a diagnostics channel.
  if (diagnostic.severity == RenderSeverity::typed)
    return {};

  std::string marker = "<!-- geist-render: severity=";
  marker += to_string(diagnostic.severity);
  marker += " route=" + comment_safe(diagnostic.route);
  if (!diagnostic.family.empty())
    marker += " family=" + comment_safe(diagnostic.family);
  marker += " reason=" + comment_safe(diagnostic.reason);
  marker += " records=" + record_range(diagnostic.source);
  if (!diagnostic.degradations.empty()) {
    marker += " degraded=";
    for (std::size_t index = 0; index < diagnostic.degradations.size();
         ++index) {
      if (index != 0)
        marker += ",";
      marker += comment_safe(diagnostic.degradations[index].reason);
    }
  }
  // A typed-degraded topic has no whole-topic detail; the block's own reason
  // is what a reader of this one file needs.
  auto detail = diagnostic.detail;
  if (detail.empty()) {
    for (const auto& degradation : diagnostic.degradations) {
      if (degradation.detail.empty())
        continue;
      detail = degradation.detail;
      break;
    }
  }
  if (!detail.empty())
    marker += " detail=\"" + comment_safe(detail) + "\"";
  marker += " -->";
  return marker;
}

} // namespace geist

namespace geist::detail {
namespace {

RenderSourceCoordinates topic_coordinates(const TopicIdentityIR& topic) {
  RenderSourceCoordinates source;
  source.start_logical_record = topic.start_logical_record;
  source.end_logical_record = topic.end_logical_record;
  return source;
}

RenderSourceCoordinates block_coordinates(const TopicIdentityIR& topic,
                                          const DocumentNodeOriginIR& origin) {
  auto source = topic_coordinates(topic);
  if (origin.slices.empty())
    return source;
  // The earliest slice in source order locates the block.
  const auto& first = *std::min_element(
      origin.slices.begin(), origin.slices.end(),
      [](const DocumentSourceSliceIR& left,
         const DocumentSourceSliceIR& right) {
        if (left.logical_record != right.logical_record)
          return left.logical_record < right.logical_record;
        if (left.segment_index != right.segment_index)
          return left.segment_index < right.segment_index;
        return left.token_begin < right.token_begin;
      });
  source.logical_record = first.logical_record;
  source.segment_index = first.segment_index;
  source.token_begin = first.token_begin;
  source.token_end = first.token_end;
  source.byte_begin = first.byte_begin;
  source.byte_end = first.byte_end;
  return source;
}

} // namespace

RenderDiagnostic classify_typed_lowering(const TopicIdentityIR& topic,
                                         const DocumentIR* document,
                                         const std::string& typed_rejection,
                                         const TypedLoweringTraceIR& trace) {
  RenderDiagnostic diagnostic;
  diagnostic.source = topic_coordinates(topic);

  if (document == nullptr) {
    diagnostic.severity = RenderSeverity::legacy_fallback;
    diagnostic.route = "legacy";
    diagnostic.reason = "typed-lowering-declined";
    // Exactly the string `bootrace --coverage` prints, by construction: the
    // metric and the renderer read this one field.
    if (!typed_rejection.empty()) {
      diagnostic.detail = typed_rejection;
    } else {
      for (const auto& declined : trace.declined)
        diagnostic.detail +=
            (diagnostic.detail.empty() ? "" : " | ") + declined;
      if (diagnostic.detail.empty())
        diagnostic.detail = "no typed family recognized the source";
    }
    return diagnostic;
  }

  diagnostic.route = "typed";
  diagnostic.family = trace.family;
  for (const auto& block : document->blocks) {
    if (block.origin.fidelity != DocumentFidelityIR::degraded)
      continue;
    RenderDegradation degradation;
    degradation.block = block.origin.detail;
    degradation.reason = block.origin.degradation_code;
    degradation.detail = block.origin.degradation_detail;
    degradation.source = block_coordinates(topic, block.origin);
    diagnostic.degradations.push_back(std::move(degradation));
  }
  if (diagnostic.degradations.empty()) {
    diagnostic.severity = RenderSeverity::typed;
    diagnostic.reason = "typed";
    return diagnostic;
  }
  diagnostic.severity = RenderSeverity::typed_degraded;
  diagnostic.reason = "degraded-block";
  return diagnostic;
}

bool markdown_has_content(const std::string& markdown) {
  std::size_t cursor = 0;
  while (cursor <= markdown.size()) {
    auto end = markdown.find('\n', cursor);
    if (end == std::string::npos)
      end = markdown.size();
    auto line = markdown.substr(cursor, end - cursor);
    cursor = end + 1;
    while (!line.empty() &&
           std::isspace(static_cast<unsigned char>(line.back())) != 0)
      line.pop_back();
    std::size_t begin = 0;
    while (begin < line.size() &&
           std::isspace(static_cast<unsigned char>(line[begin])) != 0)
      ++begin;
    line.erase(0, begin);
    if (line.empty())
      continue;
    // A heading is the topic's own title and an anchor is navigation; neither
    // is book content, so neither rescues a topic from the empty case.
    if (line.front() == '#')
      continue;
    if (line.compare(0, 7, "<a id=\"") == 0)
      continue;
    return true;
  }
  return false;
}

namespace {

// Every token a control segment spends on its own opcode and operands, plus
// every token of the topic-envelope controls whose content the renderer
// already emits as the heading. What is left is body text.
std::set<std::size_t> control_tokens(const DecodedLogicalRecordSource& record) {
  std::set<std::size_t> tokens;
  const auto add = [&](const OutputRangeIR& range) {
    if (range.end <= range.begin) return;
    for (const auto token : source_tokens_intersecting_output(
             record.assembled, range.begin, range.end))
      tokens.insert(token);
  };
  for (const auto& segment : record.control_segments) {
    if (segment.display_text) continue;
    // Opcode and operands only.  A control's payload is display text -- the
    // `ST` title, a `cselect` label, an `SR` anchor's body -- and dropping it
    // would be exactly the silent loss this route exists to prevent.  The
    // topic-envelope controls (`ctopicn`, `cparent`, `csummary`, `chdlevel`,
    // ...) spend their whole display line on opcode plus operands, so they
    // disappear without a kind test; and a record whose envelope did not
    // parse keeps its text instead of losing the record with the envelope.
    add(segment.opcode_range);
    add(segment.operand_range);
  }
  return tokens;
}

// The `SI` word that opens a subject-index display line. An `SI` entry owns
// exactly one display line and draws nothing on it, so the line carrying that
// word is dropped whole.
//
// The decoder splits a segment at `SI` but classifies it as text with an
// empty opcode, so there is no opcode to test; what identifies the entry is
// the pair of facts that a segment *begins* here and that it begins with the
// word `SI`. Requiring the segment boundary is what keeps ordinary prose that
// merely starts a line with "SI " from being deleted.
//
// Only the `SI` word itself is marked, never the segment: a segment runs to
// the next control, so an `SI` segment also spans the body text on the
// display lines *after* it (SC31-711 record 22 holds `SI executables`
// followed by a paragraph), and marking that span would delete book text.
// See `Format/markup.md` s"Subject-index display lines": 29,239 such lines in
// 31 books, and hosted BookServer serves none of them.
std::set<std::size_t> index_entry_marker_tokens(
    const DecodedLogicalRecordSource& record) {
  std::set<std::size_t> tokens;
  const auto ascii = token_words_to_ascii(record.assembled.words);
  for (const auto& segment : record.control_segments) {
    // A segment a later pass proved to be ordinary display text spells `SI`
    // by coincidence and draws its words like any other text.
    if (segment.display_text) continue;
    const auto begin = segment.complete.begin;
    if (begin + 2 > ascii.size()) continue;
    if (ascii.compare(begin, 2, "SI") != 0) continue;
    // `SI` is the whole word, not the head of a longer one.
    if (begin + 2 < ascii.size() && ascii[begin + 2] != ' ') continue;
    for (const auto token :
         source_tokens_intersecting_output(record.assembled, begin, begin + 2))
      tokens.insert(token);
  }
  return tokens;
}

} // namespace

std::vector<std::string> best_effort_lines(
    const std::vector<DecodedLogicalRecordSource>& sources,
    const std::string& title) {
  std::vector<std::string> lines;
  for (const auto& record : sources) {
    const auto display = record_display_lines(record);
    if (!display) continue;
    const auto controls = control_tokens(record);
    const auto index_markers = index_entry_marker_tokens(record);
    for (const auto& line : *display) {
      // An `SI` entry owns its whole display line and draws nothing on it.
      if (!index_markers.empty()) {
        bool is_index_entry = false;
        for (const auto& cell : display_line_cells(record, line)) {
          if (cell.token != static_cast<std::size_t>(-1) &&
              index_markers.count(cell.token) != 0) {
            is_index_entry = true;
            break;
          }
        }
        if (is_index_entry) continue;
      }
      std::string text;
      for (const auto& cell : display_line_cells(record, line)) {
        if (cell.token != static_cast<std::size_t>(-1) &&
            controls.count(cell.token) != 0) {
          // A control's own words draw nothing; keep the column so the row
          // does not shift under the text that survives.
          text += ' ';
          continue;
        }
        text += figure_display_glyph(cell.word);
      }
      while (!text.empty() && text.back() == ' ') text.pop_back();
      if (text.find_first_not_of(' ') == std::string::npos) continue;
      // The `ST` control's payload is the topic title, which the heading
      // above the block already carries; repeating it would be noise, and a
      // topic whose only display content *is* its title has no body at all.
      // A record whose envelope did not parse keeps its `ST` opcode and
      // marker slot in front of the title, so the test is on the tail.  It is
      // restricted to the first emitted line, which is where the title sits.
      if (!title.empty() && lines.empty() && text.size() >= title.size() &&
          text.compare(text.size() - title.size(), title.size(), title) == 0)
        continue;
      lines.push_back(std::move(text));
    }
  }
  // Trailing blank rows carry nothing; leading ones would only pad the block.
  while (!lines.empty() && lines.back().empty()) lines.pop_back();
  return lines;
}

void escalate_render_diagnostic(RenderDiagnostic& diagnostic,
                                bool has_content,
                                bool best_effort_available,
                                bool source_decoded) {
  if (has_content)
    return;
  if (best_effort_available) {
    diagnostic.severity = RenderSeverity::best_effort;
    // The declining route's own explanation is kept: it is why we are here.
    diagnostic.detail = diagnostic.detail.empty()
                            ? "no route produced content"
                            : "no route produced content (" +
                                  diagnostic.detail + ")";
    diagnostic.route = "best-effort";
    diagnostic.reason = "no-structured-content";
    return;
  }
  if (source_decoded) {
    // The records decoded and carry no body: a heading alone is the correct
    // rendering, not a failure. The severity of the route that got here is
    // kept; only the reason changes, so a consumer can tell an empty topic
    // apart from a lost one.
    diagnostic.reason = "empty-topic-body";
    diagnostic.detail = diagnostic.detail.empty()
                            ? "topic has no body content in source"
                            : "topic has no body content in source (" +
                                  diagnostic.detail + ")";
    return;
  }
  diagnostic.severity = RenderSeverity::failed;
  diagnostic.detail =
      diagnostic.detail.empty()
          ? "the topic's source records could not be decoded"
          : "the topic's source records could not be decoded (" +
                diagnostic.detail + ")";
  diagnostic.route = "none";
  diagnostic.reason = "no-recoverable-source";
}

std::string render_best_effort_markdown(const std::vector<std::string>& lines) {
  std::string markdown = "```text\n";
  for (const auto& line : lines) {
    markdown += line;
    markdown += "\n";
  }
  markdown += "```\n";
  return markdown;
}

std::string render_failed_markdown(const TopicIdentityIR& topic,
                                   const RenderDiagnostic& diagnostic) {
  // Deliberately visible, and deliberately labelled as a Geist diagnostic
  // rather than as book text: there is no content here to confuse it with,
  // and an absent topic must be debuggable.
  std::string markdown = "> **Geist render diagnostic** — topic `";
  markdown += topic.id;
  markdown += "` could not be rendered.\n>\n";
  markdown += "> Source logical records " +
              std::to_string(diagnostic.source.start_logical_record) + "-" +
              std::to_string(diagnostic.source.end_logical_record) + ".\n";
  markdown += "> Reason: `" + diagnostic.reason + "`";
  if (!diagnostic.detail.empty())
    markdown += " — " + diagnostic.detail;
  markdown += "\n";
  return markdown;
}

std::string format_render_degradations(const RenderDiagnostic& diagnostic) {
  std::string output;
  for (const auto& degradation : diagnostic.degradations) {
    if (!output.empty())
      output += ",";
    output += degradation.reason;
  }
  return output;
}

} // namespace geist::detail
