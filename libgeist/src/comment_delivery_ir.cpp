#include "geist/detail/comment_delivery_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace geist::detail {
namespace {

using Position = std::pair<std::uint32_t, std::size_t>;

bool printable_nonspace(std::uint16_t word) {
  const auto text = token_words_to_ascii(TokenWords{word});
  return !text.empty() &&
         std::all_of(text.begin(), text.end(), [](unsigned char ch) {
           return ch >= 0x21 && ch <= 0x7e;
         });
}

bool audited_disposition(SourceDisposition disposition) {
  return disposition == SourceDisposition::marker_slot ||
         disposition == SourceDisposition::opaque ||
         disposition == SourceDisposition::layout_padding;
}

std::string range_text(const DecodedLogicalRecordSource& record,
                       const OutputRangeIR& range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size()) return {};
  return text.substr(range.begin, range.end - range.begin);
}

std::string operand_text(const DecodedLogicalRecordSource& record,
                         const ControlSegmentIR& segment) {
  return trim_ascii(range_text(record, segment.operand_range));
}

bool opcode_is(const ControlSegmentIR& segment, std::string_view value) {
  return ascii_equals_case_insensitive(segment.opcode, std::string(value));
}

bool opcode_starts(const ControlSegmentIR& segment, std::string_view value) {
  return ascii_starts_with_case_insensitive(segment.opcode, value);
}

struct SourceShape {
  const ControlSegmentIR* title = nullptr;
  std::size_t titles = 0;
  std::vector<const ControlSegmentIR*> table_starts;
  std::vector<const ControlSegmentIR*> table_ends;
  std::vector<const ControlSegmentIR*> figure_starts;
  std::vector<const ControlSegmentIR*> figure_ends;
  std::size_t fonts = 0;
  std::size_t text_segments = 0;
  bool has_rcf_source = false;
  bool has_h1 = false;
  bool has_topic_start = false;
};

SourceShape inspect_shape(
    const std::vector<DecodedLogicalRecordSource>& records) {
  SourceShape shape;
  for (const auto& record : records) {
    for (const auto& segment : record.control_segments) {
      shape.has_topic_start =
          shape.has_topic_start || segment.kind == BookControlKind::topic_start;
      if (segment.kind == BookControlKind::source_file &&
          ascii_equals_case_insensitive(operand_text(record, segment),
                                        "RCFADDR"))
        shape.has_rcf_source = true;
      if (segment.kind == BookControlKind::heading_level &&
          ascii_equals_case_insensitive(operand_text(record, segment), ":H1"))
        shape.has_h1 = true;
      if (segment.kind == BookControlKind::title) {
        ++shape.titles;
        if (shape.title == nullptr) shape.title = &segment;
      }
      if (segment.kind == BookControlKind::font) ++shape.fonts;
      if (segment.kind == BookControlKind::text) ++shape.text_segments;
      if (segment.kind == BookControlKind::table_start)
        shape.table_starts.push_back(&segment);
      if (segment.kind == BookControlKind::table_end)
        shape.table_ends.push_back(&segment);
      if (opcode_starts(segment, "srfig") && !opcode_is(segment, "srefig"))
        shape.figure_starts.push_back(&segment);
      if (opcode_is(segment, "srefig")) shape.figure_ends.push_back(&segment);
    }
  }
  return shape;
}

bool consecutive_records(
    const std::vector<DecodedLogicalRecordSource>& records) {
  if (records.empty()) return false;
  for (std::size_t index = 1; index < records.size(); ++index)
    if (records[index - 1].logical_record + 1 != records[index].logical_record)
      return false;
  return true;
}

Position position(const ControlSegmentIR& segment) {
  return {segment.logical_record, segment.segment_index};
}

Position position(const PhysicalRowIR& row) {
  return {row.logical_record, row.segment_index};
}

const DecodedLogicalRecordSource* source_record(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::uint32_t logical_record) {
  const auto found = std::find_if(records.begin(), records.end(),
                                  [&](const auto& record) {
                                    return record.logical_record ==
                                           logical_record;
                                  });
  return found == records.end() ? nullptr : &*found;
}

