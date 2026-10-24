// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Every display line carries the revision mark it stores.
//
// The book compiler resolved SCRIPT revision codes and drew the mark's
// character at a fixed position of the display line; no revision flag or
// code reference exists (doc/boo-spec/markup.adoc, "The three-column left
// margin" and "`c.rev` revision-code definitions").  Two positions hold one:
//
//  * a body row's margin -- a one-cell blank at cell 0, the mark at cell 1, a
//    blank cell 2 and the row's drawn cells after it.  N2AH1MST 10.0 record
//    599 line 9 stores ` ` + `U+2502` + 17 blanks + the bullet + `TPADD`;
//    SC33-0668-02 FIGURES record 21 line 12 stores ` ` + `#` + `39.` ...;
//  * the `ST` revision column -- the cell glued directly after the opcode.
//    SC41-5401-00 2.1.2.5 record 240 line 9 stores `ST`, a no-space prefix,
//    `|`, then `CSLBUFSIZE (Console buffer size)`.
//
// A letter in cell 1 is row text (` I am ...`), and a bar with nothing drawn
// after it marks no row.  The conservation projection accounts each mark as a
// margin cell and lists it.
//
// Everything here is synthetic: the test builds the record by hand.

#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/layout_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/lowering/drawn_word_source.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::MarginMarkPlaceIR;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "margin_mark_line_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

struct Token {
  TokenWords words;
  std::uint8_t width = 1;
};

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;

  std::size_t line(std::vector<Token> content,
                   TokenWords prefix = TokenWords{0x25BA}) {
    std::uint16_t length = 0;
    for (const auto &token : content) length += token.width;
    const auto at = record.tokens.size();
    record.encoded_tokens.push_back({length, 1});
    record.tokens.push_back(std::move(prefix));
    for (auto &token : content) {
      record.encoded_tokens.push_back(
          {token.width == 1 ? static_cast<std::uint16_t>(next_encoded++ & 0xFF)
                            : static_cast<std::uint16_t>(0xE000 +
                                                         next_encoded++),
           token.width});
      record.tokens.push_back(std::move(token.words));
    }
    return at;
  }

  DecodedLogicalRecordSource build(std::uint32_t logical_record) {
    record.logical_record = logical_record;
    record.assembled =
        geist::detail::assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = logical_record;
    std::uint32_t byte = 1;
    for (std::size_t token = 0; token < record.tokens.size(); ++token) {
      const auto encoded = record.encoded_tokens[token];
      const auto spacing =
          !record.tokens[token].empty() && record.tokens[token].front() < 4;
      record.ir.tokens.push_back(
          {token, encoded, record.tokens[token],
           {byte, static_cast<std::uint32_t>(byte + encoded.width)}, spacing,
           spacing ? record.tokens[token].front() : std::uint16_t{3}});
      byte += encoded.width;
    }
    record.ir.payload_range = {1, byte};
    geist::detail::assign_display_line_framing(record.ir);
    record.control_segments = geist::detail::decode_control_segments(
        record.logical_record, record.assembled, record.encoded_tokens,
        record.ir.display_lines);
    geist::detail::demote_display_line_owned_controls(record);
    geist::detail::assign_display_line_margin_marks(record);
    return std::move(record);
  }
};

