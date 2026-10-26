// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A CZ list that the topic never closes ends with the topic
// (doc/boo-spec/markup.adoc, "Lists left open at the end of a topic").
//
// BookServer keeps no list stack: at the topic's `CMENU` it prints
// `</pre></ol></ul></dl>` whatever is open, and a topic without a menu
// simply ends.  The rows of the list are served as stored either way.
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
    std::cerr << "cz_list_topic_end_synthetic: " << message << '\n';
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

// A parameter list whose `EPARML` the topic never writes: SC41-5605-00
// `3.2.4.1` record 528 opens `cz FLOW PARML 3 3`, writes its terms, and ends
// with a `cz OFF TABLE` .. `cz OFF ETABLE 4 4` table and no closer; hosted
// (DT 19980512102401) serves the terms and the table and ends the page.
void a_parameter_list_open_at_the_end() {
  RecordBuilder builder;
  builder.heading("3.2.4.1", "PRTDTASTRM");
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"The", "tag", "has", "values."});
  builder.cz({"cz", "FLOW", "PARML", "3", "3"});
  builder.cz({"cz", "FLOW", "PT", "3", "7"});
  builder.row("   ", {"*AFPDS"});
  builder.cz({"cz", "BREAK", "7"});
  builder.row("       ", {"Advanced", "Function", "Printing."});
  builder.cz({"cz", "FLOW", "PT", "3", "7"});
  builder.row("   ", {"*SCS"});
  builder.cz({"cz", "BREAK", "7"});
  builder.row("       ", {"Character", "string."});
  auto extracted = run(builder.build(528), "PRTDTASTRM");
  require(extracted.topic.has_value(),
          "a parameter list open at the topic's end was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  require(count_kind(*extracted.topic, ProseBlockKindIR::definition_entry) ==
              2,
          "expected two parameter entries:\n" + topic_text(*extracted.topic));
  const auto text = topic_text(*extracted.topic);
  require(contains(text, "*AFPDS") && contains(text, "Printing.") &&
              contains(text, "*SCS") && contains(text, "string."),
          "the open list lost its words:\n" + text);
}

// The same for an unordered list nested in a definition list.
void nested_lists_open_at_the_end() {
  RecordBuilder builder;
  builder.heading("1.5.4", "Values");
  builder.cz({"cz", "FLOW", "DL", "3", "3"});
  builder.cz({"cz", "FLOW", "DT", "3", "10"});
  builder.row("   ", {"Term"});
  builder.cz({"cz", "FLOW", "DD", "10", "10"});
  builder.row("          ", {"Body."});
  builder.cz({"cz", "FLOW", "UL", "10", "10"});
  builder.cz({"cz", "FLOW", "LI", "10", "12"});
  builder.row("          ", {"Item."});
  auto extracted = run(builder.build(40), "Values");
  require(extracted.topic.has_value(),
          "lists open at the topic's end were rejected: " + extracted.error);
  if (!extracted.topic) return;
  require(count_kind(*extracted.topic, ProseBlockKindIR::list_item) == 1,
          "expected the list item:\n" + topic_text(*extracted.topic));
}

// A message list is no such list: its end is elsewhere and still declines
// when rows follow its announcement.
void an_open_message_list_still_declines() {
  RecordBuilder builder;
  builder.heading("6.1", "Messages");
  builder.cz({"cz", "FLOW", "MSGL", "3", "3"});
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"ABC001I", "Text."});
  auto extracted = run(builder.build(41), "Messages");
  require(!extracted.topic.has_value(),
          "an open message list with rows was accepted");
}

// The other end: a row-less `cz OFF EPARML` closing a list an earlier topic
// opened.  dfhlp419 `1.229.1` writes a table, a `cz FLOW P 7 7` paragraph and
// `cz OFF EPARML 0 0` before the next heading; `CZ OFF EPARML` is the
// reader's generic `CZ OFF E` closer, which prints nothing.
void a_parameter_list_closed_from_an_earlier_topic() {
  RecordBuilder builder;
  builder.heading("1.229.1", "WAIT");
  builder.cz({"cz", "FLOW", "P", "7", "7"});
  builder.row("       ", {"See", "the", "Recovery", "Guide."});
  builder.cz({"cz", "OFF", "EPARML", "0", "0"});
  builder.cz({"cz", "FLOW", "H3", "3", "3"});
  auto extracted = run(builder.build(77), "WAIT");
  require(extracted.topic.has_value(),
          "an EPARML closing an earlier topic's list was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  require(contains(topic_text(*extracted.topic), "Guide."),
          "the paragraph lost its words:\n" + topic_text(*extracted.topic));
}

// The start of a list an earlier topic opened: SC41-5304-02 `2.3.14.2`
// writes two `cz FLOW LI 3 7` items `   <bullet>   All distribution ...`
// after a table and closes them with `cz OFF EUL 0 0`; the reader has no
// item list open and serves each item as `<dd>` and the row as stored,
// bullet included.
void items_of_a_list_an_earlier_topic_opened() {
  RecordBuilder builder;
  builder.heading("2.3.14.2", "SAVDLO");
  builder.cz({"cz", "FLOW", "LI", "3", "7"});
  builder.line({words("   "), TokenWords{0x2666}, words("   "), words("All"),
                words("mail.")});
  builder.empty();
  builder.cz({"cz", "FLOW", "LI", "3", "7"});
  builder.line({words("   "), TokenWords{0x2666}, words("   "), words("All"),
                words("changed"), words("documents.")});
  builder.empty();
  builder.cz({"cz", "OFF", "EUL", "0", "0"});
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"You", "can", "omit", "folders."});
  auto extracted = run(builder.build(455), "SAVDLO");
  require(extracted.topic.has_value(),
          "items of a list an earlier topic opened were rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto text = topic_text(*extracted.topic);
  require(contains(text, "mail.") && contains(text, "documents.") &&
              contains(text, "folders."),
          "the continued items lost their words:\n" + text);
  require(count_kind(*extracted.topic, ProseBlockKindIR::list_item) == 2,
          "the bulleted rows are not two bulleted items:\n" + text);
}

}  // namespace

int main() {
  items_of_a_list_an_earlier_topic_opened();
  a_parameter_list_closed_from_an_earlier_topic();
  a_parameter_list_open_at_the_end();
  nested_lists_open_at_the_end();
  an_open_message_list_still_declines();
  return 0;
}

