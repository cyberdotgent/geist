// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Every drawn display line reaches a physical row, and no line-length byte
// ever reaches one as text (issue #66).
//
// A record payload is `<length byte><that many bytes of tokens>`.  The length
// byte is the row-control slot, always and only, but a token reader resolves
// it through the dictionary like any other token, so it routinely spells an
// ordinary word -- `as`, `the`, `*` -- or a run of blanks.  The Layout IR used
// to recognise the slot by that spelling alone, which cost it two things:
//
//   * a line whose length byte spells blanks opened no row at all, so the
//     words the reader draws on that line were in no row and every consumer
//     that reads rows lost them; and
//   * a line whose length byte spells blanks was instead paired with the last
//     drawn word of the line before it, one token early, so that word became
//     a "marker" and dropped out of the row text.
//
// A line that opens on its own control word (`SI` index terms) is that
// control's line, not display text, and must stay out of the rows either way.
//
// Everything here is synthetic: the tests build `DecodedLogicalRecordSource`
// values by hand and open no book.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/layout_ir.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenFramingRole;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
    geist_test::record_failure();
  }
}

void append(DecodedLogicalRecordSource &record, std::uint16_t encoded,
            std::uint8_t width, TokenWords words) {
  record.encoded_tokens.push_back({encoded, width});
  record.tokens.push_back(std::move(words));
}

// Rebuilds the typed IR of a hand-assembled record and lets the decoder decide
// the framing, exactly as `decode_record_payload_ir` does in production.  No
// test re-derives the display-line walk.
void refresh(DecodedLogicalRecordSource &record) {
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  record.ir.logical_record = record.logical_record;
  record.ir.tokens.clear();
  std::uint32_t byte = 0;
  for (std::size_t token = 0; token < record.tokens.size(); ++token) {
    const auto encoded = record.encoded_tokens[token];
    const auto spacing = !record.tokens[token].empty() &&
                         record.tokens[token].front() < 4;
    record.ir.tokens.push_back(
        {token, encoded, record.tokens[token],
         {byte, static_cast<std::uint32_t>(byte + encoded.width)}, spacing,
         spacing ? record.tokens[token].front() : std::uint16_t{3}});
    byte += encoded.width;
  }
  record.ir.payload_range = {0, byte};
  geist::detail::assign_display_line_framing(record.ir);
  record.control_segments = geist::detail::decode_control_segments(
      record.logical_record, record.assembled, record.encoded_tokens,
      record.ir.display_lines);
}

std::string row_texts(const geist::detail::LayoutIR &layout) {
  std::string all;
  for (const auto &run : layout.runs)
    for (const auto &row : run.rows) all += row.visible_text + "\n";
  return all;
}

bool covers_token(const geist::detail::LayoutIR &layout, std::size_t token) {
  for (const auto &run : layout.runs)
    for (const auto &row : run.rows)
      if (token >= row.token_begin && token < row.token_end) return true;
  return false;
}

