// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A `CTocE` title ends at its own display line, never at the next entry
// (issue #86).
//
// The contents topic stores one table-of-contents entry per display line:
//
//   <length byte> CTocE <level> <style> <id> <title>
//
// The length byte is a raw byte below the book's token threshold, so a token
// reader resolves it through the dictionary and it acquires an arbitrary
// spelling.  Flattened, that spelling lands between one entry's title and the
// next entry's `CTocE`, and a reader that runs the title to the next `CTocE`
// keeps it: hosted BookManager serves `About This Book`, `Symbols in
// Messages` and `Response Time Utility` where the flattened reading produced
// `About This Book %`, `Symbols in Messages ;` and `Response Time Utility
// <BOOK>`.
//
// Everything here is synthetic: the tests build `DecodedLogicalRecordSource`
// values by hand and open no book.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/container/toc_entry_framing.hpp"
#include "test_failures.hpp"

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
// the framing, exactly as the record decoder does in production.  No test
// re-derives the display-line walk.
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
        {token, encoded, record.tokens[token],
         {byte, static_cast<std::uint32_t>(byte + encoded.width)}, spacing,
         spacing ? record.tokens[token].front() : std::uint16_t{3}});
    byte += encoded.width;
  }
  record.ir.payload_range = {0, byte};
  geist::detail::assign_display_line_framing(record.ir);
}

std::string text_of(const DecodedLogicalRecordSource &record) {
  return geist::detail::token_words_to_ascii(record.assembled.words);
}

// Two contents lines whose length bytes are spelled `%` and `;` -- the
// IBMMMSTR shape, where the flattened reading produced `About This Book %`.
DecodedLogicalRecordSource two_entries_behind_spelled_length_bytes() {
  DecodedLogicalRecordSource record;
  record.logical_record = 3;
  // Line 0: 12 bytes of tokens -- `ctoce 0 1 PREFACE About This Book`.
  append(record, 12, 1, {3, '%'});
  append(record, 0x40, 2, {'c', 't', 'o', 'c', 'e'});
  append(record, 0x41, 1, {'0'});
  append(record, 0x42, 1, {'1'});
  append(record, 0x43, 2, {'P', 'R', 'E', 'F', 'A', 'C', 'E'});
  append(record, 0x44, 2, {'A', 'b', 'o', 'u', 't'});
  append(record, 0x45, 2, {'T', 'h', 'i', 's'});
  append(record, 0x46, 2, {'B', 'o', 'o', 'k'});
  // Line 1: 11 bytes of tokens -- `ctoce 1 3 1.2 Symbols in Messages`.
  append(record, 11, 1, {3, ';'});
  append(record, 0x40, 2, {'c', 't', 'o', 'c', 'e'});
  append(record, 0x47, 1, {'1'});
  append(record, 0x48, 1, {'3'});
  append(record, 0x49, 2, {'1', '.', '2'});
  append(record, 0x4a, 2, {'S', 'y', 'm', 'b', 'o', 'l', 's'});
  append(record, 0x4b, 1, {'i', 'n'});
  append(record, 0x4c, 2, {'M', 'e', 's', 's', 'a', 'g', 'e', 's'});
  refresh(record);
  return record;
}

void a_toc_title_stops_at_its_display_line() {
  const auto record = two_entries_behind_spelled_length_bytes();
  require(record.ir.display_lines_parse,
          "the synthetic contents record's display lines did not parse");
  require(record.ir.display_lines.size() == 2,
          "the synthetic contents record did not frame into two display "
          "lines");

  const auto text = text_of(record);
  // The defect is visible in the flattened string: the second line's length
  // byte is spelled `;` and sits between the first title and the next entry.
  require(text.find("Book ; ctoce") != std::string::npos,
          "the synthetic record does not reproduce the flattened shape under "
          "test; it reads '" + text + "'");

  const auto starts = geist::detail::display_line_start_output_offsets(record);
  require(starts.size() == 2,
          "the record's two display lines did not yield two starts");

  const auto entries = geist::detail::extract_toc_entries(text, starts);
  require(entries.size() == 2,
          "the framed reading did not find both TOC entries");
  if (entries.size() != 2) return;
  require(entries[0].id == "PREFACE" && entries[0].title == "About This Book",
          "the first entry's title is '" + entries[0].title +
              "', not the display line's own text 'About This Book'; the "
              "next line's length byte was read as title text");
  require(entries[1].id == "1.2" && entries[1].title == "Symbols in Messages",
          "the second entry's title is '" + entries[1].title +
              "', not 'Symbols in Messages'");
}

// Without the framing there is no decided boundary, so the reading that stood
// before it was carried must stand: the length byte's spelling is kept rather
// than guessed away.  A guess would be a heuristic, and a title that really
// ends in punctuation would lose it.
void an_unframed_contents_record_decides_nothing() {
  const auto record = two_entries_behind_spelled_length_bytes();
  const auto entries = geist::detail::extract_toc_entries(text_of(record));
  require(entries.size() == 2,
          "the unframed reading did not find both TOC entries");
  if (entries.size() != 2) return;
  require(entries[0].title == "About This Book ;",
          "an unframed record has no decided line boundary and must keep its "
          "previous reading; the title is '" + entries[0].title + "'");
}

// The whole title of a line is kept, however long: the boundary is the line,
// not a length or a punctuation class.  This is the shape that makes a
// wholesale "trim the last token" rule wrong -- `SC09-1416, ... Guide and
// Reference` is one line and keeps every word of it.
void a_toc_title_reaches_the_end_of_its_display_line() {
  DecodedLogicalRecordSource record;
  record.logical_record = 4;
  // Line 0: 14 bytes -- `ctoce 1 2 2.1.45 Report Layout Utility Guide and
  // Reference`.
  append(record, 14, 1, {3, '*'});
  append(record, 0x40, 2, {'c', 't', 'o', 'c', 'e'});
  append(record, 0x41, 1, {'1'});
  append(record, 0x42, 1, {'2'});
  append(record, 0x43, 2, {'2', '.', '1', '.', '4', '5'});
  append(record, 0x44, 2, {'R', 'e', 'p', 'o', 'r', 't'});
  append(record, 0x45, 2, {'L', 'a', 'y', 'o', 'u', 't'});
  append(record, 0x46, 2, {'U', 't', 'i', 'l', 'i', 't', 'y'});
  append(record, 0x47, 2, {'R', 'e', 'f', 'e', 'r', 'e', 'n', 'c', 'e'});
  // Line 1: 8 bytes -- `ctoce 0 1 3.0 Codes`.
  append(record, 8, 1, {3, '$'});
  append(record, 0x40, 2, {'c', 't', 'o', 'c', 'e'});
  append(record, 0x48, 1, {'0'});
  append(record, 0x49, 1, {'1'});
  append(record, 0x4a, 2, {'3', '.', '0'});
  append(record, 0x4b, 2, {'C', 'o', 'd', 'e', 's'});
  refresh(record);

  require(record.ir.display_lines_parse,
          "the long-title record's display lines did not parse");
  const auto entries = geist::detail::extract_toc_entries(
      text_of(record), geist::detail::display_line_start_output_offsets(record));
  require(entries.size() == 2, "the long-title record lost an entry");
  if (entries.empty()) return;
  require(entries[0].title == "Report Layout Utility Reference",
          "a title must reach the end of its own display line; it is '" +
              entries[0].title + "'");
}

} // namespace

int main() {
  a_toc_title_stops_at_its_display_line();
  an_unframed_contents_record_decides_nothing();
  a_toc_title_reaches_the_end_of_its_display_line();
  return 0;
}
