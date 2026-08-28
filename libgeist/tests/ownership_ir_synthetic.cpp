// Synthetic ownership-ledger fixtures for the source-cell disposition
// conflicts found by the 2026-08-28 typed-route census (96 topics rejected
// with "source cell received incompatible ownership dispositions").
//
// Cause class 1 (90 topics, e.g. GC23-046 6.9.5.1 LR 168 token 14,
// ACPZMST1 2.1.2 LR 70 token 170, SC24-5520-00 1.1.5 LR 54 token 266): an
// SRETBL end-of-table control followed by a zero-width control token and a
// horizontal-rule glyph run. The ASCII projection has no space between them,
// so the opcode word absorbed the rule and its cells were marked
// control_operand while the layout used the same token as a marker slot.
//
// Cause class 2 (6 topics, e.g. GG24-4302-00 3.2.14 LR 206 token 154,
// SC28-1881-05 1.45 LR 1060 token 2, SC28-1881-05 1.17 LR 524 token 61): a
// fixed-operand control (CMITEM) inside a fixed figure whose next word is a
// vertical rail or junction glyph, which became the "operand".
//
// Both are fixed in decode_control_segments: display-geometry code points are
// never opcode/operand material. The ledger additionally records any residual
// disagreement as a typed conflict on the affected run only.
#include "geist/detail/internal.hpp"
#include "geist/detail/layout_ir.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using geist::detail::BookControlKind;
using geist::detail::DecodedLogicalRecordSource;
using geist::detail::OwnershipConflictKind;
using geist::detail::SourceDisposition;
using geist::detail::TokenWords;

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    geist_test::record_failure();
  }
}

DecodedLogicalRecordSource make_record(std::uint32_t logical_record,
                                       std::vector<TokenWords> tokens) {
  DecodedLogicalRecordSource record;
  record.logical_record = logical_record;
  record.tokens = std::move(tokens);
  for (std::size_t token = 0; token < record.tokens.size(); ++token)
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(0x20 + token), 1});
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  record.control_segments = geist::detail::decode_control_segments(
      logical_record, record.assembled);
  return record;
}

TokenWords ascii(const char* text) {
  TokenWords words;
  for (; *text != '\0'; ++text)
    words.push_back(static_cast<std::uint16_t>(*text));
  return words;
}

TokenWords glyphs(std::uint16_t glyph, std::size_t count) {
  return TokenWords(count, glyph);
}

const geist::detail::OwnedSourceCellIR* cell_at(
    const geist::detail::OwnershipIR& ownership, std::uint32_t record,
    std::size_t token, std::size_t word) {
  const auto found = std::find_if(
      ownership.cells.begin(), ownership.cells.end(), [&](const auto& cell) {
        return cell.logical_record == record && cell.token_index == token &&
               cell.word_index == word;
      });
  return found == ownership.cells.end() ? nullptr : &*found;
}

// Class 1: SRETBL, zero-width control token, horizontal rule, origin, text.
void class1_sretbl_rule_absorbed_by_opcode() {
  const auto record = make_record(
      168, {ascii("SRETBL"), {1}, glyphs(0x2500, 12), ascii("   "),
            ascii("To"), ascii("change"), ascii("the"), ascii("defaults")});
  require(record.control_segments.size() == 1 &&
              record.control_segments[0].kind == BookControlKind::table_end &&
              record.control_segments[0].opcode == "SRETBL" &&
              record.control_segments[0].opcode_range.end == 6 &&
              record.control_segments[0].operand_range.begin == 6 &&
              record.control_segments[0].operand_range.end == 6 &&
              record.control_segments[0].payload_range.begin == 6,
          "class 1: SRETBL opcode absorbed the adjacent horizontal rule");
  const auto layout = geist::detail::extract_layout_ir({record});
  std::string error;
  require(geist::detail::verify_layout_ir({record}, layout, &error),
          "class 1: layout failed verification");
  require(layout.runs.size() == 1 && layout.runs[0].rows.size() == 1 &&
              layout.runs[0].rows[0].marker &&
              layout.runs[0].rows[0].marker->token_index == 2,
          "class 1: rule token is not the row's marker slot");
  const auto ownership = geist::detail::build_ownership_ir({record}, layout);
  require(ownership.run_conflicts.empty() && ownership.conflicts.empty(),
          "class 1: ownership still reports a disposition conflict");
  require(geist::detail::verify_ownership_ir({record}, layout, ownership,
                                             &error),
          "class 1: ownership failed verification");
  const auto* rule = cell_at(ownership, 168, 2, 0);
  const auto* rule_end = cell_at(ownership, 168, 2, 11);
  const auto* opcode = cell_at(ownership, 168, 0, 0);
  require(rule != nullptr && rule_end != nullptr && opcode != nullptr &&
              rule->disposition == SourceDisposition::marker_slot &&
              rule_end->disposition == SourceDisposition::marker_slot &&
              rule->run == layout.runs[0].id &&
              opcode->disposition == SourceDisposition::control_operand,
          "class 1: rule cells are not owned once as the marker slot");
}

