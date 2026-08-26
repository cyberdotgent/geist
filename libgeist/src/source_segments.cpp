#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace geist::detail {

namespace {

bool excluded_selector_projection_target(const std::string& target) {
  const auto lower = ascii_lower(target);
  return ascii_starts_with_case_insensitive(lower, "lnk") ||
         ascii_starts_with_case_insensitive(lower, "pic") ||
         ascii_starts_with_case_insensitive(lower, "ftnftn") ||
         ascii_starts_with_case_insensitive(lower, "figlist") ||
         ascii_starts_with_case_insensitive(lower, "tlist");
}

bool exact_decimal(const std::string& word, std::size_t expected) {
  if (word.empty()) return false;
  std::size_t value = 0;
  for (const auto character : word) {
    if (std::isdigit(static_cast<unsigned char>(character)) == 0) return false;
    const auto digit = static_cast<std::size_t>(character - '0');
    if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10)
      return false;
    value = value * 10 + digit;
  }
  return value == expected;
}

bool decoded_selector_matches(const DecodedMarkupSegmentSpan& segment,
                              const SelectorIR& selector) {
  if (!selector.canonical_operands)
    return ascii_starts_with_case_insensitive(segment.text, "cselect");
  std::istringstream input(segment.text);
  std::string opcode;
  std::string column;
  std::string length;
  std::string target;
  return input >> opcode >> column >> length >> target &&
         ascii_equals_case_insensitive(opcode, "cselect") &&
         exact_decimal(column, selector.column) &&
         exact_decimal(length, selector.length) && target == selector.target;
}

} // namespace

bool output_spans_intersect(std::size_t left_begin, std::size_t left_end,
                            std::size_t right_begin, std::size_t right_end) {
  return left_begin < right_end && right_begin < left_end;
}

std::vector<std::size_t> source_tokens_intersecting_output(
    const AssembledLogicalRecord& assembled, std::size_t output_begin,
    std::size_t output_end) {
  std::vector<std::size_t> result;
  if (output_begin >= output_end || output_begin >= assembled.words.size()) {
    return result;
  }
  output_end = std::min(output_end, assembled.words.size());
  for (const auto& token : assembled.tokens) {
    if (output_spans_intersect(output_begin, output_end, token.output_begin,
                               token.output_end)) {
      result.push_back(token.token_index);
    }
  }
  return result;
}

std::vector<std::string> clean_source_owned_selector_display_markers(
    const std::vector<std::string>& decoded_records,
    const std::vector<DecodedLogicalRecordSource>& sources) {
  auto cleaned = decoded_records;
  if (decoded_records.size() != sources.size()) return cleaned;
  const auto catalog = extract_selector_catalog_ir(sources);
  std::string selector_error;
  if (!catalog ||
      !verify_selector_catalog_ir(sources, *catalog, &selector_error))
    return cleaned;
  std::vector<std::vector<std::pair<std::size_t, std::size_t>>> all_slots(
      cleaned.size());
  for (std::size_t record_index = 0; record_index < cleaned.size();
       ++record_index) {
    const auto& source = sources[record_index];
    // Preserve the compatibility renderer's historical ownership boundary.
    // Selector IR still records these controls; this projection does not
    // consume marker-looking text in any record containing a table start.
    if (ascii_lower(decoded_records[record_index]).find("srtbl") !=
        std::string::npos)
      continue;
    const auto decoded_segments = split_decoded_markup_segment_spans(
        decoded_records[record_index]);
    std::vector<const DecodedMarkupSegmentSpan*> decoded_selectors;
    for (const auto& candidate : decoded_segments) {
      if (ascii_starts_with_case_insensitive(candidate.text, "cselect ")) {
        decoded_selectors.push_back(&candidate);
      }
    }
    std::vector<const SelectorIR*> source_selectors;
    for (const auto& selector : catalog->selectors)
      if (selector.logical_record == source.logical_record)
        source_selectors.push_back(&selector);
    // Normalization may split non-selector material differently, but selector
    // pairing itself must be unique and complete within the record.
    if (source_selectors.size() != decoded_selectors.size()) {
      return decoded_records;
    }
    auto& marker_slots = all_slots[record_index];
    for (std::size_t selector_index = 0;
         selector_index < source_selectors.size(); ++selector_index) {
      const auto& selector = *source_selectors[selector_index];
      const auto& decoded_segment = *decoded_selectors[selector_index];
      if (!decoded_selector_matches(decoded_segment, selector))
        return decoded_records;
      if (!selector.canonical_operands || selector.inside_table ||
          excluded_selector_projection_target(selector.target) ||
          !selector.display_marker_slot)
        continue;
      const auto& marker = *selector.display_marker_slot;
      auto decoded_cursor = decoded_segment.output_begin;
      for (int word = 0; word < 4; ++word) {
        while (decoded_cursor < decoded_segment.output_end &&
               std::isspace(static_cast<unsigned char>(
                   decoded_records[record_index][decoded_cursor])) != 0) {
          ++decoded_cursor;
        }
        while (decoded_cursor < decoded_segment.output_end &&
               std::isspace(static_cast<unsigned char>(
                   decoded_records[record_index][decoded_cursor])) == 0) {
          ++decoded_cursor;
        }
      }
      while (decoded_cursor < decoded_segment.output_end &&
             std::isspace(static_cast<unsigned char>(
                 decoded_records[record_index][decoded_cursor])) != 0) {
        ++decoded_cursor;
      }
      const auto decoded_marker_end =
          decoded_cursor + marker.decoded_text.size();
      if (decoded_marker_end > decoded_segment.output_end ||
          decoded_records[record_index].compare(
              decoded_cursor, marker.decoded_text.size(),
              marker.decoded_text) != 0) {
        return decoded_records;
      }
      marker_slots.emplace_back(decoded_cursor, decoded_marker_end);
    }
  }
  for (std::size_t record_index = 0; record_index < cleaned.size();
       ++record_index) {
    auto& marker_slots = all_slots[record_index];
    std::sort(marker_slots.rbegin(), marker_slots.rend());
    for (const auto& [begin, end] : marker_slots) {
      // Canonical '?' is not blanked before selector row reconstruction.  It
      // retains the native one-cell marker slot while the existing fixed-row
      // path consumes it and its guard blank. This preserves CSELECT's
      // absolute column and length without exposing the decoded dictionary
      // alias or punctuation glyph.
      cleaned[record_index].replace(begin, end - begin, "?");
    }
  }
  return cleaned;
}

} // namespace geist::detail
