#include "geist/detail/source_rows.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace geist::detail {
namespace {

bool exact_spaces(const TokenWords& words, std::size_t count) {
  return words.size() == count &&
         std::all_of(words.begin(), words.end(),
                     [](std::uint16_t word) { return word == ' '; });
}

bool marker_glyph(const TokenWords& words) {
  return words.size() == 1 &&
         std::string("$;()*!-':=<>/\"").find(
             static_cast<char>(words.front())) !=
             std::string::npos;
}

bool all_spaces(const TokenWords& words) {
  return !words.empty() &&
         std::all_of(words.begin(), words.end(),
                     [](const auto word) { return word == ' '; });
}

bool marker_field(const TokenWords& words) {
  return !words.empty() && words.size() <= 24 &&
         std::none_of(words.begin(), words.end(), [](const auto word) {
           return word < 0x20 || word == ' ' || word == 0x2666;
         });
}

std::string visible_token(const TokenWords& words) {
  TokenWords visible;
  for (const auto word : words) {
    if (word >= 0x20 && word != 0x2666) {
      visible.push_back(word);
    }
  }
  auto text = token_words_to_ascii(visible);
  const auto non_space = [](unsigned char ch) {
    return std::isspace(ch) == 0;
  };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), non_space));
  text.erase(std::find_if(text.rbegin(), text.rend(), non_space).base(),
             text.end());
  return text;
}

SourceRowBoundaryEvidence evidence_for(const std::string& marker) {
  if (!marker.empty() &&
      std::all_of(marker.begin(), marker.end(),
                  [](char ch) { return ch == '?'; })) {
    return SourceRowBoundaryEvidence::question_run;
  }
  if (!marker.empty() &&
      std::all_of(marker.begin(), marker.end(), [](char ch) {
        return ch == '|' || ch == '-' || ch == '+' || ch == '=';
      })) {
    return SourceRowBoundaryEvidence::separator;
  }
  return SourceRowBoundaryEvidence::compact_marker;
}

bool decimal(const std::string& value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](const char ch) {
           return std::isdigit(static_cast<unsigned char>(ch)) != 0;
         });
}

bool decimal_or_range(const std::string& value) {
  const auto dash = value.find('-');
  return decimal(value) ||
         (dash != std::string::npos && value.find('-', dash + 1) ==
                                          std::string::npos &&
          decimal(value.substr(0, dash)) && decimal(value.substr(dash + 1)));
}

std::string visible_slice(const DecodedLogicalRecordSource& record,
                          std::size_t begin,
                          std::size_t end) {
  std::vector<TokenWords> tokens(record.tokens.begin() + begin,
                                 record.tokens.begin() + end);
  const auto words = assemble_logical_record(tokens);
  TokenWords visible;
  for (const auto word : words) {
    if (word >= 0x20 && word != 0x2666) {
      visible.push_back(word);
    }
  }
  auto text = token_words_to_ascii(visible);
  while (!text.empty() && std::isspace(
                              static_cast<unsigned char>(text.back())) != 0) {
    text.pop_back();
  }
  return text;
}

bool origin_kind(std::size_t origin,
                 std::size_t content_origin,
                 const std::vector<std::size_t>& continuation_origins,
                 bool& continuation) {
  if (origin == content_origin) {
    continuation = false;
    return true;
  }
  continuation = std::find(continuation_origins.begin(),
                           continuation_origins.end(), origin) !=
                 continuation_origins.end();
  return continuation;
}

} // namespace

