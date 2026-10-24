// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// CZ body blocks as hosted BookServer serves them: list, note and region
// shapes whose directives carry no rows of their own, or hand their rows on
// (doc/boo-spec/markup.adoc, "Reading a CZ body").
//
// BookServer matches a control line by literal upper-case prefix.  A generic
// `CZ OFF <tag>` opens `<pre width="80">`; the generic `CZ OFF E<tag>`
// closer prints nothing and leaves that `<pre>` open until the next
// `CZ FLOW`.  Each case below is named for the shape it reproduces and
// cites the hosted page that shows it.
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
    std::cerr << "cz_body_blocks_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;

  // One display line: a length byte covering `content`, whose tokens are two
  // bytes each.
  void line(std::vector<TokenWords> content) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(TokenWords{0x25BA});
    for (auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(std::move(token));
    }
  }

  // A text row: a leading blank run and the words, one token each.
  void row(const std::string &margin, const std::vector<std::string> &text) {
    std::vector<TokenWords> content{words(margin)};
    for (const auto &word : text) content.push_back(words(word));
    line(std::move(content));
  }

  // A directive line, one token per word.
  void cz(const std::vector<std::string> &text) {
    std::vector<TokenWords> content;
    for (const auto &word : text) content.push_back(words(word));
    line(std::move(content));
  }

  // An empty display line: a zero length byte.
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
    line({words("csourcefn"), words("NYQL0INT")});
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
  for (const auto &inline_ : block.inlines) {
    if (!text.empty() && !inline_.text.empty() && text.back() != ' ' &&
        inline_.text.front() != ' ')
      text += ' ';
    text += inline_.text;
  }
  return text;
}

std::string topic_text(const geist::detail::ProseTopicIR &topic) {
  std::string text;
  for (const auto &block : topic.blocks) text += block_text(block) + "\n";
  return text;
}

bool contains(const std::string &text, const std::string &needle) {
  return text.find(needle) != std::string::npos;
}

// An empty verbatim region draws nothing.  SC31-6552-00 `1.6` record 181
// lines 46-47 are `cz OFF SYNTAX` directly followed by `cz OFF ESYNTAX 4 4`;
// hosted (DT 19950503072955) serves `</pre><pre width="80"><!-- * -->` and
// the `</pre>` of the next item -- an empty block.
void an_empty_region_draws_nothing() {
  RecordBuilder builder;
  builder.heading("1.6", "Diagrams");
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"Read", "the", "diagrams."});
  builder.empty();
  builder.cz({"cz", "OFF", "SYNTAX"});
  builder.cz({"cz", "OFF", "ESYNTAX", "4", "4"});
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"Fragments", "follow."});
  auto extracted = run(builder.build(181), "Diagrams");
  require(extracted.topic.has_value(),
          "an empty cz OFF SYNTAX region was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 2 &&
              blocks[0].kind == ProseBlockKindIR::paragraph &&
              blocks[1].kind == ProseBlockKindIR::paragraph,
          "the empty region drew a block:\n" + topic_text(*extracted.topic));
}

// The rows after the closer of an empty region stand in the `<pre>` the
// opener left open.  SC33-1220-02 `BACK_2` (DT 19980424201448) writes
// `cz OFF LINES`, `cz OFF ELINES 0 0` and then the reader's comment form
// rows; hosted serves `</pre><pre width="80"><!-- * -->` followed by
// `   Name:                                  Address:` verbatim.
void rows_after_an_empty_region_are_verbatim() {
  RecordBuilder builder;
  builder.heading("BACK_2", "Comments");
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"Please", "tell", "us."});
  builder.empty();
  builder.cz({"cz", "OFF", "LINES"});
  builder.cz({"cz", "OFF", "ELINES", "0", "0"});
  builder.line({words("   "), words("Name:"),
                words("                                  "),
                words("Address:")});
  auto extracted = run(builder.build(58), "Comments");
  require(extracted.topic.has_value(),
          "an empty cz OFF LINES region with closer rows was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 2 &&
              blocks[1].kind == ProseBlockKindIR::preformatted,
          "the rows after the empty region are not one verbatim block:\n" +
              topic_text(*extracted.topic));
  if (blocks.size() != 2) return;
  require(contains(block_text(blocks[1]), "Name:") &&
              contains(block_text(blocks[1]), "Address:"),
          "the verbatim row lost its words: '" + block_text(blocks[1]) +
              "'");
}

}  // namespace

int main() {
  an_empty_region_draws_nothing();
  rows_after_an_empty_region_are_verbatim();
  return 0;
}