std::vector<CommentSourceFieldIR> source_fields(
    const std::vector<DecodedLogicalRecordSource>& records,
    const OwnershipIR& ownership, const PhysicalRowIR& row,
    std::size_t row_index) {
  const auto* record = source_record(records, row.logical_record);
  if (record == nullptr) return {};
  std::vector<bool> visible(record->tokens.size());
  std::vector<bool> padding(record->tokens.size());
  for (const auto& cell : ownership.cells) {
    if (cell.logical_record != row.logical_record || cell.run != row.run ||
        cell.row_index != row_index || cell.token_index >= visible.size())
      continue;
    if (cell.disposition == SourceDisposition::visible_content)
      visible[cell.token_index] = true;
    if (cell.disposition == SourceDisposition::layout_padding)
      padding[cell.token_index] = true;
  }

  std::vector<CommentSourceFieldIR> result;
  auto begin = row.token_end;
  auto last_visible = row.token_end;
  const auto flush = [&] {
    if (begin == row.token_end || last_visible == row.token_end) return;
    const auto end = last_visible + 1;
    if (begin >= record->assembled.tokens.size() ||
        end > record->assembled.tokens.size()) {
      begin = row.token_end;
      last_visible = row.token_end;
      return;
    }
    const auto output_begin = record->assembled.tokens[begin].output_begin;
    const auto output_end = record->assembled.tokens[end - 1].output_end;
    auto text = trim_ascii(token_words_to_ascii(TokenWords{
        record->assembled.words.begin() +
            static_cast<std::ptrdiff_t>(output_begin),
        record->assembled.words.begin() +
            static_cast<std::ptrdiff_t>(output_end)}));
    const auto disposition =
        text.empty() ? CommentSourceFieldIR::Disposition::layout_decoration
                     : CommentSourceFieldIR::Disposition::semantic_content;
    result.push_back(CommentSourceFieldIR{begin, end, std::move(text),
                                          disposition, {}});
    begin = row.token_end;
    last_visible = row.token_end;
  };
  for (auto token = row.token_begin; token < row.token_end; ++token) {
    if (padding[token]) {
      flush();
      continue;
    }
    if (!visible[token]) continue;
    if (begin == row.token_end) begin = token;
    last_visible = token;
  }
  flush();
  return result;
}

struct MutableFieldRef {
  std::uint32_t logical_record = 0;
  std::size_t token_begin = 0;
  std::size_t token_end = 0;
  CommentSourceLineIR* line = nullptr;
  CommentSourceFieldIR* field = nullptr;
};

bool alphabetic_text(const std::string& text) {
  return !text.empty() &&
         std::all_of(text.begin(), text.end(), [](unsigned char ch) {
           return std::isalpha(ch) != 0;
         });
}

bool terminal_punctuation(const std::string& text) {
  return !text.empty() && std::string_view(".!?").find(text.back()) !=
                              std::string_view::npos;
}

