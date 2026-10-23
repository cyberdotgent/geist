// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/message_prose_rows.hpp"

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/font_span_ir.hpp"
#include "geist/detail/core/internal.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

using CellKey = std::tuple<std::uint32_t, std::size_t, std::size_t>;

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

const DecodedLogicalRecordSource* find_record(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::uint32_t logical_record) {
  const auto found = std::find_if(records.begin(), records.end(),
                                  [&](const auto& record) {
                                    return record.logical_record ==
                                           logical_record;
                                  });
  return found == records.end() ? nullptr : &*found;
}

// One decoded word `< 4` and nothing else: a spacing control without text.
bool control_only_spacing_token(const DecodedLogicalRecordSource& record,
                                std::size_t token) {
  return token < record.tokens.size() && record.tokens[token].size() == 1 &&
         record.tokens[token].front() < 4;
}

// Marker glyphs that only ever delimit rows, following the partition used by
// MessageMarkerDispositionIR: sentence punctuation and dictionary words in a
// non-origin-3 slot remain text, layout glyphs and `?` placeholder runs do not.
bool layout_glyph_marker(const MarkerSlotIR& marker) {
  const auto& text = marker.decoded_text;
  if (text.empty()) return true;
  if (std::all_of(text.begin(), text.end(),
                  [](const char ch) { return ch == '?'; }))
    return true;
  if (text.size() != 1) return false;
  return std::string("-<>/\"=()[{").find(text.front()) != std::string::npos;
}

// True when `closing` closes a `(`, `<`, `[` or `{` that `preceding` left
// open.
bool closes_open_delimiter(const std::string& preceding, const char closing) {
  char opener = '\0';
  switch (closing) {
    case ')': opener = '('; break;
    case '>': opener = '<'; break;
    case ']': opener = '['; break;
    case '}': opener = '{'; break;
    default: return false;
  }
  std::size_t depth = 0;
  for (const auto ch : preceding) {
    if (ch == opener)
      ++depth;
    else if (ch == closing && depth != 0)
      --depth;
  }
  return depth != 0;
}

std::size_t last_visible_token(const DecodedLogicalRecordSource& record,
                               std::size_t end) {
  auto last = end;
  while (last > 0) {
    --last;
    const auto& words = record.tokens[last];
    if (std::any_of(words.begin(), words.end(), [](const auto word) {
          return word >= 0x20 && word != ' ' && word <= 0xff;
        }))
      return last;
  }
  return end;
}

bool wide_space_token(const DecodedLogicalRecordSource& record,
                      std::size_t token) {
  return token < record.tokens.size() && record.tokens[token].size() >= 2 &&
         std::all_of(record.tokens[token].begin(), record.tokens[token].end(),
                     [](const auto word) { return word == ' '; });
}

// doc/boo-spec/markup.adoc "Repeated row-control signatures" signature B: a
// control-only spacing token that attaches the next token, where that next
// token is a fixed-row marker slot at the exact three-space origin, a wide
// padding run, or the record end. An attach control followed by ordinary
// visible text (`OS`+`/`+`2`, `Note`+`:`) is inter-word spacing, not a
// boundary.
using MarkerOriginLookup =
    std::function<std::optional<std::size_t>(std::size_t token)>;

bool hard_boundary_after_token(const DecodedLogicalRecordSource& record,
                               std::size_t token, std::size_t end,
                               const MarkerOriginLookup& marker_origin,
                               std::size_t* boundary_token) {
  if (!control_only_spacing_token(record, token)) return false;
  // Signature B begins with sentence punctuation (`resource` `.` control-only
  // `as` three spaces): a control-only token after an unterminated word
  // (`bridge` control-only `can` three spaces, `...21.5.1` at a record end)
  // attaches a wrapped display line of the same paragraph.
  if (token == 0) return false;
  const auto& previous = record.tokens[token - 1];
  const auto last_visible = std::find_if(
      previous.rbegin(), previous.rend(),
      [](const auto word) { return word >= 0x20 && word != ' '; });
  if (last_visible == previous.rend() ||
      (*last_visible != '.' && *last_visible != '!' && *last_visible != '?'))
    return false;
  auto next = token + 1;
  while (next < end && control_only_spacing_token(record, next)) ++next;
  if (next < end) {
    const auto origin = marker_origin(next);
    if (!(origin && *origin == 3) && !wide_space_token(record, next))
      return false;
  }
  if (boundary_token != nullptr) *boundary_token = token;
  return true;
}

MessageProseBoundaryTokenIR boundary_token_ir(
    const DecodedLogicalRecordSource& record, std::size_t token) {
  MessageProseBoundaryTokenIR result;
  result.logical_record = record.logical_record;
  result.token_index = token;
  result.spacing_control = record.tokens[token].front();
  if (token < record.ir.tokens.size())
    result.bytes = record.ir.tokens[token].byte_range;
  return result;
}

DocumentSourceSliceIR token_slice(const DecodedLogicalRecordSource& record,
                                  std::size_t segment_index,
                                  std::size_t token_begin,
                                  std::size_t token_end) {
  DocumentSourceSliceIR slice;
  slice.logical_record = record.logical_record;
  slice.segment_index = segment_index;
  slice.token_begin = token_begin;
  slice.token_end = token_end;
  if (token_begin < token_end && token_end <= record.ir.tokens.size()) {
    slice.byte_begin = record.ir.tokens[token_begin].byte_range.begin;
    slice.byte_end = record.ir.tokens[token_end - 1].byte_range.end;
  }
  return slice;
}

std::string collapse(std::string value) {
  return collapse_ascii_whitespace(trim_ascii(std::move(value)));
}

// The geometry of one drawn display row, in display columns: its first and
// past-the-last drawn cell, the width of the word its first drawn cell
// starts, whether the CFONT that styles the row opens it (the font's first
// span begins at that first drawn cell), and whether it is an example row
// (every drawn cell under an example or variable span, at least one of them
// example). Cells of control tokens and a margin revision bar are not drawn.
struct ProseRowGeometry {
  std::size_t first_column = 0;
  std::size_t end_column = 0;
  std::size_t first_word = 0;
  bool opened_by_font = false;
  bool example = false;
};