void test_line_marks() {
  RecordBuilder builder;
  // 0: the `ST` title with `|` glued into its revision column.
  const auto title = builder.line(
      {{words("ST")}, {TokenWords{0, '|'}, 2}, {words(" ")},
       {words("CSLBUFSIZE"), 2}, {words("(Console")}, {words("buffer")},
       {words("size)")}});
  // 1: an `ST` followed by a space: no mark, a leading `*` is title text.
  builder.line({{words("ST")}, {words(" ")}, {words("*ACCOUNT")}});
  // 2: a bullet row behind the `U+2502` bar.
  const auto barred = builder.line(
      {{words(" ")}, {TokenWords{0x2502}}, {words("                 ")},
       {TokenWords{0x2666}}, {words("  ")}, {words("TPADD")}});
  // 3: a selection row behind `#`.
  const auto hashed = builder.line(
      {{words(" ")}, {words("#")}, {words("39.")}, {words("  ")},
       {words("DFHJUP")}});
  // 4: a letter in cell 1 is text.
  builder.line({{words(" ")}, {words("I")}, {words("am")}});
  // 5: a bar with nothing drawn after it marks no row.
  builder.line({{words(" ")}, {words("|")}, {words("   ")}});
  // 6: an ordinary row.
  builder.line({{words("   ")}, {words("The")}, {words("row.")}});
  // 7: the revision column behind a bare attach token of its own
  // (dfheca01 2.1.4 `ST#`): the column, not the token, holds the mark.
  const auto attached = builder.line(
      {{words("ST")}, {TokenWords{1}}, {words("#")}, {words("CICS")}});
  auto record = builder.build(240);
  require(record.ir.display_lines_parse,
          "the synthetic record's display lines do not parse");
  const auto &lines = record.ir.display_lines;
  require(lines.size() == 8, "the synthetic record does not hold 8 lines");
  if (lines.size() != 8) return;

  require(lines[0].margin_mark == U'|' &&
              lines[0].margin_mark_place == MarginMarkPlaceIR::title_column &&
              lines[0].margin_mark_token == title + 2,
          "the ST revision column's `|` is not the title line's mark");
  require(!lines[1].margin_mark &&
              lines[1].margin_mark_place == MarginMarkPlaceIR::none,
          "an ST followed by a space carries a mark");
  require(lines[2].margin_mark == char32_t{0x2502} &&
              lines[2].margin_mark_place == MarginMarkPlaceIR::row_margin &&
              lines[2].margin_mark_token == barred + 2,
          "the U+2502 bar is not the bullet row's margin mark");
  require(lines[3].margin_mark == U'#' &&
              lines[3].margin_mark_token == hashed + 2,
          "the `#` is not the selection row's margin mark");
  require(!lines[4].margin_mark, "a letter in cell 1 was read as a mark");
  require(!lines[5].margin_mark, "a bar with no drawn cell marks a row");
  require(!lines[6].margin_mark, "an ordinary row carries a mark");
  require(lines[7].margin_mark == U'#' &&
              lines[7].margin_mark_place == MarginMarkPlaceIR::title_column &&
              lines[7].margin_mark_token == attached + 3,
          "the mark behind an attach token is not the title column's");

  require(geist::detail::display_line_margin_mark_at(record, barred + 2) ==
              char32_t{0x2502},
          "the mark is not found at its own token");
  require(!geist::detail::display_line_margin_mark_at(record, barred + 1),
          "a blank cell reports a mark");
  std::string error;
  require(geist::detail::verify_display_line_margin_marks(record, &error),
          "the stored marks do not verify: " + error);
  record.ir.display_lines[6].margin_mark = U'|';
  require(!geist::detail::verify_display_line_margin_marks(record),
          "a fabricated mark verifies");
  record.ir.display_lines[6].margin_mark.reset();

  // Conservation: every mark is listed and its cell projected as a blank.
  std::vector<DecodedLogicalRecordSource> records;
  records.push_back(std::move(record));
  const auto layout = geist::detail::extract_layout_ir(records);
  const auto ownership = geist::detail::build_ownership_ir(records, layout);
  std::size_t unframed = 0;
  const auto drawn =
      geist::detail::project_drawn_topic_text(records, ownership, &unframed);
  require(drawn.margin_marks.size() == 4,
          "the projection does not list the four stored marks");
  if (drawn.margin_marks.size() == 4) {
    require(drawn.margin_marks[0].place == MarginMarkPlaceIR::title_column &&
                drawn.margin_marks[0].line == 0,
            "the title mark is not listed first");
    require(drawn.margin_marks[2].mark == U'#' &&
                drawn.margin_marks[2].line == 3,
            "the `#` mark is not listed with its line");
  }
  require(drawn.text.find('#') == std::string::npos,
          "the margin mark is projected as a drawn glyph");
  require(drawn.text.find("39.") != std::string::npos &&
              drawn.text.find("TPADD") != std::string::npos,
          "the marked rows lost their text");
}

} // namespace

int main() {
  test_line_marks();
  geist_test::exit_with_failures();
  std::cout << "margin mark line checks passed\n";
  return 0;
}
