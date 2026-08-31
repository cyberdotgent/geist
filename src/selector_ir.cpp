#include "geist/detail/selector_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace geist::detail {
namespace {

std::string range_text(const DecodedLogicalRecordSource& record,
                       const OutputRangeIR& range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size()) return {};
  return text.substr(range.begin, range.end - range.begin);
}

std::optional<SelectorMarkerIR> display_marker_slot(
    const DecodedLogicalRecordSource& record,
    const ControlSegmentIR& segment,
    std::size_t column,
    std::size_t length) {
  if (segment.payload_range.begin >= segment.payload_range.end)
    return std::nullopt;

  const auto text = token_words_to_ascii(record.assembled.words);
  auto cursor = segment.payload_range.begin;
  while (cursor < segment.payload_range.end &&
         std::isspace(static_cast<unsigned char>(text[cursor])) != 0)
    ++cursor;
  const auto marker_begin = cursor;
  while (cursor < segment.payload_range.end &&
         std::isspace(static_cast<unsigned char>(text[cursor])) == 0)
    ++cursor;
  const auto marker_end = cursor;
  auto display_begin = marker_end;
  while (display_begin < segment.payload_range.end &&
         text[display_begin] == ' ')
    ++display_begin;
  if (marker_begin == marker_end || display_begin - marker_end < 3 ||
      display_begin == segment.payload_range.end)
    return std::nullopt;

  const auto marker_and_origin_words = decoded_byte_range_to_word_range(
      record.assembled, {marker_begin, display_begin + 1});
  const auto marker_words = decoded_byte_range_to_word_range(
      record.assembled, {marker_begin, marker_end});
  const auto owned = source_tokens_intersecting_output(
      record.assembled, marker_and_origin_words.begin,
      marker_and_origin_words.end);
  if (owned.size() < 3) return std::nullopt;
  const auto marker_token = owned.front();
  if (marker_token >= record.tokens.size() ||
      marker_token >= record.encoded_tokens.size() ||
      marker_token >= record.assembled.tokens.size() ||
      record.encoded_tokens[marker_token].width != 1 ||
      record.assembled.tokens[marker_token].output_begin != marker_words.begin)
    return std::nullopt;
  const auto marker_span_end =
      record.assembled.tokens[marker_token].output_end;
  if (marker_span_end != marker_words.end &&
      !(marker_span_end == marker_words.end + 1 &&
        marker_words.end < record.assembled.words.size() &&
        record.assembled.words[marker_words.end] == ' '))
    return std::nullopt;

  const auto origin_token = marker_token + 1;
  if (origin_token >= record.tokens.size() ||
      origin_token >= record.encoded_tokens.size() ||
      origin_token >= record.assembled.tokens.size() ||
      record.encoded_tokens[origin_token].width != 1 ||
      record.tokens[origin_token].size() < 2 ||
      record.tokens[origin_token].size() > 32 ||
      !all_space_words(record.tokens[origin_token]) ||
      record.assembled.tokens[origin_token].output_begin != marker_span_end)
    return std::nullopt;

  const auto native_row_length = record.tokens[origin_token].size() +
                                 segment.payload_range.end - display_begin;
  if (column > native_row_length || length > native_row_length - column)
    return std::nullopt;

  const auto& encoded = record.encoded_tokens[marker_token];
  if (marker_token >= record.ir.tokens.size()) return std::nullopt;
  return SelectorMarkerIR{
      marker_token,
      origin_token,
      encoded.value,
      encoded.width,
      record.ir.tokens[marker_token].byte_range,
      {marker_begin, marker_end},
      text.substr(marker_begin, marker_end - marker_begin),
      record.tokens[origin_token].size(),
  };
}

bool parse_decimal(const std::string& word, std::size_t* value) {
  if (word.empty()) return false;
  std::size_t parsed = 0;
  for (const auto character : word) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isdigit(byte) == 0) return false;
    const auto digit = static_cast<std::size_t>(character - '0');
    if (parsed > (std::numeric_limits<std::size_t>::max() - digit) / 10)
      return false;
    parsed = parsed * 10 + digit;
  }
  *value = parsed;
  return true;
}

bool parse_selector_operands(const std::string& text, std::size_t* column,
                             std::size_t* length, std::string* target) {
  std::istringstream input(text);
  std::string column_word;
  std::string length_word;
  std::string parsed_target;
  std::string trailing;
  if (!(input >> column_word >> length_word >> parsed_target) ||
      input >> trailing)
    return false;
  std::size_t parsed_column = 0;
  std::size_t parsed_length = 0;
  if (!parse_decimal(column_word, &parsed_column) ||
      !parse_decimal(length_word, &parsed_length) || parsed_length == 0)
    return false;
  *column = parsed_column;
  *length = parsed_length;
  *target = std::move(parsed_target);
  return true;
}

bool marker_equal(const std::optional<SelectorMarkerIR>& left,
                  const std::optional<SelectorMarkerIR>& right) {
  if (left.has_value() != right.has_value()) return false;
  if (!left) return true;
  return left->token_index == right->token_index &&
         left->origin_token_index == right->origin_token_index &&
         left->encoded_value == right->encoded_value &&
         left->encoded_width == right->encoded_width &&
         left->byte_range.begin == right->byte_range.begin &&
         left->byte_range.end == right->byte_range.end &&
         left->output_range.begin == right->output_range.begin &&
         left->output_range.end == right->output_range.end &&
         left->decoded_text == right->decoded_text &&
         left->native_origin == right->native_origin;
}

