// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A CSELECT in a declared table addresses the display row below it, and that
// row may open the next logical record.
//
// S85G-8681-00 FRONT_1 (`EHLLAPI Functions by Function Number`) is one
// `cz OFF TABLE` grid of 53 rows, each row a `cselect 13 <n> HDRFUNC<n>`
// line over its `"<name> (<n>)" in topic <id>` cell.  Five of the selectors
// are the last display line of their record: record 19 line 39 is
// `cselect 13 41 HDRFUNC7` and the row it selects, `   |     7 | "Query
// Cursor Location (7)" in topic 6.29 ... |`, is line 0 of record 20.  Hosted
// BookServer (DT 19950803164615) links that row like every other one:
// `<a href="6.29?...#HDRFUNC7">"Query Cursor Location (7)" in topic 6.29</a>`.
// The selector's flattened segment ends with its record; the row is the
// leading display text of the next record, which no control of that record
// opens.
//
// Everything here is synthetic: the records are built by hand.

#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/layout_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/lowering/prose_topic_document_lowering.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "table_cell_link_record_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords w(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}
TokenWords glyphs(std::size_t count, std::uint16_t glyph) {
  return TokenWords(count, glyph);
}
const TokenWords attach{1};

// One-byte tokens, the shape a drawn grid row is stored in.
struct RecordBuilder {
  DecodedLogicalRecordSource record;

  void line(std::vector<TokenWords> content) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(content.size()), 1});
    record.tokens.push_back({0x25BA});
    for (auto &token : content) {
      const auto joiner = !token.empty() && token.front() < 4;
      record.encoded_tokens.push_back(
          {static_cast<std::uint16_t>(joiner ? token.front() : 0x60), 1});
      record.tokens.push_back(std::move(token));
    }
  }

  DecodedLogicalRecordSource finish(std::uint32_t logical_record) {
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

// `   |    <n> | "<name> (<n>)" in topic <id><pad>|`
std::vector<TokenWords> row(const std::string &number,
                            const std::string &name, std::size_t pad) {
  return {w("   "), {0x2502}, w("    "), w(number), {0x2502},
          w("\""), attach, w(name), w("("), attach, w(number), attach,
          w(")"), attach, w("\""), w("in"), w("topic"), w("6"),
          {1, '.'}, attach, w("8"), glyphs(pad, ' '), {0x2502}};
}

// Display width of a row as the assembler draws it.
std::size_t row_width(const std::string &number, const std::string &name) {
  RecordBuilder probe;
  probe.line(row(number, name, 1));
  const auto record = probe.finish(1);
  const auto *lines = geist::detail::record_display_lines(record);
  if (lines == nullptr || lines->empty()) return 0;
  return geist::detail::display_line_cells(record, lines->front()).size();
}

std::vector<TokenWords> rule(std::uint16_t left, std::uint16_t join,
                             std::uint16_t right, std::size_t second) {
  return {w("   "), {left}, attach, glyphs(7, 0x2500), attach, {join},
          attach, glyphs(second, 0x2500), attach, {right}};
}

std::vector<TokenWords> selector(const std::string &target,
                                 std::size_t length) {
  return {w("cselect"), w("13"), w(std::to_string(length)), w(target)};
}

std::vector<DecodedLogicalRecordSource> topic() {
  const std::string first = "First";
  const std::string second = "Second";
  // Both rows are padded to one width; the label is `"<name> (<n>)" in
  // topic 6.8`.
  const auto width = std::max(row_width("1", first), row_width("2", second));
  const auto inner = width - 13;  // cells from column 12 to the rail
  const auto label = [](const std::string &name, const std::string &number) {
    return ("\"" + name + " (" + number + ")\" in topic 6.8").size();
  };
  RecordBuilder one;
  one.line({w("sh1.0")});
  one.line({w("ctopicn"), w("2")});
  one.line({w("cparent")});
  one.line({w("chdlevel"), w(":H1")});
  one.line({w("ST"), w("Functions")});
  one.line({w("cz"), w("FLOW"), w("P"), w("3"), w("3")});
  one.line({w("   "), w("The"), w("functions:")});
  one.line({});
  one.line({w("cz"), w("OFF"), w("TABLE")});
  one.line({w("SRTBLTBLUNIQ1")});
  one.line(rule(0x250C, 0x252C, 0x2510, inner));
  one.line(selector("HDRFUNC1", label(first, "1")));
  one.line(row("1", first, width - row_width("1", first) + 1));
  one.line(rule(0x251C, 0x253C, 0x2524, inner));
  // The selector is the record's last display line; its row opens the next
  // record.
  one.line(selector("HDRFUNC2", label(second, "2")));
  RecordBuilder two;
  two.line(row("2", second, width - row_width("2", second) + 1));
  two.line(rule(0x2514, 0x2534, 0x2518, inner));
  two.line({w("SRETBL")});
  two.line({w("cz"), w("OFF"), w("ETABLE"), w("0"), w("0")});
  std::vector<DecodedLogicalRecordSource> records;
  records.push_back(one.finish(19));
  records.push_back(two.finish(20));
  return records;
}

void a_selector_reaches_the_row_that_opens_the_next_record() {
  const auto records = topic();
  const auto layout = geist::detail::extract_layout_ir(records);
  std::string error;
  const auto ownership =
      geist::detail::build_verified_ownership_ir(records, layout, &error);
  require(ownership.has_value(), "ownership is not verifiable: " + error);
  if (!ownership) return;
  const auto prose = geist::detail::extract_prose_topic_ir(
      records, layout, *ownership, "Functions", nullptr, &error);
  require(prose.has_value(), "the table topic was rejected: " + error);
  if (!prose) return;
  geist::detail::TopicIdentityIR identity;
  identity.id = "1.0";
  identity.title = "Functions";
  identity.heading_level = 1;
  identity.start_logical_record = 19;
  identity.end_logical_record = 21;
  const auto document = geist::detail::lower_prose_topic_to_document_ir(
      identity, *prose, &error);
  require(document.has_value(), "the table topic did not lower: " + error);
  if (!document) return;
  const auto text = geist::detail::format_document_ir(*document);
  require(text.find("HDRFUNC1") != std::string::npos,
          "the in-record selector lost its link: " + text);
  require(text.find("HDRFUNC2") != std::string::npos,
          "the selector whose row opens the next record lost its link: " +
              text);
}

} // namespace

int main() {
  a_selector_reaches_the_row_that_opens_the_next_record();
  return 0;
}
