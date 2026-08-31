#include "geist/detail/ir/fixed_prose_topic_ir.hpp"

#include "geist/detail/core/internal.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

std::string range_text(const DecodedLogicalRecordSource& record,
                       const OutputRangeIR& range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size()) return {};
  return text.substr(range.begin, range.end - range.begin);
}

DocumentSourceSliceIR source_slice(const DecodedLogicalRecordSource& record,
                                   std::size_t segment_index,
                                   const OutputRangeIR& output) {
  DocumentSourceSliceIR result;
  result.logical_record = record.logical_record;
  result.segment_index = segment_index;
  const auto words = decoded_byte_range_to_word_range(record.assembled, output);
  const auto tokens = source_tokens_intersecting_output(
      record.assembled, words.begin, words.end);
  if (tokens.empty()) return result;
  result.token_begin = tokens.front();
  result.token_end = tokens.back() + 1;
  result.byte_begin = record.ir.tokens[result.token_begin].byte_range.begin;
  result.byte_end = record.ir.tokens[result.token_end - 1].byte_range.end;
  return result;
}

bool has_source(const DocumentSourceSliceIR& source) {
  return source.logical_record != 0 && source.token_begin < source.token_end &&
         source.byte_begin < source.byte_end;
}

bool valid_anchor_id(const std::string& value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
           return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.';
         });
}

bool same_slice(const DocumentSourceSliceIR& left,
                const DocumentSourceSliceIR& right) {
  return left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.token_begin == right.token_begin &&
         left.token_end == right.token_end &&
         left.byte_begin == right.byte_begin && left.byte_end == right.byte_end;
}

bool same_topic(const FixedProseTopicIR& left,
                const FixedProseTopicIR& right) {
  if (left.logical_record != right.logical_record ||
      left.payload_bytes.begin != right.payload_bytes.begin ||
      left.payload_bytes.end != right.payload_bytes.end ||
      left.token_count != right.token_count ||
      left.heading_level != right.heading_level ||
      left.anchor.has_value() != right.anchor.has_value() ||
      !same_slice(left.heading_source, right.heading_source) ||
      !same_slice(left.paragraph_source, right.paragraph_source) ||
      left.segments.size() != right.segments.size() ||
      format_fixed_prose_ir(left.prose) != format_fixed_prose_ir(right.prose))
    return false;
  if (left.anchor &&
      (left.anchor->id != right.anchor->id ||
       !same_slice(left.anchor->source, right.anchor->source)))
    return false;
  for (std::size_t index = 0; index < left.segments.size(); ++index) {
    const auto& a = left.segments[index];
    const auto& b = right.segments[index];
    if (a.kind != b.kind || a.opcode != b.opcode ||
        a.malformed != b.malformed || !same_slice(a.source, b.source))
      return false;
  }
  return true;
}

} // namespace

