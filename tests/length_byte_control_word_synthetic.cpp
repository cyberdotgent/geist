// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A display line's length byte that the dictionary spells as a control word
// opens no control, even right after a decoder separator.
//
// GG24-395 `3.3.1.3` record 352: token 50 is the right rail `U+2502` that
// closes the box row of line 4, and token 51 (encoded value 60, width 1) is
// line 5's length byte -- line 5 is exactly 60 bytes -- which the book's
// dictionary spells `citerm`.  OFCUSEOV `1.29.4` record 329: token 227 is the
// rail `U+2502` and token 228 (encoded value 70) is the length byte of the
// 70-byte line 23, spelled `ctoce`.  The flattened splitter opens a segment
// on each spelling, and the rail before it is a decoder separator, so the
// word looked like a control with a boundary.  Hosted BookServer prints the
// box rows of both topics (GG24-395 DT 19941215160749, OFCUSEOV DT
// 19900805103816) and neither word anywhere.
//
// The byte is row geometry and the line after it is ordinary display text.
// A control-like word that is *not* a length byte, behind the same
// separator, still declines.
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
    std::cerr << "length_byte_control_word_synthetic: " << message << '\n';
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
  // bytes each.  `spelling` is the length byte's dictionary word.
  void line(std::vector<TokenWords> content,
            TokenWords spelling = TokenWords{0x25BA}) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(std::move(spelling));
    for (auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(std::move(token));
    }
  }

  void heading(const std::string &id, const std::string &title_word) {
    line({words("sh" + id)});
    line({words("ctopicn"), words("7")});
    line({words("cparent"), words("1.0")});
    line({words("cforwardlevel"), words("1.2")});
    line({words("cbacklevel"), words("1.0")});
    line({words("csummary"), words("9"), words("0"), words("9")});
    line({words("chdlevel"), words(":H2")});
    line({words("csourcefn"), words("ALY0FRO")});
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

std::string topic_text(const geist::detail::ProseTopicIR &topic) {
  std::string text;
  for (const auto &block : topic.blocks)
    for (const auto &item : block.inlines) text += item.text + " ";
  return text;
}

// A prose row ending in a drawn rule cell (a decoder separator), then a line
// whose length byte spells `word`.  With `word_is_length_byte` false the word
// is instead the first content token of a line with an ordinary length byte.
DecodedLogicalRecordSource rule_then_word(const std::string &word,
                                          bool word_is_length_byte) {
  RecordBuilder builder;
  builder.heading("3.3.1.3", "Resources");
  builder.line({words("   "), words("Names"), words("are"), words("listed."),
                TokenWords{0x2502}});
  if (word_is_length_byte)
    builder.line({words("   "), words("Servers"), words("connect.")},
                 words(word));
  else
    builder.line({words(word), words("   "), words("Servers"),
                  words("connect.")});
  builder.line({words("   "), words("Closing"), words("row.")});
  return builder.build(352);
}

void a_length_byte_spelling_a_control_is_geometry(const std::string &word) {
  const auto label = "length byte '" + word + "': ";
  auto extracted = run(rule_then_word(word, true), "Resources");
  require(extracted.topic.has_value(),
          label + "the topic was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto text = topic_text(*extracted.topic);
  require(text.find(word) == std::string::npos,
          label + "the length byte leaked into the text: '" + text + "'");
  require(text.find("Servers") != std::string::npos &&
              text.find("connect.") != std::string::npos,
          label + "the line after the byte was lost: '" + text + "'");
  require(text.find("Closing") != std::string::npos,
          label + "the following row was lost: '" + text + "'");
}

void a_control_word_that_is_not_a_length_byte_declines(
    const std::string &word) {
  auto extracted = run(rule_then_word(word, false), "Resources");
  require(!extracted.topic.has_value(),
          "a control-like word '" + word +
              "' behind a separator that is not a length byte was admitted");
}

// A control opens its own display line, one token after the line's length
// byte.  dfhqap4a 10.0 record 1678 opens on the drawn table row
// `   |  SRL | Serbian (Latin) ... |`: token 0 is line 0's length byte
// (value 17, spelled U+251C), then a three-blank origin, the rail U+2502, a
// blank and the language code `SRL`.  The splitter opens the record's first
// segment on `SRL` as an `SR` anchor; standing after the rail it is a table
// cell, and the record's first segment is display text.
void an_anchor_shaped_cell_after_the_line_origin_is_text() {
  RecordBuilder builder;
  builder.line({words("   "), TokenWords{0x2502}, words(" "), words("SRL"),
                TokenWords{0x2502}, words("Serbian"), words("("),
                TokenWords{1}, words("Latin"), TokenWords{1}, words(")"),
                TokenWords(48, ' '), TokenWords{0x2502}},
               TokenWords{0x251C});
  builder.line({words("   "), TokenWords{0x2502}, TokenWords(6, 0x2500),
                TokenWords{0x2502}, TokenWords(60, 0x2500),
                TokenWords{0x2502}});
  builder.line({words("   "), TokenWords{0x2502}, words(" "), words("SRB"),
                TokenWords{0x2502}, words("Serbian"), words("(Cyrillic)"),
                TokenWords(45, ' '), TokenWords{0x2502}});
  const auto record = builder.build(1678);
  require(record.ir.display_lines_parse,
          "the drawn-row record's display lines did not parse");
  for (const auto &segment : record.control_segments)
    require(segment.kind == geist::detail::BookControlKind::text &&
                segment.opcode.empty(),
            "a table cell after the line's origin opened control '" +
                segment.opcode + "'");
}

}  // namespace

int main() {
  an_anchor_shaped_cell_after_the_line_origin_is_text();
  a_length_byte_spelling_a_control_is_geometry("citerm");
  a_length_byte_spelling_a_control_is_geometry("ctoce");
  a_control_word_that_is_not_a_length_byte_declines("citerm");
  a_control_word_that_is_not_a_length_byte_declines("ctoce");
  return 0;
}
