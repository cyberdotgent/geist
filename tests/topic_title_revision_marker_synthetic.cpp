// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The column directly after the `ST` opcode is the title's revision column.
// A one-cell mark glued to the opcode there is the revision bar of the
// heading, not title text, whatever width its token is stored at:
//
//   SC41-5401-00 2.1.2.5 record 240 line 9   `ST| CSLBUFSIZE (Console buffer
//                                            size)`; the `|` is a two-byte
//                                            dictionary token
//   dfhcag49 1.7.74 record 351 line 9        `ST|  DFH5293  W`, two-byte `|`
//   dfheca01 2.1.4 record 452 line 9         `ST# CICS enables the READ
//                                            TIMEOUT value`
//   GC23-0469-01 7.0 record 192 line 9       `ST| Chapter 7.  Online Books`,
//                                            a one-byte `|` (already read)
//
// Hosted BookServer serves the bar in front of the topic number and the
// title after it, e.g. SC41-5401-00 2.1.2.5 (DT 19970421115044)
// `<H3>| 2.1.2.5   CSLBUFSIZE (Console buffer size)</H3>`.  libgeist reads
// a revision margin as layout, so the title is the words after the bar.  A
// mark separated from the opcode by a space is title text (SC24-5520-00
// 6.11.3 `ST  *ACCOUNT System Service`), and a title the catalog does not
// corroborate still declines.
//
// Everything here is synthetic: the test builds whole topic records by hand,
// and every token is stored as a two-byte dictionary token.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;
using Line = std::vector<TokenWords>;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "topic_title_revision_marker_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

// The attach prefix: the next word follows without a space.
const TokenWords attach{1};

DecodedLogicalRecordSource build(const std::vector<Line> &lines,
                                 std::uint32_t logical_record) {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;
  for (const auto &content : lines) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(TokenWords{0x25BA});
    for (const auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(token);
    }
  }
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
  return record;
}

struct Extracted {
  std::vector<DecodedLogicalRecordSource> records;
  std::optional<geist::detail::ProseTopicIR> topic;
  std::string error;
};

Extracted run(const std::vector<Line> &lines, const std::string &title) {
  Extracted result;
  auto record = build(lines, 2);
  require(record.ir.display_lines_parse,
          "the synthetic topic's display lines did not parse");
  result.records.push_back(std::move(record));
  const auto layout = geist::detail::extract_layout_ir(result.records);
  std::string ownership_error;
  const auto ownership = geist::detail::build_verified_ownership_ir(
      result.records, layout, &ownership_error);
  require(ownership.has_value(),
          "the synthetic topic's ownership is not verifiable: " +
              ownership_error);
  if (!ownership) return result;
  result.topic = geist::detail::extract_prose_topic_ir(
      result.records, layout, *ownership, title, nullptr, &result.error);
  if (result.topic) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(
                result.records, layout, *ownership, title, nullptr,
                *result.topic, &verify_error),
            "the extracted topic failed its own verifier: " + verify_error);
  }
  return result;
}

std::string topic_text(const geist::detail::ProseTopicIR &topic) {
  std::string text;
  for (const auto &block : topic.blocks)
    for (const auto &item : block.inlines) text += item.text + " ";
  return text;
}

// SC41-5401-00 2.1.2.5 record 240, envelope and title line for line.
std::vector<Line> topic_with_title(const Line &title_line) {
  return {
      {words("sh2.1.2.5")},
      {words("ctopicn"), words("71")},
      {words("cparent"), words("2.1.2")},
      {words("cforwardlevel"), words("2.1.2.6")},
      {words("cbacklevel"), words("2.1.2.4")},
      {words("csummary"), words("15"), words("0"), words("15")},
      {words("chdlevel"), words(":H3")},
      {words("csourcefn"), words("RBAEHMST")},
      title_line,
      {words("   "), words("Specifies"), words("the"), words("buffer.")},
  };
}

void admitted(const std::string &label, const Line &title_line,
              const std::string &catalog, const std::string &expected) {
  auto extracted = run(topic_with_title(title_line), catalog);
  require(extracted.topic.has_value(),
          label + ": the topic was rejected: " + extracted.error);
  if (!extracted.topic) return;
  require(extracted.topic->heading_level == "h3",
          label + ": heading level '" + extracted.topic->heading_level + "'");
  require(extracted.topic->title == expected,
          label + ": title '" + extracted.topic->title + "', expected '" +
              expected + "'");
  const auto text = topic_text(*extracted.topic);
  require(text.find("Specifies the buffer.") != std::string::npos,
          label + ": the body text is missing: '" + text + "'");
}

void declined(const std::string &label, const Line &title_line,
              const std::string &catalog) {
  auto extracted = run(topic_with_title(title_line), catalog);
  require(!extracted.topic.has_value(),
          label + ": a title the catalog does not corroborate was admitted");
}

}  // namespace

int main() {
  // SC41-5401-00 2.1.2.5: a change bar glued to the opcode, a two-byte token.
  admitted("glued change bar",
           {words("ST"), attach, words("|"), words("CSLBUFSIZE"),
            words("(Console"), words("buffer"), words("size)")},
           "CSLBUFSIZE (Console Buffer Size)",
           "CSLBUFSIZE (Console buffer size)");
  // dfhcag49 1.7.74: the bar, then a one-cell gap before the message id.
  admitted("glued change bar before a gap",
           {words("ST"), attach, words("|"), words(" "), words("DFH5293"),
            words(" "), words("W")},
           "DFH5293  W", "DFH5293 W");
  // dfheca01 2.1.4: a `#` revision mark in the same column.
  admitted("glued revision mark",
           {words("ST"), attach, words("#"), words("CICS"), words("enables"),
            words("the"), words("READ"), words("TIMEOUT"), words("value")},
           "CICS Enables the READ TIMEOUT Value",
           "CICS enables the READ TIMEOUT value");
  // SC24-5520-00 6.11.3: a space after the opcode, so `*` is title text.
  admitted("punctuation after a space",
           {words("ST"), words(" "), words("*"), attach, words("ACCOUNT"),
            words("System"), words("Service")},
           "*ACCOUNT System Service", "*ACCOUNT System Service");
  // A title the catalog does not corroborate still declines, bar or not.
  declined("glued change bar, other title",
           {words("ST"), attach, words("|"), words("CSLBUFSIZE"),
            words("(Console"), words("buffer"), words("size)")},
           "CODEPAGE (Code Page)");
  // A bar after a space is not in the revision column: it is title text,
  // and the catalog title without it does not corroborate the row.
  declined("change bar after a space",
           {words("ST"), words("|"), words("CSLBUFSIZE"), words("(Console"),
            words("buffer"), words("size)")},
           "CSLBUFSIZE (Console Buffer Size)");
  return 0;
}
