// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A `,` glued to the last word of a framed display line, in front of the
// next control's line, is the line's last drawn cell.
//
//   ehxb7000 BACK_1.2.7.1 record 424 line 9 ends `... OSI Network Service,`:
//   token 190 is `02` at 0x9B172, the glue byte and `,`, inside the line,
//   and line 10 is `cfont 7 4 C ...`.  No control segment owns the token,
//   and it was dropped as decoder punctuation, so the row read
//   `Network Service Part 2: ...`.  The decoder's own separator before a
//   control stands on the next line's length byte instead (SC41-485 1.3.3
//   record 50 token 132), and that one stays unprinted.
//
// Everything here is synthetic: the test builds whole topic records by hand.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "prose_line_end_punctuation_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;

  void line(std::vector<TokenWords> content) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(TokenWords{0x25BA});
    for (auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(std::move(token));
    }
  }

  // The metadata controls, `revisions` between them and the title, then
  // the `ST` title.
  void heading(const std::vector<std::vector<TokenWords>> &revisions) {
    line({words("sh6.11.8")});
    line({words("ctopicn"), words("7")});
    line({words("cparent"), words("6.11")});
    line({words("cforwardlevel"), words("6.11.9")});
    line({words("cbacklevel"), words("6.11.7")});
    line({words("csummary"), words("9"), words("0"), words("9")});
    line({words("chdlevel"), words(":H3")});
    line({words("csourcefn"), words("HCPLOGR")});
    for (const auto &revision : revisions) line(revision);
    line({words("ST"), words("Part II")});
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

struct Extracted {
  std::vector<DecodedLogicalRecordSource> records;
  std::optional<geist::detail::ProseTopicIR> topic;
  std::string error;
};

Extracted run(DecodedLogicalRecordSource record) {
  Extracted result;
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
      result.records, layout, *ownership, "Part II", nullptr,
      &result.error);
  if (result.topic) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(
                result.records, layout, *ownership, "Part II", nullptr,
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

bool has_anchor(const geist::detail::ProseTopicIR &topic,
                const std::string &prefix) {
  return std::any_of(topic.anchors.begin(), topic.anchors.end(),
                     [&](const auto &anchor) {
                       return anchor.id.rfind(prefix, 0) == 0;
                     });
}

std::string anchor_ids(const geist::detail::ProseTopicIR &topic) {
  std::string ids;
  for (const auto &anchor : topic.anchors) ids += "[" + anchor.id + "]";
  return ids;
}

void test_glued_comma_before_control_line() {
  RecordBuilder builder;
  builder.heading({});
  builder.line({words("cfont"), words("3"), words("7"), words("C")});
  builder.line({words("   "), words("Support"), words("OSI"),
                words("Service"), TokenWords{1, ','}});
  {
    // The glue byte and `,` are one one-byte token, as in the source.
    auto &encoded = builder.record.encoded_tokens;
    encoded.back() = {2, 1};
    --encoded[encoded.size() - 6].value;
  }
  builder.line({words("cfont"), words("3"), words("7"), words("C")});
  // That line's length byte is spelled as a blank run, as in the source.
  builder.record.tokens[builder.record.tokens.size() - 5] =
      words(std::string(29, ' '));
  builder.line({words("   "), words("Part"), words("2:"), words("Provision")});
  auto extracted = run(builder.build(424));
  require(extracted.topic.has_value(),
          "glued-comma topic was rejected: " + extracted.error);
  if (!extracted.topic) return;
  std::string text;
  for (const auto &block : extracted.topic->blocks)
    for (const auto &item : block.inlines) text += item.text + " ";
  require(text.find("Service,") != std::string::npos,
          "the glued `,` at the end of the line was dropped: '" + text + "'");
}

}  // namespace

int main() {
  test_glued_comma_before_control_line();
  geist_test::exit_with_failures();
  std::cout << "prose line-end punctuation checks passed\n";
  return 0;
}
