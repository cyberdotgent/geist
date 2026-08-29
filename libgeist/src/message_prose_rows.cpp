#include "geist/detail/message_prose_rows.hpp"

#include "geist/detail/font_span_ir.hpp"
#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>
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

bool decimal(const std::string& value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](const char ch) {
           return std::isdigit(static_cast<unsigned char>(ch)) != 0;
         });
}

bool numeric_message_id(const std::string& value) {
  if (decimal(value)) return true;
  const auto hyphen = value.find('-');
  return hyphen != std::string::npos &&
         value.find('-', hyphen + 1) == std::string::npos &&
         decimal(value.substr(0, hyphen)) && decimal(value.substr(hyphen + 1));
}

std::string first_word(const DecodedLogicalRecordSource& record,
                       const OutputRangeIR& range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size()) return {};
  auto value = trim_ascii(text.substr(range.begin, range.end - range.begin));
  const auto space = value.find_first_of(" \t\r\n");
  if (space != std::string::npos) value.resize(space);
  return value;
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

// An all-space token of any width: a display line's trailing fill or the
// origin run of the next line.
bool space_run_token(const DecodedLogicalRecordSource& record,
                     std::size_t token) {
  return token < record.tokens.size() && !record.tokens[token].empty() &&
         std::all_of(record.tokens[token].begin(), record.tokens[token].end(),
                     [](const auto word) { return word == ' '; });
}

// The decoder-separator sentinel the logical decoder emits for the bullet
// glyph of a list item (BookServer renders it as `°`); see layout_ir.cpp,
// which keeps it out of a row's visible text.
constexpr std::uint16_t kDecoderSeparatorSentinel = 0x2666;

// True when the first visible content of the row starting at `token_begin`
// (its marker slot and origin run skipped) is a list-item prefix: the
// decoder-separator bullet sentinel, or an ordinal made of a digit-only token,
// a `.` attached by a spacing control, and a space run. Such a row starts its
// own block; it is never a soft wrap of the previous row.
bool list_item_prefix_row(const DecodedLogicalRecordSource& record,
                          const PhysicalRowIR& row) {
  auto token = row.token_begin;
  if (row.marker && row.marker->token_index == token) ++token;
  while (token < record.tokens.size() && space_run_token(record, token))
    ++token;
  if (token >= record.tokens.size()) return false;
  const auto& words = record.tokens[token];
  if (std::any_of(words.begin(), words.end(), [](const auto word) {
        return word == kDecoderSeparatorSentinel;
      }))
    return true;
  const auto digits =
      !words.empty() && std::all_of(words.begin(), words.end(), [](const auto word) {
        return word < 0x80 && std::isdigit(static_cast<int>(word)) != 0;
      });
  if (!digits || token + 2 >= record.tokens.size()) return false;
  const auto& stop = record.tokens[token + 1];
  return stop.size() == 2 && stop.front() < 4 && stop.back() == '.' &&
         space_run_token(record, token + 2);
}

// Format/markup.md "Repeated row-control signatures" signature B: a
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

struct SegmentPosition {
  const DecodedLogicalRecordSource* record = nullptr;
  const ControlSegmentIR* segment = nullptr;
};

