// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// `cz OFF ARTWORK` is a display region of the CZ dialect (issue #74).
//
// Hosted BookServer opens a `<pre width="80">` at the directive and serves the
// region's display rows in it character for character, substituting an `<img>`
// for the `PICTURE <n>` placeholder columns a picture selector names.  Three
// topics in the corpus carry the tag and they prove three separate things:
//
//   GX27-3999-00 `2.4` and `FRONT_1` (DT 19950730184057) wrap a
//   `cselect <col> <len> PIC<n>` picture placeholder and close the region
//   `cz OFF EHP0 <left> <indent>` -- the compiler wrote the end of the
//   highlight phrase around the artwork, not `cz OFF EARTWORK`.
//
//   SC41-4853-00 `COMMENTS` (DT 19951003131222) closes its regions
//   `cz OFF EARTWORK 0 0`, and alternates regions that draw nothing at all
//   (hosted serves an empty `<pre width="80">` for each) with regions holding
//   one 74-column `U+2500` rule, the line a reader writes a comment on.
//
// Everything here is synthetic: the test builds whole topic records by hand
// with `assemble_logical_record_with_sources` and opens no book, because
// `libgeist` may depend on no book but `packet.boo` (issue #59) and none of
// the three topics is in it.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::ProseBlockKindIR;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "cz_artwork_region_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

// A run of `count` `U+2500` rule words: one drawn token, the shape SC41-485
// `COMMENTS` writes its comment rules as.
TokenWords rule(std::size_t count) { return TokenWords(count, 0x2500); }

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;

  // One display line: a length byte covering `content`, whose tokens are two
  // bytes each.  The length byte's dictionary spelling is the row sentinel.
  void line(std::vector<TokenWords> content) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(TokenWords{0x25BA});
    for (auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(std::move(token));
    }
  }

  // The eight metadata controls and the `ST` title every prose topic opens
  // with.
  void heading(const std::string &id, const std::string &title_word) {
    line({words("sh" + id)});
    line({words("ctopicn"), words("7")});
    line({words("cparent"), words("1.0")});
    line({words("cforwardlevel"), words("1.2")});
    line({words("cbacklevel"), words("1.0")});
    line({words("csummary"), words("9"), words("0"), words("9")});
    line({words("chdlevel"), words(":H2")});
    line({words("csourcefn"), words("ALY0C02")});
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

Extracted run(DecodedLogicalRecordSource record, const std::string &title,
              const std::set<std::string> &resource_ids) {
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
      result.records, layout, *ownership, title, nullptr, &result.error,
      &resource_ids);
  if (result.topic) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(
                result.records, layout, *ownership, title, nullptr,
                *result.topic, &verify_error, &resource_ids),
            "the extracted topic failed its own verifier: " + verify_error);
  }
  return result;
}

// GX27-3999-00 `2.4`: the artwork region holds one picture placeholder row and
// two blank rows, and closes `cz OFF EHP0 4 4`.  Hosted serves the region as a
// `<pre width="80">` in which the row spelling `       PICTURE 7` is replaced
// by the `<img ... alt="PICTURE 7">` of picture 7.
DecodedLogicalRecordSource picture_topic() {
  RecordBuilder builder;
  builder.heading("2.4", "Connecting");
  builder.line({words("cz"), words("BREAK"), words("3")});
  builder.line({words("cz"), words("OFF"), words("ARTWORK")});
  builder.line({words("cselect"), words("7"), words("9"), words("PIC7")});
  // Columns 7..15 spell `PICTURE 7`: the leading token ends in a space, so the
  // assembler inserts none in front of `PICTURE`.
  builder.line({words("       "), words("PICTURE"), words("7")});
  builder.line({});
  builder.line({});
  builder.line({words("cz"), words("OFF"), words("EHP0"), words("4"),
                words("4")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("7"),
                words("7")});
  builder.line({words("       "), words("Connect"), words("the"),
                words("cables.")});
  return builder.build(30);
}

