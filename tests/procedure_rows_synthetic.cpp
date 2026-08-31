// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/procedure_rows.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
  if (!value) { std::cerr << message << '\n'; geist_test::record_failure();
  return; }
}

geist::detail::DecodedLogicalRecordSource source(std::uint32_t number,
                                                  const std::string& text) {
  geist::detail::DecodedLogicalRecordSource result;
  result.logical_record = number;
  result.assembled.words.assign(text.begin(), text.end());
  std::vector<std::pair<std::size_t, std::size_t>> special;
  for (std::size_t at = 3; at + 1 < text.size(); ++at) {
    if (std::isdigit(static_cast<unsigned char>(text[at])) == 0) continue;
    auto end = at;
    while (end < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[end])) != 0) ++end;
    if (end >= text.size() || text[end] != '.' ||
        text.substr(at - 3, 3) != "   ") continue;
    special.push_back({at - 3, at});
    special.push_back({at, end + 1});
    at = end;
  }
  auto append = [&](std::size_t begin, std::size_t end) {
    if (begin == end) return;
    const auto token = result.tokens.size();
    result.tokens.emplace_back(text.begin() + begin, text.begin() + end);
    result.encoded_tokens.push_back({static_cast<std::uint16_t>(0x30 + token),
                                     1});
    result.assembled.tokens.push_back(
        {token, false, 3, begin, end, false});
  };
  auto cursor = std::size_t{0};
  for (const auto& [begin, end] : special) {
    append(cursor, begin);
    append(begin, end);
    cursor = end;
  }
  append(cursor, text.size());
  return result;
}

geist::detail::DecodedLogicalRecordSource flat_source(
    std::uint32_t number, const std::string& text) {
  geist::detail::DecodedLogicalRecordSource result;
  result.logical_record = number;
  result.tokens.emplace_back(text.begin(), text.end());
  result.encoded_tokens.push_back({0x34, 1});
  result.assembled.words.assign(text.begin(), text.end());
  result.assembled.tokens.push_back({0, false, 3, 0, text.size(), false});
  return result;
}
}

int main() {
  const std::vector<std::string> records{
      "ST Procedure cfont 3 2 2 >    1. First step cfont 9 4 E command",
      "cfont 3 2 2     2. Second step cfont 13 4 1 explanation "
      "cfont 3 2 2     3. Third step"};
  const std::vector<geist::detail::DecodedLogicalRecordSource> sources{
      source(10, records[0]), source(11, records[1])};
  const auto flags = geist::detail::numbered_procedure_step_segments(
      records, sources);
  require(flags.size() == 2, "procedure flag record shape was lost");
  auto count = std::size_t{0};
  for (const auto& record : flags)
    for (const auto flag : record) count += flag ? 1 : 0;
  require(count == 3, "increasing cross-record procedure was not detected");

  const std::vector<std::string> multi{
      "cfont 3 2 2    9. Ninth cfont 3 3 2    10. Tenth "
      "cfont 3 3 2    11. Eleventh"};
  const auto multi_flags = geist::detail::numbered_procedure_step_segments(
      multi, {source(14, multi[0])});
  require(std::count(multi_flags[0].begin(), multi_flags[0].end(), true) == 3,
          "multi-digit procedure sequence was not detected");

  const std::vector<std::string> missing{
      "cfont 3 2 2 1. First cfont 3 2 2 3. Third"};
  const auto missing_flags = geist::detail::numbered_procedure_step_segments(
      missing, {source(12, missing[0])});
  require(std::none_of(missing_flags[0].begin(), missing_flags[0].end(),
                       [](bool flag) { return flag; }),
          "non-increasing numbers manufactured a procedure");

  const std::vector<std::string> catalog{
      "SRMSG 1 cfont 3 2 2 1. First cfont 3 2 2 2. Second"};
  const auto catalog_flags = geist::detail::numbered_procedure_step_segments(
      catalog, {source(13, catalog[0])});
  require(std::none_of(catalog_flags[0].begin(), catalog_flags[0].end(),
                       [](bool flag) { return flag; }),
          "numeric catalog was classified as a procedure");

  const std::vector<std::string> barrier{
      "cfont 3 2 2    1. First ST Unrelated cfont 3 2 2    2. Second"};
  const auto barrier_flags = geist::detail::numbered_procedure_step_segments(
      barrier, {source(15, barrier[0])});
  require(std::none_of(barrier_flags[0].begin(), barrier_flags[0].end(),
                       [](bool flag) { return flag; }),
          "procedure sequence crossed a semantic heading barrier");

  const std::vector<std::string> shifted{
      "cfont 3 2 2    1. First cfont 7 2 2        2. Second"};
  const auto shifted_flags = geist::detail::numbered_procedure_step_segments(
      shifted, {source(16, shifted[0])});
  require(std::none_of(shifted_flags[0].begin(), shifted_flags[0].end(),
                       [](bool flag) { return flag; }),
          "procedure sequence crossed physical origins");

  auto lookalike = source(17, records[0]);
  const auto origin = std::find_if(
      lookalike.tokens.begin(), lookalike.tokens.end(), [](const auto& token) {
        return token.size() == 3 &&
               std::all_of(token.begin(), token.end(),
                           [](auto word) { return word == ' '; });
      });
  lookalike.encoded_tokens[origin - lookalike.tokens.begin()].width = 2;
  const auto lookalike_flags = geist::detail::numbered_procedure_step_segments(
      records, {lookalike, source(18, records[1])});
  require(std::none_of(lookalike_flags[0].begin(), lookalike_flags[0].end(),
                       [](bool flag) { return flag; }),
          "two-byte source lookalike activated a procedure");

  auto two_byte_number = source(21, records[0]);
  const auto number_token = std::find_if(
      two_byte_number.tokens.begin(), two_byte_number.tokens.end(),
      [](const auto& token) {
        return token.size() == 2 && token[0] == '1' && token[1] == '.';
      });
  two_byte_number
      .encoded_tokens[number_token - two_byte_number.tokens.begin()].width = 2;
  const auto two_byte_number_flags =
      geist::detail::numbered_procedure_step_segments(
          records, {two_byte_number, source(22, records[1])});
  require(std::none_of(two_byte_number_flags[0].begin(),
                       two_byte_number_flags[0].end(),
                       [](bool flag) { return flag; }),
          "two-byte number token activated a procedure");

  const auto same_token_flags =
      geist::detail::numbered_procedure_step_segments(
          records, {flat_source(19, records[0]), source(20, records[1])});
  require(std::none_of(same_token_flags[0].begin(), same_token_flags[0].end(),
                       [](bool flag) { return flag; }),
          "same-token whitespace and number activated a procedure");
}
