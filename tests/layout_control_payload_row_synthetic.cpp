// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A selector payload that runs across several display lines, one of which
// opens on a length byte the dictionary spells as blanks.
//
// SC41-3550-01 2.2, logical record 40, segment 12:
//
//   cselect 14 44 HDRINSRUM                       its own display line
//   '  You Install RUMBA/400 Software" in topic 2.1.   (length byte `'`,
//                                                  then a two-byte blank run)
//   [11 blanks]   6.  Click on Install.            (length byte 0x0A, which
//                                                  the dictionary spells as
//                                                  eleven blanks, token 240)
//   *   7.  Select Yes to run RUMBA/400.           (length byte `*`)
//
// The layout builder opened a markerless "control payload" row on the first
// all-blank token of the payload -- the blank-spelled length byte -- before
// the display-line framing put the line boundaries in.  The row then held
// tokens 240-242 a second time, beside the row the line's own length byte
// opens, and the topic declined ("physical rows overlap or are out of
// source order"); hosted (DT 19950207103722) serves each step once.
//
// Everything here is synthetic: the test builds the record by hand.

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
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "layout_control_payload_row_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

struct Token {
  TokenWords words;
  std::uint8_t width = 2;
};

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;

  // One display line: a one-byte length byte spelled `spelling`, then the
  // line's tokens at their stored widths.
  void line(TokenWords spelling, std::vector<Token> content) {
    std::uint16_t length = 0;
    for (const auto &token : content) length += token.width;
    record.encoded_tokens.push_back({length, 1});
    record.tokens.push_back(std::move(spelling));
    for (auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, token.width});
      record.tokens.push_back(std::move(token.words));
    }
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

void a_blank_spelled_length_byte_opens_no_second_row() {
  RecordBuilder builder;
  builder.line(TokenWords{0x25BA},
               {{words("cselect")}, {words("14")}, {words("44")},
                {words("HDRINSRUM")}});
  builder.line(words("'"), {{words("              ")}, {words("You")},
                            {words("Install")}, {words("topic")},
                            {words("2.1.")}});
  builder.line(words("           "),
               {{words("   "), 1}, {words("6.")}, {words("Click")},
                {words("Install.")}});
  builder.line(words("*"), {{words("   "), 1}, {words("7.")},
                            {words("Select")}, {words("Yes.")}});
  std::vector<DecodedLogicalRecordSource> records;
  records.push_back(builder.build(40));
  require(records.front().ir.display_lines_parse,
          "the synthetic record's display lines did not parse");
  const auto layout = geist::detail::extract_layout_ir(records);
  std::string error;
  require(geist::detail::verify_layout_ir(records, layout, &error),
          "the selector payload's rows do not verify: " + error);
  // Every token stands in at most one row.
  std::vector<int> owners(records.front().tokens.size(), 0);
  for (const auto &run : layout.runs)
    for (const auto &row : run.rows)
      for (auto token = row.token_begin; token < row.token_end; ++token)
        ++owners[token];
  for (std::size_t token = 0; token < owners.size(); ++token)
    require(owners[token] <= 1,
            "token " + std::to_string(token) + " stands in " +
                std::to_string(owners[token]) + " rows");
}

} // namespace

int main() {
  a_blank_spelled_length_byte_opens_no_second_row();
  geist_test::exit_with_failures();
  std::cout << "layout_control_payload_row_synthetic: ok\n";
  return 0;
}