std::optional<FixedProseTopicIR> extract_fixed_prose_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const VerifiedOwnershipIR& verified_ownership,
    std::string* error) {
  const auto reject =
      [&](std::string message) -> std::optional<FixedProseTopicIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (records.size() != 1)
    return reject("fixed prose topic must occupy exactly one logical record");

  std::string verification_error;
  if (!verify_layout_ir(records, layout, &verification_error) ||
      !ownership_verified_for(verified_ownership, records, layout,
                              &verification_error))
    return reject("source layout/ownership is not canonical: " +
                  verification_error);

  const auto& record = records.front();
  const std::vector<BookControlKind> required = {
      BookControlKind::topic_start, BookControlKind::topic_number,
      BookControlKind::parent, BookControlKind::forward_level,
      BookControlKind::back_level, BookControlKind::summary,
      BookControlKind::heading_level, BookControlKind::source_file};
  if (record.control_segments.size() != required.size() + 1 &&
      record.control_segments.size() != required.size() + 2)
    return reject(
        "topic has controls or content outside the fixed prose envelope");
  for (std::size_t index = 0; index < required.size(); ++index) {
    const auto& segment = record.control_segments[index];
    if (segment.kind != required[index])
      return reject("topic metadata controls are incomplete or out of order");
    if (segment.payload_range.begin != segment.payload_range.end)
      return reject("topic metadata control contains trailing content");
    if (segment.malformed &&
        segment.kind != BookControlKind::forward_level &&
        segment.kind != BookControlKind::back_level)
      return reject("topic metadata control is malformed");
  }

  auto title_index = required.size();
  std::optional<FixedProseAnchorIR> anchor;
  if (record.control_segments.size() == required.size() + 2) {
    const auto& segment = record.control_segments[title_index++];
    const auto opcode = ascii_lower(segment.opcode);
    if (segment.kind != BookControlKind::structural || segment.malformed ||
        opcode.rfind("sr", 0) != 0 ||
        segment.payload_range.begin != segment.payload_range.end)
      return reject("pre-prose segment is not a source anchor");
    const auto id = segment.opcode.substr(2);
    if (!valid_anchor_id(id))
      return reject("fixed prose source anchor is empty or invalid");
    anchor = FixedProseAnchorIR{
        id, source_slice(record, segment.segment_index, segment.complete)};
  }
  const auto& title_segment = record.control_segments[title_index];
  if (title_segment.kind != BookControlKind::title || title_segment.malformed ||
      title_index + 1 != record.control_segments.size())
    return reject("ST prose segment is not the terminal topic segment");

  auto heading_level = ascii_lower(trim_ascii(range_text(
      record, record.control_segments[6].operand_range)));
  if (!heading_level.empty() && heading_level.front() == ':')
    heading_level.erase(heading_level.begin());
  if (heading_level.size() != 2 || heading_level.front() != 'h' ||
      heading_level.back() < '1' || heading_level.back() > '6')
    return reject("fixed prose topic heading level is invalid");

  auto prose = extract_fixed_prose_ir(records, layout, verified_ownership,
                                      &verification_error);
  if (!prose)
    return reject("inner fixed prose rejected: " + verification_error);
  if (prose->logical_record != record.logical_record ||
      prose->segment_index != title_segment.segment_index)
    return reject("inner fixed prose does not own the terminal ST segment");

  FixedProseTopicIR result;
  result.logical_record = record.logical_record;
  result.payload_bytes = record.ir.payload_range;
  result.token_count = record.ir.tokens.size();
  result.heading_level = std::move(heading_level);
  result.anchor = std::move(anchor);
  result.prose = std::move(*prose);
  result.heading_source = source_slice(
      record, result.prose.segment_index, result.prose.title_range);
  result.paragraph_source = source_slice(
      record, result.prose.segment_index, result.prose.body_range);
  if (!has_source(result.heading_source) ||
      !has_source(result.paragraph_source) ||
      (result.anchor && !has_source(result.anchor->source)))
    return reject("fixed prose semantic source provenance is incomplete");
  for (const auto& segment : record.control_segments) {
    auto source =
        source_slice(record, segment.segment_index, segment.complete);
    if (!has_source(source))
      return reject("fixed prose envelope segment provenance is incomplete");
    result.segments.push_back(
        {segment.kind, segment.opcode, segment.malformed, std::move(source)});
  }

  if (error != nullptr) error->clear();
  return result;
}

bool verify_fixed_prose_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const VerifiedOwnershipIR& ownership,
    const FixedProseTopicIR& topic, std::string* error) {
  if (!verify_fixed_prose_ir(records, layout, ownership, topic.prose, error))
    return false;
  const auto canonical =
      extract_fixed_prose_topic_ir(records, layout, ownership, error);
  if (!canonical) return false;
  if (!same_topic(*canonical, topic))
    return fail(error, "fixed prose topic differs from canonical extraction");
  if (error != nullptr) error->clear();
  return true;
}


} // namespace geist::detail
