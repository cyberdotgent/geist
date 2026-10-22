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

#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/layout_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
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
    const auto spacing =
        !record.tokens[token].empty() && record.tokens[token].front() < 4;
    record.ir.tokens.push_back(
        {token,
         encoded,
         record.tokens[token],
         {byte, static_cast<std::uint32_t>(byte + encoded.width)},
         spacing,
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
    for (const auto &row : run.rows)
      all += row.visible_text + "\n";
  return all;
}

bool covers_token(const geist::detail::LayoutIR &layout, std::size_t token) {
  for (const auto &run : layout.runs)
    for (const auto &row : run.rows)
      if (token >= row.token_begin && token < row.token_end)
        return true;
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

  require(record.ir.display_lines_parse && record.ir.display_lines.size() == 3,
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
          "are:\n" +
              texts);
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
void a_drawn_word_before_a_blank_length_byte_stays_in_its_row(bool title) {
  DecodedLogicalRecordSource record;
  record.logical_record = 32;
  if (title) {
    append(record, 2, 1, {0x2534});
    append(record, 0x60, 1, {'S', 'T'});
    append(record, 0x61, 1, {'B', 'o', 'o', 'k', 's'});
  }
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
              record.ir.display_lines.size() == (title ? 4U : 3U),
          "the synthetic record did not frame into three display lines");
  const auto shift = title ? 3U : 0U;
  require(
      record.ir.tokens[7 + shift].framing == TokenFramingRole::line_length &&
          record.ir.tokens[6 + shift].framing == TokenFramingRole::line_content,
      "the framing did not place `the` inside line 1 and the blank byte "
      "at the head of line 2");

  const auto layout = geist::detail::extract_layout_ir({record});
  std::string error;
  require(geist::detail::verify_layout_ir({record}, layout, &error),
          "snapped-boundary layout failed verification: " + error);

  const auto texts = row_texts(layout);
  require(texts.find("by the") != std::string::npos,
          "the word `the` is drawn at the end of line 1 and left the rows "
          "entirely; the rows are:\n" +
              texts);
  require(texts.find("INDD") != std::string::npos,
          "line 2 lost its row; the rows are:\n" + texts);
  for (const auto &run : layout.runs)
    for (const auto &row : run.rows)
      require(row.visible_text.find("the INDD") == std::string::npos,
              "line 2's row swallowed the previous line's last word");
  auto mutated = layout;
  for (auto &run : mutated.runs)
    for (auto &row : run.rows)
      if (row.marker && row.marker->token_index == 3 + shift) {
        row.token_begin = 6 + shift;
        row.marker->token_index = row.token_begin;
        row.marker->byte_range = record.ir.tokens[row.token_begin].byte_range;
      }
  if (title)
    require(
        !geist::detail::verify_layout_ir({record}, mutated, &error),
        "a native payload word admitted as a title marker passed verification");
}

void legacy_compact_fragments_keep_their_model(bool title) {
  DecodedLogicalRecordSource record;
  record.logical_record = 33;
  if (title) {
    append(record, 2, 1, {'<'});
    append(record, 0x60, 1, {'S', 'T'});
    append(record, 0x61, 1, {'B', 'o', 'o', 'k', 's'});
  }
  append(record, 5, 1, {'<'});
  append(record, 0x50, 1, {' ', ' ', ' '});
  append(record, 0x51, 1, {'F', 'i', 'r', 's', 't'});
  append(record, 0x52, 1, {'s', 'l', 'o', 't'});
  append(record, 0x53, 1, {' ', ' ', ' ', ' ', ' ', ' ', ' '});
  append(record, 0x54, 1, {'S', 'e', 'c', 'o', 'n', 'd'});
  refresh(record);
  const auto layout = geist::detail::extract_layout_ir({record});
  std::string error;
  require(
      record.ir.display_lines_parse && layout.runs.size() == 1 &&
          layout.runs.front().rows.size() == 2 &&
          geist::detail::verify_layout_ir({record}, layout, &error),
      "legacy compact physical fragments were conflated with native rows: " +
          error + " runs=" + std::to_string(layout.runs.size()) + " rows=" +
          std::to_string(
              layout.runs.empty() ? 0 : layout.runs.front().rows.size()) +
          " text=" + row_texts(layout));
}

void snapping_a_title_boundary_extends_its_existing_payload_row() {
  DecodedLogicalRecordSource record;
  record.logical_record = 33;
  append(record, 4, 1, {' '});
  append(record, 0x60, 1, {'S', 'T'});
  append(record, 0x61, 1, {' ', ' '});
  append(record, 0x62, 1, {'S', 'a', 'm', 'p', 'l', 'e'});
  append(record, 0x63, 1, {'M', 'e', 's', 's', 'a', 'g', 'e', 's'});
  append(record, 3, 1, {' '});
  append(record, 0x64, 1, {' ', ' ', ' '});
  append(record, 0x65, 1, {'I', 'n', 't', 'r', 'o'});
  append(record, 0x66, 1, {'b', 'e', 'f', 'o', 'r', 'e'});
  refresh(record);
  const auto layout = geist::detail::extract_layout_ir({record});
  std::string error;
  require(record.ir.display_lines_parse &&
              geist::detail::verify_layout_ir({record}, layout, &error),
          "title prefix-snap fixture failed native verification: " + error);
  require(layout.runs.size() == 1 && layout.runs.front().rows.size() == 2,
          "title/intro must retain their two physical fragments");
  if (layout.runs.empty() || layout.runs.front().rows.size() != 2)
    return;
  const auto &title = layout.runs.front().rows[0];
  const auto &intro = layout.runs.front().rows[1];
  require(title.visible_text == "Sample Messages" && title.token_end == 5 &&
              !title.marker && intro.token_begin == 5 &&
              intro.visible_text == "Intro before",
          "snapping the intro prefix orphaned the title's terminal word: " +
              row_texts(layout));
  const auto ownership =
      geist::detail::build_verified_ownership_ir({record}, layout, &error);
  require(ownership.has_value(), "snapped title ownership failed: " + error);
  if (ownership)
    for (const auto &cell : ownership->ir().cells)
      if (cell.token_index == 4)
        require(
            cell.disposition ==
                geist::detail::SourceDisposition::visible_content,
            "title's terminal word did not regain visible source ownership");
}

void only_an_unplaced_selector_crosses_the_record_boundary(bool consumed) {
  DecodedLogicalRecordSource opening;
  opening.logical_record = 40;
  append(opening, 1, 1, {'<'});
  append(opening, 0x60, 1,
         {'c', 's', 'e', 'l', 'e', 'c', 't', ' ', '3', ' ', '5', ' ', 'T', 'A',
          'R', 'G', 'E', 'T'});
  if (consumed) {
    append(opening, 2, 1, {'<'});
    append(opening, 0x61, 1, {' ', ' ', ' '});
    append(opening, 0x62, 1, {'F', 'i', 'r', 's', 't'});
  }
  refresh(opening);
  DecodedLogicalRecordSource continuation;
  continuation.logical_record = 41;
  append(continuation, 5, 1, {'<'});
  append(continuation, 0x50, 1, {' ', ' ', ' '});
  append(continuation, 0x51, 1, {'F', 'i', 'r', 's', 't'});
  append(continuation, 0x52, 1, {'s', 'l', 'o', 't'});
  append(continuation, 0x53, 1, {' ', ' ', ' ', ' ', ' ', ' ', ' '});
  append(continuation, 0x54, 1, {'S', 'e', 'c', 'o', 'n', 'd'});
  refresh(continuation);
  const auto layout = geist::detail::extract_layout_ir({opening, continuation});
  std::size_t rows = 0;
  for (const auto &run : layout.runs)
    for (const auto &row : run.rows)
      if (row.logical_record == 41)
        ++rows;
  std::string error;
  require(rows == (consumed ? 2U : 1U) &&
              geist::detail::verify_layout_ir({opening, continuation}, layout,
                                              &error),
          "a consumed selector changed unrelated next-record fragments: " +
              error);
}

void selected_native_row_ends_before_unselected_compact_fragments() {
  DecodedLogicalRecordSource record;
  record.logical_record = 42;
  append(record, 1, 1, {'<'});
  append(record, 0x60, 1,
         {'c', 's', 'e', 'l', 'e', 'c', 't', ' ', '3', ' ', '6', ' ', 'T', 'A',
          'R', 'G', 'E', 'T'});
  append(record, 2, 1, {'<'});
  append(record, 0x61, 1, {' ', ' ', ' '});
  append(record, 0x62, 1, {'C', 'h', 'o', 'i', 'c', 'e'});
  // The following line is not selected. Its blank-spelled native length
  // cannot extend the selected row; its inner compact boundary remains.
  append(record, 5, 1, {' ', ' ', ' '});
  append(record, 0x63, 1, {' ', ' ', ' '});
  append(record, 0x64, 1, {'F', 'i', 'r', 's', 't'});
  append(record, 0x65, 1, {'s', 'l', 'o', 't'});
  append(record, 0x66, 1, {' ', ' ', ' ', ' ', ' ', ' ', ' '});
  append(record, 0x67, 1, {'S', 'e', 'c', 'o', 'n', 'd'});
  refresh(record);
  const auto layout = geist::detail::extract_layout_ir({record});
  std::string error;
  require(record.ir.display_lines_parse && layout.runs.size() == 1 &&
              layout.runs.front().rows.size() == 3 &&
              geist::detail::verify_layout_ir({record}, layout, &error),
          "selected native row consumed unselected compact text: " + error +
              " rows=" + row_texts(layout));
  if (layout.runs.empty() || layout.runs.front().rows.empty())
    return;
  const auto &selected = layout.runs.front().rows.front();
  require(selected.token_begin == 2 && selected.token_end == 5 &&
              selected.visible_text == "Choice",
          "selected native row does not end at its proven display-line end");
  if (layout.runs.front().rows.size() != 3)
    return;
  require(layout.runs.front().rows[1].visible_text == "First" &&
              layout.runs.front().rows[2].visible_text == "Second",
          "later unselected line lost its legacy compact fragments");
  auto mutated = layout;
  mutated.runs.front().rows.front().token_end = 8;
  mutated.runs.front().rows.erase(mutated.runs.front().rows.begin() + 1);
  require(
      !geist::detail::verify_layout_ir({record}, mutated, &error),
      "verifier accepted an authoritative native row spanning the next line");
}

// An `SRTBL` table envelope that ends its record continues into the next
// record's leading text segment, and the first drawn line there is a row of
// the envelope.  SC21-8295-03 `COMMENTS` record 968 ends with
// `SRTBLTBLUNIQ161` and its header rules; record 969 opens with the envelope's
// only body line, `   | Overall satisfaction ... |`, then `SRETBL`.  Hosted
// BookServer (`SC21-829`, DT 19970409135933) serves that line as the
// `Overall satisfaction` row of the table's `<table>`.
void a_table_envelope_crosses_the_record_boundary(bool envelope) {
  DecodedLogicalRecordSource opening;
  opening.logical_record = 968;
  append(opening, 1, 1, {'<'});
  if (envelope)
    append(opening, 0x60, 1,
           {'S', 'R', 'T', 'B', 'L', 'T', 'B', 'L', 'U', 'N', 'I', 'Q', '1'});
  else
    append(opening, 0x60, 1, {'S', 'R', 'L', 'I', 'S', 'X'});
  refresh(opening);
  DecodedLogicalRecordSource continuation;
  continuation.logical_record = 969;
  // The line's length byte spells three blanks, as record 969 token 0 does.
  // The text segment that continues the envelope begins at its first word:
  // the line's length byte and origin run lie before it, so the row cannot
  // be recognised from the segment's own tokens.
  append(continuation, 3, 1, {' ', ' ', ' '});
  append(continuation, 0x50, 1, {' ', ' ', ' '});
  append(continuation, 0x53, 1, {'O', 'v', 'e', 'r', 'a', 'l', 'l'});
  append(continuation, 0x54, 1,
         {'s', 'a', 't', 'i', 's', 'f', 'a', 'c', 't', 'i', 'o', 'n'});
  refresh(continuation);
  const auto layout = geist::detail::extract_layout_ir({opening, continuation});
  std::string texts;
  for (const auto &run : layout.runs)
    for (const auto &row : run.rows)
      if (row.logical_record == 969)
        texts += "[" + row.visible_text + "]";
  std::string error;
  require(geist::detail::verify_layout_ir({opening, continuation}, layout,
                                          &error),
          "the continued envelope's layout did not verify: " + error);
  if (envelope)
    require(texts.find("Overall satisfaction") != std::string::npos,
            "the continued table envelope's first line is in no row: " +
                texts);
  else
    require(texts.empty(),
            "a record after a non-envelope control gained a row: " + texts);
}

} // namespace

int main() {
  a_blank_spelled_length_byte_still_opens_its_row();
  a_drawn_word_before_a_blank_length_byte_stays_in_its_row(false);
  a_drawn_word_before_a_blank_length_byte_stays_in_its_row(true);
  legacy_compact_fragments_keep_their_model(false);
  legacy_compact_fragments_keep_their_model(true);
  snapping_a_title_boundary_extends_its_existing_payload_row();
  only_an_unplaced_selector_crosses_the_record_boundary(false);
  only_an_unplaced_selector_crosses_the_record_boundary(true);
  selected_native_row_ends_before_unselected_compact_fragments();
  a_table_envelope_crosses_the_record_boundary(true);
  a_table_envelope_crosses_the_record_boundary(false);
  return 0;
}
