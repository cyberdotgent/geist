// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A display line's length byte is row geometry whatever word the dictionary
// spells for it, inside the topic metadata envelope too.  EZ12GMST 1.2.134
// record 939 frames line 6 (`SRLEN inet_addr, inet_network, ...`) and line
// 10 (`ST  inet_addr, inet_network, ...`) with the length byte 0x31 = 49,
// and token 49 of that book's dictionary is `csourcefn`.  The flattened
// splitter therefore opens two operand-less `csourcefn` segments, one
// between `csummary` and `SRLEN` and one between `SRHDRINET` and `ST`; the
// genuine `csourcefn J1AH9INE` stands on line 8.  BookServer reads whole
// lines and never sees a control in a length byte.
//
// Everything here is synthetic: the test builds whole topic records by hand.

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
    std::cerr << "envelope_length_byte_opcode_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

// Lines whose length byte the dictionary spells as a word: line index ->
// spelling.  Empty: every length byte spells the usual glyph.
std::vector<std::pair<std::size_t, std::string>> spelled_lengths;

DecodedLogicalRecordSource build(const std::vector<Line> &lines,
                                 std::uint32_t logical_record) {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const auto &content = lines[index];
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    TokenWords length{0x25BA};
    for (const auto &[line, spelling] : spelled_lengths)
      if (line == index) length = words(spelling);
    record.tokens.push_back(length);
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

std::vector<Line> topic() {
  return {
      {words("sh1.2.134")},
      {words("ctopicn"), words("235")},
      {words("cparent"), words("1.2")},
      {words("cforwardlevel"), words("1.2.135")},
      {words("cbacklevel"), words("1.2.133")},
      {words("csummary"), words("110"), words("0"), words("110")},
      {words("SRLEN"), words("inet_addr,"), words("inet_network")},
      {words("chdlevel"), words(":H2")},
      {words("csourcefn"), words("J1AH9INE")},
      {words("SRHDRINET")},
      {words("ST"), words(" "), words("inet_addr,"), words("inet_network")},
      {words("   "), words("The"), words("subroutines"), words("convert.")},
  };
}

}  // namespace

int main() {
  // The same topic with ordinary length bytes reads as before.
  {
    spelled_lengths.clear();
    auto extracted = run(topic(), "inet_addr, inet_network");
    require(extracted.topic.has_value(),
            "plain length bytes: the topic was rejected: " + extracted.error);
  }
  // EZ12GMST 1.2.134: the length bytes of the SRLEN and ST lines spell
  // `csourcefn`.
  spelled_lengths = {{6, "csourcefn"}, {10, "csourcefn"}};
  auto extracted = run(topic(), "inet_addr, inet_network");
  require(extracted.topic.has_value(),
          "length bytes spelling csourcefn: the topic was rejected: " +
              extracted.error);
  if (extracted.topic) {
    require(extracted.topic->heading_level == "h2",
            "heading level '" + extracted.topic->heading_level + "'");
    require(extracted.topic->title == "inet_addr, inet_network",
            "title '" + extracted.topic->title + "'");
    const auto text = topic_text(*extracted.topic);
    require(text.find("csourcefn") == std::string::npos &&
                text.find("The subroutines convert.") != std::string::npos,
            "body text '" + text + "'");
  }
  return 0;
}