std::optional<std::pair<SegmentPosition, SegmentPosition>>
introduction_envelope(const std::vector<DecodedLogicalRecordSource>& records,
                      std::string* error) {
  SegmentPosition title;
  for (const auto& record : records) {
    for (const auto& segment : record.control_segments) {
      if (segment.kind == BookControlKind::message_start &&
          numeric_message_id(first_word(record, segment.operand_range))) {
        if (title.segment == nullptr) {
          fail(error, "message prose has no title segment before the catalog");
          return std::nullopt;
        }
        if (segment.source_tokens.empty()) {
          fail(error, "message catalog start has no source tokens");
          return std::nullopt;
        }
        return std::pair{title, SegmentPosition{&record, &segment}};
      }
      if (segment.kind == BookControlKind::title &&
          !segment.source_tokens.empty())
        title = {&record, &segment};
    }
  }
  fail(error, "message prose has no numeric SRMSG catalog");
  return std::nullopt;
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

std::string gml_tag(const std::string& record) {
  if (record.empty() || record.front() != ':') return {};
  const auto end = record.find_first_of(". ");
  return ascii_lower(record.substr(1, end == std::string::npos
                                          ? std::string::npos
                                          : end - 1));
}

std::string gml_content(const std::string& record) {
  const auto dot = record.find('.');
  return dot == std::string::npos ? std::string{} : record.substr(dot + 1);
}

bool prose_tag(const std::string& tag) {
  return tag == "p" || tag == "line" || tag == "pinline";
}

bool word_boundary_at(const std::string& text, std::size_t offset) {
  return offset >= text.size() ||
         std::isalnum(static_cast<unsigned char>(text[offset])) == 0;
}

} // namespace

