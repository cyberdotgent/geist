// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/render/render_diagnostic_ir.hpp"
#include "geist/detail/lowering/source_rows_document_lowering.hpp"
#include "geist/detail/lowering/topic_lowering_outcome.hpp"

#include "geist/detail/ir/figure_block_ir.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/selector_link_ir.hpp"
#include "geist/detail/ir/sr_anchor_name.hpp"

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
    diagnostic.severity = RenderSeverity::best_effort;
    diagnostic.route = "best-effort";
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
  const auto collect = [&](const auto& self,
                           const BlockSequenceIR& blocks) -> void {
    for (const auto& block : blocks) {
      if (block.origin.fidelity == DocumentFidelityIR::degraded) {
        RenderDegradation degradation;
        degradation.block = block.origin.detail;
        degradation.reason = block.origin.degradation_code;
        degradation.detail = block.origin.degradation_detail;
        degradation.source = block_coordinates(topic, block.origin);
        diagnostic.degradations.push_back(std::move(degradation));
      }
      std::visit(
          [&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ListBlockIR>) {
              for (const auto& item : node.items) self(self, item.blocks);
            } else if constexpr (std::is_same_v<T, DefinitionListBlockIR>) {
              for (const auto& entry : node.entries)
                self(self, entry.definition_blocks);
            } else if constexpr (std::is_same_v<T, TableBlockIR>) {
              for (const auto& row : node.rows)
                for (const auto& cell : row.cells) self(self, cell.blocks);
            }
          },
          block.node);
    }
  };
  collect(collect, document->blocks);
  if (diagnostic.degradations.empty()) {
    diagnostic.severity = RenderSeverity::typed;
    diagnostic.reason = "typed";
    return diagnostic;
  }
  diagnostic.severity = RenderSeverity::typed_degraded;
  diagnostic.reason = "degraded-block";
  return diagnostic;
}

