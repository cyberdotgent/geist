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
  return trim_ascii(token_words_to_ascii(visible));
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
  return kind == BookControlKind::font || kind == BookControlKind::title ||
         kind == BookControlKind::select ||
         kind == BookControlKind::table_start ||
         kind == BookControlKind::menu_item ||
         kind == BookControlKind::message_start;
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
        if (origin != marker + 1 || origin >= record.tokens.size() ||
            origin >= record.encoded_tokens.size() ||
            marker >= record.encoded_tokens.size() ||
            record.encoded_tokens[marker].width != 1 ||
            record.encoded_tokens[origin].width != 1 ||
            !exact_spaces(record.tokens[origin]) ||
            visible_token(record.tokens[marker]).empty())
          continue;
        boundaries.push_back({marker, origin});
      }
      if (boundaries.empty()) continue;

      DisplayRunIR run;
      run.id = next_run++;
      run.control_kind = segment.kind;
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
        row.start = PhysicalRowStartKind::explicit_marker_slot;
        const auto marker_text = visible_token(record.tokens[boundary.marker]);
        row.break_before = marker_break(marker_text);
        row.marker = MarkerSlotIR{
            record.logical_record,
            boundary.marker,
            record.encoded_tokens[boundary.marker].value,
            record.encoded_tokens[boundary.marker].width,
            marker_text,
        };
        row.visible_text = visible_slice(record, boundary.origin, end,
                                         row.native_origin);
        if (!row.visible_text.empty()) run.rows.push_back(std::move(row));
      }
      if (!run.rows.empty()) layout.runs.push_back(std::move(run));
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
           row.marker->encoded_width != 1))
        return fail("physical row marker provenance is invalid");
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
  if (row.marker) out << " marker='" << row.marker->decoded_text << "'";
  out << " text='" << row.visible_text << "'";
  return out.str();
}

} // namespace geist::detail