std::vector<FixedSourceRow> slice_fixed_source_rows(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::size_t content_origin,
    const std::vector<std::size_t>& continuation_origins) {
  std::vector<FixedSourceRow> rows;
  for (const auto& record : records) {
    struct Boundary {
      std::size_t marker = 0;
      std::size_t origin = 0;
      bool continuation = false;
    };
    std::vector<Boundary> boundaries;
    const auto count = std::min(record.tokens.size(),
                                record.encoded_tokens.size());
    for (std::size_t token = 1; token < count; ++token) {
      bool continuation = false;
      if (!origin_kind(record.tokens[token].size(), content_origin,
                       continuation_origins, continuation) ||
          !exact_spaces(record.tokens[token], record.tokens[token].size()) ||
          record.encoded_tokens[token - 1].width != 1) {
        continue;
      }
      const auto marker = visible_token(record.tokens[token - 1]);
      if (marker.empty()) {
        continue;
      }
      boundaries.push_back({token - 1, token, continuation});
    }
    for (std::size_t index = 0; index < boundaries.size(); ++index) {
      const auto& boundary = boundaries[index];
      const auto end = index + 1 < boundaries.size()
                           ? boundaries[index + 1].marker
                           : record.tokens.size();
      if (end <= boundary.origin) {
        continue;
      }
      FixedSourceRow row;
      row.logical_record = record.logical_record;
      row.origin = record.tokens[boundary.origin].size();
      row.continuation = boundary.continuation;
      // Retain the origin token while assembling because it carries spacing
      // state for the next token, then remove its exact visible prefix.
      row.text = visible_slice(record, boundary.origin, end);
      row.text.erase(0, std::min(row.origin, row.text.size()));
      row.marker.logical_record = record.logical_record;
      row.marker.token_index = boundary.marker;
      row.marker.origin_token = boundary.origin;
      row.marker.encoded_value = record.encoded_tokens[boundary.marker].value;
      row.marker.text = visible_token(record.tokens[boundary.marker]);
      row.marker.evidence = evidence_for(row.marker.text);
      rows.push_back(std::move(row));
    }
  }
  return rows;
}

std::vector<SourceRowMarker> source_row_markers(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::size_t key_origin) {
  std::vector<SourceRowMarker> markers;
  for (const auto& row : slice_fixed_source_rows(records, key_origin)) {
    auto following = row.text;
    const auto end = following.find_first_of(" \t\r\n");
    if (end != std::string::npos) {
      following.erase(end);
    }
    if (!following.empty() && row.marker.text.size() <= 24) {
      markers.push_back(
          {row.marker.text, std::move(following), row.marker});
    }
  }
  return markers;
}

bool has_semantic_srmsg_source_candidate(
    const std::vector<std::string>& decoded_records) {
  auto saw_semantic_message = false;
  auto saw_font_row = false;
  for (const auto& record : decoded_records) {
    const auto lower = ascii_lower(record);
    saw_font_row = saw_font_row || lower.find("cfont ") != std::string::npos;
    for (auto at = lower.find("srmsg "); at != std::string::npos;
         at = lower.find("srmsg ", at + 6)) {
      auto value = at + 6;
      while (value < lower.size() &&
             std::isspace(static_cast<unsigned char>(lower[value])) != 0) {
        ++value;
      }
      const auto operand_begin = value;
      while (value < record.size() &&
             std::isspace(static_cast<unsigned char>(record[value])) == 0) {
        ++value;
      }
      const auto operand = record.substr(operand_begin, value - operand_begin);
      const auto numeric_or_range = decimal_or_range(operand);
      const auto symbolic = std::any_of(
          operand.begin(), operand.end(), [](const char ch) {
            return std::islower(static_cast<unsigned char>(ch)) != 0;
          });
      if (numeric_or_range || symbolic) {
        saw_semantic_message = true;
        break;
      }
    }
  }
  return saw_semantic_message && saw_font_row;
}

void project_semantic_srmsg_source_markers(
    std::vector<std::string>& rendered,
    const std::vector<std::string>& decoded_records,
    const std::vector<DecodedLogicalRecordSource>& sources) {
  if (!has_semantic_srmsg_source_candidate(decoded_records)) {
    return;
  }
  for (const auto& row : slice_fixed_source_rows(sources, 3)) {
    if (row.marker.text.empty() || row.text.empty() ||
        !std::all_of(row.marker.text.begin(), row.marker.text.end(),
                     [](char ch) {
                       return std::isalpha(static_cast<unsigned char>(ch)) != 0;
                     })) {
      continue;
    }
    const auto following = row.text.substr(0, row.text.find_first_of(" \t\r\n"));
    if (following.empty()) {
      continue;
    }
    const auto needle = row.marker.text + " " + following;
    auto projected = false;
    for (auto& line : rendered) {
      for (auto at = line.find(needle); at != std::string::npos;
           at = line.find(needle, at)) {
        const auto left_boundary = at == 0 ||
            std::isalnum(static_cast<unsigned char>(line[at - 1])) == 0;
        const auto after = at + needle.size();
        const auto right_boundary = after == line.size() ||
            std::isalnum(static_cast<unsigned char>(line[after])) == 0;
        if (!left_boundary || !right_boundary) {
          ++at;
          continue;
        }
        line.erase(at, row.marker.text.size() + 1);
        projected = true;
        break;
      }
      if (projected) {
        break;
      }
    }
  }
}