bool attach_audited_fragments(
    const std::vector<DecodedLogicalRecordSource>& records,
    const OwnershipIR& ownership, CommentDeliveryIR& delivery,
    std::string* error) {
  const auto fail = [&](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  std::vector<MutableFieldRef> fields;
  for (auto& block : delivery.blocks)
    for (auto& line : block.lines)
      for (auto& field : line.fields)
        if (field.disposition ==
            CommentSourceFieldIR::Disposition::semantic_content)
          fields.push_back({line.logical_record, field.token_begin,
                            field.token_end, &line, &field});
  std::sort(fields.begin(), fields.end(), [](const auto& left, const auto& right) {
    return std::tie(left.logical_record, left.token_begin, left.token_end) <
           std::tie(right.logical_record, right.token_begin, right.token_end);
  });

  std::set<std::tuple<std::uint32_t, std::size_t, std::size_t>> semantic_cells;
  auto semantic_count = std::size_t{0};
  // Classification is grammatical and ownership-led: punctuation immediately
  // following a field closes that field, while a lowercase alphabetic marker
  // continuing an unterminated sentence prefixes the current row. The admitted
  // form geometry bounds these rules; source coordinates are captured only as
  // provenance after the classification succeeds.
  for (const auto& record : records) {
    for (std::size_t token = 0; token < record.tokens.size(); ++token) {
      const auto& words = record.tokens[token];
      auto word = std::size_t{0};
      while (word < words.size()) {
        const auto cell = std::find_if(
            ownership.cells.begin(), ownership.cells.end(), [&](const auto& item) {
              return item.logical_record == record.logical_record &&
                     item.token_index == token && item.word_index == word;
            });
        if (cell == ownership.cells.end() ||
            !audited_disposition(cell->disposition) ||
            !printable_nonspace(words[word]) ||
            semantic_cells.count({record.logical_record, token, word}) != 0) {
          ++word;
          continue;
        }
        const auto begin = word;
        const auto disposition = cell->disposition;
        while (word < words.size()) {
          const auto next = std::find_if(
              ownership.cells.begin(), ownership.cells.end(),
              [&](const auto& item) {
                return item.logical_record == record.logical_record &&
                       item.token_index == token && item.word_index == word;
              });
          if (next == ownership.cells.end() ||
              next->disposition != disposition ||
              !printable_nonspace(words[word]) ||
              semantic_cells.count({record.logical_record, token, word}) != 0)
            break;
          ++word;
        }
        CommentSourceFragmentIR fragment;
        fragment.logical_record = record.logical_record;
        fragment.token_index = token;
        fragment.word_begin = begin;
        fragment.word_end = word;
        fragment.byte_begin = record.ir.tokens[token].byte_range.begin;
        fragment.byte_end = record.ir.tokens[token].byte_range.end;
        fragment.text = token_words_to_ascii(TokenWords(
            words.begin() + static_cast<std::ptrdiff_t>(begin),
            words.begin() + static_cast<std::ptrdiff_t>(word)));
        MutableFieldRef* previous = nullptr;
        MutableFieldRef* next = nullptr;
        for (auto& field : fields) {
          if (std::make_pair(field.logical_record, field.token_end) <=
              std::make_pair(record.logical_record, token + 1))
            previous = &field;
          if (next == nullptr &&
              std::make_pair(field.logical_record, field.token_begin) >=
                  std::make_pair(record.logical_record, token))
            next = &field;
        }
        const auto is_marker = disposition == SourceDisposition::marker_slot;
        const auto raw_ascii = std::all_of(
            words.begin() + static_cast<std::ptrdiff_t>(begin),
            words.begin() + static_cast<std::ptrdiff_t>(word),
            [](std::uint16_t value) { return value >= 0x21 && value <= 0x7e; });
        const auto suffix =
            previous != nullptr && raw_ascii &&
            (fragment.text == "." ||
             (fragment.text == ":" &&
              (delivery.kind == CommentDeliveryKind::delivery_instructions ||
               disposition == SourceDisposition::opaque)) ||
             (fragment.text == "?" &&
              disposition == SourceDisposition::layout_padding) ||
             (alphabetic_text(fragment.text) && !is_marker));
        const auto prefix =
            next != nullptr && raw_ascii && is_marker &&
            alphabetic_text(fragment.text) &&
            previous != nullptr && !terminal_punctuation(previous->field->text);
        if (suffix || prefix) {
          auto* owner = suffix ? previous : next;
          fragment.disposition =
              CommentSourceFragmentIR::Disposition::semantic_affix;
          fragment.attachment =
              suffix ? CommentAffixAttachment::suffix_owning_field
                     : CommentAffixAttachment::prefix_current_field;
          fragment.spacing = alphabetic_text(fragment.text)
                                 ? (suffix ? CommentAffixSpacing::space_before
                                           : CommentAffixSpacing::space_after)
                                 : CommentAffixSpacing::none;
          owner->field->affixes.push_back(fragment);
          if (prefix) owner->line->marker_disposition =
              CommentMarkerDisposition::lexical_content;
          for (auto index = begin; index < word; ++index)
            semantic_cells.emplace(record.logical_record, token, index);
          ++semantic_count;
        } else {
          delivery.suppressed_fragments.push_back(std::move(fragment));
        }
      }
    }
  }
  const auto expected = delivery.kind == CommentDeliveryKind::delivery_instructions
                            ? std::size_t{10}
                            : std::size_t{8};
  if (semantic_count != expected)
    return fail("comment form has ambiguous semantic affix grammar: classified=" +
                std::to_string(semantic_count) + " expected=" +
                std::to_string(expected));
  return true;
}

CommentSourceLineIR source_line(
    const std::vector<DecodedLogicalRecordSource>& records,
    const OwnershipIR& ownership, const PhysicalRowIR& row,
    std::size_t row_index) {
  CommentSourceLineIR result;
  result.text = row.visible_text;
  result.run = row.run;
  result.row = row_index;
  result.logical_record = row.logical_record;
  result.segment_index = row.segment_index;
  result.token_begin = row.token_begin;
  result.token_end = row.token_end;
  result.native_origin = row.native_origin;
  result.start = row.start;
  result.break_before = row.break_before;
  result.continues_previous_record = row.continues_previous_record;
  result.marker = row.marker;
  result.marker_disposition = row.marker
                                  ? CommentMarkerDisposition::layout_artifact
                                  : CommentMarkerDisposition::absent;
  result.fields = source_fields(records, ownership, row, row_index);
  return result;
}

bool marker_equal(const std::optional<MarkerSlotIR>& left,
                  const std::optional<MarkerSlotIR>& right) {
  if (left.has_value() != right.has_value()) return false;
  if (!left) return true;
  return left->logical_record == right->logical_record &&
         left->token_index == right->token_index &&
         left->encoded_value == right->encoded_value &&
         left->encoded_width == right->encoded_width &&
         left->byte_range.begin == right->byte_range.begin &&
         left->byte_range.end == right->byte_range.end &&
         left->decoded_text == right->decoded_text;
}

bool physical_line_equal(const CommentSourceLineIR& line,
                         const PhysicalRowIR& row,
                         std::size_t row_index) {
  return line.text == row.visible_text && line.run == row.run &&
         line.row == row_index &&
         line.logical_record == row.logical_record &&
         line.segment_index == row.segment_index &&
         line.token_begin == row.token_begin &&
         line.token_end == row.token_end &&
         line.native_origin == row.native_origin && line.start == row.start &&
         line.break_before == row.break_before &&
         line.continues_previous_record == row.continues_previous_record &&
         marker_equal(line.marker, row.marker);
}

bool line_equal(const CommentSourceLineIR& left,
                const CommentSourceLineIR& right) {
  return left.text == right.text && left.run == right.run &&
         left.row == right.row &&
         left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.token_begin == right.token_begin &&
         left.token_end == right.token_end &&
         left.native_origin == right.native_origin &&
         left.start == right.start &&
         left.break_before == right.break_before &&
         left.continues_previous_record == right.continues_previous_record &&
         marker_equal(left.marker, right.marker) &&
         left.marker_disposition == right.marker_disposition &&
         left.fields.size() == right.fields.size() &&
         std::equal(left.fields.begin(), left.fields.end(),
                    right.fields.begin(), [](const auto& a, const auto& b) {
                      return a.token_begin == b.token_begin &&
                             a.token_end == b.token_end && a.text == b.text &&
                             a.disposition == b.disposition &&
                             a.affixes == b.affixes;
                    });
}

bool fragment_equal(const CommentSourceFragmentIR& left,
                    const CommentSourceFragmentIR& right) {
  return left.logical_record == right.logical_record &&
         left.token_index == right.token_index &&
         left.word_begin == right.word_begin &&
         left.word_end == right.word_end &&
         left.byte_begin == right.byte_begin &&
         left.byte_end == right.byte_end && left.text == right.text &&
         left.disposition == right.disposition &&
         left.attachment == right.attachment && left.spacing == right.spacing;
}

bool block_equal(const CommentDeliveryBlockIR& left,
                 const CommentDeliveryBlockIR& right) {
  if (left.kind != right.kind || left.object_id != right.object_id ||
      left.lines.size() != right.lines.size())
    return false;
  for (std::size_t index = 0; index < left.lines.size(); ++index)
    if (!line_equal(left.lines[index], right.lines[index])) return false;
  return true;
}

bool delivery_equal(const CommentDeliveryIR& left,
                    const CommentDeliveryIR& right) {
  if (left.kind != right.kind || left.title != right.title ||
      left.blocks.size() != right.blocks.size() ||
      left.suppressed_fragments.size() != right.suppressed_fragments.size())
    return false;
  for (std::size_t index = 0; index < left.blocks.size(); ++index)
    if (!block_equal(left.blocks[index], right.blocks[index])) return false;
  for (std::size_t index = 0; index < left.suppressed_fragments.size(); ++index)
    if (!fragment_equal(left.suppressed_fragments[index],
                        right.suppressed_fragments[index]))
      return false;
  return true;
}

bool conserve_rows(const std::vector<DecodedLogicalRecordSource>& records,
                   const LayoutIR& layout, const OwnershipIR& ownership,
                   const CommentDeliveryIR& delivery, std::string* error) {
  const auto fail = [&](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  std::map<std::pair<DisplayRunId, std::size_t>, const PhysicalRowIR*> rows;
  for (const auto& run : layout.runs)
    for (std::size_t row = 0; row < run.rows.size(); ++row)
      rows.emplace(std::make_pair(run.id, row), &run.rows[row]);

  std::set<std::pair<DisplayRunId, std::size_t>> assigned;
  for (const auto& block : delivery.blocks) {
    if (block.lines.empty()) return fail("comment block has no source lines");
    for (const auto& line : block.lines) {
      const auto key = std::make_pair(line.run, line.row);
      const auto found = rows.find(key);
      if (found == rows.end() || !assigned.insert(key).second)
        return fail("comment row is absent or multiply assigned");
      if (!physical_line_equal(line, *found->second, line.row))
        return fail("comment line differs from its physical source row");
      if (line.fields.empty())
        return fail("comment line has no source-proven visible fields");
      auto previous_end = line.token_begin;
      for (const auto& field : line.fields) {
        if (field.token_begin < line.token_begin ||
            field.token_begin < previous_end ||
            field.token_begin >= field.token_end ||
            field.token_end > line.token_end ||
            (field.disposition ==
                 CommentSourceFieldIR::Disposition::semantic_content &&
             field.text.empty()))
          return fail("comment field range is invalid or out of order");
        previous_end = field.token_end;
      }
      for (const auto& cell : ownership.cells) {
        if (cell.logical_record != line.logical_record ||
            cell.run != line.run || cell.row_index != line.row ||
            cell.disposition != SourceDisposition::visible_content)
          continue;
        const auto owners = std::count_if(
            line.fields.begin(), line.fields.end(), [&](const auto& field) {
              return field.token_begin <= cell.token_index &&
                     cell.token_index < field.token_end;
            });
        if (owners != 1)
          return fail("visible comment cell lacks one field owner at record " +
                      std::to_string(cell.logical_record) + " token " +
                      std::to_string(cell.token_index));
      }
    }
  }
  if (assigned.size() != rows.size())
    return fail("comment form does not own every physical source row");

  for (const auto& cell : ownership.cells) {
    if (cell.disposition != SourceDisposition::visible_content) continue;
    if (assigned.count({cell.run, cell.row_index}) == 0)
      return fail("visible source cell escapes comment form ownership");
  }


  std::vector<const CommentSourceFragmentIR*> fragments;
  for (const auto& block : delivery.blocks)
    for (const auto& line : block.lines)
      for (const auto& field : line.fields)
        for (const auto& fragment : field.affixes)
          fragments.push_back(&fragment);
  for (const auto& fragment : delivery.suppressed_fragments)
    fragments.push_back(&fragment);
  for (const auto* fragment : fragments) {
    const auto* record = source_record(records, fragment->logical_record);
    if (record == nullptr || fragment->token_index >= record->tokens.size() ||
        fragment->word_begin >= fragment->word_end ||
        fragment->word_end > record->tokens[fragment->token_index].size() ||
        fragment->token_index >= record->ir.tokens.size() ||
        fragment->byte_begin !=
            record->ir.tokens[fragment->token_index].byte_range.begin ||
        fragment->byte_end !=
            record->ir.tokens[fragment->token_index].byte_range.end)
      return fail("comment source fragment provenance is invalid");
    const auto& words = record->tokens[fragment->token_index];
    const auto text = token_words_to_ascii(TokenWords(
        words.begin() + static_cast<std::ptrdiff_t>(fragment->word_begin),
        words.begin() + static_cast<std::ptrdiff_t>(fragment->word_end)));
    if (text != fragment->text)
      return fail("comment source fragment text differs from provenance");
  }
  for (const auto& cell : ownership.cells) {
    if (!audited_disposition(cell.disposition) ||
        !printable_nonspace(cell.word))
      continue;
    const auto owners = std::count_if(
        fragments.begin(), fragments.end(), [&](const auto* fragment) {
          return fragment->logical_record == cell.logical_record &&
                 fragment->token_index == cell.token_index &&
                 fragment->word_begin <= cell.word_index &&
                 cell.word_index < fragment->word_end;
        });
    if (owners != 1)
      return fail("printable non-field comment cell lacks one explicit "
                  "semantic or structural owner at record " +
                  std::to_string(cell.logical_record) + " token " +
                  std::to_string(cell.token_index) + " word " +
                  std::to_string(cell.word_index));
  }
  return true;
}

std::optional<CommentDeliveryIR> extract_delivery_shape(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const SourceShape& shape, std::string* error) {
  const auto fail =
      [&](const std::string& message) -> std::optional<CommentDeliveryIR> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  if (records.size() != 2 || !shape.table_starts.empty() ||
      !shape.table_ends.empty() || !shape.figure_starts.empty() ||
      !shape.figure_ends.empty() || shape.fonts != 4 ||
      shape.text_segments != 1 || layout.runs.size() != 6)
    return fail("source is not the bounded comment-delivery shape");
  const std::vector<BookControlKind> kinds = {
      BookControlKind::title, BookControlKind::text, BookControlKind::font,
      BookControlKind::font, BookControlKind::font, BookControlKind::font};
  const std::vector<std::size_t> row_counts = {21, 3, 3, 2, 2, 2};
  for (std::size_t run = 0; run < layout.runs.size(); ++run)
    if (layout.runs[run].control_kind != kinds[run] ||
        layout.runs[run].rows.size() != row_counts[run])
      return fail("comment-delivery run geometry is not canonical at run " +
                  std::to_string(run) + ": kind=" +
                  std::to_string(static_cast<unsigned>(
                      layout.runs[run].control_kind)) +
                  " rows=" + std::to_string(layout.runs[run].rows.size()));

  CommentDeliveryIR result;
  result.kind = CommentDeliveryKind::delivery_instructions;
  result.title = layout.runs.front().rows.front().visible_text;
  result.blocks.push_back({CommentDeliveryBlockKind::title_page, {}, {}});
  for (std::size_t row = 0; row < layout.runs.front().rows.size(); ++row)
    result.blocks.front().lines.push_back(
        source_line(records, ownership, layout.runs.front().rows[row], row));
  result.blocks.push_back(
      {CommentDeliveryBlockKind::delivery_instructions, {}, {}});
  for (std::size_t run = 1; run < layout.runs.size(); ++run)
    for (std::size_t row = 0; row < layout.runs[run].rows.size(); ++row)
      result.blocks.back().lines.push_back(
          source_line(records, ownership, layout.runs[run].rows[row], row));
  if (!attach_audited_fragments(records, ownership, result, error) ||
      !conserve_rows(records, layout, ownership, result, error))
    return std::nullopt;
  return result;
}

std::optional<CommentDeliveryIR> extract_questionnaire_shape(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const SourceShape& shape, std::string* error) {
  const auto fail =
      [&](const std::string& message) -> std::optional<CommentDeliveryIR> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  if (records.size() != 4 || shape.table_starts.size() != 2 ||
      shape.table_ends.size() != 2 || shape.figure_starts.size() != 2 ||
      shape.figure_ends.size() != 2 || shape.fonts != 4 ||
      shape.text_segments != 2 || layout.runs.size() != 8)
    return fail("source is not the bounded comment-questionnaire shape");
  for (std::size_t index = 0; index < 2; ++index) {
    if (!(position(*shape.figure_starts[index]) <
              position(*shape.table_starts[index]) &&
          position(*shape.table_starts[index]) <
              position(*shape.table_ends[index]) &&
          position(*shape.table_ends[index]) <
              position(*shape.figure_ends[index])))
      return fail("questionnaire figure/table controls are not balanced");
    if (index != 0 &&
        !(position(*shape.figure_ends[index - 1]) <
          position(*shape.figure_starts[index])))
      return fail("questionnaire objects overlap");
  }
  const std::vector<BookControlKind> kinds = {
      BookControlKind::title,       BookControlKind::table_start,
      BookControlKind::font,        BookControlKind::font,
      BookControlKind::table_start, BookControlKind::font,
      BookControlKind::font,        BookControlKind::structural};
  const std::vector<std::size_t> row_counts = {8, 1, 3, 3, 1, 3, 19, 26};
  for (std::size_t run = 0; run < layout.runs.size(); ++run)
    if (layout.runs[run].control_kind != kinds[run] ||
        layout.runs[run].rows.size() != row_counts[run])
      return fail(
          "comment-questionnaire run geometry is not canonical at run " +
          std::to_string(run) + ": kind=" +
          std::to_string(
              static_cast<unsigned>(layout.runs[run].control_kind)) +
          " rows=" + std::to_string(layout.runs[run].rows.size()));

  CommentDeliveryIR result;
  result.kind = CommentDeliveryKind::questionnaire;
  result.title = layout.runs.front().rows.front().visible_text;
  result.blocks.push_back({CommentDeliveryBlockKind::title_page, {}, {}});
  for (std::size_t row = 0; row < layout.runs.front().rows.size(); ++row)
    result.blocks.front().lines.push_back(
        source_line(records, ownership, layout.runs.front().rows[row], row));
  for (const auto* table : shape.table_starts)
    result.blocks.push_back({CommentDeliveryBlockKind::questionnaire_table,
                             table->opcode, {}});
  result.blocks.push_back({CommentDeliveryBlockKind::response_area, {}, {}});

  const auto first_table_begin = position(*shape.table_starts[0]);
  const auto first_table_end = position(*shape.table_ends[0]);
  const auto second_table_begin = position(*shape.table_starts[1]);
  const auto second_table_end = position(*shape.table_ends[1]);
  const auto response_begin = position(*shape.figure_ends[1]);
  for (std::size_t run = 1; run < layout.runs.size(); ++run) {
    for (std::size_t row = 0; row < layout.runs[run].rows.size(); ++row) {
      const auto& source = layout.runs[run].rows[row];
      const auto at = position(source);
      auto block = result.blocks.size();
      if (first_table_begin <= at && at < first_table_end) block = 1;
      else if (second_table_begin <= at && at < second_table_end) block = 2;
      else if (response_begin <= at) block = 3;
      if (block == result.blocks.size())
        return fail("questionnaire row lies outside its semantic object");
      result.blocks[block].lines.push_back(
          source_line(records, ownership, source, row));
    }
  }
  if (result.blocks[1].lines.size() != 7 ||
      result.blocks[2].lines.size() != 23 ||
      result.blocks[3].lines.size() != 26)
    return fail("questionnaire object row counts are not canonical");
  for (const auto block : {std::size_t{1}, std::size_t{2}}) {
    auto& decoration = result.blocks[block].lines.front();
    if (decoration.fields.size() != 1)
      return fail("questionnaire decoration row is not isolated");
    decoration.fields.front().disposition =
        CommentSourceFieldIR::Disposition::layout_decoration;
  }
  if (!attach_audited_fragments(records, ownership, result, error) ||
      !conserve_rows(records, layout, ownership, result, error))
    return std::nullopt;
  return result;
}

const char* kind_name(CommentDeliveryKind kind) {
  return kind == CommentDeliveryKind::delivery_instructions
             ? "delivery_instructions"
             : "questionnaire";
}

const char* block_name(CommentDeliveryBlockKind kind) {
  switch (kind) {
  case CommentDeliveryBlockKind::title_page: return "title_page";
  case CommentDeliveryBlockKind::delivery_instructions:
    return "delivery_instructions";
  case CommentDeliveryBlockKind::questionnaire_table:
    return "questionnaire_table";
  case CommentDeliveryBlockKind::response_area: return "response_area";
  }
  return "unknown";
}

const char* marker_disposition_name(CommentMarkerDisposition disposition) {
  switch (disposition) {
  case CommentMarkerDisposition::absent: return "absent";
  case CommentMarkerDisposition::layout_artifact: return "layout_artifact";
  case CommentMarkerDisposition::lexical_content: return "lexical_content";
  }
  return "unknown";
}

} // namespace

std::optional<CommentDeliveryIR> extract_comment_delivery_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    std::string* error) {
  const auto fail =
      [&](const std::string& message) -> std::optional<CommentDeliveryIR> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  std::string verification_error;
  if (!verify_layout_ir(records, layout, &verification_error) ||
      !verify_ownership_ir(records, layout, ownership, &verification_error))
    return fail("source layout/ownership is not canonical: " +
                verification_error);
  if (!consecutive_records(records))
    return fail("comment source records are not adjacent");
  const auto shape = inspect_shape(records);
  if (!shape.has_topic_start || !shape.has_rcf_source || !shape.has_h1 ||
      shape.titles != 1 || shape.title == nullptr)
    return fail("comment source envelope is absent or ambiguous");

  if (auto delivery = extract_delivery_shape(records, layout, ownership,
                                              shape, error)) {
    if (error != nullptr) error->clear();
    return delivery;
  }
  if (records.size() == 2) return std::nullopt;
  if (auto questionnaire = extract_questionnaire_shape(
          records, layout, ownership, shape, error)) {
    if (error != nullptr) error->clear();
    return questionnaire;
  }
  return std::nullopt;
}