std::optional<ProseRowGeometry> prose_row_geometry(
    const DecodedLogicalRecordSource& record, const DisplayLineIR& line,
    const std::set<std::size_t>& control_tokens,
    const std::optional<FontControlSpansIR>& font) {
  const auto cells = display_line_cells(record, line);
  std::vector<bool> drawn(cells.size(), false);
  bool any = false;
  for (std::size_t column = 0; column < cells.size(); ++column) {
    const auto& cell = cells[column];
    if (cell.word <= ' ') continue;
    if (cell.token != static_cast<std::size_t>(-1) &&
        (control_tokens.count(cell.token) != 0 ||
         (column == 1 &&
          is_display_line_margin_revision_bar(record, cell.token))))
      continue;
    drawn[column] = true;
    any = true;
  }
  if (!any) return std::nullopt;
  ProseRowGeometry geometry;
  geometry.first_column = static_cast<std::size_t>(
      std::find(drawn.begin(), drawn.end(), true) - drawn.begin());
  for (auto column = geometry.first_column; column < drawn.size(); ++column)
    if (drawn[column]) geometry.end_column = column + 1;
  for (auto column = geometry.first_column;
       column < drawn.size() && drawn[column]; ++column)
    ++geometry.first_word;
  if (!font || font->spans.empty()) return geometry;
  geometry.opened_by_font = font->spans.front().column == geometry.first_column;
  bool example_span = false;
  bool covered = true;
  for (std::size_t column = 0; column < drawn.size(); ++column) {
    if (!drawn[column]) continue;
    const auto span = std::find_if(
        font->spans.begin(), font->spans.end(),
        [&](const FontSpanIR& candidate) {
          return candidate.column <= column &&
                 column - candidate.column < candidate.length;
        });
    if (span == font->spans.end() ||
        (!font_style_is_example(span->style) &&
         span->style != FontStyleIR::variable)) {
      covered = false;
      break;
    }
    example_span = example_span || font_style_is_example(span->style);
  }
  geometry.example = covered && example_span;
  return geometry;
}

// A boundary between two drawn rows of one paragraph is a line break, not a
// reflow join, when the CFONT of the later row opens it and either both rows
// are example rows (example text is never filled), or the earlier row ends
// with room for the later row's first word before `widest`, the end of the
// paragraph's widest row: a filling formatter would have set that word on
// the earlier row, so the source ended the row on purpose.
bool row_boundary_is_line_break(const ProseRowGeometry& before,
                                const ProseRowGeometry& after,
                                std::size_t widest) {
  if (!after.opened_by_font) return false;
  if (before.example && after.example) return true;
  return before.end_column + 1 + after.first_word <= widest;
}

// The source cell of one word of a projected piece; `token` is npos for a
// separator the projector inserted.
struct PieceSource {
  std::size_t token = static_cast<std::size_t>(-1);
  std::size_t word = 0;
};

struct PieceGlyph {
  std::size_t token = 0;
  std::size_t word = 0;
  std::size_t offset = 0;
};

// Replays `collapse(token_words_to_ascii(piece))` byte for byte, carrying
// each word's source cell, and hands back the offset of every glyph that
// survives. Declines when the replay does not reproduce `expected`.
std::optional<std::vector<PieceGlyph>> project_piece_glyphs(
    const TokenWords& piece, const std::vector<PieceSource>& sources,
    const std::string& expected) {
  if (sources.size() != piece.size()) return std::nullopt;
  const auto blank = [](const char ch) {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isspace(byte) != 0 || byte < 0x20;
  };
  const auto trimmed = [&](const char ch) { return blank(ch) || ch == '?'; };
  std::string raw;
  std::vector<std::size_t> owner;
  constexpr auto none = static_cast<std::size_t>(-1);
  for (std::size_t index = 0; index < piece.size(); ++index) {
    const auto bytes = token_words_to_ascii({piece[index]});
    for (const auto byte : bytes) {
      raw.push_back(byte);
      owner.push_back(sources[index].token == none ? none : index);
    }
  }
  const auto trim = [&](std::string& text, std::vector<std::size_t>& owners) {
    std::size_t begin = 0;
    auto end = text.size();
    while (end > begin && trimmed(text[end - 1])) --end;
    while (begin < end && trimmed(text[begin])) ++begin;
    text = text.substr(begin, end - begin);
    owners = std::vector<std::size_t>(owners.begin() + begin,
                                      owners.begin() + end);
  };
  trim(raw, owner);
  std::string out;
  std::vector<std::size_t> out_owner;
  bool in_space = false;
  for (std::size_t at = 0; at < raw.size(); ++at) {
    if (blank(raw[at])) {
      in_space = true;
      continue;
    }
    if (in_space && !out.empty()) {
      out.push_back(' ');
      out_owner.push_back(none);
    }
    out.push_back(raw[at]);
    out_owner.push_back(owner[at]);
    in_space = false;
  }
  trim(out, out_owner);
  if (out != expected) return std::nullopt;
  std::vector<PieceGlyph> glyphs;
  for (std::size_t at = 0; at < out.size(); ++at)
    if (out_owner[at] != none && (at == 0 || out_owner[at - 1] != out_owner[at]))
      glyphs.push_back({sources[out_owner[at]].token,
                        sources[out_owner[at]].word, at});
  return glyphs;
}

} // namespace

