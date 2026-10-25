// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A generic `cz OFF <tag>` region that no closer and no `cz FLOW` ends runs to
// the end of the topic.
//
// BookServer's generic `CZ OFF` opens `<pre width="80">` and prints every row
// after it as stored until a `CZ FLOW` closes it (doc/boo-spec/markup.adoc,
// "Reading a CZ body").  When the topic ends first, the `<pre>` simply holds
// the rest of the topic.  SC41-3425-00 `APPENDIX1.1.6` (DT 19950525161600)
// ends with `cz OFF CGRAPHIC` (record 1404 line 17) and thirty rows of its
// presentation-address worksheet; hosted serves `</pre><pre width="80">`, the
// rows as stored and `</pre>` at the end of the body.  The CICS message
// topics of `dfhedb02.boo` are the same shape: each one is `cz OFF MSG`, the
// message rule and the headline row, and nothing after it.
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
using geist::detail::ProseBlockKindIR;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "cz_region_topic_end_synthetic: " << message << '\n';
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

  void row(const std::string &margin, const std::vector<std::string> &text) {
    std::vector<TokenWords> content{words(margin)};
    for (const auto &word : text) content.push_back(words(word));
    line(std::move(content));
  }

  void cz(const std::vector<std::string> &text) {
    std::vector<TokenWords> content;
    for (const auto &word : text) content.push_back(words(word));
    line(std::move(content));
  }

  void empty() {
    record.encoded_tokens.push_back({0, 1});
    record.tokens.push_back(TokenWords{1});
  }

  void heading(const std::string &id, const std::string &title_word) {
    line({words("sh" + id)});
    line({words("ctopicn"), words("7")});
    line({words("cparent"), words("1.0")});
    line({words("cforwardlevel"), words("1.2")});
    line({words("cbacklevel"), words("1.0")});
    line({words("csummary"), words("9"), words("0"), words("9")});
    line({words("chdlevel"), words(":H2")});
    line({words("csourcefn"), words("DFHEDB00")});
    line({words("ST"), words(title_word)});
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

Extracted run(DecodedLogicalRecordSource record, const std::string &title) {
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

std::string block_text(const geist::detail::ProseBlockIR &block) {
  std::string text;
  for (const auto &inline_ : block.inlines) text += inline_.text;
  return text;
}

bool contains(const std::string &text, const std::string &needle) {
  return text.find(needle) != std::string::npos;
}

// The message topic: `cz OFF MSG`, then the headline row, then the end of the
// topic.  The region is one verbatim block holding the row as stored.
void a_region_running_to_the_topic_end_is_verbatim() {
  RecordBuilder builder;
  builder.heading("4.1.1.1.1", "DFH5139");
  builder.empty();
  builder.cz({"cz", "OFF", "MSG"});
  builder.row("   ", {"DFH5139", "W", "CONSIDER", "IMPLICATIONS."});
  builder.row("   ", {"Second", "row."});
  auto extracted = run(builder.build(640), "DFH5139");
  require(extracted.topic.has_value(),
          "a cz OFF MSG region running to the topic end was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 1 &&
              blocks[0].kind == ProseBlockKindIR::preformatted,
          "the region is not one verbatim block: " +
              std::to_string(blocks.size()) + " block(s)");
  if (blocks.size() != 1) return;
  const auto text = block_text(blocks[0]);
  require(contains(text, "CONSIDER") && contains(text, "Second") &&
              text.find("CONSIDER") < text.find("Second"),
          "the verbatim block lost or reordered its rows: '" + text + "'");
}

// Rows after a flowed paragraph, then the region at the end: the paragraph
// stays a paragraph and the region holds only its own rows.
void a_trailing_region_after_a_paragraph() {
  RecordBuilder builder;
  builder.heading("APPENDIX1.1.6", "Worksheet");
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"Sketch", "the", "addresses", "here."});
  builder.empty();
  builder.cz({"cz", "OFF", "CGRAPHIC"});
  builder.row("   ", {"Application", "entity"});
  builder.row("   ", {"nicknames______"});
  auto extracted = run(builder.build(1404), "Worksheet");
  require(extracted.topic.has_value(),
          "a cz OFF CGRAPHIC region running to the topic end was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 2 &&
              blocks[0].kind == ProseBlockKindIR::paragraph &&
              blocks[1].kind == ProseBlockKindIR::preformatted,
          "expected a paragraph and a verbatim block, got " +
              std::to_string(blocks.size()) + " block(s)");
  if (blocks.size() != 2) return;
  require(contains(block_text(blocks[1]), "Application") &&
              contains(block_text(blocks[1]), "nicknames") &&
              !contains(block_text(blocks[1]), "Sketch"),
          "the trailing region holds the wrong rows: '" +
              block_text(blocks[1]) + "'");
}

// Another region opener before the end leaves the first region's boundary
// unproven: the topic still declines.
void a_second_opener_before_the_end_declines() {
  RecordBuilder builder;
  builder.heading("4.1.1.1.2", "DFH5151");
  builder.cz({"cz", "OFF", "MSG"});
  builder.row("   ", {"DFH5151", "I", "RESOURCE", "NOT", "ALTERED."});
  builder.cz({"cz", "OFF", "CGRAPHIC"});
  builder.row("   ", {"Drawn", "row."});
  auto extracted = run(builder.build(641), "DFH5151");
  require(!extracted.topic.has_value(),
          "an unclosed region followed by another opener was admitted");
}

}  // namespace

int main() {
  a_region_running_to_the_topic_end_is_verbatim();
  a_trailing_region_after_a_paragraph();
  a_second_opener_before_the_end_declines();
  geist_test::exit_with_failures();
  std::cout << "cz region topic end checks passed\n";
  return 0;
}
