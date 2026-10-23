// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// `SRGLS` names a glossary anchor from the rest of its own display line and
// nothing more.  The flattened control segment runs on to the next control,
// so the display lines after it are body text.
//
//   SH24-5219-00 3.0 record 371: a bare `SRGLS` on line 0, then the paragraph
//   `Much of the information in this section is procedural ...` on lines
//   1-3.  Hosted BookServer (DT 19920710083758) serves the paragraph as text
//   and no `GLS` anchor at all: a bare `SRGLS` only makes the reader look
//   ahead for a glossary letter box.
//   SH24-5219-00 3.0 record 370: `SRGLS Installing` is served as
//   `<a name="GLS Installing">` and its text stays body text.
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
    std::cerr << "prose_glossary_anchor_synthetic: " << message << '\n';
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

void test_bare_glossary_control_keeps_its_paragraph() {
  RecordBuilder builder;
  builder.heading({});
  builder.line({words("   "), words("The"), words("chapters"),
                words("follow.")});
  builder.line({words("SRGLS")});
  builder.line({words("   "), words("Much"), words("of"), words("the"),
                words("information"), words("is"), words("procedural.")});
  builder.line({words("   "), words("There"), words("are"), words("also"),
                words("examples.")});
  auto extracted = run(builder.build(371));
  require(extracted.topic.has_value(),
          "bare SRGLS topic was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto text = topic_text(*extracted.topic);
  require(text.find("Much of the information is procedural.") !=
                  std::string::npos &&
              text.find("There are also examples.") != std::string::npos,
          "the paragraph after a bare SRGLS was lost from the text: '" + text +
              "'");
  require(!has_anchor(*extracted.topic, "GLS"),
          "a bare SRGLS named an anchor: " + anchor_ids(*extracted.topic));
}

void test_glossary_term_is_its_own_line() {
  RecordBuilder builder;
  builder.heading({});
  builder.line({words("SRGLS"), words("Installing")});
  builder.line({words("   "), words("Installing."), words("Once"),
                words("you"), words("have"), words("planned,")});
  builder.line({words("   "), words("you"), words("can"), words("start.")});
  auto extracted = run(builder.build(370));
  require(extracted.topic.has_value(),
          "SRGLS term topic was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto text = topic_text(*extracted.topic);
  require(text.find("Once you have planned,") != std::string::npos &&
              text.find("you can start.") != std::string::npos,
          "the text after `SRGLS Installing` was lost: '" + text + "'");
  require(has_anchor(*extracted.topic, "GLS Installing") &&
              !has_anchor(*extracted.topic, "GLS Installing "),
          "`SRGLS Installing` did not name exactly `GLS Installing`: " +
              anchor_ids(*extracted.topic));
}

}  // namespace

int main() {
  test_bare_glossary_control_keeps_its_paragraph();
  test_glossary_term_is_its_own_line();
  geist_test::exit_with_failures();
  std::cout << "prose glossary anchor checks passed\n";
  return 0;
}