std::optional<MessageProseIntroductionIR> extract_message_prose_paragraphs_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const MessageProseEnvelopeIR& envelope, std::string* error) {
  const auto reject = [&](std::string message) {
    fail(error, std::move(message));
    return std::optional<MessageProseIntroductionIR>{};
  };
  const auto* begin_record = find_record(records, envelope.begin_record);
  const auto* catalog_record = find_record(records, envelope.catalog_record);
  if (begin_record == nullptr || catalog_record == nullptr ||
      envelope.catalog_segment >= catalog_record->control_segments.size() ||
      catalog_record->control_segments[envelope.catalog_segment]
          .source_tokens.empty())
    return reject("message prose envelope coordinates are invalid");
  const auto& catalog_segment =
      catalog_record->control_segments[envelope.catalog_segment];

  // Token span strictly between the envelope start and the catalog start.
  struct RecordSpan {
    const DecodedLogicalRecordSource* record = nullptr;
    std::size_t begin = 0;
    std::size_t end = 0;
  };
  std::vector<RecordSpan> spans;
  for (const auto& record : records) {
    if (record.logical_record < begin_record->logical_record ||
        record.logical_record > catalog_record->logical_record)
      continue;
    RecordSpan span{&record, 0, record.tokens.size()};
    if (&record == begin_record) span.begin = envelope.begin_token;
    if (&record == catalog_record)
      span.end = catalog_segment.source_tokens.front();
    if (span.begin > span.end)
      return reject("message prose title/catalog envelope is inverted");
    spans.push_back(span);
    for (const auto& segment : record.control_segments) {
      if (segment.source_tokens.empty()) continue;
      const auto first = segment.source_tokens.front();
      const auto last = segment.source_tokens.back();
      if (last < span.begin || first >= span.end) continue;
      const auto straddles_start =
          &record == begin_record && first < span.begin && last >= span.begin;
      if ((first < span.begin && !straddles_start) || last >= span.end)
        return reject("message prose segment straddles the envelope");
      if (segment.kind == BookControlKind::spacing) {
        // `c.sp` is a SCRIPT vertical-space directive; its whole display line
        // is the control (SC34-425 record 1476 line 10 is exactly
        // `c.sp 1 c`), so it draws nothing and its tokens are control text,
        // not prose. A spacing segment that owns visible content is not
        // explained here and stays fail-closed.
        for (const auto token : segment.source_tokens)
          for (std::size_t word = 0; word < record.tokens[token].size();
               ++word) {
            const auto owner = std::find_if(
                ownership.cells.begin(), ownership.cells.end(),
                [&](const auto& cell) {
                  return cell.logical_record == record.logical_record &&
                         cell.token_index == token && cell.word_index == word;
                });
            if (owner != ownership.cells.end() &&
                owner->disposition == SourceDisposition::visible_content)
              return reject(
                  "message prose spacing control carries visible text: " +
                  segment.opcode);
          }
        continue;
      }
      // A `SRETBL` that closes an embedded drawing may straddle the envelope
      // start the same way a title segment does: the flattened splitter gives
      // it the display lines that follow it, and those are prose again.
      // A `cselect` marks a hotspot of the display line after it; its
      // opcode and operands are control text and the flattened splitter
      // gives it the following rows as payload, which are prose. The
      // caller places the hotspot on those rows.
      // A segment the splitter cut at a display line's length byte, because
      // the byte's dictionary word spells a control, is that row's text:
      // SC31-711 4.3.5 record 151 line 13 opens at 0x146F1 with `2D` (45),
      // spelled `cbacklevel`, and hosted 4.3.5 (DT 19941010174546) serves
      // `   the 1.3.6.1.4.1.2.6.21.6.1 enterprise ID, ...` in the paragraph.
      const auto opcode_is_line_length =
          is_display_line_length_token(record, first);
      if (!opcode_is_line_length &&
          segment.kind != BookControlKind::text &&
          segment.kind != BookControlKind::font &&
          segment.kind != BookControlKind::select &&
          segment.kind != BookControlKind::structural &&
          !(straddles_start && (segment.kind == BookControlKind::title ||
                                segment.kind == BookControlKind::table_end)))
        return reject("message prose envelope contains a non-prose control: " +
                      segment.opcode);
    }
  }
  if (spans.empty()) return reject("message prose envelope has no records");

  std::map<CellKey, const OwnedSourceCellIR*> cells;
  for (const auto& cell : ownership.cells)
    cells.emplace(CellKey{cell.logical_record, cell.token_index,
                          cell.word_index},
                  &cell);

  // Rows inside the envelope, keyed by record and token.
  struct RowRef {
    std::size_t run_index = 0;
    std::size_t row_index = 0;
    const PhysicalRowIR* row = nullptr;
  };
  std::map<std::pair<std::uint32_t, std::size_t>, RowRef> rows_by_start;
  std::map<std::pair<std::uint32_t, std::size_t>, RowRef> row_by_token;
  std::optional<std::size_t> first_run;
  std::optional<std::size_t> end_run;
  for (std::size_t run_index = 0; run_index < layout.runs.size(); ++run_index) {
    const auto& run = layout.runs[run_index];
    std::size_t inside = 0;
    std::size_t leading_outside = 0;
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto& row = run.rows[row_index];
      const auto span = std::find_if(spans.begin(), spans.end(),
                                     [&](const auto& candidate) {
                                       return candidate.record
                                                  ->logical_record ==
                                              row.logical_record;
                                     });
      if (span == spans.end() || row.token_begin < span->begin ||
          row.token_end > span->end) {
        // The run that carries the envelope start may keep its leading rows
        // (the topic title) outside the envelope.
        if (inside == 0 && span != spans.end() &&
            span->record == begin_record && row.token_end <= span->begin) {
          ++leading_outside;
          continue;
        }
        // The title row itself may continue with prose past the envelope
        // start; its tokens from the start onwards belong to the envelope.
        if (inside == 0 && span != spans.end() &&
            span->record == begin_record && row.token_begin < span->begin &&
            row.token_end > span->begin && row.token_end <= span->end) {
          ++inside;
          RowRef ref{run_index, row_index, &row};
          rows_by_start.emplace(std::pair{row.logical_record, span->begin},
                                ref);
          for (auto token = span->begin; token < row.token_end; ++token)
            row_by_token.emplace(std::pair{row.logical_record, token}, ref);
        }
        continue;
      }
      ++inside;
      RowRef ref{run_index, row_index, &row};
      rows_by_start.emplace(std::pair{row.logical_record, row.token_begin},
                            ref);
      for (auto token = row.token_begin; token < row.token_end; ++token)
        row_by_token.emplace(std::pair{row.logical_record, token}, ref);
    }
    if (inside == 0) continue;
    if (inside + leading_outside != run.rows.size())
      return reject("message prose display run straddles the envelope");
    // As above: the display run a `SRETBL` opens carries the prose that
    // follows the drawing it closed, not part of the drawing.
    if (run.control_kind != BookControlKind::text &&
        run.control_kind != BookControlKind::font &&
        run.control_kind != BookControlKind::select &&
        run.control_kind != BookControlKind::title &&
        run.control_kind != BookControlKind::table_end &&
        run.control_kind != BookControlKind::structural)
      return reject("message prose envelope contains a non-prose run");
    if (!first_run) first_run = run_index;
    if (end_run && *end_run != run_index)
      return reject("message prose runs are not contiguous");
    end_run = run_index + 1;
  }
  if (!first_run) return reject("message prose envelope has no display rows");

  MessageProseIntroductionIR result;
  result.first_run_index = *first_run;
  result.end_run_index = *end_run;
  result.first_catalog_segment =
      token_slice(*catalog_record, catalog_segment.segment_index,
                  catalog_segment.source_tokens.front(),
                  catalog_segment.source_tokens.back() + 1);

  MessageProseParagraphIR current;
  bool open = false;
  std::optional<DisplayRunId> current_run;
  std::vector<MessageProseBoundaryTokenIR> pending_boundary;
  // The drawn display rows of the open paragraph, in order, and the row
  // boundaries inside it that may be line breaks (see
  // `MessageProseParagraphIR::line_breaks`). The paragraph's widest row is
  // only known once it closes, so the breaks are decided in `finish`.
  std::vector<ProseRowGeometry> paragraph_rows;
  struct BreakCandidate {
    std::size_t offset = 0;
    ProseRowGeometry before;
    ProseRowGeometry after;
  };
  std::vector<BreakCandidate> break_candidates;
  const auto finish = [&]() {
    if (open && !collapse(current.text).empty()) {
      // Pieces are collapsed and joined by one space, so collapsing the
      // whole text changes nothing; if it ever did, the recorded offsets
      // would no longer name separators and no break is kept.
      const auto collapsed = collapse(current.text);
      if (collapsed == current.text) {
        std::size_t widest = 0;
        for (const auto& row : paragraph_rows)
          widest = std::max(widest, row.end_column);
        // A row that contributed no text leaves its candidate at the same
        // offset as the next row's; the later row is the one that follows.
        std::map<std::size_t, bool> decided;
        for (const auto& candidate : break_candidates)
          if (candidate.offset > 0 && candidate.offset < current.text.size() &&
              current.text[candidate.offset] == ' ')
            decided[candidate.offset] = row_boundary_is_line_break(
                candidate.before, candidate.after, widest);
        for (const auto& [offset, line_break] : decided)
          if (line_break) current.line_breaks.push_back(offset);
      } else {
        current.glyphs.clear();
        current.glyphs_complete = false;
      }
      current.text = collapsed;
      result.paragraphs.push_back(std::move(current));
    }
    break_candidates.clear();
    current = {};
    open = false;
  };
  const auto begin_paragraph = [&](bool run_start) {
    finish();
    // A paragraph opens on the row last seen; earlier rows are not its own.
    if (paragraph_rows.size() > 1)
      paragraph_rows.erase(paragraph_rows.begin(), paragraph_rows.end() - 1);
    open = true;
    current.opened_by_run_start = run_start;
    current.opening_boundary = std::move(pending_boundary);
    pending_boundary.clear();
  };

  const RecordSpan* previous_span = nullptr;
  for (const auto& span : spans) {
    const auto& record = *span.record;
    // Compatibility continuation evidence: the first envelope row may
    // continue the preceding paragraph's run. Native framing below can
    // independently prove a prefix before a different run.
    std::optional<std::size_t> first_row_token;
    for (auto token = span.begin; token < span.end; ++token) {
      const auto found = rows_by_start.find({record.logical_record, token});
      if (found != rows_by_start.end()) {
        first_row_token = token;
        break;
      }
    }
    const auto record_continues_run =
        first_row_token && open && current_run &&
        layout.runs[rows_by_start.at({record.logical_record, *first_row_token})
                        .run_index]
                .id == *current_run;

    std::size_t piece_begin_token = span.begin;
    TokenWords piece;
    std::vector<PieceSource> piece_sources;
    const auto push_space = [&] {
      piece.push_back(' ');
      piece_sources.push_back({});
    };
    const auto flush_piece = [&](std::size_t token_end,
                                 std::size_t segment_index) {
      const auto text = collapse(token_words_to_ascii(piece));
      const auto glyphs = project_piece_glyphs(piece, piece_sources, text);
      piece.clear();
      piece_sources.clear();
      if (!text.empty()) {
        if (!open) begin_paragraph(false);
        if (!current.text.empty()) current.text.push_back(' ');
        const auto base = current.text.size();
        current.text += text;
        if (!glyphs) {
          current.glyphs.clear();
          current.glyphs_complete = false;
        } else if (current.glyphs_complete) {
          for (const auto& glyph : *glyphs)
            current.glyphs.push_back({record.logical_record, glyph.token,
                                      glyph.word, base + glyph.offset});
        }
        current.source_slices.push_back(
            token_slice(record, segment_index, piece_begin_token, token_end));
      }
      piece_begin_token = token_end;
    };

    // Opcode/operand tokens of controls inside the envelope (index entries,
    // the deferred title control) are control text, never prose.
    std::set<std::size_t> control_tokens;
    for (const auto& segment : record.control_segments) {
      if (segment.kind == BookControlKind::spacing) {
        // A `c.sp` line draws nothing: opcode and operand alike are control
        // text, so the whole segment is withheld from the prose pieces.
        for (const auto token : segment.source_tokens)
          control_tokens.insert(token);
        continue;
      }
      // A segment opened by a row's length byte has no opcode or operand of
      // its own; its whole span is that row (see the envelope check above).
      if (segment.kind == BookControlKind::text ||
          segment.payload_range.begin <= segment.complete.begin ||
          (!segment.source_tokens.empty() &&
           is_display_line_length_token(record,
                                        segment.source_tokens.front())))
        continue;
      const auto words = decoded_byte_range_to_word_range(
          record.assembled,
          {segment.complete.begin, segment.payload_range.begin});
      for (const auto token : source_tokens_intersecting_output(
               record.assembled, words.begin, words.end))
        control_tokens.insert(token);
    }
    if (const auto* lines = record_display_lines(record))
      for (const auto& line : *lines)
        if (is_subject_index_display_line(record, line))
          for (auto token = line.prefix_token + 1; token < line.token_end; ++token)
            control_tokens.insert(token);

    // A record may start with native prose before its first compatibility
    // layout row. That later row's font run says nothing about the prefix.
    // Admit only whole, indented, source-positioned rows in consecutive
    // framed records, continuing an open paragraph, following a proven blank
    // row, or opening the target of a terminal font instruction. This is
    // additional source evidence, never
    // blanket forgiveness for opaque cells.
    std::set<std::size_t> native_prefix_tokens;
    const auto* native_lines = record_display_lines(record);
    const auto* preceding_lines = previous_span == nullptr ? nullptr :
        record_display_lines(*previous_span->record);
    const auto after_blank = preceding_lines != nullptr &&
        !preceding_lines->empty() &&
        collapse(display_line_text(*previous_span->record,
                                   preceding_lines->back())).empty();
    std::optional<DocumentSourceSliceIR> deferred_font;
    if (previous_span != nullptr && preceding_lines != nullptr &&
        !preceding_lines->empty()) {
      const auto& prior = *previous_span->record;
      const auto& last = preceding_lines->back();
      for (const auto& segment : prior.control_segments)
        if (segment.kind == BookControlKind::font &&
            !segment.source_tokens.empty() &&
            segment.source_tokens.front() == last.prefix_token + 1 &&
            segment.source_tokens.back() < last.token_end &&
            last.prefix_token >= previous_span->begin &&
            last.token_end == prior.tokens.size()) {
          const auto font = decode_font_control_spans(prior, segment);
          if (font && !font->spans.empty())
            deferred_font = token_slice(prior, segment.segment_index,
                                       last.prefix_token + 1, last.token_end);
        }
    }
    if ((open || after_blank || deferred_font) && previous_span != nullptr && native_lines != nullptr &&
        preceding_lines != nullptr && !preceding_lines->empty() &&
        previous_span->record->logical_record + 1 == record.logical_record &&
        previous_span->end == previous_span->record->tokens.size()) {
      const auto prefix_end = first_row_token.value_or(span.end);
      for (const auto& line : *native_lines) {
        if (line.prefix_token < span.begin || line.token_end > prefix_end)
          break;
        if (is_subject_index_display_line(record, line)) continue;
        const auto text = display_line_text(record, line);
        if (collapse(text).empty()) continue;
        if (text.front() != ' ') break;
        bool proven = true;
        for (const auto& conflict : ownership.run_conflicts)
          if (conflict.logical_record == record.logical_record &&
              conflict.token_index >= line.prefix_token &&
              conflict.token_index < line.token_end)
            proven = false;
        for (auto token = line.prefix_token + 1; token < line.token_end; ++token) {
          if (control_tokens.count(token) != 0) { proven = false; break; }
          // A native list bullet is the row's marker, decided from the
          // line's own cells; it draws no prose and the caller reads the
          // list from the framing (SC34-425 2.4.10.1 record 1490 line 0 is
          // the length byte, a three-blank run, `U+2666` (`03` at
          // `0x15C3EC`), a two-blank run and `When searching ...`).
          if (is_display_line_list_bullet(record, token)) continue;
          for (std::size_t word = 0; word < record.tokens[token].size(); ++word) {
            const auto cell = cells.find({record.logical_record, token, word});
            if (cell == cells.end() ||
                cell->second->field_role != SourceFieldRole::positioned ||
                (cell->second->disposition != SourceDisposition::opaque &&
                 !(word == 0 && record.tokens[token][word] < 4 &&
                   cell->second->disposition == SourceDisposition::control_operand)) ||
                record.tokens[token][word] > 0xff) {
              proven = false;
              break;
            }
          }
        }
        if (!proven) break;
        for (auto token = line.prefix_token + 1; token < line.token_end; ++token)
          native_prefix_tokens.insert(token);
      }
    }
    if (deferred_font && !native_prefix_tokens.empty() &&
        (native_lines == nullptr || native_lines->empty() ||
         native_prefix_tokens.count(native_lines->front().prefix_token + 1) == 0))
      return reject("deferred message prose font has no first native target row");
    std::size_t current_segment = 0;
    bool segment_row_seen = false;
    for (auto token = span.begin; token < span.end; ++token) {
      for (const auto& segment : record.control_segments)
        if (!segment.source_tokens.empty() &&
            segment.source_tokens.front() <= token &&
            token <= segment.source_tokens.back() &&
            segment.segment_index != current_segment) {
          current_segment = segment.segment_index;
          segment_row_seen = false;
        }
      if (row_by_token.count({record.logical_record, token}) != 0)
        segment_row_seen = true;
      if (control_tokens.count(token) != 0 &&
          !is_display_line_length_token(record, token)) {
        const auto row_start =
            rows_by_start.find({record.logical_record, token});
        if (row_start != rows_by_start.end())
          return reject("message prose control token starts a physical row");
        flush_piece(token, current_segment);
        piece_begin_token = token + 1;
        continue;
      }
      const MarkerOriginLookup marker_origin =
          [&](std::size_t candidate) -> std::optional<std::size_t> {
        const auto found =
            rows_by_start.find({record.logical_record, candidate});
        if (found == rows_by_start.end() || !found->second.row->marker ||
            found->second.row->marker->token_index != candidate)
          return std::nullopt;
        return found->second.row->native_origin;
      };
      std::size_t boundary = 0;
      if (hard_boundary_after_token(record, token, span.end, marker_origin,
                                    &boundary)) {
        flush_piece(token, current_segment);
        pending_boundary.push_back(boundary_token_ir(record, token));
        finish();
        piece_begin_token = token + 1;
        continue;
      }
      const auto row_start = rows_by_start.find({record.logical_record, token});
      if (row_start != rows_by_start.end()) {
        const auto& ref = row_start->second;
        const auto run_id = layout.runs[ref.run_index].id;
        if (!current_run || *current_run != run_id) {
          // A new display run opens a paragraph when it is the first run or
          // a CFONT run whose first span starts the display line at the
          // three-space origin (`cfont 3 5 2 Note:`). A run that starts
          // mid-line (`cfont 25 3 C` inside a sentence, a CFONT without
          // spans, a record-continuation text run) wraps the open paragraph.
          const auto& run = layout.runs[ref.run_index];
          // Native prefix text still pending in front of this row opened
          // the paragraph the row continues: no empty display line stands
          // between them. SC34-425 2.4.10.1 record 1490 line 0
          // (`   °   When searching ... no more`) is unrowed and line 1
          // (`       tokens on that line, ...`) opens LayoutIR's first row;
          // hosted DT 19921112160049 serves both lines in one `<p>`.
          if (!open && !collapse(token_words_to_ascii(piece)).empty())
            flush_piece(token, current_segment);
          bool starts_line = !open;
          if (!starts_line && run.control_kind == BookControlKind::font) {
            const auto* row_record = find_record(records, ref.row->logical_record);
            if (row_record != nullptr &&
                ref.row->segment_index < row_record->control_segments.size()) {
              const auto spans = decode_font_control_spans(
                  *row_record,
                  row_record->control_segments[ref.row->segment_index]);
              starts_line = spans && !spans->spans.empty() &&
                            spans->spans.front().column == 3;
            }
          }
          if (starts_line) {
            flush_piece(token, current_segment);
            begin_paragraph(true);
          }
          current_run = run_id;
        } else if (!open) {
          begin_paragraph(false);
        }
        current.source_rows.push_back({run_id, ref.row_index});
      }
      if (is_display_line_length_token(record, token)) {
        // Process its physical-row boundary above, but never emit the native
        // length byte's dictionary spelling, including for an unrowed prefix.
        flush_piece(token, current_segment);
        piece_begin_token = token + 1;
        const auto* line = display_line_of_token(record, token);
        if (deferred_font && native_lines != nullptr && !native_lines->empty() &&
            token == native_lines->front().prefix_token &&
            native_prefix_tokens.count(token + 1) != 0) {
          begin_paragraph(true);
          current.leading_font_source = deferred_font;
        }
        if (line != nullptr && collapse(display_line_text(record, *line)).empty()) {
          finish();
          paragraph_rows.clear();
        } else if (line != nullptr && native_lines != nullptr &&
                   !native_lines->empty() && line >= &native_lines->front() &&
                   line <= &native_lines->back()) {
          const auto line_index =
              static_cast<std::size_t>(line - &native_lines->front());
          // The CFONT that styles this row: the control alone on the row
          // above it, or the one the previous record ended on.
          std::optional<FontControlSpansIR> font;
          if (line_index > 0) {
            const auto& above = (*native_lines)[line_index - 1];
            bool control_row = above.prefix_token + 1 < above.token_end;
            for (auto token = above.prefix_token + 1;
                 control_row && token < above.token_end; ++token)
              control_row = control_tokens.count(token) != 0;
            for (const auto& segment : record.control_segments)
              if (control_row && segment.kind == BookControlKind::font &&
                  !segment.source_tokens.empty() &&
                  segment.source_tokens.front() == above.prefix_token + 1)
                font = decode_font_control_spans(record, segment);
          } else if (deferred_font) {
            if (const auto* font_record =
                    find_record(records, deferred_font->logical_record);
                font_record != nullptr &&
                deferred_font->segment_index <
                    font_record->control_segments.size())
              font = decode_font_control_spans(
                  *font_record,
                  font_record->control_segments[deferred_font->segment_index]);
          }
          if (const auto geometry =
                  prose_row_geometry(record, *line, control_tokens, font)) {
            if (open && !current.text.empty() && !paragraph_rows.empty()) {
              break_candidates.push_back(
                  {current.text.size(), paragraph_rows.back(), *geometry});
              paragraph_rows.push_back(*geometry);
            } else {
              paragraph_rows.assign(1, *geometry);
            }
          }
        }
        continue;
      }
      const auto owner = row_by_token.find({record.logical_record, token});
      const auto* row =
          owner == row_by_token.end() ? nullptr : owner->second.row;
      const auto& words = record.tokens[token];
      // A row's marker slot that the record's own framing calls a display
      // line's length byte is structure, whatever word the dictionary spells
      // for it (`as`, `*`, `and`). The origin width and the glyph alphabet
      // are the fallbacks for a record whose payload does not tile into
      // display lines and whose framing therefore has no answer.
      const auto structural_marker =
          row != nullptr && row->marker && row->marker->token_index == token &&
          (row->native_origin == 3 || layout_glyph_marker(*row->marker) ||
           (record_framing_is_decided(record.ir) &&
            is_display_line_length_token(record, token)));
      // A lone delimiter attached by a spacing control as the very last
      // visible token before the catalog closes the flattened segment; it is
      // not prose (compare MessageMarkerDispositionIR::closing_delimiter_bridge).
      // Only the catalog's own record ends the envelope that way: at the end
      // of an earlier record the attached glyph is the sentence's own.
      // SC34-425 2.4.27 record 1627 ends on line 26 ` |     invoked:`
      // (`invoked`, attach `00`, `:` = `27` at `0x1673c0`) and hosted DT
      // 19921112160049 serves `invoked:`.
      const auto closing_delimiter =
          &record == catalog_record &&
          token > span.begin && control_only_spacing_token(record, token - 1) &&
          words.size() == 1 && words.front() < 0x80 &&
          std::isalnum(static_cast<unsigned char>(words.front())) == 0 &&
          words.front() != ' ' && last_visible_token(record, span.end) == token;
      // A lone layout glyph that closes its decoded segment, or that a
      // padding run follows, is the marker slot of a display line whose
      // origin the next control swallowed (`each of /` before a CFONT).
      //
      // Decided framing overrules the padding evidence in two cases. A
      // glyph followed on its own display line by more drawn cells is a
      // cell of that line, not a slot in front of the next one: SC34-425
      // 2.4.15.1 record 1559 line 18 is ` |   001201-` + a 34-blank run +
      // `BADCPY1 '.'`, served as `<samp>001201-</samp>`. And a glyph that
      // closes a delimiter its paragraph opened is the sentence's own:
      // record 1557 line 17 ends `(treated as a comment line)`, served
      // with its `)`, where the following "padding" is line 18's length
      // byte.
      const auto segment_terminal_glyph = [&] {
        if (words.size() != 1 || words.front() >= 0x80 ||
            std::string("-<>/\"=()[{").find(
                static_cast<char>(words.front())) == std::string::npos)
          return false;
        if (record_framing_is_decided(record.ir) &&
            !is_display_line_length_token(record, token)) {
          if (const auto* line = display_line_of_token(record, token)) {
            for (auto later = token + 1; later < line->token_end; ++later)
              for (const auto word : record.tokens[later])
                if (word > ' ' && word <= 0xff) return false;
            if (closes_open_delimiter(
                    current.text + " " +
                        collapse(token_words_to_ascii(piece)),
                    static_cast<char>(words.front())))
              return false;
          }
        }
        const auto next = token + 1;
        return next >= span.end || wide_space_token(record, next) ||
               control_tokens.count(next) != 0;
      }();
      // The decoder's control boundary before the catalog start is one
      // unowned non-alphanumeric word ending the envelope's last token.
      const auto boundary_glyph_word = [&](std::size_t word) {
        const auto owner = cells.find({record.logical_record, token, word});
        if (&record != catalog_record || word + 1 != words.size() ||
            words[word] >= 0x80 || words[word] == ' ' ||
            std::isalnum(static_cast<unsigned char>(words[word])) != 0 ||
            (owner != cells.end() &&
             owner->second->disposition != SourceDisposition::opaque))
          return false;
        for (auto later = token + 1; later < span.end; ++later) {
          const auto& later_words = record.tokens[later];
          for (std::size_t index = 0; index < later_words.size(); ++index) {
            const auto later_value = later_words[index];
            if (later_value < 0x20 || later_value == ' ' ||
                later_value == '?' || later_value > 0xff)
              continue;
            const auto later_cell =
                cells.find({record.logical_record, later, index});
            if (later_cell == cells.end() ||
                later_cell->second->disposition !=
                    SourceDisposition::control_operand)
              return false;
          }
        }
        return true;
      };
      // A revision bar in the margin of its display line is the reader's
      // revision margin, not prose (doc/boo-spec/markup.adoc,
      // `markup_trap_revision_margin`). SC34-425 2.4.21 record 1601 line 15 is
      // ` ` (token 71), `|` (token 72) and `Purpose` under `cfont 3 7 K`, the
      // span counting from column 0 of the margin. A span of the row's font
      // (its own run's, or the one deferred from the previous record) that
      // reaches column 1 styles the bar as drawn text, and so does a font
      // that does not decode: the bar then stays and the callers' column
      // checks decide.
      const auto font_claims_bar = [&](const std::uint32_t logical_record,
                                       const std::size_t segment_index) {
        const auto* font_record = find_record(records, logical_record);
        if (font_record == nullptr ||
            segment_index >= font_record->control_segments.size())
          return true;
        const auto font = decode_font_control_spans(
            *font_record, font_record->control_segments[segment_index]);
        if (!font) return true;
        return std::any_of(font->spans.begin(), font->spans.end(),
                           [](const auto& span) {
                             return span.length != 0 && span.column <= 1 &&
                                    span.column + span.length > 1;
                           });
      };
      const auto margin_bar = [&] {
        if (!is_display_line_margin_revision_bar(record, token)) return false;
        const auto* line = display_line_of_token(record, token);
        // The deferred font of the previous record's last line styles this
        // record's first native line (SC34-425 2.4.11.1 record 1500 line 25
        // `cfont 3 5 2 9 5 2` over record 1501 line 0 ` | Total Lines`).
        if (deferred_font && native_lines != nullptr &&
            !native_lines->empty() && line != nullptr &&
            line->prefix_token == native_lines->front().prefix_token &&
            font_claims_bar(deferred_font->logical_record,
                            deferred_font->segment_index))
          return false;
        // A font run's spans are its CFONT's, which the run's first row
        // carries; a later row of the run may sit in the next record's
        // continuation segment (SC34-425 2.4.27 record 1626 segment 0).
        if (owner != row_by_token.end()) {
          const auto& run = layout.runs[owner->second.run_index];
          if (run.control_kind == BookControlKind::font &&
              (run.rows.empty() ||
               font_claims_bar(run.rows.front().logical_record,
                               run.rows.front().segment_index)))
            return false;
        }
        return true;
      }();
      // A segment the splitter opened at a row's length byte (its word
      // spells a control) is that row and the indented rows after it,
      // which LayoutIR leaves unrowed: SC31-711 4.3.5 record 151 lines
      // 13-15, from `2D` (45, spelled `cbacklevel`) at 0x146F1, are
      // `   the 1.3.6.1.4.1.2.6.21.6.1 enterprise ID, ...` through
      // `   to AIX NetView/6000.`, and hosted 4.3.5 (DT 19941010174546)
      // serves them in the introduction paragraph.
      const auto length_opened_row = [&] {
        if (!record_framing_is_decided(record.ir)) return false;
        const auto* line = display_line_of_token(record, token);
        const auto owner_segment = std::find_if(
            record.control_segments.begin(), record.control_segments.end(),
            [&](const auto& candidate) {
              return candidate.segment_index == current_segment;
            });
        if (line == nullptr ||
            owner_segment == record.control_segments.end() ||
            owner_segment->source_tokens.empty() ||
            !is_display_line_length_token(
                record, owner_segment->source_tokens.front()) ||
            line->prefix_token < owner_segment->source_tokens.front())
          return false;
        const auto text = display_line_text(record, *line);
        return !text.empty() && text.front() == ' ';
      }();
      for (std::size_t word = 0; word < words.size(); ++word) {
        const auto value = words[word];
        // Spacing controls attach tokens; they never contribute a glyph or
        // a separator of their own.
        if (value < 0x20) continue;
        if (margin_bar) {
          push_space();
          continue;
        }
        const auto cell = cells.find({record.logical_record, token, word});
        const auto disposition = cell == cells.end()
                                     ? SourceDisposition::opaque
                                     : cell->second->disposition;
        // Decoder placeholder words (control boundaries, `?` runs) sit
        // above the code page and never carry prose.
        // The splitter's operand of a length-opened segment is the row's
        // first drawn word (`the` after `cbacklevel` above).
        if ((disposition == SourceDisposition::control_operand &&
             !length_opened_row) ||
            structural_marker || closing_delimiter || segment_terminal_glyph ||
            value > 0xff || boundary_glyph_word(word)) {
          push_space();
          continue;
        }
        if (disposition == SourceDisposition::opaque && value != ' ') {
          // Decided framing states which display line a cell is drawn on. A
          // destination line -- a structural `SR<id>` control alone on its
          // display line -- draws nothing, and the indented row after it is
          // drawn text of the destination's payload, never control text:
          // SC34-425 2.4.32 record 1641 line 33 is `SRSPTTPRE` alone and
          // line 34 is `   input list.`, which LayoutIR leaves unrowed and
          // hosted DT 19921112160049 serves as
          // `<a name="SPTTPRE">   input list.</a>`. Other controls keep
          // their payload's unrowed cells as control text: a SCRIPT line
          // such as `c.cp` (SC34-425 2.4.24 record 1614 line 25) rides in a
          // CFONT payload and hosted serves nothing for it.
          const auto payload_row = [&] {
            if (!record_framing_is_decided(record.ir)) return false;
            const auto* line = display_line_of_token(record, token);
            const auto owner_segment = std::find_if(
                record.control_segments.begin(), record.control_segments.end(),
                [&](const auto& candidate) {
                  return candidate.segment_index == current_segment;
                });
            if (line == nullptr ||
                owner_segment == record.control_segments.end() ||
                owner_segment->source_tokens.empty() ||
                owner_segment->kind != BookControlKind::structural ||
                owner_segment->malformed || owner_segment->display_text ||
                owner_segment->opcode.size() <= 2 ||
                !ascii_starts_with_case_insensitive(owner_segment->opcode,
                                                    "SR"))
              return false;
            const auto* control_line = display_line_of_token(
                record, owner_segment->source_tokens.front());
            if (control_line == nullptr ||
                line->prefix_token <= control_line->prefix_token ||
                owner_segment->source_tokens.front() !=
                    control_line->prefix_token + 1 ||
                collapse(display_line_text(record, *control_line)) !=
                    owner_segment->opcode)
              return false;
            const auto text = display_line_text(record, *line);
            return !text.empty() && text.front() == ' ';
          }();
          const auto admitted =
              (record_continues_run && open && token < *first_row_token) ||
              native_prefix_tokens.count(token) != 0 || payload_row ||
              length_opened_row;
          // Unowned printable cells of a mid-record segment before its first
          // physical row (or of a rowless segment) are control text that the
          // decoder left unsegmented (index terms), not prose. Unowned cells
          // after a row of the same segment are the gap LayoutIR leaves
          // between two rows (a sentence stop before wide padding): prose.
          const auto rowless =
              row_by_token.count({record.logical_record, token}) == 0;
          if (!admitted && current_segment != 0 && rowless &&
              !segment_row_seen) {
            push_space();
            continue;
          }
          const auto row_gap = rowless && segment_row_seen;
          if (!admitted && !row_gap)
            return reject("message prose envelope has an unowned source cell "
                          "at LR" + std::to_string(record.logical_record) +
                          " token " + std::to_string(token) + " word " +
                          std::to_string(word) + " value " +
                          std::to_string(value));
        }
        piece.push_back(value);
        piece_sources.push_back({token, word});
      }
      // Inter-token spacing is decoder evidence: the assembler records an
      // inserted space at the end of a token's output span.
      const auto& assembled = record.assembled;
      if (token < assembled.tokens.size()) {
        const auto& output = assembled.tokens[token];
        const auto trailing_space =
            output.output_end > output.output_begin &&
            output.output_end <= assembled.sources.size() &&
            assembled.sources[output.output_end - 1].kind ==
                LogicalWordSourceKind::inserted_space;
        if (trailing_space) push_space();
      }
    }
    flush_piece(span.end, current_segment);
    previous_span = &span;
  }
  finish();
  if (result.paragraphs.empty())
    return reject("message prose envelope projected no paragraphs");
  return result;
}

} // namespace geist::detail
