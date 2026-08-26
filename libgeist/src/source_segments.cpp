#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>

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
    std::vector<std::pair<std::size_t, std::size_t>> removals;
    std::size_t selector_index = 0;
    for (const auto& segment : source_segments) {
      if (!ascii_starts_with_case_insensitive(segment.text, "cselect ")) {
        continue;
      }
      if (selector_index >= decoded_selectors.size()) {
        break;
      }
      const auto& decoded_segment = *decoded_selectors[selector_index++];
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
             std::isalpha(static_cast<unsigned char>(
                 assembled_text[cursor])) != 0) {
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
          source.tokens[origin_token].size() != 3 ||
          !std::all_of(source.tokens[origin_token].begin(),
                       source.tokens[origin_token].end(),
                       [](const auto word) { return word == ' '; }) ||
          source.assembled.tokens[origin_token].output_begin !=
              marker_span_end) {
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
      removals.emplace_back(decoded_cursor, decoded_marker_end);
    }
    std::sort(removals.rbegin(), removals.rend());
    for (const auto& [begin, end] : removals) {
      cleaned[record_index].erase(begin, end - begin);
    }
  }
  return cleaned;
}

} // namespace geist::detail
