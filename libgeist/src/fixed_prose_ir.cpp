#include "geist/detail/fixed_prose_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace geist::detail {
namespace {

bool all_spaces(const TokenWords& words) {
  return !words.empty() &&
         std::all_of(words.begin(), words.end(),
                     [](const auto word) { return word == ' '; });
}

bool exact_spaces(const TokenWords& words, std::size_t count) {
  return words.size() == count && all_spaces(words);
}

bool marker_field(const TokenWords& words) {
  return !words.empty() && words.size() <= 24 &&
         std::none_of(words.begin(), words.end(), [](const auto word) {
           return word < 0x20 || word == ' ' || word == 0x2666;
         });
}

std::string range_text(const DecodedLogicalRecordSource& record,
                       const OutputRangeIR& range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size()) return {};
  return text.substr(range.begin, range.end - range.begin);
}

bool printable(std::uint16_t word) {
  return word >= 0x20 && word != 0x2666 &&
         (word > 0xff ||
          std::isspace(static_cast<unsigned char>(word)) == 0);
}

const PhysicalRowIR* find_layout_row(const LayoutIR& layout,
                                     std::uint32_t logical_record,
                                     std::size_t segment_index,
                                     std::size_t marker_token,
                                     DisplayRunId& run_id,
                                     std::size_t& row_index) {
  for (const auto& run : layout.runs) {
    if (run.control_kind != BookControlKind::title) continue;
    for (std::size_t row = 0; row < run.rows.size(); ++row) {
      const auto& candidate = run.rows[row];
      if (candidate.logical_record == logical_record &&
          candidate.segment_index == segment_index && candidate.marker &&
          candidate.marker->token_index == marker_token) {
        run_id = run.id;
        row_index = row;
        return &candidate;
      }
    }
  }
  return nullptr;
}

bool owned_as(const OwnershipIR& ownership, std::uint32_t logical_record,
              std::size_t token, SourceDisposition disposition,
              DisplayRunId run, std::size_t row) {
  auto found = false;
  for (const auto& cell : ownership.cells) {
    if (cell.logical_record != logical_record || cell.token_index != token)
      continue;
    found = true;
    if (cell.disposition != disposition || cell.run != run ||
        cell.row_index != row)
      return false;
  }
  return found;
}

bool row_equal(const FixedProseRowIR& left, const FixedProseRowIR& right) {
  return left.run == right.run && left.row == right.row &&
         left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.marker_token == right.marker_token &&
         left.origin_token == right.origin_token &&
         left.marker_value == right.marker_value &&
         left.marker_width == right.marker_width &&
         left.marker_bytes.begin == right.marker_bytes.begin &&
         left.marker_bytes.end == right.marker_bytes.end &&
         left.origin_value == right.origin_value &&
         left.origin_width == right.origin_width &&
         left.origin_bytes.begin == right.origin_bytes.begin &&
         left.origin_bytes.end == right.origin_bytes.end &&
         left.projected_range.begin == right.projected_range.begin &&
         left.projected_range.end == right.projected_range.end;
}

bool prose_equal(const FixedProseIR& left, const FixedProseIR& right) {
  if (left.logical_record != right.logical_record ||
      left.segment_index != right.segment_index ||
      left.segment_range.begin != right.segment_range.begin ||
      left.segment_range.end != right.segment_range.end ||
      left.payload_range.begin != right.payload_range.begin ||
      left.payload_range.end != right.payload_range.end ||
      left.title_range.begin != right.title_range.begin ||
      left.title_range.end != right.title_range.end ||
      left.body_range.begin != right.body_range.begin ||
      left.body_range.end != right.body_range.end ||
      left.title_body_boundary.begin != right.title_body_boundary.begin ||
      left.title_body_boundary.end != right.title_body_boundary.end ||
      left.title != right.title || left.paragraph != right.paragraph ||
      left.rows.size() != right.rows.size())
    return false;
  for (std::size_t row = 0; row < left.rows.size(); ++row)
    if (!row_equal(left.rows[row], right.rows[row])) return false;
  return true;
}

} // namespace

