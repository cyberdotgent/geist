#include "geist/detail/comment_delivery_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace geist::detail {
namespace {

using Position = std::pair<std::uint32_t, std::size_t>;

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

CommentSourceLineIR source_line(const PhysicalRowIR& row,
                                std::size_t row_index) {
  return {row.visible_text,
          row.run,
          row_index,
          row.logical_record,
          row.segment_index,
          row.token_begin,
          row.token_end,
          row.native_origin,
          row.start,
          row.break_before,
          row.continues_previous_record,
          row.marker};
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
         marker_equal(left.marker, right.marker);
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
      left.blocks.size() != right.blocks.size())
    return false;
  for (std::size_t index = 0; index < left.blocks.size(); ++index)
    if (!block_equal(left.blocks[index], right.blocks[index])) return false;
  return true;
}

bool conserve_rows(const LayoutIR& layout, const OwnershipIR& ownership,
                   const CommentDeliveryIR& delivery, std::string* error) {
  const auto fail = [&](const char* message) {
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
      const auto expected = source_line(*found->second, line.row);
      if (!line_equal(line, expected))
        return fail("comment line differs from its physical source row");
    }
  }
  if (assigned.size() != rows.size())
    return fail("comment form does not own every physical source row");

  for (const auto& cell : ownership.cells) {
    if (cell.disposition != SourceDisposition::visible_content) continue;
    if (assigned.count({cell.run, cell.row_index}) == 0)
      return fail("visible source cell escapes comment form ownership");
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
        source_line(layout.runs.front().rows[row], row));
  result.blocks.push_back(
      {CommentDeliveryBlockKind::delivery_instructions, {}, {}});
  for (std::size_t run = 1; run < layout.runs.size(); ++run)
    for (std::size_t row = 0; row < layout.runs[run].rows.size(); ++row)
      result.blocks.back().lines.push_back(
          source_line(layout.runs[run].rows[row], row));
  if (!conserve_rows(layout, ownership, result, error)) return std::nullopt;
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
        source_line(layout.runs.front().rows[row], row));
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
      result.blocks[block].lines.push_back(source_line(source, row));
    }
  }
  if (result.blocks[1].lines.size() != 7 ||
      result.blocks[2].lines.size() != 23 ||
      result.blocks[3].lines.size() != 26)
    return fail("questionnaire object row counts are not canonical");
  if (!conserve_rows(layout, ownership, result, error)) return std::nullopt;
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
    for (const auto& line : source.lines)
      out << "comment_line source=" << line.run << ':' << line.row
          << " record=" << line.logical_record
          << " segment=" << line.segment_index << " tokens=["
          << line.token_begin << ',' << line.token_end << ") text='"
          << line.text << "'\n";
  }
  return out.str();
}

} // namespace geist::detail
