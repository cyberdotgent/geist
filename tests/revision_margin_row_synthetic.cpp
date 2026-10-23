// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A drawn row behind a revision bar keeps its first word.
//
// ICHA6MST 3.1.2 record 561 lines 25-26 (hosted DT 19910801172557):
//
//   line 25  `cfont 3 1 2 8 6 2 15 7 2 23 6 2`
//   line 26  ` | 1    Master Console Action`
//
// Line 26 is one display line: its length byte (9, spelled as a blank by the
// dictionary), a one-cell blank, the revision bar `|`, the routing code `1`,
// a three-cell blank run, then `Master`, `Console` and `Action`.  Hosted
// serves ` | <B>1</B>    <B>Master</B> <B>Console</B> <B>Action</B>`: the
// `cfont` columns count across the margin, and cell 3 is the `1`.
//
// A one-byte word followed by a blank run looks like a row-control slot and
// its origin, so a reader that guesses rows from token shapes splits the line
// at `1`, calls the `1` a marker and drops it -- and every span of the
// `cfont` then lands three cells early (`**M**aster`).  The record's own
// display-line framing says the line is one row; the word inside it is text.
//
// Everything here is synthetic: the test builds the record by hand.

#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/layout_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "revision_margin_row_synthetic: " << message << '\n';
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

  // One display line: its length byte (spelled `prefix`) and its tokens.
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
    return std::move(record);
  }
};

void test_routing_code_behind_revision_bar() {
  RecordBuilder builder;
  builder.line({{words("cfont")}, {words("3")}, {words("1")}, {words("2")},
                {words("8")}, {words("6")}, {words("2")}});
  // The length byte of the drawn row is spelled as a blank.
  const auto row_line =
      builder.line({{words(" ")},
                    {words("|")},
                    {words("1")},
                    {words("   ")},
                    {words("Master"), 2},
                    {words("Console"), 2},
                    {words("Action")}},
                   words(" "));
  builder.line({});
  builder.line({{words(" ")}, {words("|")}, {words("      ")}, {words("The")},
                {words("message"), 2}, {words("indicates.")}},
               words(")"));
  std::vector<DecodedLogicalRecordSource> records;
  records.push_back(builder.build(561));
  const auto &record = records.front();
  require(record.ir.display_lines_parse,
          "the synthetic record's display lines do not parse");
  const auto bar = row_line + 2;
  const auto code = row_line + 3;
  require(geist::detail::token_words_to_ascii(record.tokens[code]) == "1",
          "the synthetic routing code is not where the test expects it");

  const auto layout = geist::detail::extract_layout_ir(records);
  const geist::detail::PhysicalRowIR *holder = nullptr;
  for (const auto &run : layout.runs)
    for (const auto &row : run.rows) {
      require(row.token_begin != code,
              "a row opens on the routing code `1`: it was read as a "
              "row-control slot");
      if (row.token_begin <= bar && code < row.token_end) holder = &row;
    }
  require(holder != nullptr,
          "no row holds the revision bar and the routing code together");
  if (holder != nullptr)
    require(holder->visible_text.find("1    Master Console Action") !=
                std::string::npos,
            "the drawn row lost its routing code: '" + holder->visible_text +
                "'");

  const auto ownership = geist::detail::build_ownership_ir(records, layout);
  for (const auto &cell : ownership.cells)
    if (cell.logical_record == 561 && cell.token_index == code)
      require(cell.disposition ==
                  geist::detail::SourceDisposition::visible_content,
              "the routing code `1` is not owned as visible content");
}

// The same loss without a revision bar and without a blank-spelled length
// byte.  SC30-3347-06 C.2 record 401 lines 18-19 (hosted DT 19930330181700):
// `cfont 26 1 2`, then a line whose length byte is spelled as a glyph run,
// a 26-blank origin (one two-byte token), the severity code `0`, a
// three-blank run and `Provides information only.`.  Hosted serves
// `<B>0</B>    Provides information only.`; the code is the line's first
// word, not its row-control slot.
void test_first_word_after_glyph_spelled_length_byte() {
  RecordBuilder builder;
  builder.line({{words("cfont")}, {words("26")}, {words("1")}, {words("2")}});
  const auto row_line = builder.line(
      {{words(std::string(26, ' ')), 2},
       {words("0")},
       {words("   ")},
       {words("Provides"), 2},
       {words("information")},
       {words("only.")}},
      TokenWords(10, 0x2500));
  builder.line({});
  builder.line({{words("cfont")}, {words("26")}, {words("1")}, {words("2")}});
  builder.line({{words(std::string(26, ' ')), 2},
                {words("4")},
                {words("   ")},
                {words("Indicates"), 2},
                {words("an")},
                {words("error.")}},
               TokenWords(10, 0x2500));
  std::vector<DecodedLogicalRecordSource> records;
  records.push_back(builder.build(401));
  const auto &record = records.front();
  require(record.ir.display_lines_parse,
          "the glyph-spelled record's display lines do not parse");
  const auto code = row_line + 2;
  require(geist::detail::token_words_to_ascii(record.tokens[code]) == "0",
          "the synthetic severity code is not where the test expects it");
  const auto layout = geist::detail::extract_layout_ir(records);
  bool held = false;
  for (const auto &run : layout.runs)
    for (const auto &row : run.rows) {
      require(row.token_begin != code,
              "a row opens on the severity code `0`: it was read as a "
              "row-control slot");
      if (row.token_begin < code && code < row.token_end &&
          row.visible_text.find("0    Provides") != std::string::npos)
        held = true;
    }
  require(held, "no row holds the severity code with its text");
  const auto ownership = geist::detail::build_ownership_ir(records, layout);
  for (const auto &cell : ownership.cells)
    if (cell.logical_record == 401 && cell.token_index == code)
      require(cell.disposition ==
                  geist::detail::SourceDisposition::visible_content,
              "the severity code `0` is not owned as visible content");
}

} // namespace

int main() {
  test_routing_code_behind_revision_bar();
  test_first_word_after_glyph_spelled_length_byte();
  geist_test::exit_with_failures();
  std::cout << "revision margin row checks passed\n";
  return 0;
}