std::optional<MessageProseIntroductionIR>
extract_message_prose_introduction_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership, std::string* error) {
  const auto envelope = introduction_envelope(records, error);
  if (!envelope) return std::nullopt;
  const auto& [title, catalog] = *envelope;
  MessageProseEnvelopeIR explicit_envelope;
  explicit_envelope.begin_record = title.record->logical_record;
  explicit_envelope.begin_token = title.segment->source_tokens.back() + 1;
  explicit_envelope.catalog_record = catalog.record->logical_record;
  explicit_envelope.catalog_segment = catalog.segment->segment_index;
  return extract_message_prose_paragraphs_ir(records, layout, ownership,
                                             explicit_envelope, error);
}

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
      if (segment.kind != BookControlKind::text &&
          segment.kind != BookControlKind::font &&
          segment.kind != BookControlKind::structural &&
          !(straddles_start && segment.kind == BookControlKind::title))
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
    if (run.control_kind != BookControlKind::text &&
        run.control_kind != BookControlKind::font &&
        run.control_kind != BookControlKind::title &&
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
      const auto structural_marker =
          row != nullptr && row->marker && row->marker->token_index == token &&
          (row->native_origin == 3 || layout_glyph_marker(*row->marker));
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

std::string format_message_prose_introduction_ir(
    const MessageProseIntroductionIR& introduction) {
  std::ostringstream out;
  out << "message_prose_introduction runs=[" << introduction.first_run_index
      << ',' << introduction.end_run_index << ") catalog=LR"
      << introduction.first_catalog_segment.logical_record << " segment="
      << introduction.first_catalog_segment.segment_index << '\n';
  for (const auto& paragraph : introduction.paragraphs) {
    out << "paragraph run_start=" << (paragraph.opened_by_run_start ? 1 : 0)
        << " boundary=[";
    for (const auto& token : paragraph.opening_boundary)
      out << "LR" << token.logical_record << ':' << token.token_index << ' ';
    out << "] slices=[";
    for (const auto& slice : paragraph.source_slices)
      out << "LR" << slice.logical_record << ":" << slice.segment_index << '['
          << slice.token_begin << ',' << slice.token_end << ") ";
    out << "] text='" << paragraph.text << "'\n";
  }
  return out.str();
}

std::vector<std::string> render_message_prose_introduction_gml(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const MessageProseIntroductionIR& introduction) {
  std::vector<std::string> rendered;
  for (const auto& paragraph : introduction.paragraphs) {
    auto text = paragraph.text;
    if (!paragraph.source_rows.empty()) {
      const auto& first = paragraph.source_rows.front();
      const auto run = std::find_if(
          layout.runs.begin(), layout.runs.end(),
          [&](const auto& candidate) { return candidate.id == first.display_run; });
      if (run != layout.runs.end() && first.row_index < run->rows.size() &&
          run->control_kind == BookControlKind::font) {
        const auto& row = run->rows[first.row_index];
        const auto* record = find_record(records, row.logical_record);
        if (record != nullptr &&
            row.segment_index < record->control_segments.size()) {
          const auto spans = decode_font_control_spans(
              *record, record->control_segments[row.segment_index]);
          if (spans) {
            for (const auto& span : spans->spans) {
              const char* tag = span.style == FontStyleIR::highlight_1   ? "hp1"
                                : span.style == FontStyleIR::highlight_2 ? "hp2"
                                : span.style == FontStyleIR::highlight_3 ? "hp3"
                                                                          : nullptr;
              // The span must start at the row origin, begin with the row's
              // own visible text, and end on a word boundary of the
              // projected paragraph (a punctuation marker slot may follow
              // the row's last word inside the span).
              const auto visible = collapse(row.visible_text);
              if (tag == nullptr || span.column != row.native_origin ||
                  span.length == 0 || span.length < visible.size() ||
                  span.length > text.size() ||
                  text.compare(0, visible.size(), visible) != 0 ||
                  !word_boundary_at(text, span.length))
                continue;
              text = ":" + std::string(tag) + "." + text.substr(0, span.length) +
                     ":e" + tag + "." + text.substr(span.length);
              break;
            }
          }
        }
      }
    }
    rendered.push_back(":p." + text);
  }
  return rendered;
}

std::optional<std::vector<MessageProseRowJoinIR>>
extract_message_prose_row_joins_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership, std::string* error) {
  // Joins apply to numeric and symbolic SRMSG catalogs alike; only the
  // presence of a message catalog is required.
  const auto has_catalog = std::any_of(
      records.begin(), records.end(), [](const auto& record) {
        return std::any_of(
            record.control_segments.begin(), record.control_segments.end(),
            [&](const auto& segment) {
              return segment.kind == BookControlKind::message_start &&
                     !first_word(record, segment.operand_range).empty();
            });
      });
  if (!has_catalog) {
    fail(error, "message prose has no SRMSG catalog");
    return std::nullopt;
  }
  std::map<CellKey, const OwnedSourceCellIR*> cells;
  for (const auto& cell : ownership.cells)
    cells.emplace(CellKey{cell.logical_record, cell.token_index,
                          cell.word_index},
                  &cell);
  const auto unowned_printable = [&](const DecodedLogicalRecordSource& record,
                                     std::size_t token) {
    for (std::size_t word = 0; word < record.tokens[token].size(); ++word) {
      const auto value = record.tokens[token][word];
      if (value == ' ' || value < 0x20) continue;
      const auto cell = cells.find({record.logical_record, token, word});
      if (cell == cells.end() ||
          cell->second->disposition == SourceDisposition::opaque)
        return true;
    }
    return false;
  };
  // Tokens in [begin, end) of one record separate two rows: any spacing
  // boundary or unowned printable content there prevents a join.
  const auto gap_allows_join = [&](const DecodedLogicalRecordSource& record,
                                   std::size_t begin, std::size_t end,
                                   const PhysicalRowIR* next_row) {
    const MarkerOriginLookup marker_origin =
        [&](std::size_t candidate) -> std::optional<std::size_t> {
      if (next_row != nullptr && next_row->marker &&
          next_row->logical_record == record.logical_record &&
          next_row->marker->token_index == candidate)
        return next_row->native_origin;
      return std::nullopt;
    };
    for (auto token = begin; token < end && token < record.tokens.size();
         ++token) {
      if (hard_boundary_after_token(record, token, end, marker_origin, nullptr))
        return false;
      if (unowned_printable(record, token)) return false;
    }
    // The attach control immediately before the candidate row's own marker
    // slot lies at `end`; signature B is complete when that marker sits at
    // the three-space origin.
    return true;
  };

  std::vector<MessageProseRowJoinIR> joins;
  for (const auto& run : layout.runs) {
    if (run.control_kind != BookControlKind::text &&
        run.control_kind != BookControlKind::font)
      continue;
    for (std::size_t row_index = 1; row_index < run.rows.size(); ++row_index) {
      const auto& row = run.rows[row_index];
      const auto& previous = run.rows[row_index - 1];
      if (row.break_before != PhysicalBreakKind::soft_wrap) continue;
      const auto* record = find_record(records, row.logical_record);
      const auto* previous_record =
          find_record(records, previous.logical_record);
      if (record == nullptr || previous_record == nullptr) continue;
      bool allowed = false;
      if (row.logical_record == previous.logical_record) {
        allowed = gap_allows_join(*record, previous.token_end,
                                  row.token_begin + 1, &row);
      } else if (row.continues_previous_record) {
        allowed = gap_allows_join(*previous_record, previous.token_end,
                                  previous_record->tokens.size(), nullptr) &&
                  gap_allows_join(*record, 0, row.token_begin + 1, &row);
      }
      if (!allowed) continue;
      // A bullet or ordinal after the origin run opens a list item, not a
      // wrapped line (N2AH1MST 22.0 LR1984/LR1985, 23.0 LR2009).
      if (list_item_prefix_row(*record, row)) continue;
      MessageProseRowJoinIR join;
      join.source_row = {run.id, row_index};
      join.source = token_slice(*record, row.segment_index, row.token_begin,
                                row.token_end);
      join.start = row.start;
      join.marker = row.marker;
      join.previous_visible_text = collapse(previous.visible_text);
      join.visible_text = collapse(row.visible_text);
      if (join.previous_visible_text.empty() || join.visible_text.empty())
        continue;
      joins.push_back(std::move(join));
    }
  }
  return joins;
}