std::vector<std::string> clean_source_owned_toc_title_markers(
    const std::vector<std::string>& decoded_records,
    const std::vector<DecodedLogicalRecordSource>& sources) {
  auto cleaned = decoded_records;
  std::set<std::uint16_t> learned;
  for (const auto& source : sources) {
    const auto count = std::min(source.tokens.size(),
                                source.encoded_tokens.size());
    for (std::size_t token = 0; token < count; ++token) {
      if (source.encoded_tokens[token].width == 1 &&
          marker_glyph(source.tokens[token])) {
        learned.insert(source.encoded_tokens[token].value);
      }
    }
  }
  for (std::size_t record = 0;
       record < sources.size() && record < cleaned.size(); ++record) {
    const auto& source = sources[record];
    const auto count = std::min({source.tokens.size(),
                                 source.encoded_tokens.size(),
                                 source.assembled.tokens.size()});
    std::vector<std::pair<std::size_t, std::size_t>> removals;
    for (std::size_t token = 0; token < count; ++token) {
      if (source.encoded_tokens[token].width != 1 ||
          learned.count(source.encoded_tokens[token].value) == 0 ||
          !marker_glyph(source.tokens[token])) {
        continue;
      }
      const auto& span = source.assembled.tokens[token];
      auto next = span.output_end;
      while (next < source.assembled.words.size() &&
             std::isspace(static_cast<unsigned char>(
                 source.assembled.words[next])) != 0) {
        ++next;
      }
      const auto tail = ascii_lower(token_words_to_ascii(TokenWords(
          source.assembled.words.begin() + next,
          source.assembled.words.end())));
      if (tail.rfind("ctoce ", 0) != 0 && tail.rfind("ctocdef=", 0) != 0 &&
          tail.rfind("etoc", 0) != 0) {
        continue;
      }
      const auto prefix =
          ascii_lower(cleaned[record].substr(0, span.output_begin));
      if (prefix.rfind("ctoce ") == std::string::npos) {
        continue;
      }
      removals.push_back({span.output_begin, span.output_end});
    }
    for (auto removal = removals.rbegin(); removal != removals.rend();
         ++removal) {
      cleaned[record].erase(removal->first, removal->second - removal->first);
    }
  }
  return cleaned;
}

