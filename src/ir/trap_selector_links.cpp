// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/trap_selector_links.hpp"

#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/selector_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"

#include <algorithm>
#include <cctype>

namespace geist::detail {

std::optional<TrapSelectorSource> read_trap_selector_source(
    const DecodedLogicalRecordSource& record, const ControlSegmentIR& segment,
    std::string* error, const DecodedLogicalRecordSource* next_record) {
  const auto reject = [&](std::string reason) -> std::optional<TrapSelectorSource> {
    if (error != nullptr) *error = std::move(reason);
    return std::nullopt;
  };
  const auto selectors = extract_selector_catalog_ir({record}, error);
  if (!selectors) return std::nullopt;
  const auto selector = std::find_if(selectors->selectors.begin(), selectors->selectors.end(),
      [&](const auto& candidate) { return candidate.segment_index == segment.segment_index; });
  if (selector == selectors->selectors.end() || !selector->canonical_operands ||
      selector->target.empty() ||
      !std::all_of(selector->target.begin(), selector->target.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_' || ch == '-';
      }) || ascii_starts_with_case_insensitive(selector->target, "PIC") ||
      ascii_lower(selector->target) == "lnk")
    return reject("trap selector target is not a supported anchor");
  const auto* lines = record_display_lines(record);
  const auto opcode_tokens = source_tokens_intersecting_output(record.assembled,
      decoded_byte_range_to_word_range(record.assembled, segment.opcode_range).begin,
      decoded_byte_range_to_word_range(record.assembled, segment.opcode_range).end);
  const auto* control = opcode_tokens.empty() ? nullptr :
      display_line_of_token(record, opcode_tokens.front());
  const auto control_words = decoded_byte_range_to_word_range(record.assembled,
      {segment.opcode_range.begin, segment.operand_range.end});
  const auto control_tokens = source_tokens_intersecting_output(
      record.assembled, control_words.begin, control_words.end);
  if (lines == nullptr || control == nullptr || control_tokens.empty() ||
      opcode_tokens.front() != control->prefix_token + 1 ||
      display_line_of_token(record, control_tokens.back()) != control)
    return reject("trap selector has no native target row");
  // Consecutive selector lines queue onto the first row drawn after them:
  // N2AH1MST record 563 lines 11 and 12 are `cselect 22 4 FTNFTNUNIQ22` and
  // `cselect 31 4 FTNFTNUNIQ23`, and hosted 10.0 (DT 19910329000100) links
  // both `(*)` marks of line 13. A line is skipped only when a selector
  // control of this record opens it and ends on it.
  const auto selector_line = [&](const DisplayLineIR& line) {
    return std::any_of(selectors->selectors.begin(), selectors->selectors.end(),
        [&](const auto& candidate) {
          if (candidate.segment_index >= record.control_segments.size())
            return false;
          const auto& other = record.control_segments[candidate.segment_index];
          const auto words = decoded_byte_range_to_word_range(record.assembled,
              {other.opcode_range.begin, other.operand_range.end});
          const auto tokens = source_tokens_intersecting_output(
              record.assembled, words.begin, words.end);
          return !tokens.empty() && tokens.front() == line.prefix_token + 1 &&
                 display_line_of_token(record, tokens.back()) == &line &&
                 tokens.back() + 1 == line.token_end;
        });
  };
  // A line that only names a destination draws nothing either: SC34-425
  // 2.4.33 record 1647 lines 6-8 are `cselect 33 11 FIGADAOUT`,
  // `SRSPTTPST` and `   the format of the ADAOUT file.  Figure 89 shows a
  // sample ADAOUT file.`, and hosted DT 19921112160049 links `  Figure 89`
  // on line 8. The name is a structural `SR<id>` control alone on its line.
  const auto destination_line = [&](const DisplayLineIR& line) {
    return std::any_of(record.control_segments.begin(),
                       record.control_segments.end(), [&](const auto& other) {
      if (other.kind != BookControlKind::structural || other.malformed ||
          other.display_text || other.opcode.size() <= 2 ||
          !ascii_starts_with_case_insensitive(other.opcode, "SR"))
        return false;
      const auto words = decoded_byte_range_to_word_range(record.assembled,
          {other.opcode_range.begin, other.operand_range.end});
      const auto tokens = source_tokens_intersecting_output(
          record.assembled, words.begin, words.end);
      return !tokens.empty() && tokens.front() == line.prefix_token + 1 &&
             tokens.back() + 1 == line.token_end &&
             collapse_ascii_whitespace(trim_ascii(display_line_text(record, line))) ==
                 other.opcode;
    });
  };
  // A `cfont` line draws nothing either; it styles the row after it, and
  // the selector marks that row: N2AH1MST record 685 lines 34-36 are
  // `cselect 53 4 FTNFTNUNIQ24`, `cfont 11 7 2` and
  // `           Source:  Hardware configuration definition (*) (HCD)`, and
  // hosted 14.0 (DT 19910329000100) links ` (*)`, cells 53-56 of line 36.
  // The selector then waits for the font control's row like a queued one.
  const auto font_line = [&](const DisplayLineIR& line) {
    return std::any_of(record.control_segments.begin(),
                       record.control_segments.end(), [&](const auto& other) {
      if (other.kind != BookControlKind::font || other.malformed ||
          other.display_text)
        return false;
      const auto words = decoded_byte_range_to_word_range(record.assembled,
          {other.opcode_range.begin, other.operand_range.end});
      const auto tokens = source_tokens_intersecting_output(
          record.assembled, words.begin, words.end);
      return !tokens.empty() && tokens.front() == line.prefix_token + 1 &&
             tokens.back() + 1 == line.token_end;
    });
  };
  auto target = control + 1;
  const auto* lines_end = lines->data() + lines->size();
  bool skipped = false;
  bool queued = false;
  while (target != lines_end &&
         (selector_line(*target) || destination_line(*target) ||
          font_line(*target))) {
    queued = queued || selector_line(*target) || font_line(*target);
    ++target;
    skipped = true;
  }
  // A selector that closes its record marks the next record's first line:
  // SC34-425 2.4.33 record 1648 ends on line 29 `cselect 13 9 FIGADAOUT`
  // and record 1649 line 0 is `   Notice in Figure 89 that ...`, served
  // with `Figure 89` linked.  A queue that closes its record waits for that
  // row the same way: SC09-138 F.1 record 2050 ends with line 30
  // `cselect 64 13 HDRFETCH` and line 31 `cfont 36 5 X 65 5 E`, record 2051
  // line 0 is `   (2)  In the situation of doing a fetch to a load module,
  // see "fetch -- Get`, and hosted F.1 (DT 19910321130500) links
  // `"fetch -- Get`, cells 64-76 of that row.
  const auto* target_record = &record;
  const DisplayLineIR* row = nullptr;
  if (target != lines_end) {
    row = &*target;
  } else if (next_record != nullptr &&
             next_record->logical_record == record.logical_record + 1) {
    const auto* next_lines = record_display_lines(*next_record);
    if (next_lines != nullptr && !next_lines->empty()) {
      target_record = next_record;
      row = &next_lines->front();
    }
  }
  if (row == nullptr)
    return reject(skipped ? "trap selector queue has no native target row"
                          : "trap selector has no native target row");
  const auto columns = display_line_cells(*target_record, *row);
  if (selector->column >= columns.size() ||
      selector->length > columns.size() - selector->column)
    return reject("trap selector exceeds its native target row");
  return TrapSelectorSource{selector->target,
      {columns.begin() + selector->column,
       columns.begin() + selector->column + selector->length},
      row->prefix_token, queued, target_record->logical_record};
}