std::optional<FixedProseIR> extract_fixed_prose_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const VerifiedOwnershipIR& verified_ownership,
    std::string* error) {
  const auto fail =
      [&](const std::string& message) -> std::optional<FixedProseIR> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  const OwnershipIR& ownership = verified_ownership;
  std::string verification_error;
  if (!verify_layout_ir(records, layout, &verification_error) ||
      !ownership_verified_for(verified_ownership, records, layout,
                              &verification_error))
    return fail("source layout/ownership is not canonical: " +
                verification_error);

  const DecodedLogicalRecordSource* source = nullptr;
  const ControlSegmentIR* title_segment = nullptr;
  for (const auto& record : records) {
    for (const auto& segment : record.control_segments) {
      if (segment.kind != BookControlKind::title) continue;
      if (title_segment != nullptr)
        return fail("multiple ST title segments cannot own one prose block");
      if (segment.malformed)
        return fail("malformed ST segment cannot own fixed prose");
      source = &record;
      title_segment = &segment;
    }
  }
  if (source == nullptr || title_segment == nullptr)
    return fail("source has no unique ST title/prose segment");

  const auto payload = range_text(*source, title_segment->payload_range);
  if (payload.empty() ||
      annotate_decoded_placeholders(payload).find("<geist-placeholder") !=
          std::string::npos)
    return fail("ST payload is empty or placeholder-framed");
  auto question_run = std::size_t{0};
  for (const auto ch : payload) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 0x20) return fail("ST payload contains a nested control");
    question_run = ch == '?' ? question_run + 1 : 0;
    if (question_run >= 5)
      return fail("ST payload is a fixed display, not ordinary prose");
  }

  const auto payload_words = decoded_byte_range_to_word_range(
      source->assembled, title_segment->payload_range);
  const auto owned = source_tokens_intersecting_output(
      source->assembled, payload_words.begin, payload_words.end);
  if (owned.empty()) return fail("ST payload has no source-token ownership");
  for (const auto token : owned) {
    if (token >= source->tokens.size() ||
        token >= source->encoded_tokens.size() ||
        token >= source->assembled.tokens.size() ||
        token >= source->ir.tokens.size())
      return fail("ST payload source vectors are incomplete");
    if (source->tokens[token].size() == 1 &&
        (source->tokens[token].front() == 0x03 ||
         source->tokens[token].front() == 0x2666))
      return fail("ST payload contains a semantic display frame");
  }

  FixedProseIR result;
  result.logical_record = source->logical_record;
  result.segment_index = title_segment->segment_index;
  result.segment_range = title_segment->complete;
  result.payload_range = title_segment->payload_range;
  auto first_marker = source->tokens.size();
  for (std::size_t at = 0; at + 2 < owned.size(); ++at) {
    const auto marker = owned[at];
    const auto origin = owned[at + 1];
    const auto following = owned[at + 2];
    if (origin != marker + 1 || following != origin + 1 ||
        source->encoded_tokens[marker].width != 1 ||
        source->encoded_tokens[origin].width != 1 ||
        !marker_field(source->tokens[marker]) ||
        !exact_spaces(source->tokens[origin], 3) ||
        all_spaces(source->tokens[following]))
      continue;
    const auto projected = decoded_word_range_to_byte_range(
        source->assembled,
        {source->assembled.tokens[marker].output_begin,
         source->assembled.tokens[origin].output_end});
    const auto begin = projected.begin;
    const auto end = projected.end;
    if (begin < title_segment->payload_range.begin ||
        end > title_segment->payload_range.end)
      return fail("fixed prose row escapes its ST payload");
    DisplayRunId run = 0;
    std::size_t row_index = 0;
    const auto* layout_row = find_layout_row(
        layout, source->logical_record, title_segment->segment_index, marker,
        run, row_index);
    if (layout_row == nullptr || layout_row->token_begin != marker ||
        !owned_as(ownership, source->logical_record, marker,
                  SourceDisposition::marker_slot, run, row_index) ||
        !owned_as(ownership, source->logical_record, origin,
                  SourceDisposition::layout_origin, run, row_index))
      return fail("fixed prose marker/origin lacks exclusive row ownership");
    first_marker = std::min(first_marker, marker);
    result.rows.push_back(
        {run,
         row_index,
         source->logical_record,
         title_segment->segment_index,
         marker,
         origin,
         source->encoded_tokens[marker].value,
         source->encoded_tokens[marker].width,
         source->ir.tokens[marker].byte_range,
         source->encoded_tokens[origin].value,
         source->encoded_tokens[origin].width,
         source->ir.tokens[origin].byte_range,
         {begin, end}});
  }
  if (result.rows.size() < 2)
    return fail("ST payload lacks repeated source-proven prose rows");

  std::vector<OutputRangeIR> body_gaps;
  for (std::size_t at = 0; at < owned.size(); ++at) {
    const auto token = owned[at];
    if (token >= first_marker || source->encoded_tokens[token].width != 1 ||
        source->tokens[token].size() < 10 ||
        !all_spaces(source->tokens[token]))
      continue;
    auto end_at = at + 1;
    while (end_at < owned.size() && owned[end_at] < first_marker &&
           all_spaces(source->tokens[owned[end_at]]))
      ++end_at;
    body_gaps.push_back(decoded_word_range_to_byte_range(
        source->assembled,
        {source->assembled.tokens[token].output_begin,
         source->assembled.tokens[owned[end_at - 1]].output_end}));
  }
  if (body_gaps.size() != 1 ||
      body_gaps.front().end - body_gaps.front().begin < 9)
    return fail("ST title/body boundary is absent or ambiguous");
  result.title_body_boundary = body_gaps.front();
  result.title_range = {title_segment->payload_range.begin,
                        result.title_body_boundary.begin};
  result.body_range = {result.title_body_boundary.end,
                       title_segment->payload_range.end};
  result.title = collapse_ascii_whitespace(
      trim_ascii(range_text(*source, result.title_range)));

  auto projected = range_text(*source, result.body_range);
  for (auto row = result.rows.rbegin(); row != result.rows.rend(); ++row) {
    if (row->projected_range.end <= result.body_range.begin) continue;
    const auto begin = row->projected_range.begin - result.body_range.begin;
    const auto end = row->projected_range.end - result.body_range.begin;
    if (begin > end || end > projected.size())
      return fail("fixed prose compatibility range is invalid");
    projected.replace(begin, end - begin, end - begin, ' ');
  }
  result.paragraph = collapse_ascii_whitespace(trim_ascii(std::move(projected)));
  if (result.title.empty() || result.paragraph.empty())
    return fail("fixed prose title or body is empty");

  // Every printable opaque cell in the admitted ST payload would otherwise
  // escape the semantic conservation partition. Padding may remain opaque.
  for (const auto& cell : ownership.cells) {
    if (cell.logical_record != source->logical_record ||
        cell.token_index >= source->assembled.tokens.size() ||
        cell.disposition != SourceDisposition::opaque ||
        !printable(cell.word))
      continue;
    const auto& span = source->assembled.tokens[cell.token_index];
    const auto bytes = decoded_word_range_to_byte_range(
        source->assembled, {span.output_begin, span.output_end});
    if (bytes.begin < title_segment->payload_range.end &&
        title_segment->payload_range.begin < bytes.end)
      return fail("ST payload contains unowned printable source content");
  }

  if (error != nullptr) error->clear();
  return result;
}