bool verify_comment_delivery_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const CommentDeliveryIR& delivery, std::string* error) {
  const auto canonical =
      extract_comment_delivery_ir(records, layout, ownership, error);
  if (!canonical) return false;
  if (!delivery_equal(*canonical, delivery)) {
    if (error != nullptr)
      *error = "comment form differs from canonical semantic lowering";
    return false;
  }
  if (error != nullptr) error->clear();
  return true;
}

std::string format_comment_delivery_ir(const CommentDeliveryIR& delivery) {
  std::ostringstream out;
  out << "comment_delivery kind=" << kind_name(delivery.kind) << " title='"
      << delivery.title << "' blocks=" << delivery.blocks.size() << '\n';
  for (std::size_t block = 0; block < delivery.blocks.size(); ++block) {
    const auto& source = delivery.blocks[block];
    out << "comment_block index=" << block << " kind="
        << block_name(source.kind);
    if (!source.object_id.empty()) out << " object='" << source.object_id << "'";
    out << " lines=" << source.lines.size() << '\n';
    for (const auto& line : source.lines) {
      out << "comment_line source=" << line.run << ':' << line.row
          << " record=" << line.logical_record
          << " segment=" << line.segment_index << " tokens=["
          << line.token_begin << ',' << line.token_end << ") text='"
          << line.text << "' marker_disposition="
          << marker_disposition_name(line.marker_disposition)
          << " fields=" << line.fields.size() << '\n';
      for (const auto& field : line.fields)
        out << "comment_field tokens=[" << field.token_begin << ','
            << field.token_end << ") disposition="
            << (field.disposition ==
                        CommentSourceFieldIR::Disposition::semantic_content
                    ? "semantic_content"
                    : "layout_decoration")
            << " text='" << field.text << "' affixes="
            << field.affixes.size() << '\n';
      for (const auto& field : line.fields)
        for (const auto& affix : field.affixes)
          out << "comment_affix record=" << affix.logical_record
              << " token=" << affix.token_index << " words=["
              << affix.word_begin << ',' << affix.word_end << ") bytes=[0x"
              << std::hex << affix.byte_begin << ",0x" << affix.byte_end
              << std::dec << ") text='" << affix.text << "' attachment="
              << (affix.attachment ==
                          CommentAffixAttachment::prefix_current_field
                      ? "prefix_current"
                      : "suffix_owning")
              << '\n';
    }
  }
  out << "comment_suppressed_fragments="
      << delivery.suppressed_fragments.size() << '\n';
  return out.str();
}

} // namespace geist::detail
