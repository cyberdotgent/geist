// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/layout/layout_ir.hpp"

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <utility>

namespace geist::detail {
namespace {

std::string visible_token(const TokenWords& words) {
  TokenWords visible;
  for (const auto word : words) {
    if (word >= 0x20 && word != 0x2666) visible.push_back(word);
  }
  auto text = token_words_to_ascii(visible);
  const auto printable = [](const unsigned char ch) {
    return ch >= 0x20 && std::isspace(ch) == 0;
  };
  text.erase(text.begin(),
             std::find_if(text.begin(), text.end(), printable));
  text.erase(std::find_if(text.rbegin(), text.rend(), printable).base(),
             text.end());
  return text;
}

std::string visible_slice(const DecodedLogicalRecordSource& record,
                          std::size_t begin, std::size_t end,
                          std::size_t origin) {
  std::vector<TokenWords> tokens(record.tokens.begin() + begin,
                                 record.tokens.begin() + end);
  auto text = token_words_to_ascii(assemble_logical_record(tokens));
  if (text.size() >= origin) text.erase(0, origin);
  return trim_ascii(std::move(text));
}

// An end control whose payload is the display text that follows it.  The
// BUILD 1.3 artwork envelope's `ceartdesc` closes the picture description
// and carries the figure's caption lines the way `SREFIG` carries the prose
// after a figure (SG24-4815-01 1.1 record 26 segment 1).
bool structural_end_control(const ControlSegmentIR& segment) {
  const auto opcode = ascii_lower(segment.opcode);
  return segment.kind == BookControlKind::table_end ||
         segment.kind == BookControlKind::art_description_end ||
         opcode == "srefig";
}

bool layout_control(const ControlSegmentIR& segment) {
  const auto kind = segment.kind;
  return kind == BookControlKind::text || kind == BookControlKind::font ||
         kind == BookControlKind::title ||
         kind == BookControlKind::select ||
         kind == BookControlKind::table_start ||
         kind == BookControlKind::menu_item ||
         kind == BookControlKind::message_start ||
         structural_end_control(segment);
}

bool authoritative_native_rows(
    const std::vector<DecodedLogicalRecordSource>& records,
    const DecodedLogicalRecordSource& record, const ControlSegmentIR& segment) {
  if (segment.source_tokens.empty()) return false;
  if (segment.kind == BookControlKind::select)
    return true;
  if (segment.kind != BookControlKind::text || segment.segment_index != 0)
    return false;
  // A selector whose payload crosses a record boundary still addresses the
  // next record's first native display line. This is source adjacency, not
  // a generated-list/topic classification.
  //
  // An `SRTBL` table envelope that is the last control of its record
  // continues the same way: the next record's leading text segment holds the
  // envelope's next drawn line.  SC21-8295-03 `COMMENTS` record 968 ends with
  // `SRTBLTBLUNIQ161` and its header rules, and record 969 opens with the
  // envelope's only body line `   | Overall satisfaction ... |` (token 0 is
  // its length byte, spelled as three blanks), then `SRETBL`.  The text
  // segment begins at the first word, so the line's own length byte and
  // origin run lie outside it and no compact boundary finds the row.  Hosted
  // BookServer (`SC21-829`, DT 19970409135933) serves that line as the
  // `Overall satisfaction` row of the table.
  const auto previous = std::find_if(
      records.begin(), records.end(), [&](const auto& candidate) {
        return candidate.logical_record + 1 == record.logical_record;
      });
  if (previous == records.end() || previous->control_segments.empty())
    return false;
  const auto& carried = previous->control_segments.back();
  return (carried.kind == BookControlKind::select &&
          carried.payload_range.begin == carried.payload_range.end) ||
         carried.kind == BookControlKind::table_start;
}

bool authoritative_native_line(
    const std::vector<DecodedLogicalRecordSource>& records,
    const DecodedLogicalRecordSource& record, const ControlSegmentIR& segment,
    const DisplayLineIR& line) {
  if (!authoritative_native_rows(records, record, segment)) return false;
  const auto* lines = record_display_lines(record);
  if (lines == nullptr) return false;
  // A selector addresses its next drawn line, not every later line in the
  // compatibility payload. A continued selector uses the same source proof
  // at the beginning of the adjacent record.
  for (const auto& candidate : *lines) {
    auto content = candidate.prefix_token + 1;
    if (content >= candidate.token_end ||
        !all_space_words(record.tokens[content]))
      continue;
    while (content < candidate.token_end &&
           all_space_words(record.tokens[content]))
      ++content;
    if (content == candidate.token_end ||
        candidate.token_end > segment.source_tokens.back() + 1 ||
        !std::binary_search(segment.source_tokens.begin(),
                            segment.source_tokens.end(), content))
      continue;
    return candidate.prefix_token == line.prefix_token;
  }
  return false;
}

const ControlSegmentIR* source_segment(
    const std::vector<DecodedLogicalRecordSource>& records,
    const PhysicalRowIR& row) {
  const auto record = std::find_if(records.begin(), records.end(),
                                   [&](const auto& candidate) {
                                     return candidate.logical_record ==
                                            row.logical_record;
                                   });
  if (record == records.end() ||
      row.segment_index >= record->control_segments.size())
    return nullptr;
  return &record->control_segments[row.segment_index];
}

bool has_no_following_control(
    const std::vector<DecodedLogicalRecordSource>& records,
    const PhysicalRowIR& row) {
  const auto record = std::find_if(records.begin(), records.end(),
                                   [&](const auto& candidate) {
                                     return candidate.logical_record ==
                                            row.logical_record;
                                   });
  return record != records.end() && !record->control_segments.empty() &&
         row.segment_index + 1 == record->control_segments.size();
}

bool run_can_continue(
    const std::vector<DecodedLogicalRecordSource>& records,
    const DisplayRunIR& run) {
  if (run.rows.empty() || !has_no_following_control(records, run.rows.back()))
    return false;
  if (run.control_kind == BookControlKind::font) return true;
  const auto* segment = source_segment(records, run.rows.front());
  return segment != nullptr && structural_end_control(*segment);
}

bool run_origin_allows_continuation(
    const std::vector<DecodedLogicalRecordSource>& records,
    const DisplayRunIR& run) {
  if (run.control_kind == BookControlKind::font) return true;
  if (run.rows.empty()) return false;
  const auto* segment = source_segment(records, run.rows.front());
  return segment != nullptr && structural_end_control(*segment);
}

std::vector<std::size_t>
word_byte_offsets(const AssembledLogicalRecord& assembled) {
  std::vector<std::size_t> offsets(assembled.words.size() + 1);
  for (std::size_t word = 0; word < assembled.words.size(); ++word) {
    offsets[word + 1] = offsets[word] +
                        token_words_to_ascii({assembled.words[word]}).size();
  }
  return offsets;
}

// One `<column> <length>` display-geometry pair of a CFONT or CSELECT
// operand. Both controls address display columns of exactly one display row
// (doc/boo-spec/markup.adoc, "Spans And The Display Row").
struct DisplaySpan {
  std::size_t column = 0;
  std::size_t length = 0;
};

std::string segment_operand_text(const DecodedLogicalRecordSource& record,
                                 const ControlSegmentIR& segment) {
  const auto text = token_words_to_ascii(record.assembled.words);
  const auto& range = segment.operand_range;
  if (range.begin >= range.end || range.end > text.size()) return {};
  return text.substr(range.begin, range.end - range.begin);
}

// The display spans a font or selector control states. A CFONT operand is a
// sequence of `<column> <length> <code>` triples; a CSELECT operand starts
// with one `<column> <length>` pair. Anything else yields no spans, so the
// caller keeps its existing behaviour.
std::vector<DisplaySpan> segment_display_spans(
    const DecodedLogicalRecordSource& record, const ControlSegmentIR& segment) {
  if (segment.malformed) return {};
  std::vector<std::string> words;
  std::istringstream operands(segment_operand_text(record, segment));
  std::string word;
  while (operands >> word) words.push_back(word);
  const auto decimal = [](const std::string& value, std::size_t& out) {
    if (value.empty() || value.size() > 9) return false;
    out = 0;
    for (const auto ch : value) {
      if (std::isdigit(static_cast<unsigned char>(ch)) == 0) return false;
      out = out * 10 + static_cast<std::size_t>(ch - '0');
    }
    return true;
  };
  std::vector<DisplaySpan> spans;
  if (segment.kind == BookControlKind::font) {
    // The final triple may carry the `,` operand separator glued to its code.
    if (words.empty() || words.size() % 3 != 0) return {};
    for (std::size_t index = 0; index + 2 < words.size(); index += 3) {
      DisplaySpan span;
      if (!decimal(words[index], span.column) ||
          !decimal(words[index + 1], span.length) || span.length == 0)
        return {};
      spans.push_back(span);
    }
    return spans;
  }
  if (segment.kind == BookControlKind::select) {
    DisplaySpan span;
    if (words.size() < 2 || !decimal(words[0], span.column) ||
        !decimal(words[1], span.length) || span.length == 0)
      return {};
    spans.push_back(span);
    return spans;
  }
  return {};
}

bool word_aligned(const std::string& text, std::size_t begin,
                  std::size_t end) {
  if (end <= begin || end > text.size()) return false;
  if (text[begin] == ' ' || text[end - 1] == ' ') return false;
  if (begin > 0 && text[begin - 1] != ' ') return false;
  if (end < text.size() && text[end] != ' ') return false;
  return true;
}

// A row boundary whose marker glyph a span of the same control covers is not
// a marker slot at all: the glyph is styled display text of the row that is
// still open (doc/boo-spec/markup.adoc, "Spans And The Display Row" -- "A leading
// glyph is only a row marker when no pending span covers exactly its
// columns", and "A span holds its row open").
//
// `row_text` is the display text of the open row extended over the candidate
// marker token, and the marker's decoded word stands at its end. The
// operand's columns and the row's display cells differ by one shift, the
// row's left margin, which the Layout IR does not model; the shift is
// therefore derived from the span that would cover the marker word exactly
// and is admitted only when it is unique and makes *every* span of the
// control land on whole display words of this row.
bool span_covers_row_marker(const std::vector<DisplaySpan>& spans,
                            const std::string& row_text,
                            const std::string& marker_word) {
  if (spans.empty() || marker_word.empty() ||
      row_text.size() < marker_word.size())
    return false;
  const auto marker_begin = row_text.size() - marker_word.size();
  if (row_text.compare(marker_begin, marker_word.size(), marker_word) != 0)
    return false;
  if (!word_aligned(row_text, marker_begin, row_text.size())) return false;
  std::size_t admitted = 0;
  for (const auto& covering : spans) {
    // The shift is the row's left margin, so it is never negative. A row
    // whose stored text is *longer* than the columns the operand names
    // carries bytes the reader does not display -- a decoder placeholder run
    // is the common case -- and the Layout IR cannot say where they fall, so
    // such a row stays fail-closed.
    if (covering.length != marker_word.size() ||
        covering.column < marker_begin)
      continue;
    const auto shift = covering.column - marker_begin;
    const auto aligned = std::all_of(
        spans.begin(), spans.end(), [&](const DisplaySpan& span) {
          return span.column >= shift &&
                 word_aligned(row_text, span.column - shift,
                              span.column - shift + span.length);
        });
    if (aligned) ++admitted;
  }
  return admitted == 1;
}

PhysicalBreakKind marker_break(const std::string& marker) {
  if (!marker.empty() &&
      std::all_of(marker.begin(), marker.end(),
                  [](const auto ch) { return ch == '?'; }))
    return PhysicalBreakKind::soft_wrap;
  return PhysicalBreakKind::unknown;
}

} // namespace

LayoutIR extract_layout_ir(
    const std::vector<DecodedLogicalRecordSource>& records) {
  LayoutIR layout;
  DisplayRunId next_run = 1;
  for (const auto& record : records) {
    const auto byte_offsets = word_byte_offsets(record.assembled);
    for (const auto& segment : record.control_segments) {
      if (!layout_control(segment) || segment.source_tokens.empty())
        continue;
      struct Boundary {
        std::size_t marker;
        std::size_t origin;
      };
      std::vector<Boundary> boundaries;
      std::vector<std::pair<std::size_t, std::size_t>> native_row_ends;
      for (std::size_t at = 1; at < segment.source_tokens.size(); ++at) {
        const auto marker = segment.source_tokens[at - 1];
        const auto origin = segment.source_tokens[at];
        const auto marker_byte = marker < record.assembled.tokens.size()
                                     ? byte_offsets[record.assembled.tokens[marker]
                                                        .output_begin]
                                     : std::size_t{0};
        const auto origin_byte = origin < record.assembled.tokens.size()
                                     ? byte_offsets[record.assembled.tokens[origin]
                                                        .output_begin]
                                     : std::size_t{0};
        if (origin != marker + 1 || origin >= record.tokens.size() ||
            origin >= record.encoded_tokens.size() ||
            marker >= record.encoded_tokens.size() ||
            record.encoded_tokens[marker].width != 1 ||
            record.encoded_tokens[origin].width != 1 ||
            !all_space_words(record.tokens[origin]) ||
            visible_token(record.tokens[marker]).empty() ||
            marker_byte < segment.payload_range.begin ||
            origin_byte < segment.payload_range.begin)
          continue;
        boundaries.push_back({marker, origin});
      }
      // Placeholder question runs may be excluded from the compatibility
      // segment's trimmed byte span even though their following origin token
      // remains owned by the payload. Recover that immediately adjacent,
      // source-proven boundary without admitting other out-of-segment words.
      for (const auto origin : segment.source_tokens) {
        if (origin == 0 || origin >= record.tokens.size() ||
            !all_space_words(record.tokens[origin]) ||
            std::any_of(boundaries.begin(), boundaries.end(),
                        [&](const auto& boundary) {
                          return boundary.origin == origin;
                        }))
          continue;
        const auto marker = origin - 1;
        const auto marker_text = visible_token(record.tokens[marker]);
        if (marker >= record.encoded_tokens.size() ||
            record.encoded_tokens[marker].width != 1 ||
            marker_break(marker_text) != PhysicalBreakKind::soft_wrap)
          continue;
        boundaries.push_back({marker, origin});
      }
      std::sort(boundaries.begin(), boundaries.end(),
                [](const auto& left, const auto& right) {
                  return left.marker < right.marker;
                });

      // A marker/origin boundary whose row carries no display text at all is
      // a candidate for two different readings: an empty display row, or a
      // display word the row model mistook for a marker slot. The control's
      // own operand decides. Only where a span of this control covers the
      // marker word exactly, and the same margin shift puts every other span
      // of the control on a whole display word of the same row, is the
      // boundary withdrawn and its marker returned to the open row.
      auto spans = segment_display_spans(record, segment);
      if (spans.empty() && segment.kind == BookControlKind::text &&
          segment.segment_index == 0) {
        // A control payload that reaches the end of its record continues into
        // the leading text segment of the immediately adjacent record, so its
        // operand still states the geometry of the rows stored there. This is
        // the same adjacency the display run itself is joined on below.
        const auto previous = std::find_if(
            records.begin(), records.end(), [&](const auto& candidate) {
              return candidate.logical_record + 1 == record.logical_record;
            });
        if (previous != records.end() && !previous->control_segments.empty()) {
          const auto& carried = previous->control_segments.back();
          if (carried.kind == BookControlKind::font ||
              carried.kind == BookControlKind::select)
            spans = segment_display_spans(*previous, carried);
        }
      }
      // Inner boundaries only, walked from the end so that withdrawing one
      // leaves the rows in front of it to be judged against the list it
      // leaves behind.
      const auto candidates = spans.empty() ? std::size_t{1} : boundaries.size();
      for (std::size_t at = candidates; at-- > 1;) {
        const auto& boundary = boundaries[at];
        const auto end = at + 1 < boundaries.size()
                             ? boundaries[at + 1].marker
                             : segment.source_tokens.back() + 1;
        if (end <= boundary.origin || end > record.tokens.size()) continue;
        if (!visible_slice(record, boundary.origin, end,
                           record.tokens[boundary.origin].size())
                 .empty())
          continue;
        const auto open = boundaries[at - 1].marker;
        if (open >= boundary.marker ||
            boundary.marker + 1 > record.tokens.size())
          continue;
        std::vector<TokenWords> tokens(record.tokens.begin() + open,
                                       record.tokens.begin() +
                                           boundary.marker + 1);
        auto row_text =
            token_words_to_ascii(assemble_logical_record(tokens));
        while (!row_text.empty() && row_text.back() == ' ') row_text.pop_back();
        if (!span_covers_row_marker(spans, row_text,
                                    visible_token(
                                        record.tokens[boundary.marker])))
          continue;
        boundaries.erase(boundaries.begin() +
                         static_cast<std::ptrdiff_t>(at));
      }

      DisplayRunIR run;
      run.id = next_run++;
      run.control_kind = segment.kind;

      // A control payload may begin directly with its native-origin token,
      // without the otherwise common compact marker slot. Admit it only when
      // that token starts wholly inside the typed payload range.
      if (segment.kind != BookControlKind::text) {
        for (const auto token : segment.source_tokens) {
          if (token >= record.tokens.size() ||
              token >= record.assembled.tokens.size() ||
              !all_space_words(record.tokens[token]))
            continue;
          const auto begin = byte_offsets[record.assembled.tokens[token]
                                              .output_begin];
          if (begin < segment.payload_range.begin ||
              (!boundaries.empty() && token >= boundaries.front().marker))
            continue;
          if (token > 0 && token - 1 < record.assembled.tokens.size()) {
            const auto previous_begin = byte_offsets[
                record.assembled.tokens[token - 1].output_begin];
            if (previous_begin >= segment.payload_range.begin &&
                !visible_token(record.tokens[token - 1]).empty())
              continue;
          }
          const auto end = boundaries.empty()
                               ? segment.source_tokens.back() + 1
                               : boundaries.front().marker;
          if (end <= token || end > record.tokens.size()) break;
          PhysicalRowIR row;
          row.run = run.id;
          row.logical_record = record.logical_record;
          row.segment_index = segment.segment_index;
          row.token_begin = token;
          row.token_end = end;
          row.native_origin = record.tokens[token].size();
          row.start = PhysicalRowStartKind::control_payload;
          row.break_before = PhysicalBreakKind::hard_object;
          row.visible_text = visible_slice(record, token, end,
                                           row.native_origin);
          if (!row.visible_text.empty()) run.rows.push_back(std::move(row));
          break;
        }
      }

      // Row coverage from the record's own display-line framing.
      //
      // Every display line of a record payload opens on a length byte, and
      // that byte is the row-control slot -- always and only. The marker
      // heuristic above recognises the slot by the word the dictionary
      // spells for it, so a length byte the dictionary happens to spell as
      // blanks, or as an ordinary word standing where the payload's own
      // opening text would stand, is invisible to it and the row that line
      // draws is then built by nobody. Its words are drawn by the source and
      // reachable by no row at all.
      //
      // The framing stored on the record names those slots exactly, so read
      // it rather than guess again. Only a line whose first content token is
      // the row's native display origin -- the leading blanks that place the
      // row on the display -- is admitted: a line that opens on its own
      // control word instead (an `SI` index term, a `cfont`, an `SRMSG`) is
      // that control's line, not display text this segment draws in a row,
      // and stays out. That distinction is per display line, not per
      // segment, so an index term sharing a segment with an introduction
      // sentence keeps the sentence and leaves the term behind.
      //
      // Framing also disproves heuristic boundaries *inside* a drawn line:
      // punctuation, a caption word followed by padding, and a one-cell
      // ordinal prefix are all payload on a source-proven selected line.
      // Selection columns describe that text; they do not create line slots.
      // Title introductions retain their compact physical fragments, but
      // receive the exact prefix/origin reversal repair below as well.
      const auto* lines = authoritative_native_rows(records, record, segment) ||
                                  segment.kind == BookControlKind::title ||
                                  segment.kind == BookControlKind::text ||
                                  segment.kind == BookControlKind::font
                              ? record_display_lines(record)
                              : nullptr;
      if (lines != nullptr) {
        const auto opened = boundaries.empty()
                                ? segment.source_tokens.back() + 1
                                : boundaries.front().marker;
        const auto owned = [&](const std::size_t token) {
          return std::binary_search(segment.source_tokens.begin(),
                                    segment.source_tokens.end(), token);
        };
        bool admitted = false;
        for (const auto& line : *lines) {
          const auto marker = line.prefix_token;
          const auto origin = marker + 1;
          const auto authoritative =
              authoritative_native_line(records, record, segment, line);
          // Other fixed-layout consumers still use compact physical fragments
          // rather than whole native rows. Preserve that compatibility model:
          // its framing pass only fills uncovered leading display lines.
          //
          // A markerless control-payload row opened on this very line's
          // length byte and holding only the revision bar does not cover the
          // line: it is the speculative claim
          // the replacement below exists for, made because the byte is
          // spelled as a blank.  Left standing, it ends at the first
          // heuristic boundary inside the line, and the word that boundary
          // calls a marker is lost.  ICHA6MST 3.1.2 record 561 line 26
          // ` | 1    Master Console Action` (length byte 9 spelled ` `) was
          // cut at the routing code `1`, which vanished, and the `cfont`
          // above it then styled `M` instead of `1`; hosted (DT
          // 19910801172557) serves ` | <B>1</B>    <B>Master</B> ...`.
          //
          // Only a row that holds nothing but the line's margin revision bar
          // is replaced: the bar is margin, not a row of its own.  Other
          // compact fragments of a first line (a glossary term before its
          // `.`) are what their consumers read.
          const auto margin_bar_only = [&](const PhysicalRowIR& row) {
            bool bar = false;
            for (auto token = row.token_begin;
                 token < row.token_end && token < record.tokens.size();
                 ++token) {
              if (visible_token(record.tokens[token]).empty()) continue;
              if (!is_display_line_margin_revision_bar(record, token))
                return false;
              bar = true;
            }
            return bar;
          };
          const auto speculative_only =
              !run.rows.empty() &&
              std::all_of(run.rows.begin(), run.rows.end(),
                          [&](const auto& row) {
                            return row.start ==
                                       PhysicalRowStartKind::control_payload &&
                                   row.token_begin == marker &&
                                   margin_bar_only(row);
                          });
          if (!authoritative &&
              ((!run.rows.empty() && !speculative_only) || marker >= opened))
            continue;
          if (origin >= line.token_end ||
              origin >= record.tokens.size() ||
              marker >= record.encoded_tokens.size() ||
              record.encoded_tokens[marker].width != 1 ||
              !all_space_words(record.tokens[origin]))
            continue;
          if (authoritative) {
            // Compatibility text ranges trim leading blanks. Source-owned
            // content within the complete native line proves its prefix and
            // origin even if both were outside that trimmed range.
            auto content = origin;
            while (content < line.token_end &&
                   all_space_words(record.tokens[content]))
              ++content;
            if (content == line.token_end || !owned(content) ||
                line.token_end > segment.source_tokens.back() + 1)
              continue;
            const auto content_byte =
                content < record.assembled.tokens.size()
                    ? byte_offsets[record.assembled.tokens[content].output_begin]
                    : std::size_t{0};
            if (content_byte < segment.payload_range.begin) continue;
          } else {
            // Retain the established leading-coverage admission exactly for
            // consumers of compact fragments, including ownership of both
            // original slots and the payload byte boundary.
            const auto marker_byte =
                marker < record.assembled.tokens.size()
                    ? byte_offsets[record.assembled.tokens[marker].output_begin]
                    : std::size_t{0};
            if (!owned(marker) || !owned(origin) ||
                marker_byte < segment.payload_range.begin)
              continue;
          }
          // A blank-spelled length byte can also have opened a speculative
          // markerless control-payload row above. The native row replaces
          // that claim; keep only any preceding same-line control payload.
          for (auto& row : run.rows)
            if (row.token_begin < marker && row.token_end > marker) {
              row.token_end = marker;
              row.visible_text = visible_slice(record, row.token_begin,
                                               row.token_end,
                                               row.native_origin);
            }
          run.rows.erase(
              std::remove_if(run.rows.begin(), run.rows.end(),
                             [&](const auto& row) {
                               return row.visible_text.empty() ||
                                      (row.token_begin >= marker &&
                                       row.token_begin < line.token_end);
                             }),
              run.rows.end());
          // A boundary the heuristic placed inside this line stands on a word
          // the framing calls line content, so it is not a row-control slot
          // and the row it opened began in the middle of a drawn line. The
          // line's own length byte replaces it.
          //
          // A boundary whose origin is the next line's length byte is that
          // next line's start read one token early; the snap below moves it
          // onto the length byte, so it is kept here (packet GLOSSARY record
          // 366 line 21 ends on `for`, read as the slot before line 22).
          boundaries.erase(
              std::remove_if(boundaries.begin(), boundaries.end(),
                             [&](const auto& boundary) {
                               return boundary.marker > marker &&
                                      boundary.marker < line.token_end &&
                                      !is_display_line_length_token(
                                          record, boundary.origin);
                             }),
              boundaries.end());
          boundaries.push_back({marker, origin});
          if (authoritative) {
            native_row_ends.emplace_back(marker, line.token_end);
            // Close the selected line exactly where its native framing ends.
            // Recover only the *start* of the next drawn line if its blank
            // length spelling hid that boundary. Keep that line's existing
            // inner compact fragments: it is not selected by this control.
            for (const auto& following : *lines) {
              if (following.prefix_token < line.token_end) continue;
              auto content = following.prefix_token + 1;
              if (content >= following.token_end) continue;
              const auto following_origin = content;
              if (!all_space_words(record.tokens[content])) break;
              while (content < following.token_end &&
                     all_space_words(record.tokens[content])) ++content;
              if (content == following.token_end) continue;
              if (owned(content) &&
                  following.token_end <= segment.source_tokens.back() + 1)
                boundaries.push_back({following.prefix_token,
                                      following_origin});
              break;
            }
          }
          admitted = true;
        }
        // A boundary whose origin run is itself a length byte is the same
        // pair read one token early: the slot it calls a marker is the last
        // drawn word of the line before, and the row it opens therefore
        // silently drops that word. Snap the pair onto the line the framing
        // states.
        for (auto& boundary : boundaries) {
          const auto next = boundary.origin + 1;
          if (!is_display_line_length_token(record, boundary.origin) ||
              next >= record.tokens.size() || record.tokens[next].empty() ||
              !owned(boundary.origin) || !owned(next))
            continue;
          // A markerless control-payload row may already have been closed at
          // the speculative marker. Moving that boundary must return the
          // displaced terminal word to its preceding row as well; otherwise
          // neither row owns it even though the marker itself is corrected.
          for (auto& row : run.rows)
            if (row.token_end == boundary.marker) {
              row.token_end = boundary.origin;
              row.visible_text = visible_slice(record, row.token_begin,
                                               row.token_end,
                                               row.native_origin);
            }
          boundary = {boundary.origin, next};
          admitted = true;
        }
        // A boundary that calls the first word of a framed display line its
        // marker reads that word as the row-control slot, and the row it
        // opens drops it.  The slot is the line's length byte, always and
        // only, so such a boundary moves onto the line's own length byte.
        // SC30-3347-06 C.2 record 401 line 19 stores its length byte as a
        // glyph run, a 26-blank origin, then `0` and `Provides information
        // only.`; hosted (DT 19930330181700) serves `<B>0</B>`.  ICHA6MST 3.1.2
        // record 562 line 5 is ` | 9    System Security` behind a revision
        // bar.  Only a word with a letter or digit counts: the punctuation
        // slots of compact fragments (a glossary term's `.`) stay as they are.
        for (auto& boundary : boundaries) {
          if (is_display_line_length_token(record, boundary.marker)) continue;
          const auto* line = display_line_of_token(record, boundary.marker);
          if (line == nullptr || boundary.marker <= line->prefix_token + 1)
            continue;
          const auto origin = line->prefix_token + 1;
          if (origin >= record.tokens.size() || !owned(origin) ||
              !all_space_words(record.tokens[origin]))
            continue;
          bool leading = true;
          for (auto token = origin + 1; token < boundary.marker; ++token)
            if (!all_space_words(record.tokens[token]) &&
                !is_display_line_margin_revision_bar(record, token))
              leading = false;
          const auto word = visible_token(record.tokens[boundary.marker]);
          const auto drawn = std::any_of(word.begin(), word.end(), [](char ch) {
            return std::isalnum(static_cast<unsigned char>(ch)) != 0;
          });
          if (!leading || !drawn) continue;
          for (auto& row : run.rows)
            if (row.token_begin < line->prefix_token &&
                row.token_end > line->prefix_token) {
              row.token_end = line->prefix_token;
              row.visible_text = visible_slice(record, row.token_begin,
                                               row.token_end,
                                               row.native_origin);
            }
          run.rows.erase(
              std::remove_if(run.rows.begin(), run.rows.end(),
                             [&](const auto& row) {
                               return row.visible_text.empty() ||
                                      (row.token_begin >= line->prefix_token &&
                                       row.token_begin < boundary.marker);
                             }),
              run.rows.end());
          boundary = {line->prefix_token, origin};
          admitted = true;
        }
        if (admitted) {
          std::sort(boundaries.begin(), boundaries.end(),
                    [](const auto& left, const auto& right) {
                      return left.marker < right.marker;
                    });
          boundaries.erase(std::unique(boundaries.begin(), boundaries.end(),
                                       [](const auto& left, const auto& right) {
                                         return left.marker == right.marker;
                                       }),
                           boundaries.end());
        }
      }

      // The markerless control-payload row above was cut at the first
      // boundary the marker heuristic had found.  The framing pass can since
      // have put an earlier boundary in front of it (a line whose length
      // byte the dictionary spells as blanks): the row then starts inside, or
      // reaches into, rows the boundaries build below, and the run would
      // hold the same tokens twice.  SC41-3550-01 2.2 record 40 segment 12
      // read the step number `6` (tokens 240-242) both as that speculative
      // row and as the row its own line's length byte opens.  The boundary
      // rows are the line-proven ones, so the speculative row yields to them.
      if (!boundaries.empty()) {
        const auto first_marker = boundaries.front().marker;
        for (auto& row : run.rows)
          if (row.start == PhysicalRowStartKind::control_payload &&
              row.token_begin < first_marker && row.token_end > first_marker) {
            row.token_end = first_marker;
            row.visible_text = visible_slice(record, row.token_begin,
                                             row.token_end, row.native_origin);
          }
        run.rows.erase(
            std::remove_if(run.rows.begin(), run.rows.end(),
                           [&](const auto& row) {
                             return row.start ==
                                        PhysicalRowStartKind::control_payload &&
                                    (row.token_begin >= first_marker ||
                                     row.visible_text.empty());
                           }),
            run.rows.end());
      }

      for (std::size_t row_index = 0; row_index < boundaries.size();
           ++row_index) {
        const auto& boundary = boundaries[row_index];
        auto end = row_index + 1 < boundaries.size()
                             ? boundaries[row_index + 1].marker
                             : segment.source_tokens.back() + 1;
        for (const auto& native_end : native_row_ends)
          if (native_end.first == boundary.marker)
            end = std::min(end, native_end.second);
        if (end <= boundary.origin || end > record.tokens.size()) continue;
        PhysicalRowIR row;
        row.run = run.id;
        row.logical_record = record.logical_record;
        row.segment_index = segment.segment_index;
        row.token_begin = boundary.marker;
        row.token_end = end;
        row.native_origin = record.tokens[boundary.origin].size();
        const auto marker_text = visible_token(record.tokens[boundary.marker]);
        row.break_before = marker_break(marker_text);
        row.start = row.break_before == PhysicalBreakKind::soft_wrap
                        ? PhysicalRowStartKind::placeholder_wrap
                        : PhysicalRowStartKind::explicit_marker_slot;
        row.marker = MarkerSlotIR{
            record.logical_record,
            boundary.marker,
            record.encoded_tokens[boundary.marker].value,
            record.encoded_tokens[boundary.marker].width,
            boundary.marker < record.ir.tokens.size()
                ? record.ir.tokens[boundary.marker].byte_range
                : SourceByteRange{},
            marker_text,
        };
        row.visible_text = visible_slice(record, boundary.origin, end,
                                         row.native_origin);
        if (!row.visible_text.empty()) run.rows.push_back(std::move(row));
      }
      if (run.rows.empty()) continue;

      // A leading marker row in an otherwise control-free immediately
      // adjacent record continues the final display run of the prior record.
      // Any intervening typed control prevents this mechanical join.
      const auto adjacent_continuation =
          segment.kind == BookControlKind::text && segment.segment_index == 0 &&
          !layout.runs.empty() && !layout.runs.back().rows.empty() &&
          run_can_continue(records, layout.runs.back()) &&
          layout.runs.back().rows.back().logical_record + 1 ==
              record.logical_record;
      if (adjacent_continuation) {
        auto& previous = layout.runs.back();
        for (std::size_t index = 0; index < run.rows.size(); ++index) {
          auto& row = run.rows[index];
          row.run = previous.id;
          if (index == 0) {
            row.start = PhysicalRowStartKind::record_continuation;
            row.break_before = PhysicalBreakKind::soft_wrap;
            row.continues_previous_record = true;
          }
          previous.rows.push_back(std::move(row));
        }
      } else {
        layout.runs.push_back(std::move(run));
      }
    }
  }
  return layout;
}

bool verify_layout_ir(const std::vector<DecodedLogicalRecordSource>& records,
                      const LayoutIR& layout, std::string* error) {
  const auto fail = [&](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  DisplayRunId previous_run = 0;
  for (const auto& run : layout.runs) {
    if (run.id <= previous_run || run.rows.empty())
      return fail("display run IDs are not ordered or a run is empty");
    const PhysicalRowIR* previous_row = nullptr;
    for (const auto& row : run.rows) {
      const auto source = std::find_if(
          records.begin(), records.end(), [&](const auto& record) {
            return record.logical_record == row.logical_record;
          });
      if (source == records.end() || row.run != run.id ||
          row.token_begin >= row.token_end ||
          row.token_end > source->tokens.size() || row.visible_text.empty())
        return fail("physical row source range or ownership is invalid");
      if (row.marker &&
          (row.marker->logical_record != row.logical_record ||
           row.marker->token_index != row.token_begin ||
           row.marker->encoded_width != 1 ||
           (row.token_begin < source->ir.tokens.size() &&
            (row.marker->byte_range.begin !=
                 source->ir.tokens[row.token_begin].byte_range.begin ||
             row.marker->byte_range.end !=
                 source->ir.tokens[row.token_begin].byte_range.end))))
        return fail("physical row marker provenance is invalid");
      const auto* segment = source_segment(records, row);
      if (row.marker && segment != nullptr &&
          segment->kind == BookControlKind::title &&
          is_display_line_length_token(*source, row.marker->token_index + 1))
        return fail("title row marker is the word before a native line prefix");
      if (row.marker && segment != nullptr) {
        if (const auto* lines = record_display_lines(*source))
          for (const auto& line : *lines) {
            if (row.marker->token_index == line.prefix_token &&
                authoritative_native_line(records, *source, *segment, line) &&
                row.token_end != line.token_end)
              return fail("selected native row does not end at its display-line end");
            if (row.marker->token_index > line.prefix_token &&
                row.marker->token_index < line.token_end &&
                authoritative_native_line(records, *source, *segment, line) &&
                line.prefix_token + 1 < line.token_end &&
                all_space_words(source->tokens[line.prefix_token + 1]))
              return fail("physical row marker is inside a native display line");
          }
      }
      if (row.native_origin == 0)
        return fail("physical row has no native display origin");
      if (row.start == PhysicalRowStartKind::control_payload && row.marker)
        return fail("markerless control-payload row owns a marker");
      if ((row.start == PhysicalRowStartKind::explicit_marker_slot ||
           row.start == PhysicalRowStartKind::placeholder_wrap ||
           row.start == PhysicalRowStartKind::record_continuation) &&
          !row.marker)
        return fail("marker-started physical row has no marker provenance");
      if (previous_row != nullptr) {
        if (row.logical_record < previous_row->logical_record ||
            (row.logical_record == previous_row->logical_record &&
             row.token_begin < previous_row->token_end))
          return fail("physical rows overlap or are out of source order");
      }
      if (row.continues_previous_record &&
          (previous_row == nullptr ||
           row.start != PhysicalRowStartKind::record_continuation ||
           row.break_before != PhysicalBreakKind::soft_wrap ||
           previous_row->logical_record + 1 != row.logical_record ||
           row.segment_index != 0 ||
           source->control_segments.empty() ||
           source->control_segments.front().kind != BookControlKind::text ||
           !has_no_following_control(records, *previous_row) ||
           !run_origin_allows_continuation(records, run)))
        return fail("cross-record physical continuation is not adjacent");
      previous_row = &row;
    }
    previous_run = run.id;
  }
  if (error != nullptr) error->clear();
  return true;
}

std::string format_physical_row_ir(const PhysicalRowIR& row) {
  std::ostringstream out;
  out << "run=" << row.run << " record=" << row.logical_record
      << " segment=" << row.segment_index << " tokens=[" << row.token_begin
      << ',' << row.token_end << ") origin=" << row.native_origin;
  if (row.marker)
    out << " marker='" << row.marker->decoded_text << "' marker_value="
        << row.marker->encoded_value << " marker_width="
        << static_cast<unsigned>(row.marker->encoded_width)
        << " marker_bytes=[0x" << std::hex << row.marker->byte_range.begin
        << ",0x" << row.marker->byte_range.end << ")" << std::dec;
  out << " text='" << row.visible_text << "'";
  return out.str();
}

} // namespace geist::detail
