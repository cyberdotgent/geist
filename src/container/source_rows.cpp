#include "geist/detail/container/source_rows.hpp"

#include <string>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <set>

namespace geist::detail {
namespace {

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
      // The width is the token's own, so this asks only that every word in it
      // is a space -- an origin run of any width, the empty token included.
      if (!origin_kind(record.tokens[token].size(), content_origin,
                       continuation_origins, continuation) ||
          !space_run_of_width(record.tokens[token],
                              record.tokens[token].size()) ||
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

} // namespace geist::detail
