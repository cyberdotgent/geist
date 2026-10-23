// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The `CZ` directive grammar as hosted BookServer applies it
// (doc/boo-spec/markup.adoc, "CZ layout directives", "Reading a CZ body").
//
// 1. Rows before the first directive are body rows.  After the topic heading
//    BookServer opens the body's `<pre width="132">` and prints every row
//    until a directive changes the layout; a `CZ` topic whose first rows
//    carry no directive is served exactly like a topic of the flattened
//    dialect.  SC31-8223-01 `1.1` (DT 19980528101033) carries a single
//    directive, its closing `cz OFF EFIG 0 0`, and hosted serves the whole
//    body -- `The program-to-program (PPI) interface ...` and the rest -- in
//    that `<pre>`, one `<p>` per empty row.
//
// 2. A display line's length byte opens no control, even where the book's
//    dictionary spells it `cz`.  dfhcag49 `1.1` record 52: token 32 (encoded
//    value 13, one byte) is the genuine `cz` of `cz BREAK 3`, and token 35 at
//    0x2AB48 has the same value 13 and is the length byte of the 13-byte row
//    `   Message identifiers are of two types.`.  Read as a control it was a
//    `cz` whose mode is a space run; read as a length byte it is the row's
//    control slot, and the row is text.  A length byte spelled `cz` in front
//    of another control line (token 47, `cmitem 1.1.1 ...`) was a bare,
//    "incomplete" `cz`; it is the same slot.
//
// 3. A directive whose mode is not FLOW, OFF or BREAK draws nothing and
//    changes nothing.  SG24-5314-00 `2.3.5.16` closes its table region with
//    the line `cz ON TABLE 0 0`; hosted (DT 19990209152814) prints neither
//    the line nor anything for it, and the rows after it continue the open
//    layout.
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
    std::cerr << "cz_directive_grammar_synthetic: " << message << '\n';
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

  // A bulleted row as the CZ dialect stores it: an origin run, the one-byte
  // `U+2666` slot, a two-cell gap and the words (SC31-8223-01 1.1 record 26
  // line 29, tokens 250-254: `   ` value 31, `U+2666` value 3, `  ` value
  // 30, `Filtering`, `alerts`).
  void bullet_row(const std::vector<std::string> &text) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(3 + 2 * text.size()), 1});
    record.tokens.push_back(TokenWords{0x25BA});
    record.encoded_tokens.push_back({31, 1});
    record.tokens.push_back(words("   "));
    record.encoded_tokens.push_back({3, 1});
    record.tokens.push_back(TokenWords{0x2666});
    record.encoded_tokens.push_back({30, 1});
    record.tokens.push_back(words("  "));
    for (const auto &word : text) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(words(word));
    }
  }

  // An empty display line: a zero length byte, which the dictionary spells
  // as the bare spacing marker (dfhcag49 1.1 record 52 token 44).
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
    line({words("csourcefn"), words("DFHG4010")});
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

// The visible words of a block, one space between inline runs.
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

// 1. Two paragraphs of body rows, then the topic's first directive.
void rows_before_the_first_directive_are_body_rows() {
  RecordBuilder builder;
  builder.heading("1.1", "Interface");
  builder.line({words("   "), words("The"), words("interface"),
                words("allows"), words("programs")});
  builder.line({words("   "), words("to"), words("send"), words("alerts.")});
  builder.empty();
  builder.line({words("   "), words("Alerts"), words("are"),
                words("processed"), words("in"), words("order.")});
  builder.empty();
  builder.bullet_row({"Filtering", "alerts"});
  builder.empty();
  builder.bullet_row({"Logging", "alerts"});
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("Closing"), words("paragraph.")});
  auto extracted = run(builder.build(26), "Interface");
  require(extracted.topic.has_value(),
          "a CZ topic whose body opens before its first directive was "
          "rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 5,
          "expected two leading paragraphs, two bullet items and the FLOW P "
          "paragraph, got " +
              std::to_string(blocks.size()) + " blocks:\n" +
              topic_text(*extracted.topic));
  if (blocks.size() != 5) return;
  for (const std::size_t index : {0u, 1u, 4u})
    require(blocks[index].kind == ProseBlockKindIR::paragraph,
            "a leading body row did not become a paragraph");
  // A bulleted body row is a list item, as it is in a topic with no
  // directive at all; the two items form one list.
  require(blocks[2].kind == ProseBlockKindIR::list_item &&
              blocks[3].kind == ProseBlockKindIR::list_item &&
              !blocks[2].ordered &&
              blocks[2].list_ordinal == blocks[3].list_ordinal,
          "the bulleted leading rows are not one unordered list");
  require(contains(block_text(blocks[2]), "Filtering") &&
              contains(block_text(blocks[3]), "Logging"),
          "the bulleted rows lost their text:\n" +
              topic_text(*extracted.topic));
  require(contains(block_text(blocks[0]), "interface") &&
              contains(block_text(blocks[0]), "alerts."),
          "the first leading paragraph lost its rows: '" +
              block_text(blocks[0]) + "'");
  require(contains(block_text(blocks[1]), "processed"),
          "the empty row did not separate the second leading paragraph: '" +
              block_text(blocks[1]) + "'");
  require(contains(block_text(blocks[4]), "Closing"),
          "the FLOW P paragraph moved: '" + block_text(blocks[4]) + "'");
}