// Class 2: CMITEM whose next word is a vertical rail inside a fixed figure.
void class2_cmitem_rail_absorbed_by_operand() {
  const auto record = make_record(
      206, {ascii("cmitem"), ascii("   "), glyphs(0x2502, 1), ascii("   "),
            ascii("BUFFER"), ascii("-"), ascii("NBA=")});
  require(record.control_segments.size() == 1 &&
              record.control_segments[0].kind == BookControlKind::menu_item &&
              record.control_segments[0].malformed &&
              record.control_segments[0].operand_range.begin ==
                  record.control_segments[0].operand_range.end &&
              record.control_segments[0].payload_range.begin ==
                  record.control_segments[0].opcode_range.end,
          "class 2: CMITEM operand absorbed the vertical rail");
  const auto layout = geist::detail::extract_layout_ir({record});
  std::string error;
  require(geist::detail::verify_layout_ir({record}, layout, &error),
          "class 2: layout failed verification");
  require(layout.runs.size() == 1 && !layout.runs[0].rows.empty() &&
              layout.runs[0].rows[0].marker &&
              layout.runs[0].rows[0].marker->token_index == 2,
          "class 2: rail token is not the row's marker slot");
  const auto ownership = geist::detail::build_ownership_ir({record}, layout);
  require(ownership.run_conflicts.empty() &&
              geist::detail::verify_ownership_ir({record}, layout, ownership,
                                                 &error),
          "class 2: ownership still reports a disposition conflict");
  const auto* rail = cell_at(ownership, 206, 2, 0);
  require(rail != nullptr &&
              rail->disposition == SourceDisposition::marker_slot,
          "class 2: rail cell is not owned as the marker slot");

  // The same shape with a zero-width control token between a real operand
  // and a junction glyph (SC28-1881-05 1.17 LR 524): the operand survives,
  // the glyph is payload.
  const auto junction = make_record(
      524, {ascii("cmitem"), ascii(" "), ascii("1.2"), {1},
            glyphs(0x2524, 1), ascii("   "), ascii("OL")});
  const auto& segment = junction.control_segments.at(0);
  const auto text = geist::detail::token_words_to_ascii(junction.assembled.words);
  require(segment.kind == BookControlKind::menu_item && !segment.malformed &&
              geist::detail::trim_ascii(text.substr(
                  segment.operand_range.begin,
                  segment.operand_range.end - segment.operand_range.begin)) ==
                  "1.2" &&
              text.substr(segment.payload_range.begin, 1) == "?",
          "class 2: operand before the junction glyph was not preserved");
  const auto junction_layout = geist::detail::extract_layout_ir({junction});
  const auto junction_ownership =
      geist::detail::build_ownership_ir({junction}, junction_layout);
  require(junction_ownership.run_conflicts.empty() &&
              geist::detail::verify_ownership_ir(
                  {junction}, junction_layout, junction_ownership, &error),
          "class 2: junction glyph after a real operand still conflicts");
}