std::vector<std::string> project_source_owned_st_prose_rows(
    const std::vector<std::string>& decoded_records,
    const std::vector<DecodedLogicalRecordSource>& sources) {
  if (decoded_records.size() != sources.size()) {
    return decoded_records;
  }

  struct Candidate {
    std::size_t record = 0;
    DecodedMarkupSegmentSpan source_segment;
    DecodedMarkupSegmentSpan decoded_segment;
    std::size_t body_begin = 0;
    std::vector<std::pair<std::size_t, std::size_t>> marker_rows;
  } candidate;
  auto st_count = std::size_t{0};

  for (std::size_t record = 0; record < sources.size(); ++record) {
    const auto source_text =
        token_words_to_ascii(sources[record].assembled.words);
    const auto source_segments = split_decoded_markup_segment_spans(source_text);
    const auto decoded_segments =
        split_decoded_markup_segment_spans(decoded_records[record]);
    if (source_segments.size() != decoded_segments.size()) {
      return decoded_records;
    }
    for (std::size_t segment = 0; segment < source_segments.size(); ++segment) {
      if (!ascii_starts_with_case_insensitive(source_segments[segment].text,
                                              "st ")) {
        continue;
      }
      ++st_count;
      if (st_count != 1 ||
          !ascii_starts_with_case_insensitive(decoded_segments[segment].text,
                                              "st ") ||
          source_segments[segment].text != decoded_segments[segment].text) {
        return decoded_records;
      }
      if (annotate_decoded_placeholders(decoded_segments[segment].text).find(
              "<geist-placeholder") != std::string::npos) {
        return decoded_records;
      }
      auto question_run = std::size_t{0};
      for (const auto ch : decoded_segments[segment].text) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20) {
          return decoded_records;
        }
        question_run = ch == '?' ? question_run + 1 : 0;
        if (question_run >= 5) {
          return decoded_records;
        }
      }
      candidate.record = record;
      candidate.source_segment = source_segments[segment];
      candidate.decoded_segment = decoded_segments[segment];

      const auto& source = sources[record];
      const auto owned = source_tokens_intersecting_output(
          source.assembled, candidate.source_segment.output_begin,
          candidate.source_segment.output_end);
      if (owned.empty() ||
          std::any_of(owned.begin(), owned.end(), [&](const auto token) {
            return token >= source.tokens.size() ||
                   token >= source.encoded_tokens.size() ||
                   token >= source.assembled.tokens.size();
          })) {
        return decoded_records;
      }
      for (const auto token : owned) {
        if (source.tokens[token].size() == 1 &&
            (source.tokens[token].front() == 0x03 ||
             source.tokens[token].front() == 0x2666)) {
          return decoded_records;
        }
      }
      auto first_marker = source.tokens.size();
      for (std::size_t at = 0; at + 1 < owned.size(); ++at) {
        const auto marker = owned[at];
        const auto origin = owned[at + 1];
        if (origin != marker + 1 || origin >= source.tokens.size() ||
            origin >= source.encoded_tokens.size() ||
            source.encoded_tokens[marker].width != 1 ||
            source.encoded_tokens[origin].width != 1 ||
            !marker_field(source.tokens[marker]) ||
            !exact_spaces(source.tokens[origin], 3)) {
          continue;
        }
        if (at + 2 >= owned.size() || owned[at + 2] != origin + 1 ||
            all_spaces(source.tokens[owned[at + 2]])) {
          continue;
        }
        const auto begin = source.assembled.tokens[marker].output_begin;
        const auto end = source.assembled.tokens[origin].output_end;
        if (begin < candidate.source_segment.output_begin ||
            end > candidate.source_segment.output_end) {
          return decoded_records;
        }
        first_marker = std::min(first_marker, marker);
        candidate.marker_rows.emplace_back(begin, end);
      }
      if (candidate.marker_rows.size() < 2) {
        return decoded_records;
      }

      // The title/body transition is the unique wide source padding before
      // the first proven row marker. Include adjacent space tokens so no
      // residual display origin remains in the paragraph.
      std::vector<std::pair<std::size_t, std::size_t>> body_gaps;
      for (std::size_t at = 0; at < owned.size(); ++at) {
        const auto token = owned[at];
        if (token >= first_marker || token >= source.tokens.size() ||
            source.encoded_tokens[token].width != 1 ||
            source.tokens[token].size() < 10 ||
            !all_spaces(source.tokens[token])) {
          continue;
        }
        auto end_at = at + 1;
        while (end_at < owned.size() && owned[end_at] < first_marker &&
               all_spaces(source.tokens[owned[end_at]])) {
          ++end_at;
        }
        const auto begin = source.assembled.tokens[token].output_begin;
        const auto end = source.assembled.tokens[owned[end_at - 1]].output_end;
        body_gaps.emplace_back(begin, end);
      }
      if (body_gaps.size() != 1) {
        return decoded_records;
      }
      if (body_gaps.front().second - body_gaps.front().first < 9) {
        return decoded_records;
      }
      candidate.body_begin = body_gaps.front().first;
      candidate.marker_rows.push_back(body_gaps.front());
    }
  }
  if (st_count != 1) {
    return decoded_records;
  }

  auto projected = decoded_records;
  auto edits = candidate.marker_rows;
  std::sort(edits.rbegin(), edits.rend());
  for (const auto& [source_begin, source_end] : edits) {
    const auto relative_begin =
        source_begin - candidate.source_segment.output_begin;
    const auto relative_end = source_end - candidate.source_segment.output_begin;
    const auto decoded_begin = candidate.decoded_segment.output_begin + relative_begin;
    const auto decoded_end = candidate.decoded_segment.output_begin + relative_end;
    const auto length = decoded_end - decoded_begin;
    auto replacement = std::string(length, ' ');
    if (source_begin == candidate.body_begin) {
      replacement.replace(0, 9, " c.cp 0: ");
    }
    projected[candidate.record].replace(decoded_begin,
                                        length,
                                        replacement);
  }
  return projected;
}

} // namespace geist::detail