void an_artwork_region_places_its_picture() {
  auto extracted = run(picture_topic(), "Connecting", {"7"});
  require(extracted.topic.has_value(),
          "a topic whose artwork region holds a picture was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  // The picture is the region's whole content, so the figure family claims
  // the placeholder row and places the image.
  const auto &figures = extracted.topic->figures.blocks;
  require(figures.size() == 1,
          "the artwork region's picture did not become one figure block; it "
          "has " + std::to_string(figures.size()));
  if (figures.size() != 1) return;
  require(figures.front().target == "7",
          "the picture addresses resource '" + figures.front().target +
              "', not 7");
  require(figures.front().placeholder_text == "PICTURE 7",
          "the picture's placeholder words are '" +
              figures.front().placeholder_text + "', not 'PICTURE 7'");
  // The region itself draws nothing else: the placeholder columns are the
  // picture and the two blank rows below it draw no row at all, so the only
  // block left is the paragraph the closer's flow directive opens.
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 1 &&
              blocks.front().kind == ProseBlockKindIR::paragraph,
          "the artwork region drew a block of its own, or the paragraph after "
          "it was lost: the topic has " + std::to_string(blocks.size()) +
              " block(s)");
  if (blocks.size() != 1) return;
  require(blocks.front().preformatted_lines.empty(),
          "the artwork region kept the placeholder words as verbatim rows");
}

// SC41-4853-00 `COMMENTS`: an empty artwork region followed by one holding a
// drawn rule, both closed `cz OFF EARTWORK 0 0`.
DecodedLogicalRecordSource rule_topic() {
  RecordBuilder builder;
  builder.heading("2.5", "Comments");
  builder.line({words("cz"), words("BREAK"), words("3")});
  builder.line({words("cz"), words("OFF"), words("ARTWORK")});
  builder.line({words("cz"), words("OFF"), words("EARTWORK"), words("0"),
                words("0")});
  builder.line({words("cz"), words("OFF"), words("ARTWORK")});
  builder.line({words("   "), rule(20)});
  builder.line({});
  builder.line({words("cz"), words("OFF"), words("EARTWORK"), words("0"),
                words("0")});
  return builder.build(458);
}

void an_artwork_region_draws_its_rule_and_nothing_when_empty() {
  auto extracted = run(rule_topic(), "Comments", {});
  require(extracted.topic.has_value(),
          "a topic whose artwork regions hold a drawn rule was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 1,
          "the empty artwork region drew something, or the rule did not: the "
          "topic has " + std::to_string(blocks.size()) + " block(s)");
  if (blocks.size() != 1) return;
  require(blocks.front().kind == ProseBlockKindIR::preformatted,
          "the drawn rule did not stay verbatim");
  require(blocks.front().preformatted_lines.size() == 1,
          "the rule region kept " +
              std::to_string(blocks.front().preformatted_lines.size()) +
              " verbatim rows, not one");
  if (blocks.front().preformatted_lines.size() != 1) return;
  require(blocks.front().preformatted_lines.front() ==
              "   " + std::string(20, '_'),
          "the drawn rule reads '" + blocks.front().preformatted_lines.front() +
              "', not its 20 rule columns at the region's own left margin");
}

// Fail closed: a region no closer follows claims nothing.  `cz OFF ARTWORK`
// closes on `EARTWORK` or on `EHP0` and on nothing else.
DecodedLogicalRecordSource unclosed_topic() {
  RecordBuilder builder;
  builder.heading("2.4", "Connecting");
  builder.line({words("cz"), words("BREAK"), words("3")});
  builder.line({words("cz"), words("OFF"), words("ARTWORK")});
  builder.line({words("   "), rule(20)});
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("Body"), words("text.")});
  return builder.build(30);
}

void an_unclosed_artwork_region_fails_closed() {
  auto extracted = run(unclosed_topic(), "Connecting", {});
  require(!extracted.topic.has_value(),
          "an artwork region with no closer was admitted");
  if (extracted.topic) return;
  require(extracted.error.find("cz OFF ARTWORK is not closed by cz OFF "
                               "EARTWORK") != std::string::npos,
          "the unclosed artwork region declined as '" + extracted.error +
              "', not for the missing closer");
}

} // namespace

int main() {
  an_artwork_region_places_its_picture();
  an_artwork_region_draws_its_rule_and_nothing_when_empty();
  an_unclosed_artwork_region_fails_closed();
  return 0;
}
