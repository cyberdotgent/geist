// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// CZ definition terms as BookServer reads them (doc/boo-spec/markup.adoc,
// "Reading a CZ body").
//
// `cz FLOW DT <left> <indent>` prints `<dt>` and remembers the last number
// of the line, the indent.  The next text row is split at that column: the
// cells before it are the term, `<dd>` and the cells from it on the
// definition.  The left column plays no part in the split.
//
// Everything here is synthetic: the test builds whole topic records by hand.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/lowering/prose_topic_document_lowering.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
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
using geist::detail::ProseBlockKindIR;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "cz_definition_terms_synthetic: " << message << '\n';
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

std::size_t count_kind(const geist::detail::ProseTopicIR &topic,
                       ProseBlockKindIR kind) {
  return static_cast<std::size_t>(
      std::count_if(topic.blocks.begin(), topic.blocks.end(),
                    [kind](const auto &block) { return block.kind == kind; }));
}

// A term whose indent equals its left column: GC31-8249-00 `GLOSSARY`
// record 56 opens `cz FLOW DL 3 3` and writes `cz FLOW DT 3 3` before each
// row `   Contrast with: This refers to ...`.  Hosted (DT 19970304141430)
// serves `<dt>  <dd><B>Contrast</B> <B>with:</B> This refers ...`: the row
// starts at the indent column, so the term is empty and the row is the
// definition.
void a_term_whose_indent_is_its_left_column() {
  RecordBuilder builder;
  builder.heading("GLOSSARY", "Glossary");
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"The", "following", "are", "used:"});
  builder.empty();
  builder.cz({"cz", "FLOW", "DL", "3", "3"});
  builder.cz({"cz", "FLOW", "DT", "3", "3"});
  builder.row("   ", {"Contrast", "with:", "This", "refers", "to"});
  builder.row("   ", {"a", "different", "meaning."});
  builder.empty();
  builder.cz({"cz", "FLOW", "DT", "3", "3"});
  builder.row("   ", {"See:", "This", "refers", "the", "reader."});
  builder.empty();
  builder.cz({"cz", "OFF", "EDL", "0", "0"});
  auto extracted = run(builder.build(56), "Glossary");
  require(extracted.topic.has_value(),
          "cz FLOW DT 3 3 terms were rejected: " + extracted.error);
  if (!extracted.topic) return;
  require(count_kind(*extracted.topic, ProseBlockKindIR::definition_entry) ==
              2,
          "expected two definition entries:\n" + topic_text(*extracted.topic));
  for (const auto &block : extracted.topic->blocks)
    if (block.kind == ProseBlockKindIR::definition_entry)
      require(block.term_inline_count == 0,
              "a term took words: '" + block_text(block) + "'");
  const auto text = topic_text(*extracted.topic);
  require(contains(text, "Contrast") && contains(text, "meaning.") &&
              contains(text, "reader."),
          "a definition lost its words:\n" + text);
}

// A term with no display row before the next term or the list's closer
// prints only `<dt>`.  SX26-3751-07 `1.1` record 12 writes
// `cz FLOW DT 3 13` twice in a row, the second with the row
// `   **        Exponentiation`; hosted (DT 19960422095654) serves
// `<dt><dt>   **       <dd>Exponentiation`.  An empty `<dt>` before
// `</dl>` is the same.
void a_term_without_rows_before_the_next_term() {
  RecordBuilder builder;
  builder.heading("1.1", "Operators");
  builder.cz({"cz", "FLOW", "DL", "3", "3"});
  builder.cz({"cz", "FLOW", "DT", "3", "13"});
  builder.cz({"cz", "FLOW", "DT", "3", "13"});
  builder.row("   ", {"**", "        ", "Exponentiation"});
  builder.cz({"cz", "FLOW", "DT", "3", "13"});
  builder.row("   ", {"*", "         ", "Multiplication"});
  builder.cz({"cz", "FLOW", "DT", "3", "13"});
  builder.cz({"cz", "OFF", "EDL", "0", "0"});
  auto extracted = run(builder.build(12), "Operators");
  require(extracted.topic.has_value(),
          "row-less cz FLOW DT terms were rejected: " + extracted.error);
  if (!extracted.topic) return;
  require(count_kind(*extracted.topic, ProseBlockKindIR::definition_entry) ==
              2,
          "expected two definition entries:\n" + topic_text(*extracted.topic));
  const auto text = topic_text(*extracted.topic);
  require(contains(text, "Exponentiation") && contains(text, "Multiplication"),
          "a definition lost its words:\n" + text);
}