std::string format_message_prose_row_join_ir(
    const MessageProseRowJoinIR& join) {
  std::ostringstream out;
  out << "message_prose_row_join run=" << join.source_row.display_run
      << " row=" << join.source_row.row_index << " LR"
      << join.source.logical_record << ':' << join.source.segment_index << '['
      << join.source.token_begin << ',' << join.source.token_end << ") start="
      << (join.start == PhysicalRowStartKind::record_continuation
              ? "record_continuation"
              : "placeholder_wrap")
      << " previous='" << join.previous_visible_text << "' text='"
      << join.visible_text << "'";
  return out.str();
}

std::size_t project_message_prose_row_joins_gml(
    std::vector<std::string>& rendered,
    const std::vector<MessageProseRowJoinIR>& joins) {
  std::size_t applied = 0;
  std::size_t cursor = 0;
  for (const auto& join : joins) {
    for (std::size_t index = cursor + 1; index < rendered.size(); ++index) {
      if (!prose_tag(gml_tag(rendered[index]))) continue;
      const auto content = gml_content(rendered[index]);
      if (content.compare(0, join.visible_text.size(), join.visible_text) != 0 ||
          !word_boundary_at(content, join.visible_text.size()))
        continue;
      const auto previous_tag = gml_tag(rendered[index - 1]);
      if (!prose_tag(previous_tag) && previous_tag != "note") continue;
      const auto previous = trim_ascii(gml_content(rendered[index - 1]));
      const auto& tail = join.previous_visible_text;
      if (previous.size() < tail.size() ||
          previous.compare(previous.size() - tail.size(), tail.size(), tail) !=
              0)
        continue;
      rendered[index - 1] = trim_ascii(rendered[index - 1]) + " " +
                            trim_ascii(content);
      rendered.erase(rendered.begin() + static_cast<std::ptrdiff_t>(index));
      cursor = index - 1;
      ++applied;
      break;
    }
  }
  return applied;
}

