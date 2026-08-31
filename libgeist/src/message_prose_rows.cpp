#include "geist/detail/message_prose_rows.hpp"

#include "geist/detail/display_lines.hpp"
#include "geist/detail/font_span_ir.hpp"
#include "geist/detail/internal.hpp"

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
      if (segment.kind != BookControlKind::text &&
          segment.kind != BookControlKind::font &&
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
  const auto finish = [&]() {
    if (open && !collapse(current.text).empty()) {
      current.text = collapse(current.text);
      result.paragraphs.push_back(std::move(current));
    }
    current = {};
    open = false;
  };
  const auto begin_paragraph = [&](bool run_start) {
    finish();
    open = true;
    current.opened_by_run_start = run_start;
    current.opening_boundary = std::move(pending_boundary);
    pending_boundary.clear();
  };

  for (const auto& span : spans) {
    const auto& record = *span.record;
    // Continuation prefix admission: unowned printable cells before the
    // record's first envelope row are admitted only while the paragraph of
    // the previous record is still open and that row continues its run.
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
    const auto flush_piece = [&](std::size_t token_end,
                                 std::size_t segment_index) {
      const auto text = collapse(token_words_to_ascii(piece));
      piece.clear();
      if (!text.empty()) {
        if (!open) begin_paragraph(false);
        if (!current.text.empty()) current.text.push_back(' ');
        current.text += text;
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
      if (segment.kind == BookControlKind::text ||
          segment.payload_range.begin <= segment.complete.begin)
        continue;
      const auto words = decoded_byte_range_to_word_range(
          record.assembled,
          {segment.complete.begin, segment.payload_range.begin});
      for (const auto token : source_tokens_intersecting_output(
               record.assembled, words.begin, words.end))
        control_tokens.insert(token);
    }
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
      if (control_tokens.count(token) != 0) {
        const auto row_start =
            rows_by_start.find({record.logical_record, token});
        if (row_start != rows_by_start.end())
          return reject("message prose control token starts a physical row");
        piece.push_back(' ');
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
      const auto closing_delimiter =
          token > span.begin && control_only_spacing_token(record, token - 1) &&
          words.size() == 1 && words.front() < 0x80 &&
          std::isalnum(static_cast<unsigned char>(words.front())) == 0 &&
          words.front() != ' ' && last_visible_token(record, span.end) == token;
      // A lone layout glyph that closes its decoded segment, or that a
      // padding run follows, is the marker slot of a display line whose
      // origin the next control swallowed (`each of /` before a CFONT).
      const auto segment_terminal_glyph = [&] {
        if (words.size() != 1 || words.front() >= 0x80 ||
            std::string("-<>/\"=()[{").find(
                static_cast<char>(words.front())) == std::string::npos)
          return false;
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
      for (std::size_t word = 0; word < words.size(); ++word) {
        const auto value = words[word];
        // Spacing controls attach tokens; they never contribute a glyph or
        // a separator of their own.
        if (value < 0x20) continue;
        const auto cell = cells.find({record.logical_record, token, word});
        const auto disposition = cell == cells.end()
                                     ? SourceDisposition::opaque
                                     : cell->second->disposition;
        // Decoder placeholder words (control boundaries, `?` runs) sit
        // above the code page and never carry prose.
        if (disposition == SourceDisposition::control_operand ||
            structural_marker || closing_delimiter || segment_terminal_glyph ||
            value > 0xff || boundary_glyph_word(word)) {
          piece.push_back(' ');
          continue;
        }
        if (disposition == SourceDisposition::opaque && value != ' ') {
          const auto admitted = record_continues_run && open &&
                                token < *first_row_token;
          // Unowned printable cells of a mid-record segment before its first
          // physical row (or of a rowless segment) are control text that the
          // decoder left unsegmented (index terms), not prose. Unowned cells
          // after a row of the same segment are the gap LayoutIR leaves
          // between two rows (a sentence stop before wide padding): prose.
          const auto rowless =
              row_by_token.count({record.logical_record, token}) == 0;
          if (!admitted && current_segment != 0 && rowless &&
              !segment_row_seen) {
            piece.push_back(' ');
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
        if (trailing_space) piece.push_back(' ');
      }
    }
    flush_piece(span.end, current_segment);
  }
  finish();
  if (result.paragraphs.empty())
    return reject("message prose envelope projected no paragraphs");
  return result;
}

} // namespace geist::detail
