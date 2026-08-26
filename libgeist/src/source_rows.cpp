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

bool has_numeric_srmsg_source_candidate(
    const std::vector<std::string>& decoded_records) {
  auto saw_numeric_message = false;
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
      if (value < lower.size() &&
          std::isdigit(static_cast<unsigned char>(lower[value])) != 0) {
        saw_numeric_message = true;
        break;
      }
    }
  }
  return saw_numeric_message && saw_font_row;
}

void project_numeric_srmsg_source_markers(
    std::vector<std::string>& rendered,
    const std::vector<std::string>& decoded_records,
    const std::vector<DecodedLogicalRecordSource>& sources) {
  if (!has_numeric_srmsg_source_candidate(decoded_records)) {
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

} // namespace geist::detail
