// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// `c.rev <id> <bar>` declares a revision code; its bar operand is one
// character, and two of its stored spellings are words that a `?`-trimmed
// projection would erase.
//
//   SC09-138 FRONT record 2 line 16      `c.rev DUNNO ?`  bar word 0x003F
//   PRG1SORT PREFACE.5 record 17 line 9  `c.rev REL3 |`   bar word U+2502
//   SC24-5520-00 6.11.8 record 1599      `c.rev DOC |` .. bar word U+2502
//
// Hosted BookServer prints none of these lines (SC09-138 DT 19910321130500,
// PRG1SORT DT 19900829171904, SC24-5520-00 DT 19911011135123).  Each is a
// well-formed declaration, in the envelope and after the title alike.  A
// declaration without a bar, or with a bar of two characters, is still
// malformed and declines.
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

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "revision_bar_operand_synthetic: " << message << '\n';
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
    line({words("ST"), words("Recording")});
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
      result.records, layout, *ownership, "Recording", nullptr,
      &result.error);
  if (result.topic) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(
                result.records, layout, *ownership, "Recording", nullptr,
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

std::vector<TokenWords> revision(const std::string &id, TokenWords bar) {
  return {words("c.rev"), words(id), std::move(bar)};
}

// Body declarations close the topic, as SC24-5520-00 6.11.8 record 1599
// does; the flattened segment of a declaration that prose followed would
// carry that prose as payload, which is not modelled and declines.
DecodedLogicalRecordSource topic(
    const std::vector<std::vector<TokenWords>> &envelope,
    const std::vector<std::vector<TokenWords>> &body,
    bool closing_row = false) {
  RecordBuilder builder;
  builder.heading(envelope);
  builder.line({words("   "), words("An"), words("installation"),
                words("may"), words("write.")});
  builder.line({words("   "), words("Closing"), words("row.")});
  for (const auto &revision : body) builder.line(revision);
  if (closing_row)
    builder.line({words("   "), words("Trailing"), words("prose.")});
  return builder.build(1599);
}

void well_formed(const std::string &label,
                 const std::vector<std::vector<TokenWords>> &envelope,
                 const std::vector<std::vector<TokenWords>> &body) {
  auto extracted = run(topic(envelope, body));
  require(extracted.topic.has_value(),
          label + ": the topic was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto text = topic_text(*extracted.topic);
  require(text.find("rev") == std::string::npos &&
              text.find("DUNNO") == std::string::npos &&
              text.find("REL3") == std::string::npos &&
              text.find("SEVCOD") == std::string::npos,
          label + ": a declaration leaked into the text: '" + text + "'");
  require(text.find("installation") != std::string::npos &&
              text.find("Closing") != std::string::npos,
          label + ": the body text was lost: '" + text + "'");
}

void malformed(const std::string &label,
               const std::vector<std::vector<TokenWords>> &envelope,
               const std::vector<std::vector<TokenWords>> &body,
               bool closing_row = false) {
  auto extracted = run(topic(envelope, body, closing_row));
  require(!extracted.topic.has_value(),
          label + ": a malformed revision declaration was admitted");
}

}  // namespace

int main() {
  const TokenWords ascii_bar = words("|");
  const TokenWords question_bar = words("?");
  const TokenWords box_bar{0x2502};

  well_formed("ASCII bar in the envelope", {revision("REL2", ascii_bar)}, {});
  well_formed("`?` bar in the envelope",
              {revision("REL2", ascii_bar), revision("DUNNO", question_bar)},
              {});
  well_formed("U+2502 bar in the envelope", {revision("REL3", box_bar)}, {});
  well_formed("U+2502 bar after the title", {},
              {revision("DOC", box_bar), revision("SPE", box_bar),
               revision("SEVCOD", words("#"))});
  well_formed("`?` bar after the title", {},
              {revision("DUNNO", question_bar)});

  malformed("no bar in the envelope", {{words("c.rev"), words("REL3")}}, {});
  malformed("no bar after the title", {}, {{words("c.rev"), words("REL3")}});
  malformed("two-character bar", {}, {revision("REL3", words("||"))});
  malformed("box corner as bar", {}, {revision("REL3", TokenWords{0x250C})});
  malformed("prose after a body declaration", {},
            {revision("DOC", box_bar)}, true);
  return 0;
}
