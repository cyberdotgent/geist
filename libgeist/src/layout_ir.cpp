#include "geist/detail/layout_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace geist::detail {
namespace {

bool exact_spaces(const TokenWords& words) {
  return !words.empty() &&
         std::all_of(words.begin(), words.end(),
                     [](const auto word) { return word == ' '; });
}

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

bool layout_control(BookControlKind kind) {
  return kind == BookControlKind::text || kind == BookControlKind::font ||
         kind == BookControlKind::title ||
         kind == BookControlKind::select ||
         kind == BookControlKind::table_start ||
         kind == BookControlKind::menu_item ||
         kind == BookControlKind::message_start;
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
      if (!layout_control(segment.kind) || segment.source_tokens.empty())
        continue;
      struct Boundary {
        std::size_t marker;
        std::size_t origin;
      };
      std::vector<Boundary> boundaries;
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
            !exact_spaces(record.tokens[origin]) ||
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
            !exact_spaces(record.tokens[origin]) ||
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
              !exact_spaces(record.tokens[token]))
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
      for (std::size_t row_index = 0; row_index < boundaries.size();
           ++row_index) {
        const auto& boundary = boundaries[row_index];
        const auto end = row_index + 1 < boundaries.size()
                             ? boundaries[row_index + 1].marker
                             : segment.source_tokens.back() + 1;
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
          layout.runs.back().control_kind == BookControlKind::font &&
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
           previous_row->logical_record + 1 != row.logical_record))
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