std::optional<TrapTextLinkIR> project_trap_selector_link(
    const TrapSelectorSource& selector,
    const std::string& text, const TrapGlyphOffsets& glyph_offsets,
    DocumentSourceSliceIR source, std::string* error) {
  const auto reject = [&](std::string reason) -> std::optional<TrapTextLinkIR> {
    if (error != nullptr) *error = std::move(reason);
    return std::nullopt;
  };
  std::optional<std::size_t> begin, end;
  std::string selected;
  for (const auto& cell : selector.cells) {
    if (cell.word == ' ') { selected.push_back(' '); continue; }
    const auto projected = glyph_offsets.find({cell.token, cell.word_index});
    if (projected == glyph_offsets.end() || (end && projected->second < *end))
      return reject("trap selector does not retain all source glyphs");
    if (!begin) begin = projected->second;
    const auto glyph = token_words_to_ascii({cell.word});
    end = projected->second + glyph.size();
    selected += glyph;
  }
  selected = collapse_ascii_whitespace(trim_ascii(std::move(selected)));
  if (!begin || !end || *begin >= *end || *end > text.size() ||
      text.substr(*begin, *end - *begin) != selected)
    return reject("trap selector projection disagrees with its source columns");
  return TrapTextLinkIR{*begin, *end, selector.target, std::move(source)};
}

} // namespace geist::detail
