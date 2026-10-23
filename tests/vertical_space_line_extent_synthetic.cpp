// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// `c.sp` draws nothing, whatever its operands: BookServer skips every
// control line whose name starts with `C.`.  Its operands are the words of
// its own display line, and the rows after that line are body text even
// when the record's control segment runs on into them.
//
//   GC41-5051-01 1.0 record 4 line 9         `c.sp 1p c`; hosted (DT
//                                            19980619084541) serves the
//                                            heading, a paragraph break and
//                                            the subtopic menu, no `1p`
//   ehxb7000 BACK_1.2.7.1 record 417 line 9  `c.sp 0p8 c`, an empty line, then
//                                            `   The following documents are
//                                            ISO standards ...`; the control
//                                            segment reaches into those rows
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
    std::cerr << "vertical_space_line_extent_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

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

std::vector<Line> topic(const Line &spacing, bool body_after_blank) {
  std::vector<Line> lines = {
      {words("sh1.0")},
      {words("ctopicn"), words("2")},
      {words("cparent")},
      {words("cforwardlevel"), words("2.0")},
      {words("cbacklevel"), words("FRONT")},
      {words("csummary"), words("3"), words("0"), words("3")},
      {words("chdlevel"), words(":H1")},
      {words("csourcefn"), words("EHXB7000")},
      {words("ST"), words(" "), words("Specified"), words("Environment")},
      spacing,
  };
  if (body_after_blank) lines.push_back({});
  lines.push_back({words("   "), words("The"), words("following"),
                   words("documents"), words("are")});
  lines.push_back({words("   "), words("ISO"), words("standards.")});
  return lines;
}

void admitted(const std::string &label, const Line &spacing,
              bool body_after_blank, const std::string &hidden) {
  auto extracted =
      run(topic(spacing, body_after_blank), "Specified Environment");
  require(extracted.topic.has_value(),
          label + ": the topic was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto text = topic_text(*extracted.topic);
  require(text.find("The following documents are ISO standards.") !=
              std::string::npos,
          label + ": the body after the control line is missing: '" + text +
              "'");
  require(text.find(hidden) == std::string::npos,
          label + ": the c.sp operand '" + hidden + "' is drawn: '" + text +
              "'");
}

}  // namespace

int main() {
  // GC41-5051-01 1.0: a point count glued to its unit.
  admitted("count in points", {words("c.sp"), words("1p"), words("c")}, true,
           "1p");
  // ehxb7000 BACK_1.2.7.1: a fractional point count, then a blank line and
  // the body the control segment runs into.
  admitted("fractional points before a paragraph",
           {words("c.sp"), words("0p8"), words("c")}, true, "0p8");
  // The same control directly followed by the body rows.
  admitted("fractional points before a row",
           {words("c.sp"), words("0p8"), words("c")}, false, "0p8");
  // A documented form still draws nothing.
  admitted("bare count", {words("c.sp"), words("1")}, true, "1 ");
  return 0;
}