bool byte_ranges_equal(const std::vector<SourceByteRange>& left,
                       const std::vector<SourceByteRange>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index)
    if (left[index].begin != right[index].begin ||
        left[index].end != right[index].end)
      return false;
  return true;
}

bool selector_equal(const SelectorIR& left, const SelectorIR& right) {
  return left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.selector_ordinal == right.selector_ordinal &&
         left.canonical_operands == right.canonical_operands &&
         left.rejection_reason == right.rejection_reason &&
         left.column == right.column && left.length == right.length &&
         left.target == right.target &&
         left.complete_range.begin == right.complete_range.begin &&
         left.complete_range.end == right.complete_range.end &&
         left.operand_range.begin == right.operand_range.begin &&
         left.operand_range.end == right.operand_range.end &&
         left.payload_range.begin == right.payload_range.begin &&
         left.payload_range.end == right.payload_range.end &&
         left.source_tokens == right.source_tokens &&
         byte_ranges_equal(left.source_byte_ranges,
                           right.source_byte_ranges) &&
         left.display_payload == right.display_payload &&
         left.inside_table == right.inside_table &&
         marker_equal(left.display_marker_slot, right.display_marker_slot);
}

} // namespace

std::optional<SelectorCatalogIR> extract_selector_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::string* error) {
  const auto fail =
      [&](const std::string& message) -> std::optional<SelectorCatalogIR> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  SelectorCatalogIR catalog;
  bool table_open = false;
  for (const auto& record : records) {
    std::size_t ordinal = 0;
    for (const auto& segment : record.control_segments) {
      if (segment.kind == BookControlKind::table_end) table_open = false;
      if (segment.kind == BookControlKind::table_start) table_open = true;
      if (segment.kind != BookControlKind::select) continue;

      SelectorIR selector;
      selector.logical_record = record.logical_record;
      selector.segment_index = segment.segment_index;
      selector.selector_ordinal = ordinal++;
      selector.complete_range = segment.complete;
      selector.operand_range = segment.operand_range;
      selector.payload_range = segment.payload_range;
      selector.source_tokens = segment.source_tokens;
      for (const auto token : segment.source_tokens) {
        if (token >= record.ir.tokens.size())
          return fail("selector source token is outside token IR");
        selector.source_byte_ranges.push_back(record.ir.tokens[token].byte_range);
      }
      selector.display_payload = range_text(record, segment.payload_range);
      selector.inside_table = table_open;
      if (segment.malformed) {
        selector.rejection_reason = "control operand count is incomplete";
      } else if (!parse_selector_operands(
                     range_text(record, segment.operand_range),
                     &selector.column, &selector.length, &selector.target)) {
        selector.rejection_reason = "operands are not canonical";
      } else {
        selector.canonical_operands = true;
        selector.display_marker_slot = display_marker_slot(
            record, segment, selector.column, selector.length);
      }
      catalog.selectors.push_back(std::move(selector));
    }
  }
  if (catalog.selectors.empty()) return fail("source contains no selectors");
  if (error != nullptr) error->clear();
  return catalog;
}

bool verify_selector_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const SelectorCatalogIR& catalog,
    std::string* error) {
  const auto fail = [&](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  const auto canonical = extract_selector_catalog_ir(records);
  if (!canonical) return fail("source does not admit canonical selector IR");
  if (canonical->selectors.size() != catalog.selectors.size())
    return fail("selector count differs from canonical lowering");
  for (std::size_t index = 0; index < catalog.selectors.size(); ++index)
    if (!selector_equal(canonical->selectors[index], catalog.selectors[index]))
      return fail("selector differs from canonical lowering at index " +
                  std::to_string(index));
  if (error != nullptr) error->clear();
  return true;
}

std::string format_selector_catalog_ir(const SelectorCatalogIR& catalog) {
  std::ostringstream out;
  out << "selector_catalog selectors=" << catalog.selectors.size() << '\n';
  for (const auto& selector : catalog.selectors) {
    out << "selector";
    if (!selector.canonical_operands)
      out << " rejected='" << selector.rejection_reason << "'";
    out << " target='" << selector.target << "' column=" << selector.column
        << " length=" << selector.length << " source="
        << selector.logical_record << ':' << selector.segment_index
        << " ordinal=" << selector.selector_ordinal
        << " operands=[" << selector.operand_range.begin << ','
        << selector.operand_range.end << ") payload=["
        << selector.payload_range.begin << ',' << selector.payload_range.end
        << ") table=" << (selector.inside_table ? "yes" : "no");
    if (selector.display_marker_slot) {
      const auto& marker = *selector.display_marker_slot;
      out << " marker='" << marker.decoded_text << "' token="
          << marker.token_index << " value=" << marker.encoded_value
          << " width=" << static_cast<unsigned>(marker.encoded_width)
          << " bytes=[" << marker.byte_range.begin << ','
          << marker.byte_range.end << ") origin=" << marker.native_origin;
    }
    out << '\n';
  }
  return out.str();
}

} // namespace geist::detail