bool verify_fixed_prose_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const VerifiedOwnershipIR& ownership,
    const FixedProseIR& prose, std::string* error) {
  const auto canonical = extract_fixed_prose_ir(records, layout, ownership);
  if (!canonical) {
    if (error != nullptr) *error = "source does not admit fixed prose";
    return false;
  }
  if (!prose_equal(*canonical, prose)) {
    if (error != nullptr)
      *error = "fixed prose structure differs from canonical lowering";
    return false;
  }
  if (error != nullptr) error->clear();
  return true;
}

std::string format_fixed_prose_ir(const FixedProseIR& prose) {
  std::ostringstream out;
  out << "fixed_prose record=" << prose.logical_record
      << " segment=" << prose.segment_index << " title='" << prose.title
      << "' paragraph='" << prose.paragraph << "' boundary=["
      << prose.title_body_boundary.begin << ','
      << prose.title_body_boundary.end << ") rows=" << prose.rows.size()
      << '\n';
  for (const auto& row : prose.rows)
    out << "fixed_prose_row run=" << row.run << " row=" << row.row
        << " record=" << row.logical_record
        << " segment=" << row.segment_index
        << " marker_token=" << row.marker_token
        << " marker_value=" << row.marker_value
        << " marker_width=" << static_cast<unsigned>(row.marker_width)
        << " marker_bytes=[" << row.marker_bytes.begin << ','
        << row.marker_bytes.end << ") origin_token=" << row.origin_token
        << " origin_value=" << row.origin_value
        << " origin_width=" << static_cast<unsigned>(row.origin_width)
        << " origin_bytes=[" << row.origin_bytes.begin << ','
        << row.origin_bytes.end << ") projected=["
        << row.projected_range.begin << ',' << row.projected_range.end
        << ")\n";
  return out.str();
}

} // namespace geist::detail
