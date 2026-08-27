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
  if (decoded_records.size() != sources.size()) return decoded_records;
  const auto layout = extract_layout_ir(sources);
  const auto ownership = build_ownership_ir(sources, layout);
  const auto prose = extract_fixed_prose_ir(sources, layout, ownership);
  if (!prose) return decoded_records;
  const auto source = std::find_if(
      sources.begin(), sources.end(), [&](const auto& record) {
        return record.logical_record == prose->logical_record;
      });
  if (source == sources.end()) return decoded_records;
  const auto record = static_cast<std::size_t>(source - sources.begin());
  if (token_words_to_ascii(source->assembled.words) != decoded_records[record])
    return decoded_records;

  auto projected = decoded_records;
  std::vector<OutputRangeIR> edits;
  edits.reserve(prose->rows.size() + 1);
  for (const auto& row : prose->rows) edits.push_back(row.projected_range);
  edits.push_back(prose->title_body_boundary);
  std::sort(edits.begin(), edits.end(), [](const auto& left,
                                           const auto& right) {
    return left.begin > right.begin;
  });
  for (const auto& edit : edits) {
    if (edit.begin > edit.end || edit.end > projected[record].size())
      return decoded_records;
    const auto length = edit.end - edit.begin;
    auto replacement = std::string(length, ' ');
    if (edit.begin == prose->title_body_boundary.begin)
      replacement.replace(0, 9, " c.cp 0: ");
    projected[record].replace(edit.begin, length, replacement);
  }
  return projected;
}

} // namespace geist::detail