std::vector<MessageProseLexicalMarkerIR> extract_message_prose_lexical_markers_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout) {
  std::vector<MessageProseLexicalMarkerIR> markers;
  for (const auto& run : layout.runs) {
    if (run.control_kind != BookControlKind::text &&
        run.control_kind != BookControlKind::font)
      continue;
    for (std::size_t row_index = 1; row_index < run.rows.size(); ++row_index) {
      const auto& row = run.rows[row_index];
      const auto& previous = run.rows[row_index - 1];
      if (!row.marker || row.native_origin == 3 ||
          row.logical_record != previous.logical_record ||
          row.segment_index != previous.segment_index)
        continue;
      const auto& text = row.marker->decoded_text;
      if (text.empty() ||
          !std::all_of(text.begin(), text.end(), [](const unsigned char ch) {
            return std::isalnum(ch) != 0;
          }))
        continue;
      const auto* record = find_record(records, row.logical_record);
      if (record == nullptr) continue;
      // A hanging word is followed by the previous display line's trailing
      // fill run and then the origin run of this line; a slot followed by a
      // single origin run is the row-boundary control (see the header).
      const auto fill = row.marker->token_index + 1;
      const auto origin = fill + 1;
      if (origin >= row.token_end || !space_run_token(*record, fill) ||
          !space_run_token(*record, origin))
        continue;
      MessageProseLexicalMarkerIR item;
      item.source_row = {run.id, row_index};
      item.source = token_slice(*record, row.segment_index,
                                row.marker->token_index,
                                row.marker->token_index + 1);
      item.marker = *row.marker;
      item.line_fill.logical_record = record->logical_record;
      item.line_fill.token_index = fill;
      item.line_fill.width = record->tokens[fill].size();
      if (fill < record->ir.tokens.size())
        item.line_fill.bytes = record->ir.tokens[fill].byte_range;
      item.previous_visible_text = collapse(previous.visible_text);
      item.visible_text = collapse(row.visible_text);
      if (item.previous_visible_text.empty() || item.visible_text.empty())
        continue;
      markers.push_back(std::move(item));
    }
  }
  return markers;
}

std::size_t project_message_prose_lexical_markers_gml(
    std::vector<std::string>& rendered,
    const std::vector<MessageProseLexicalMarkerIR>& markers) {
  std::size_t applied = 0;
  for (const auto& marker : markers) {
    const auto dropped = marker.previous_visible_text + " " + marker.visible_text;
    const auto restored = marker.previous_visible_text + " " +
                          marker.marker.decoded_text + " " + marker.visible_text;
    for (auto& line : rendered) {
      if (!prose_tag(gml_tag(line))) continue;
      const auto at = line.find(dropped);
      if (at == std::string::npos) continue;
      if (at > 0 && std::isalnum(static_cast<unsigned char>(line[at - 1])) != 0)
        continue;
      if (!word_boundary_at(line, at + dropped.size())) continue;
      line.replace(at, dropped.size(), restored);
      ++applied;
      break;
    }
  }
  return applied;
}

std::string format_message_prose_lexical_marker_ir(
    const MessageProseLexicalMarkerIR& marker) {
  std::ostringstream out;
  out << "message_prose_lexical_marker run=" << marker.source_row.display_run
      << " row=" << marker.source_row.row_index << " LR"
      << marker.source.logical_record << ':' << marker.source.segment_index
      << " token=" << marker.source.token_begin << " marker='"
      << marker.marker.decoded_text << "' fill=" << marker.line_fill.width
      << " previous='"
      << marker.previous_visible_text << "' text='" << marker.visible_text
      << "'";
  return out.str();
}

std::optional<MessageProseSourceIR> build_message_prose_source_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::string* error) {
  MessageProseSourceIR result;
  result.layout = extract_layout_ir(records);
  if (!verify_layout_ir(records, result.layout, error)) return std::nullopt;
  // One build, one verification: verify_ownership_ir would have rebuilt the
  // canonical ledger to compare against, and copying the verified ledger into
  // the result costs far less than deriving it a second time.
  const auto verified =
      build_verified_ownership_ir(records, result.layout, error);
  if (!verified) return std::nullopt;
  result.ownership = verified->ir();
  auto joins = extract_message_prose_row_joins_ir(records, result.layout,
                                                  result.ownership, error);
  if (!joins) return std::nullopt;
  result.joins = std::move(*joins);
  result.lexical_markers =
      extract_message_prose_lexical_markers_ir(records, result.layout);
  result.introduction = extract_message_prose_introduction_ir(
      records, result.layout, result.ownership,
      &result.introduction_rejection);
  return result;
}

} // namespace geist::detail