RenderDiagnostic classify_topic_lowering(
    const TopicIdentityIR& topic, const TopicLoweringOutcomeIR* outcome) {
  if (outcome == nullptr) {
    auto diagnostic = classify_typed_lowering(topic, nullptr, {}, {});
    escalate_render_diagnostic(diagnostic, false, false, false);
    return diagnostic;
  }
  const auto* typed_document = outcome->typed_claimed && outcome->document
                                   ? &*outcome->document
                                   : nullptr;
  auto diagnostic = classify_typed_lowering(
      topic, typed_document, outcome->rejection, outcome->trace);
  if (outcome->representation == TopicRepresentationKindIR::structured) {
    const auto body = outcome->document &&
                      document_ir_has_body(*outcome->document);
    escalate_render_diagnostic(diagnostic, body, false,
                               outcome->source_decoded);
  } else if (outcome->representation ==
             TopicRepresentationKindIR::source_rows) {
    escalate_render_diagnostic(diagnostic, false,
                               outcome->source_rows_have_body,
                               outcome->source_decoded);
    // The family describes the structured attempt, not the representation
    // ultimately selected.  A source-row document remains a family-less
    // best-effort result even when a typed heading-only attempt preceded it.
    diagnostic.family.clear();
    diagnostic.degradations.clear();
  } else {
    escalate_render_diagnostic(diagnostic, false, false,
                               outcome->source_decoded);
  }
  return diagnostic;
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
    // A dictionary spelling on a proven line-length byte opens no control,
    // regardless of its apparent operand grammar. Keep metadata/font segment
    // kinds intact for their typed consumers, but never suppress display text
    // merely because such a byte resembles their opcode.
    if (is_display_line_length_opcode(record, segment))
      continue;
    // Opcode and operands only.  A control's payload is display text -- the
    // `ST` title, a `cselect` label, an `SR` anchor's body -- and dropping it
    // would be exactly the silent loss this route exists to prevent.  The
    // topic-envelope controls (`ctopicn`, `cparent`, `csummary`, `chdlevel`,
    // ...) spend their whole display line on opcode plus operands, so they
    // disappear without a kind test; and a record whose envelope did not
    // parse keeps its text instead of losing the record with the envelope.
    add(segment.opcode_range);
    add(segment.operand_range);
    // A `LNK` selector's alternative list is control metadata too, and it
    // sits in the payload where the rule above would have kept it as display
    // text.  Hosted BookServer prints no character of it; leaving it in
    // spells the raw tuple `<BOOK> <> <> <SC24-5444> <ANY> <HCPA3>` onto the
    // page -- and, because the list owns its own display line, as an extra
    // row that pushes the drawn box art apart (SC24-5527-02 1.0).
    for (const auto token :
         selector_link_alternative_tokens(record, segment))
      tokens.insert(token);
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
// merely starts a line with "SI " from being deleted. Native-framed records
// additionally require the keyword immediately after the line-length byte;
// an indentation token before literal SI disproves an index instruction.
//
// Only the `SI` word itself is marked, never the segment: a segment runs to
// the next control, so an `SI` segment also spans the body text on the
// display lines *after* it (SC31-711 record 22 holds `SI executables`
// followed by a paragraph), and marking that span would delete book text.
// See `doc/boo-spec/markup.adoc` s"Subject-index display lines": 29,239 such lines in
// 31 books, and hosted BookServer serves none of them.
std::set<std::size_t> index_entry_marker_tokens(
    const DecodedLogicalRecordSource& record) {
  std::set<std::size_t> tokens;
  if (const auto* lines = record_display_lines(record)) {
    for (const auto& line : *lines)
      if (is_subject_index_display_line(record, line))
        tokens.insert(line.prefix_token + 1);
    return tokens;
  }
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

// A display line whose whole visible content is one `c.<xx>` body-control
// opcode standing at the line origin, with at most one operand word after it.
// Such a line is SCRIPT pagination or revision state -- `c.cp` keep-together,
// `c.cc` conditional column -- and it draws nothing: hosted BookServer prints
// no character of it (SC33-033 PREFACE.1 stores `c.cc 4` between a paragraph
// and a fence and serves only those two, DT 19930422134757; DREICMST 1.5.6.3
// stores a bare `c.cp`, DT 19911219125856).
//
// The decoder does not always keep the control boundary in front of these
// words -- SG24-204 record 153 puts `c.cp` inside an `SREFIG` segment, so
// `control_tokens` never sees it and the word reaches the page as prose (258
// `c.cp` and 24 `c.cc` lines across the checked-in exports).  The framing is
// what decides it instead: the opcode stands directly after the line's length
// byte and owns the line.  This is the same reading the typed route already
// uses (`body_control_line`, prose_topic_lines.cpp) and the same shape as the
// `SI` suppression below -- a control that owns a whole display line and
// draws nothing on it.
bool body_control_display_line(const DecodedLogicalRecordSource& record,
                               const DisplayLineIR& line) {
  if (line.token_end <= line.prefix_token + 1) return false;
  const auto* words = display_text_words(record, line.prefix_token + 1);
  if (words == nullptr) return false;
  std::string text;
  for (const auto word : *words) {
    if (word < 4) continue;
    if (word > 0x7F) return false;
    text.push_back(static_cast<char>(word));
  }
  text = trim_ascii(text);
  if (text.size() < 4 || text.compare(0, 2, "c.") != 0) return false;
  for (std::size_t at = 2; at < text.size(); ++at)
    if (std::islower(static_cast<unsigned char>(text[at])) == 0) return false;
  // At most one operand word after the opcode; anything more is a line the
  // reader draws, and this pass may not delete drawn text.
  std::size_t visible = 0;
  for (auto token = line.prefix_token + 2; token < line.token_end; ++token) {
    const auto* rest = display_text_words(record, token);
    if (rest == nullptr) continue;
    for (const auto word : *rest)
      if (word >= 4 && word != ' ') {
        ++visible;
        break;
      }
    if (visible > 1) return false;
  }
  return true;
}

} // namespace

std::vector<BestEffortLineIR> best_effort_display_lines(
    const std::vector<DecodedLogicalRecordSource>& sources,
    const std::string& title) {
  std::vector<BestEffortLineIR> lines;
  for (std::size_t record_index = 0; record_index < sources.size();
       ++record_index) {
    const auto& record = sources[record_index];
    const auto display = record_display_lines(record);
    if (!display) continue;
    const auto controls = control_tokens(record);
    const auto index_markers = index_entry_marker_tokens(record);
    for (std::size_t line_index = 0; line_index < display->size();
         ++line_index) {
      const auto& line = (*display)[line_index];
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
      // A body-control line draws nothing, whether or not the decoder kept
      // the control boundary in front of its opcode.
      if (body_control_display_line(record, line)) continue;
      BestEffortLineIR emitted;
      emitted.record_index = record_index;
      emitted.display_line_index = line_index;
      auto& text = emitted.text;
      for (const auto& cell : display_line_cells(record, line)) {
        // One byte offset per display column, so a consumer holding a column
        // range -- a selector's covered span -- can find the bytes that
        // column range names without re-deriving the row.
        emitted.column_offsets.push_back(text.size());
        if (cell.token != static_cast<std::size_t>(-1) &&
            controls.count(cell.token) != 0) {
          // A control's own words draw nothing; keep the column so the row
          // does not shift under the text that survives.
          text += ' ';
          continue;
        }
        text += figure_display_glyph(cell.word);
      }
      emitted.column_offsets.push_back(text.size());
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
      lines.push_back(std::move(emitted));
    }
  }
  // Trailing blank rows carry nothing; leading ones would only pad the block.
  while (!lines.empty() && lines.back().text.empty()) lines.pop_back();
  return lines;
}

std::vector<std::string> best_effort_lines(
    const std::vector<DecodedLogicalRecordSource>& sources,
    const std::string& title) {
  std::vector<std::string> lines;
  for (auto& line : best_effort_display_lines(sources, title))
    lines.push_back(std::move(line.text));
  return lines;
}

std::vector<std::string> best_effort_anchors(
    const std::vector<DecodedLogicalRecordSource>& sources) {
  std::vector<std::string> anchors;
  for (const auto& record : sources) {
    const auto ascii = token_words_to_ascii(record.assembled.words);
    for (const auto& segment : record.control_segments) {
      if (segment.display_text) continue;
      // `SRMSG <id>` names the message it opens; the anchor is the operand,
      // spelled the way the rest of the book references it.
      if (segment.kind == BookControlKind::message_start) {
        if (segment.operand_range.end <= segment.operand_range.begin) continue;
        const auto begin = std::min(segment.operand_range.begin, ascii.size());
        const auto end = std::min(segment.operand_range.end, ascii.size());
        auto id = trim_ascii(ascii.substr(begin, end - begin));
        if (id.empty()) continue;
        id = "MSG " + id;
        // Blank runs stay as stored where the control opens its row.
        if (const auto stored = message_row_anchor_name(record, segment);
            stored)
          id = *stored;
        if (std::find(anchors.begin(), anchors.end(), id) == anchors.end())
          anchors.push_back(std::move(id));
        continue;
      }
      if (segment.kind != BookControlKind::structural &&
          segment.kind != BookControlKind::table_start)
        continue;
      // The opcode is `SR` plus the anchor id; `SRTBL<id>` and `SRFIG<id>`
      // name the object, and a bare `SR<id>` names a spot in the text.
      const auto& opcode = segment.opcode;
      if (opcode.size() <= 2) continue;
      if (ascii_lower(opcode.substr(0, 2)) != "sr") continue;
      auto id = opcode.substr(2);
      if (id.empty()) continue;
      // A footnote is local to the page that carries it, not a destination
      // the book can reference; the typed families resolve `SRFTN` to
      // nothing, and a verbatim topic must not publish one either.
      if (ascii_lower(id.substr(0, 3)) == "ftn") continue;
      // The reader names the anchor after the whole row: `SRGLS logical
      // unit  (LU)` is `GLS logical unit  (LU)`, blanks as stored.
      if (!segment.source_tokens.empty())
        if (const auto stored =
                sr_row_anchor_name(record, segment.source_tokens.front());
            stored && stored->size() > id.size() &&
            ascii_equals_case_insensitive(stored->substr(0, id.size()), id) &&
            (*stored)[id.size()] == ' ')
          id = *stored;
      if (std::find(anchors.begin(), anchors.end(), id) == anchors.end())
        anchors.push_back(std::move(id));
    }
  }
  return anchors;
}

std::vector<std::string> best_effort_footnote_anchors(
    const std::vector<DecodedLogicalRecordSource>& sources) {
  std::vector<std::string> anchors;
  for (const auto& record : sources) {
    for (const auto& segment : record.control_segments) {
      if (segment.display_text) continue;
      if (segment.kind != BookControlKind::structural) continue;
      const auto& opcode = segment.opcode;
      if (opcode.size() <= 5) continue;
      if (ascii_lower(opcode.substr(0, 5)) != "srftn") continue;
      auto id = opcode.substr(2);
      if (std::find(anchors.begin(), anchors.end(), id) == anchors.end())
        anchors.push_back(std::move(id));
    }
  }
  return anchors;
}

void escalate_render_diagnostic(RenderDiagnostic& diagnostic,
                                bool has_content,
                                bool best_effort_available,
                                bool source_decoded) {
  if (has_content)
    return;
  if (best_effort_available) {
    diagnostic.severity = RenderSeverity::best_effort;
    diagnostic.route = "best-effort";
    // A topic the typed dispatcher declined already says so, and its own
    // rejection is the precise explanation; restating it as "no structured
    // content" would replace the reason with a vaguer one and wrap the
    // detail in a second layer of prose.
    if (diagnostic.reason != "typed-lowering-declined") {
      diagnostic.detail = diagnostic.detail.empty()
                              ? "no route produced content"
                              : "no route produced content (" +
                                    diagnostic.detail + ")";
      diagnostic.reason = "no-structured-content";
    }
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
