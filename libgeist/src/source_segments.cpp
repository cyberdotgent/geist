#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace geist::detail {

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
  const auto count = std::min(cleaned.size(), sources.size());
  bool source_table_open = false;
  for (std::size_t record_index = 0; record_index < count; ++record_index) {
    const auto& source = sources[record_index];
    const auto assembled_text = token_words_to_ascii(source.assembled.words);
    const auto source_segments =
        split_decoded_markup_segment_spans(assembled_text);
    const auto decoded_segments = split_decoded_markup_segment_spans(
        decoded_records[record_index]);
    std::vector<const DecodedMarkupSegmentSpan*> decoded_selectors;
    for (const auto& candidate : decoded_segments) {
      if (ascii_starts_with_case_insensitive(candidate.text, "cselect ")) {
        decoded_selectors.push_back(&candidate);
      }
    }
    std::vector<std::pair<const DecodedMarkupSegmentSpan*, bool>>
        source_selectors;
    for (const auto& candidate : source_segments) {
      if (ascii_starts_with_case_insensitive(candidate.text, "sretbl")) {
        source_table_open = false;
      } else if (ascii_starts_with_case_insensitive(candidate.text, "srtbl")) {
        source_table_open = true;
      }
      if (ascii_starts_with_case_insensitive(candidate.text, "cselect ")) {
        source_selectors.emplace_back(&candidate, source_table_open);
      }
    }
    // Normalization may split non-selector material differently, but selector
    // pairing itself must be unique and complete within the record.
    if (source_selectors.size() != decoded_selectors.size()) {
      continue;
    }
    std::vector<std::pair<std::size_t, std::size_t>> marker_slots;
    for (std::size_t selector_index = 0;
         selector_index < source_selectors.size(); ++selector_index) {
      const auto& [source_selector, selector_in_table] =
          source_selectors[selector_index];
      const auto& segment = *source_selector;
      const auto& decoded_segment = *decoded_selectors[selector_index];
      // Table composition owns cell boundaries and wrapped selector rows.
      // Rewriting a native marker there can change row/cell ownership, so the
      // prose selector projection must fail closed across logical records.
      if (selector_in_table) {
        continue;
      }
      std::istringstream fields(segment.text);
      std::string control;
      std::string target;
      std::size_t column = 0;
      std::size_t length = 0;
      if (!(fields >> control >> column >> length >> target) || length == 0) {
        continue;
      }
      const auto lower_target = ascii_lower(target);
      if (ascii_starts_with_case_insensitive(lower_target, "lnk") ||
          ascii_starts_with_case_insensitive(lower_target, "pic") ||
          ascii_starts_with_case_insensitive(lower_target, "ftnftn") ||
          ascii_starts_with_case_insensitive(lower_target, "figlist") ||
          ascii_starts_with_case_insensitive(lower_target, "tlist") ||
          ascii_lower(decoded_records[record_index]).find("srtbl") !=
              std::string::npos) {
        continue;
      }
      auto cursor = segment.output_begin;
      for (int word = 0; word < 4; ++word) {
        while (cursor < segment.output_end &&
               std::isspace(static_cast<unsigned char>(
                   assembled_text[cursor])) != 0) {
          ++cursor;
        }
        while (cursor < segment.output_end &&
               std::isspace(static_cast<unsigned char>(
                   assembled_text[cursor])) == 0) {
          ++cursor;
        }
      }
      while (cursor < segment.output_end &&
             std::isspace(static_cast<unsigned char>(
                 assembled_text[cursor])) != 0) {
        ++cursor;
      }
      const auto marker_begin = cursor;
      while (cursor < segment.output_end &&
             std::isspace(static_cast<unsigned char>(
                 assembled_text[cursor])) == 0) {
        ++cursor;
      }
      const auto marker_end = cursor;
      auto display_begin = marker_end;
      while (display_begin < segment.output_end &&
             assembled_text[display_begin] == ' ') {
        ++display_begin;
      }
      if (marker_begin == marker_end || display_begin - marker_end < 3 ||
          display_begin == segment.output_end) {
        continue;
      }
      const auto owned = source_tokens_intersecting_output(
          source.assembled, marker_begin, display_begin + 1);
      if (owned.size() < 3) {
        continue;
      }
      const auto marker_token = owned.front();
      if (marker_token >= source.encoded_tokens.size() ||
          marker_token >= source.assembled.tokens.size() ||
          source.encoded_tokens[marker_token].width != 1 ||
          source.assembled.tokens[marker_token].output_begin != marker_begin) {
        continue;
      }
      const auto marker_span_end = source.assembled.tokens[marker_token].output_end;
      if (marker_span_end != marker_end &&
          !(marker_span_end == marker_end + 1 &&
            assembled_text[marker_end] == ' ')) {
        continue;
      }
      const auto origin_token = marker_token + 1;
      if (origin_token >= source.tokens.size() ||
          origin_token >= source.encoded_tokens.size() ||
          origin_token >= source.assembled.tokens.size() ||
          source.encoded_tokens[origin_token].width != 1 ||
          source.tokens[origin_token].size() < 2 ||
          source.tokens[origin_token].size() > 32 ||
          !std::all_of(source.tokens[origin_token].begin(),
                       source.tokens[origin_token].end(),
                       [](const auto word) { return word == ' '; }) ||
          source.assembled.tokens[origin_token].output_begin !=
              marker_span_end) {
        continue;
      }
      const auto native_row_length = source.tokens[origin_token].size() +
                                     (segment.output_end - display_begin);
      if (column > native_row_length || length > native_row_length - column) {
        continue;
      }
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
      const auto decoded_marker_end = decoded_cursor +
                                      (marker_end - marker_begin);
      if (decoded_marker_end > decoded_segment.output_end ||
          decoded_records[record_index].compare(
              decoded_cursor, marker_end - marker_begin,
              assembled_text, marker_begin, marker_end - marker_begin) != 0) {
        continue;
      }
      marker_slots.emplace_back(decoded_cursor, decoded_marker_end);
    }
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