// Three display lines in one text payload.  The first opens on the `SI` index
// term control; the second's length byte spells eight blanks; the third's
// spells a single box glyph.  Only the third was ever recognised.
void a_blank_spelled_length_byte_still_opens_its_row() {
  DecodedLogicalRecordSource record;
  record.logical_record = 31;
  // Line 0: `SI index term` -- five bytes of tokens, opening on the control's
  // own word rather than on a display origin.
  append(record, 5, 1, {0x2500, 0x2500});
  append(record, 0x50, 1, {'S', 'I'});
  append(record, 0x51, 2, {'i', 'n', 'd', 'e', 'x'});
  append(record, 0x52, 2, {'t', 'e', 'r', 'm'});
  // Line 1: six bytes of tokens, its length byte spelled as blanks.
  append(record, 6, 1, {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '});
  append(record, 0x53, 1, {' ', ' ', ' '});
  append(record, 0x54, 2, {'D', 'r', 'a', 'w', 'n'});
  append(record, 0x55, 2, {'s', 'e', 'n', 't', 'e', 'n', 'c', 'e'});
  append(record, 0x56, 1, {'.'});
  // Line 2: three bytes of tokens, its length byte spelled as a box glyph.
  append(record, 3, 1, {0x2534});
  append(record, 0x57, 1, {' ', ' ', ' '});
  append(record, 0x58, 2, {'T', 'a', 'i', 'l'});
  refresh(record);

  require(record.ir.display_lines_parse &&
              record.ir.display_lines.size() == 3,
          "the synthetic record did not frame into three display lines");
  require(record.ir.tokens[4].framing == TokenFramingRole::line_length,
          "token 4 is the blank-spelled length byte and was not stamped as "
          "one");

  const auto layout = geist::detail::extract_layout_ir({record});
  std::string error;
  require(geist::detail::verify_layout_ir({record}, layout, &error),
          "row-coverage layout failed verification: " + error);

  const auto texts = row_texts(layout);
  require(texts.find("Drawn sentence") != std::string::npos,
          "the line whose length byte spells blanks reached no row; its rows "
          "are:\n" + texts);
  require(texts.find("Tail") != std::string::npos,
          "the glyph-spelled line lost its row; its rows are:\n" + texts);
  // The index term shares the payload with the sentence and must stay out of
  // it: hosted serves index terms from the index, never from the topic body.
  require(texts.find("index term") == std::string::npos,
          "an SI index-term line was admitted to a display row:\n" + texts);
  require(!covers_token(layout, 1) && !covers_token(layout, 2) &&
              !covers_token(layout, 3),
          "the index term's own tokens were claimed by a display row");
}

// Two display lines where the first ends on a drawn word and the second's
// length byte spells three blanks.  Read by spelling alone that pair looks
// like `<marker><origin>` one token early, and the drawn word is lost.
void a_drawn_word_before_a_blank_length_byte_stays_in_its_row() {
  DecodedLogicalRecordSource record;
  record.logical_record = 32;
  // Line 0: `   Purpose`, so the line under test is not the record's first.
  append(record, 3, 1, {0x2534});
  append(record, 0x50, 1, {' ', ' ', ' '});
  append(record, 0x51, 2, {'P', 'u', 'r', 'p', 'o', 's', 'e'});
  // Line 1: `   by the` -- its last word is drawn text of this line.
  append(record, 3, 1, {0x2534});
  append(record, 0x52, 1, {' ', ' ', ' '});
  append(record, 0x53, 1, {'b', 'y'});
  append(record, 0x54, 1, {'t', 'h', 'e'});
  // Line 2: its length byte spells exactly the three blanks an origin run
  // would, which is the whole ambiguity.
  append(record, 3, 1, {' ', ' ', ' '});
  append(record, 0x55, 1, {' ', ' ', ' '});
  append(record, 0x56, 2, {'I', 'N', 'D', 'D'});
  refresh(record);

  require(record.ir.display_lines_parse &&
              record.ir.display_lines.size() == 3,
          "the synthetic record did not frame into three display lines");
  require(record.ir.tokens[7].framing == TokenFramingRole::line_length &&
              record.ir.tokens[6].framing == TokenFramingRole::line_content,
          "the framing did not place `the` inside line 1 and the blank byte "
          "at the head of line 2");

  const auto layout = geist::detail::extract_layout_ir({record});
  std::string error;
  require(geist::detail::verify_layout_ir({record}, layout, &error),
          "snapped-boundary layout failed verification: " + error);

  const auto texts = row_texts(layout);
  require(texts.find("by the") != std::string::npos,
          "the word `the` is drawn at the end of line 1 and left the rows "
          "entirely; the rows are:\n" + texts);
  require(texts.find("INDD") != std::string::npos,
          "line 2 lost its row; the rows are:\n" + texts);
  for (const auto &run : layout.runs)
    for (const auto &row : run.rows)
      require(row.visible_text.find("the INDD") == std::string::npos,
              "line 2's row swallowed the previous line's last word");
}

} // namespace

int main() {
  a_blank_spelled_length_byte_still_opens_its_row();
  a_drawn_word_before_a_blank_length_byte_stays_in_its_row();
  return 0;
}