// A row-less term before any other directive still has no reading.
void a_term_without_rows_before_a_break_declines() {
  RecordBuilder builder;
  builder.heading("8.3.1.3", "Purge");
  builder.cz({"cz", "FLOW", "DL", "3", "3"});
  builder.cz({"cz", "FLOW", "DT", "18", "34"});
  builder.cz({"cz", "BREAK", "3"});
  builder.row("   ", {"Text."});
  builder.cz({"cz", "OFF", "EDL", "0", "0"});
  auto extracted = run(builder.build(13), "Purge");
  require(!extracted.topic.has_value(),
          "a row-less cz FLOW DT before cz BREAK was accepted");
}

// A parameter term whose rows an anchor line splits: SC33-1653-06 `5.4.3`
// record 1006 stores `cz FLOW PT 3 7`, the row
// `   RECFORM=([UNDEFINED|VARIABLE|FIXED]`, `SRSPTRECF` and the row
// `                    [,BLOCKED|UNBLOCKED][,DOSISAM])` before `cz BREAK 7`;
// SC31-8248-01 `2.15` record 244 puts `SRSPTDQSPL` between the term
// `   position.length` and its description at the indent column.  The reader
// prints the anchor between the rows, which run on as one paragraph.
void a_parameter_term_split_by_an_anchor() {
  RecordBuilder builder;
  builder.heading("5.4.3", "DFHFCT");
  builder.cz({"cz", "FLOW", "PARML", "3", "3"});
  builder.cz({"cz", "FLOW", "PT", "3", "7"});
  builder.row("   ", {"RECFORM=([UNDEFINED|FIXED]"});
  builder.cz({"SRSPTRECF"});
  builder.row("                    ", {"[,BLOCKED|UNBLOCKED])"});
  builder.cz({"cz", "BREAK", "7"});
  builder.row("       ", {"Code", "this", "to", "describe", "records."});
  builder.empty();
  builder.cz({"cz", "FLOW", "PT", "3", "18"});
  builder.row("   ", {"position.length"});
  builder.cz({"SRSPTDQSPL"});
  builder.row("                  ", {"The", "starting", "position."});
  builder.empty();
  builder.cz({"cz", "OFF", "EPARML", "0", "0"});
  auto extracted = run(builder.build(1006), "DFHFCT");
  require(extracted.topic.has_value(),
          "a parameter term split by an anchor was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  require(count_kind(*extracted.topic, ProseBlockKindIR::definition_entry) ==
              2,
          "expected two parameter entries:\n" + topic_text(*extracted.topic));
  const auto text = topic_text(*extracted.topic);
  require(contains(text, "RECFORM") && contains(text, "UNBLOCKED") &&
              contains(text, "records.") && contains(text, "starting"),
          "a parameter entry lost its words:\n" + text);
  for (const auto *id : {"SPTRECF", "SPTDQSPL"})
    require(std::any_of(extracted.topic->anchors.begin(),
                        extracted.topic->anchors.end(),
                        [&](const auto &anchor) {
                          return anchor.id == id &&
                                 anchor.position <
                                     extracted.topic->blocks.size();
                        }),
            std::string("anchor ") + id + " is not placed at an entry");
}

}  // namespace

int main() {
  a_parameter_term_split_by_an_anchor();
  a_term_whose_indent_is_its_left_column();
  a_term_without_rows_before_the_next_term();
  a_term_without_rows_before_a_break_declines();
  return 0;
}

