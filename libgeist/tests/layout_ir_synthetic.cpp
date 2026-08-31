#include "geist/detail/internal.hpp"
#include "test_failures.hpp"

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
    geist_test::record_failure();
    return;
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
  const auto ownership =
      geist::detail::build_ownership_ir({opening, continuation}, layout);
  require(geist::detail::verify_ownership_ir(
              {opening, continuation}, layout, ownership, &error),
          "valid source ownership failed conservation verification");
  require(std::any_of(ownership.cells.begin(), ownership.cells.end(),
                      [](const auto& cell) {
                        return cell.disposition ==
                                   geist::detail::SourceDisposition::
                                       control_operand;
                      }) &&
              std::any_of(ownership.cells.begin(), ownership.cells.end(),
                          [](const auto& cell) {
                            return cell.disposition ==
                                   geist::detail::SourceDisposition::marker_slot;
                          }) &&
              std::any_of(ownership.cells.begin(), ownership.cells.end(),
                          [](const auto& cell) {
                            return cell.disposition ==
                                   geist::detail::SourceDisposition::
                                       layout_origin;
                          }) &&
              std::any_of(ownership.cells.begin(), ownership.cells.end(),
                          [](const auto& cell) {
                            return cell.disposition ==
                                   geist::detail::SourceDisposition::
                                       visible_content;
                          }),
          "ownership ledger did not retain its structural disposition classes");
  const auto positioned = [&](std::uint32_t record, std::size_t token,
                              std::size_t word) {
    return std::find_if(ownership.row_cells.begin(), ownership.row_cells.end(),
                        [&](const auto& cell) {
                          return cell.logical_record == record &&
                                 cell.token_index == token &&
                                 cell.word_index == word;
                        });
  };
  const auto opening_boundary = positioned(10, 1, 0);
  const auto opening_origin = positioned(10, 2, 2);
  const auto opening_content = positioned(10, 3, 0);
  require(opening_boundary != ownership.row_cells.end() &&
              opening_boundary->role == geist::detail::RowCellRole::boundary &&
              !opening_boundary->display_column &&
              opening_origin != ownership.row_cells.end() &&
              opening_origin->role == geist::detail::RowCellRole::origin &&
              opening_origin->display_column == 2 &&
              opening_content != ownership.row_cells.end() &&
              opening_content->role == geist::detail::RowCellRole::content &&
              opening_content->display_column == 3,
          "positioned ledger lost boundary/origin/content source geometry");
  require(ownership.row_cells.size() ==
              std::count_if(ownership.cells.begin(), ownership.cells.end(),
                            [](const auto& cell) { return cell.run != 0; }),
          "positioned ledger does not correspond exactly to row ownership");

  const auto generated_controls = make_record(
      20, {{3, 'c','.','s','p',' ','3','p',' ','p',' ','c'},
           {3, 'c','z',' ','B','R','E','A','K',' ','3'}});
  const auto generated_control_layout =
      geist::detail::extract_layout_ir({generated_controls});
  const auto generated_control_ownership = geist::detail::build_ownership_ir(
      {generated_controls}, generated_control_layout);
  require(generated_control_layout.runs.empty() &&
              geist::detail::verify_ownership_ir(
                  {generated_controls}, generated_control_layout,
                  generated_control_ownership, &error) &&
              std::all_of(generated_control_ownership.cells.begin(),
                          generated_control_ownership.cells.end(),
                          [](const auto& cell) {
                            return cell.disposition ==
                                   geist::detail::SourceDisposition::
                                       control_operand;
                          }),
          "output-neutral spacing/CZ operands escaped structural ownership");
  require(geist::detail::format_ownership_ir(ownership).find(
              "disposition=") != std::string::npos,
          "ownership IR has no stable diagnostic projection");
  require(geist::detail::format_ownership_ir(ownership).find(
              "role=boundary field=") != std::string::npos &&
              geist::detail::format_ownership_ir(ownership).find(
                  "column=none") != std::string::npos,
          "positioned row-cell IR has no stable diagnostic projection");

  auto duplicate = ownership;
  duplicate.cells.push_back(duplicate.cells.back());
  require(!geist::detail::verify_ownership_ir(
              {opening, continuation}, layout, duplicate, &error) &&
              !error.empty(),
          "duplicate source-cell ownership passed verification");

  auto missing = ownership;
  missing.cells.pop_back();
  require(!geist::detail::verify_ownership_ir(
              {opening, continuation}, layout, missing, &error) &&
              !error.empty(),
          "ownership ledger with a source-cell gap passed verification");

  auto duplicate_position = ownership;
  duplicate_position.row_cells.push_back(duplicate_position.row_cells.back());
  require(!geist::detail::verify_ownership_ir(
              {opening, continuation}, layout, duplicate_position, &error) &&
              !error.empty(),
          "duplicate positioned source cell passed verification");

  auto moved_position = ownership;
  auto movable = std::find_if(moved_position.row_cells.begin(),
                              moved_position.row_cells.end(), [](auto& cell) {
                                return cell.display_column.has_value();
                              });
  require(movable != moved_position.row_cells.end(),
          "positioned test fixture has no display cell");
  ++*movable->display_column;
  require(!geist::detail::verify_ownership_ir(
              {opening, continuation}, layout, moved_position, &error) &&
              !error.empty(),
          "moved positioned source cell passed verification");

  auto semantic_boundary = ownership;
  auto boundary = std::find_if(
      semantic_boundary.row_cells.begin(), semantic_boundary.row_cells.end(),
      [](const auto& cell) {
        return cell.role == geist::detail::RowCellRole::boundary;
      });
  require(boundary != semantic_boundary.row_cells.end(),
          "positioned test fixture has no boundary cell");
  boundary->display_column = 0;
  require(!geist::detail::verify_ownership_ir(
              {opening, continuation}, layout, semantic_boundary, &error) &&
              !error.empty(),
          "semantic boundary placement passed geometry verification");

  auto mismatched_role = ownership;
  auto owned_origin = std::find_if(
      mismatched_role.cells.begin(), mismatched_role.cells.end(),
      [](const auto& cell) {
        return cell.disposition ==
               geist::detail::SourceDisposition::layout_origin;
      });
  require(owned_origin != mismatched_role.cells.end(),
          "ownership test fixture has no origin cell");
  owned_origin->disposition =
      geist::detail::SourceDisposition::visible_content;
  require(!geist::detail::verify_ownership_ir(
              {opening, continuation}, layout, mismatched_role, &error) &&
              !error.empty(),
          "positioned role/source-ownership mismatch passed verification");

  const auto spelling_neutral = make_record(
      19, {{3, 'c','f','o','n','t',' ','3',' ','4',' ','C'},
           {'a','n','d'}, {' ',' '}, {'A'}, {'B'}});
  const auto spelling_neutral_layout =
      geist::detail::extract_layout_ir({spelling_neutral});
  const auto spelling_neutral_ownership = geist::detail::build_ownership_ir(
      {spelling_neutral}, spelling_neutral_layout);
  require(geist::detail::verify_ownership_ir(
              {spelling_neutral}, spelling_neutral_layout,
              spelling_neutral_ownership, &error) &&
              spelling_neutral_layout.runs.size() == 1 &&
              spelling_neutral_layout.runs.front().rows.size() == 1,
          "lexical-looking compact boundary did not retain source geometry");
  const auto& spelling_cells = spelling_neutral_ownership.row_cells;
  require(std::count_if(spelling_cells.begin(), spelling_cells.end(),
                        [](const auto& cell) {
                          return cell.role ==
                                     geist::detail::RowCellRole::boundary &&
                                 !cell.display_column;
                        }) == 3,
          "boundary role depended on decoded marker spelling");
  const auto second_content = std::find_if(
      spelling_cells.begin(), spelling_cells.end(), [](const auto& cell) {
        return cell.token_index == 4 && cell.word_index == 0;
      });
  require(second_content != spelling_cells.end() &&
              second_content->display_column == 4,
          "decoder-inserted spacing was not preserved as a column gap");

  const auto wide_lookalike = make_record(
      21, {{3, 'c','f','o','n','t',' ','3',' ','4',' ','C'},
           {'a','n','d'}, {' ',' '}, {'t','e','x','t'}},
      {1, 2, 1, 1});
  require(geist::detail::extract_layout_ir({wide_lookalike}).runs.empty(),
          "multi-byte lexical lookalike became a compact boundary");

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

  const auto end_payload = make_record(
      30, {{3, 'c','f','o','n','t',' ','3',' ','4',' ','C'},
           {'<'}, {' ',' ',' '}, {'t','a','b','l','e',' ','r','o','w'},
           {3, 'S','R','E','F','I','G'}, TokenWords(7, ' '),
           {'S','p','e','c','i','f','i','c',' ','c','o','m','m','e','n','t','s'}});
  const auto end_payload_continuation = make_record(
      31, {{'<'}, {' ',' ',' '},
           {'c','o','n','t','i','n','u','e','d',' ','f','o','r','m'}});
  const auto ended = geist::detail::extract_layout_ir(
      {end_payload, end_payload_continuation});
  require(ended.runs.size() == 2 &&
              ended.runs.front().control_kind ==
                  geist::detail::BookControlKind::font &&
              ended.runs.front().rows.size() == 1 &&
              ended.runs.back().control_kind ==
                  geist::detail::BookControlKind::structural &&
              ended.runs.back().rows.size() == 2 &&
              ended.runs.back().rows.front().visible_text ==
                  "Specific comments" &&
              ended.runs.back().rows.front().start ==
                  geist::detail::PhysicalRowStartKind::control_payload &&
              ended.runs.back().rows.front().break_before ==
                  geist::detail::PhysicalBreakKind::hard_object &&
              ended.runs.back().rows.back().continues_previous_record,
          "visible SREFIG payload was dropped or crossed the prior run");
  require(geist::detail::verify_layout_ir(
              {end_payload, end_payload_continuation}, ended, &error),
          "SREFIG payload layout failed verification");
  const auto ended_ownership = geist::detail::build_ownership_ir(
      {end_payload, end_payload_continuation}, ended);
  require(geist::detail::verify_ownership_ir(
              {end_payload, end_payload_continuation}, ended,
              ended_ownership, &error),
          "SREFIG payload ownership failed conservation verification");

  const auto table_end_payload = make_record(
      40, {{3, 'S','R','E','T','B','L'}, TokenWords(5, ' '),
           {'A','d','d','r','e','s','s',' ','b','l','o','c','k'}});
  const auto table_end_layout =
      geist::detail::extract_layout_ir({table_end_payload});
  require(table_end_layout.runs.size() == 1 &&
              table_end_layout.runs.front().control_kind ==
                  geist::detail::BookControlKind::table_end &&
              table_end_layout.runs.front().rows.size() == 1 &&
              table_end_layout.runs.front().rows.front().visible_text ==
                  "Address block" &&
              table_end_layout.runs.front().rows.front().start ==
                  geist::detail::PhysicalRowStartKind::control_payload,
          "visible SRETBL payload did not establish a control-payload row");

  const auto end_barrier = make_record(
      50, {{3, 'c','f','o','n','t',' ','3',' ','4',' ','C'},
           {'<'}, {' ',' ',' '}, {'f','i','n','a','l',' ','r','o','w'},
           {3, 'S','R','E','F','I','G'}});
  const auto after_barrier = make_record(
      51, {{'<'}, {' ',' ',' '}, {'n','e','w',' ','o','b','j','e','c','t'}});
  const auto barred =
      geist::detail::extract_layout_ir({end_barrier, after_barrier});
  require(barred.runs.size() == 2 &&
              std::none_of(barred.runs.begin(), barred.runs.end(),
                           [](const auto& run) {
                             return std::any_of(
                                 run.rows.begin(), run.rows.end(),
                                 [](const auto& row) {
                                   return row.continues_previous_record;
                                 });
                           }),
          "empty structural end control failed to block continuation");

  auto invalid_barrier_crossing = barred;
  invalid_barrier_crossing.runs.front().rows.push_back(
      invalid_barrier_crossing.runs.back().rows.front());
  invalid_barrier_crossing.runs.front().rows.back().run =
      invalid_barrier_crossing.runs.front().id;
  invalid_barrier_crossing.runs.front().rows.back().start =
      geist::detail::PhysicalRowStartKind::record_continuation;
  invalid_barrier_crossing.runs.front().rows.back().break_before =
      geist::detail::PhysicalBreakKind::soft_wrap;
  invalid_barrier_crossing.runs.front().rows.back().continues_previous_record =
      true;
  invalid_barrier_crossing.runs.pop_back();
  require(!geist::detail::verify_layout_ir(
              {end_barrier, after_barrier}, invalid_barrier_crossing, &error) &&
              !error.empty(),
          "layout verifier accepted continuation across an end-control barrier");

  const auto title_end = make_record(
      60, {{3, 'S','T'}, {'<'}, {' ',' ',' '}, {'t','i','t','l','e'}});
  const auto after_title = make_record(
      61, {{'<'}, {' ',' ',' '}, {'u','n','r','e','l','a','t','e','d'}});
  auto invalid_title_crossing =
      geist::detail::extract_layout_ir({title_end, after_title});
  require(invalid_title_crossing.runs.size() == 2,
          "title continuation negative did not produce two source runs");
  invalid_title_crossing.runs.front().rows.push_back(
      invalid_title_crossing.runs.back().rows.front());
  invalid_title_crossing.runs.front().rows.back().run =
      invalid_title_crossing.runs.front().id;
  invalid_title_crossing.runs.front().rows.back().start =
      geist::detail::PhysicalRowStartKind::record_continuation;
  invalid_title_crossing.runs.front().rows.back().break_before =
      geist::detail::PhysicalBreakKind::soft_wrap;
  invalid_title_crossing.runs.front().rows.back().continues_previous_record =
      true;
  invalid_title_crossing.runs.pop_back();
  require(!geist::detail::verify_layout_ir(
              {title_end, after_title}, invalid_title_crossing, &error) &&
              !error.empty(),
          "layout verifier accepted continuation from a title run");

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