// Genuine disagreement: a hand-built row claims control-operand cells. The
// conflict is typed and scoped to that run; the other run stays owned.
void run_scoped_conflict_leaves_other_runs_owned() {
  const auto record = make_record(
      10, {{3, 'c', 'f', 'o', 'n', 't', ' ', '3', ' ', '4', ' ', 'C'},
           {'<'}, ascii("   "), ascii("First"), ascii("row")});
  auto layout = geist::detail::extract_layout_ir({record});
  require(layout.runs.size() == 1 && layout.runs[0].rows.size() == 1,
          "fixture: expected one owned display run");
  geist::detail::DisplayRunIR bogus;
  bogus.id = layout.runs.back().id + 1;
  bogus.control_kind = BookControlKind::font;
  geist::detail::PhysicalRowIR row;
  row.run = bogus.id;
  row.logical_record = 10;
  row.token_begin = 0;
  row.token_end = 1;
  row.native_origin = 1;
  row.start = geist::detail::PhysicalRowStartKind::explicit_marker_slot;
  row.marker = geist::detail::MarkerSlotIR{10, 0, 0x20, 1, {}, "cfont"};
  row.visible_text = "cfont 3 4 C";
  bogus.rows.push_back(row);
  layout.runs.push_back(bogus);

  const auto ownership = geist::detail::build_ownership_ir({record}, layout);
  std::string error;
  require(ownership.conflicts.empty() && ownership.run_conflicts.size() == 1 &&
              ownership.run_conflicts[0].run == bogus.id &&
              ownership.run_conflicts[0].kind ==
                  OwnershipConflictKind::incompatible_disposition &&
              ownership.run_conflicts[0].existing ==
                  SourceDisposition::control_operand &&
              ownership.run_conflicts[0].requested ==
                  SourceDisposition::marker_slot &&
              ownership.run_conflicts[0].logical_record == 10 &&
              ownership.run_conflicts[0].token_index == 0,
          "conflict was not recorded as a typed run-scoped reason");
  require(geist::detail::ownership_run_conflicted(ownership, bogus.id) &&
              !geist::detail::ownership_run_conflicted(ownership,
                                                       layout.runs[0].id),
          "conflict scope is not limited to the offending run");
  require(geist::detail::verify_ownership_ir({record}, layout, ownership,
                                             &error),
          "ledger with a run-scoped conflict failed verification");
  require(std::none_of(ownership.cells.begin(), ownership.cells.end(),
                       [&](const auto& cell) { return cell.run == bogus.id; }) &&
              std::none_of(ownership.row_cells.begin(),
                           ownership.row_cells.end(),
                           [&](const auto& cell) {
                             return cell.run == bogus.id;
                           }),
          "conflicted run still owns cells");
  const auto* content = cell_at(ownership, 10, 3, 0);
  const auto* operand = cell_at(ownership, 10, 0, 7);
  require(content != nullptr && operand != nullptr &&
              content->disposition == SourceDisposition::visible_content &&
              content->run == layout.runs[0].id &&
              operand->disposition == SourceDisposition::control_operand,
          "the unaffected run lost its ownership");
  require(geist::detail::format_ownership_ir(ownership).find(
              "run_conflict run=2 row=0 record=10 token=0 word=1 value=99 "
              "kind=incompatible_disposition existing=control_operand "
              "requested=marker_slot") != std::string::npos,
          "run conflict has no stable diagnostic projection");

  // A run that re-claims cells another run owns is a duplicate assignment.
  geist::detail::DisplayRunIR duplicate = layout.runs[0];
  duplicate.id = bogus.id + 1;
  for (auto& duplicate_row : duplicate.rows) duplicate_row.run = duplicate.id;
  layout.runs.push_back(duplicate);
  const auto duplicated = geist::detail::build_ownership_ir({record}, layout);
  require(duplicated.run_conflicts.size() == 2 &&
              duplicated.run_conflicts[1].run == duplicate.id &&
              duplicated.run_conflicts[1].kind ==
                  OwnershipConflictKind::duplicate_row_assignment &&
              geist::detail::verify_ownership_ir({record}, layout, duplicated,
                                                 &error),
          "duplicate row assignment was not a typed run-scoped conflict");

  // Tampering with the recorded conflicts fails closed.
  auto tampered = duplicated;
  tampered.run_conflicts.pop_back();
  require(!geist::detail::verify_ownership_ir({record}, layout, tampered,
                                              &error),
          "a ledger with missing conflicts passed verification");
}

} // namespace

int main() {
  class1_sretbl_rule_absorbed_by_opcode();
  class2_cmitem_rail_absorbed_by_operand();
  run_scoped_conflict_leaves_other_runs_owned();
  return 0;
}
