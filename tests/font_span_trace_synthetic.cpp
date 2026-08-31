// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// `bootrace --fonts` resolves a CFONT span on display columns (issue #82).
//
// A CFONT operand triple is `<column> <length> <code>`, and the columns are
// display columns of the row the control stands in front of
// (doc/boo-spec/markup.adoc, "Spans And The Display Row").  The trace used to resolve
// them against the flattened ASCII projection of the decoded record instead.
// A projection byte is a display column only where every word of the record
// renders exactly one byte wide, and a display line's length byte is the
// plainest case where it does not: the reader never prints that byte, but the
// projection spells it with whatever dictionary word the value happens to
// name, so every column of the row behind it is displaced by the width of
// that spelling.  SC09-2417-00 record 29 (topic `PREFACE.2.1`) is the
// reported case -- its `cfont 11 9 P 22 13 V` reported `>> STAT` and
// `ENT required` against hosted's `<kbd>STATEMENT</kbd>` and
// `<var>required_item</var>` (DT 19961114175628) -- and that book may not be
// a test dependency (issue #59), so the record here is built by hand with
// `assemble_logical_record_with_sources`.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "font_span_trace_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string& text) {
  return TokenWords(text.begin(), text.end());
}

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;

  // One display line: a length byte covering `content`, whose tokens are two
  // bytes each.  `length_spelling` is the dictionary word the byte's value
  // happens to name; the reader displays none of it.
  void line(const std::string& length_spelling,
            std::vector<TokenWords> content) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(words(length_spelling));
    for (auto& token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(std::move(token));
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

} // namespace

int main() {
  // Two display lines.  The first is the control alone; the second is the row
  // it styles, one token wide, with `STATEMENT` at display columns 11..19 and
  // `item` at 21..24.  The row's length byte is spelled `azb`, so the
  // flattened ASCII projection of the record carries three bytes and the
  // assembler's space in front of a row that starts at column 0 -- the
  // projection and the columns are four apart, and nothing in the row itself
  // says so.
  RecordBuilder builder;
  builder.line("<", {words("cfont"), words("11"), words("9"), words("P"),
                     words("21"), words("4"), words("V")});
  builder.line("azb", {words("           STATEMENT item")});
  const auto record = builder.build(7);

  require(geist::detail::record_display_lines(record) != nullptr,
          "the synthetic record's display lines do not parse");
  const auto* lines = geist::detail::record_display_lines(record);
  if (lines == nullptr || lines->size() != 2) {
    require(false, "expected exactly two display lines");
    return 0;
  }
  require(geist::detail::display_line_text(record, (*lines)[0]) ==
              "cfont 11 9 P 21 4 V",
          "the control line does not read as its own display text");
  require(geist::detail::display_line_text(record, (*lines)[1]) ==
              "           STATEMENT item",
          "the styled row does not read as its own display text");

  const auto decoded =
      geist::detail::token_words_to_ascii(record.assembled.words);
  require(decoded.find("azb") != std::string::npos,
          "the projection does not carry the length byte's spelling, so the "
          "fixture no longer reproduces the defect");

  const auto traced = geist::detail::trace_decoded_records(
      {decoded}, {record}, 7, std::map<std::string, std::string>{});
  if (traced.size() != 1 || traced.front().font_spans.size() != 2) {
    require(false, "expected two traced font spans, got " +
                       std::to_string(traced.empty()
                                          ? 0
                                          : traced.front().font_spans.size()));
    return 0;
  }
  const auto& first = traced.front().font_spans[0];
  const auto& second = traced.front().font_spans[1];

  // The operands are reported as the columns they are.
  require(first.offset == 11 && first.length == 9,
          "the first span's operands were rewritten");
  require(second.offset == 21 && second.length == 4,
          "the second span's operands were rewritten");
  // And the text is the row's cells at those columns.
  require(first.text == "STATEMENT",
          "the first span mapped onto '" + first.text + "', not 'STATEMENT'");
  require(second.text == "item",
          "the second span mapped onto '" + second.text + "', not 'item'");
  require(first.code == "P" && second.code == "V",
          "a span lost its style code");

  return 0;
}