// 2a. A row whose length byte the dictionary spells `cz`.
void a_length_byte_spelled_cz_is_the_row_slot() {
  RecordBuilder builder;
  builder.heading("1.1", "Identifiers");
  builder.line({words("cz"), words("BREAK"), words("3")});
  builder.line({words("   "), words("Message"), words("identifiers"),
                words("are"), words("of"), words("two"), words("types.")},
               words("cz"));
  auto extracted = run(builder.build(52), "Identifiers");
  require(extracted.topic.has_value(),
          "a row whose length byte spells `cz` was read as a control: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto text = topic_text(*extracted.topic);
  require(contains(text, "Message") && contains(text, "types."),
          "the row framed by a `cz` length byte lost its text:\n" + text);
  require(!contains(text, "cz"),
          "the `cz` length byte leaked into the text:\n" + text);
}

// 2b. The same byte in front of the topic's first directive: the row it
// frames precedes every genuine directive and is a body row as well.
void a_length_byte_spelled_cz_before_any_directive() {
  RecordBuilder builder;
  builder.heading("1.4.1", "DFH0100");
  builder.line({words("   "), words("DFH0100"), words("CICS"),
                words("SUBSYSTEM"), words("IS"), words("NOW"),
                words("INITIALIZED")},
               words("cz"));
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("4")});
  builder.line({words("   "), words("Explanation:"), words("None.")});
  auto extracted = run(builder.build(86), "DFH0100");
  require(extracted.topic.has_value(),
          "a `cz` length byte before the first directive was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto text = topic_text(*extracted.topic);
  require(contains(text, "SUBSYSTEM") && contains(text, "Explanation:"),
          "the message rows were not both kept:\n" + text);
}

// 3. `cz ON TABLE 0 0` draws nothing and closes nothing: the row after it
// continues the paragraph the FLOW P opened.
void an_unknown_mode_is_inert() {
  RecordBuilder builder;
  builder.heading("2.3.5.16", "GMCF");
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("Alpha"), words("row")});
  builder.line({words("cz"), words("ON"), words("TABLE"), words("0"),
                words("0")});
  builder.line({words("   "), words("Beta"), words("row")});
  auto extracted = run(builder.build(178), "GMCF");
  require(extracted.topic.has_value(),
          "a `cz ON TABLE 0 0` line was not inert: " + extracted.error);
  if (!extracted.topic) return;
  const auto text = topic_text(*extracted.topic);
  require(!contains(text, "ON") && !contains(text, "TABLE"),
          "the inert directive leaked its words:\n" + text);
  require(extracted.topic->blocks.size() == 1 &&
              contains(text, "Alpha") && contains(text, "Beta"),
          "the rows around the inert directive are not one paragraph:\n" +
              text);
}

// 1b. A title line cut at a title word spelled like a control is no leading
// body row: S85G-8681-00 11.1 record 1217 line 8 is `ST  SRPI
// Compatibility`, and the splitter opened a segment on `SRPI`.  Hosted
// heads the topic `11.1   SRPI Compatibility`; reading the rest of the
// line as the body's first row would print the title as a paragraph, so the
// topic declines.
void a_split_title_line_is_no_leading_row() {
  RecordBuilder builder;
  builder.line({words("sh11.1")});
  builder.line({words("ctopicn"), words("7")});
  builder.line({words("cparent"), words("11.0")});
  builder.line({words("cforwardlevel"), words("11.2")});
  builder.line({words("cbacklevel"), words("10.0")});
  builder.line({words("csummary"), words("9"), words("0"), words("9")});
  builder.line({words("chdlevel"), words(":H2")});
  builder.line({words("csourcefn"), words("C78EOC10")});
  builder.line({words("ST"), words("SRPI"), words("Compatibility")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("PC/3270"), words("supports"),
                words("SRPI.")});
  auto extracted = run(builder.build(1217), "SRPI Compatibility");
  if (!extracted.topic) return;  // declined: correct
  const auto text = topic_text(*extracted.topic);
  require(!contains(text, "Compatibility"),
          "the split title line became a body row:\n" + text);
}

}  // namespace

int main() {
  rows_before_the_first_directive_are_body_rows();
  a_split_title_line_is_no_leading_row();
  a_length_byte_spelled_cz_is_the_row_slot();
  a_length_byte_spelled_cz_before_any_directive();
  an_unknown_mode_is_inert();
  return 0;
}
