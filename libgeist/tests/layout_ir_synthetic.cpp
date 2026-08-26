#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

DecodedLogicalRecordSource make_record(
    std::uint32_t logical_record, std::vector<TokenWords> tokens,
    std::vector<std::uint8_t> widths = {}) {
  DecodedLogicalRecordSource record;
  record.logical_record = logical_record;
  record.tokens = std::move(tokens);
  if (widths.empty()) widths.assign(record.tokens.size(), 1);
  for (std::size_t token = 0; token < record.tokens.size(); ++token) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(0x20 + token), widths[token]});
  }
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  record.control_segments = geist::detail::decode_control_segments(
      logical_record, record.assembled);
  return record;
}

} // namespace

int main() {
  const auto opening = make_record(
      10, {{3, 'c','f','o','n','t',' ','3',' ','4',' ','C'},
           {'<'}, {' ',' ',' '}, {'F','i','r','s','t',' ','r','o','w'}});
  const auto continuation = make_record(
      11, {{'<'}, {' ',' ',' '},
           {'c','o','n','t','i','n','u','e','d',' ','t','e','x','t'}});
  const auto layout = geist::detail::extract_layout_ir(
      {opening, continuation});
  std::string error;
  require(geist::detail::verify_layout_ir({opening, continuation}, layout,
                                           &error),
          "valid cross-record layout failed verification");
  require(layout.runs.size() == 1 && layout.runs.front().rows.size() == 2 &&
              layout.runs.front().rows.back().continues_previous_record &&
              layout.runs.front().rows.back().start ==
                  geist::detail::PhysicalRowStartKind::record_continuation,
          "adjacent control-free record did not continue its display run");

  const auto separated = geist::detail::extract_layout_ir(
      {opening, make_record(12, continuation.tokens)});
  require(separated.runs.size() == 2,
          "non-adjacent logical records shared a display run");

  const auto intervening = make_record(
      11, {{2, 'S','T',' ','H','e','a','d','i','n','g','?'},
           {3, 'c','f','o','n','t',' ','3',' ','4',' ','C'},
           {'<'}, {' ',' ',' '}, {'s','e','p','a','r','a','t','e'}});
  const auto interrupted =
      geist::detail::extract_layout_ir({opening, intervening});
  require(interrupted.runs.size() >= 2 &&
              std::none_of(interrupted.runs.begin(), interrupted.runs.end(),
                           [](const auto& run) {
                             return std::any_of(
                                 run.rows.begin(), run.rows.end(),
                                 [](const auto& row) {
                                   return row.continues_previous_record;
                                 });
                           }),
          "intervening control did not terminate display-run continuity");

  const auto question_wrap = make_record(
      20, {{3, 'c','f','o','n','t',' ','3',' ','4',' ','C'},
           TokenWords(20, '?'), {' ',' ',' '}, {'w','r','a','p'}});
  const auto wrapped = geist::detail::extract_layout_ir({question_wrap});
  if (wrapped.runs.empty() || wrapped.runs.front().rows.empty() ||
      wrapped.runs.front().rows.front().break_before !=
          geist::detail::PhysicalBreakKind::soft_wrap) {
    std::cerr << "question token='"
              << geist::detail::token_words_to_ascii(question_wrap.tokens[1])
              << "' width="
              << static_cast<int>(question_wrap.encoded_tokens[1].width)
              << " segments=" << question_wrap.control_segments.size()
              << '\n';
    for (const auto& segment : question_wrap.control_segments) {
      std::cerr << geist::detail::format_control_segment_ir(segment)
                << " source_tokens=";
      for (const auto token : segment.source_tokens) std::cerr << token << ',';
      std::cerr << '\n';
    }
    for (const auto& run : wrapped.runs) {
      for (const auto& row : run.rows) {
        std::cerr << geist::detail::format_physical_row_ir(row) << '\n';
      }
    }
  }
  require(wrapped.runs.size() == 1 &&
              wrapped.runs.front().rows.front().break_before ==
                  geist::detail::PhysicalBreakKind::soft_wrap,
          "question-run row was not classified as a soft wrap");

  auto lookalike = question_wrap;
  lookalike.encoded_tokens[1].width = 2;
  const auto rejected = geist::detail::extract_layout_ir({lookalike});
  require(rejected.runs.empty(),
          "two-byte marker lookalike established a physical row");

  auto invalid = layout;
  invalid.runs.front().rows.back().logical_record = 13;
  require(!geist::detail::verify_layout_ir({opening, continuation}, invalid,
                                            &error) &&
              !error.empty(),
          "non-adjacent continuation passed layout verification");
  return 0;
}
