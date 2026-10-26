// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// CZ footnotes in the forms books store besides `SRFTN<id>` followed by
// `cz FLOW FN` (doc/boo-spec/markup.adoc, "Footnotes").
//
// BookServer prints `<a name="<id>"><hr><h5>` for the `SRFTN<id>` line and
// `</h5></a>` for `SREFTN`, and reads `cz FLOW FN` as the generic
// `CZ FLOW`, which only closes an open `<pre>`.  So the footnote is the rows
// between the two markers, whether a `cz FLOW FN` stands before the anchor,
// after it, or nowhere.
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
    std::cerr << "cz_footnote_forms_synthetic: " << message << '\n';
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

// The text of the footnote block named `id` and of the blocks it parents.
std::optional<std::string> footnote_text(
    const geist::detail::ProseTopicIR &topic, const std::string &id) {
  for (std::size_t index = 0; index < topic.blocks.size(); ++index) {
    const auto &block = topic.blocks[index];
    if (block.kind != ProseBlockKindIR::footnote || block.anchor_id != id)
      continue;
    std::string text = block_text(block);
    for (const auto &child : topic.blocks)
      if (child.parent_block == index) text += "\n" + block_text(child);
    return text;
  }
  return std::nullopt;
}

// The footnote with no `cz FLOW FN` at all: SC41-5612-02 `2.4` record 195
// lines 23-25 are `SRFTNREXLN`, ` |  (6) For REXX procedures, the limit is
// 33.` and `SREFTN`; hosted (DT 19980522111551) serves
// `<a name="FTNREXLN"><hr><h5>` and the row.  Two in a row are two
// footnotes (b1bq1abd `1.5.6` records 228-230 hold seven).
void footnotes_without_a_flow_directive() {
  RecordBuilder builder;
  builder.heading("2.4", "Naming");
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"The", "limit", "is", "10."});
  builder.empty();
  builder.cz({"cz", "FLOW", "H3", "3", "3"});
  builder.cz({"SRFTNREXLN"});
  builder.row("    ", {"(6)", "For", "REXX", "procedures."});
  builder.cz({"SREFTN"});
  builder.empty();
  builder.cz({"SRFTNMRGFN"});
  builder.row("    ", {"(7)", "Merged", "strings"});
  builder.row("       ", {"only."});
  builder.cz({"SREFTN"});
  auto extracted = run(builder.build(195), "Naming");
  require(extracted.topic.has_value(),
          "footnotes without cz FLOW FN were rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto first = footnote_text(*extracted.topic, "FTNREXLN");
  const auto second = footnote_text(*extracted.topic, "FTNMRGFN");
  require(first && second &&
              count_kind(*extracted.topic, ProseBlockKindIR::footnote) == 2,
          "expected the footnotes FTNREXLN and FTNMRGFN:\n" +
              topic_text(*extracted.topic));
  if (!first || !second) return;
  require(contains(*first, "REXX") && contains(*second, "Merged") &&
              contains(*second, "only."),
          "a footnote lost its words:\n" + topic_text(*extracted.topic));
}

// The flow directive before its anchor: dfhtma09 `3.1.3` record 398 line 40
// is `cz FLOW FN 3 7`, line 41 `SRFTNFTNUNIQ40`, then the footnote rows and
// `SREFTN`.  The reader's `cz FLOW` only closes the `<pre>`; the anchor
// line opens the footnote.
void a_flow_directive_before_its_anchor() {
  RecordBuilder builder;
  builder.heading("3.1.3", "RRMS");
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"See", "RRMS."});
  builder.empty();
  builder.cz({"cz", "FLOW", "FN", "3", "7"});
  builder.cz({"SRFTNFTNUNIQ40"});
  builder.row("    ", {"(1)", "RRMS", "comprises", "three"});
  builder.row("       ", {"services."});
  builder.cz({"SREFTN"});
  builder.empty();
  builder.cz({"cz", "FLOW", "FN", "3", "7"});
  builder.cz({"SRFTNFTNUNIQ41"});
  builder.row("    ", {"(2)", "A", "unit", "of", "recovery."});
  builder.cz({"SREFTN"});
  auto extracted = run(builder.build(398), "RRMS");
  require(extracted.topic.has_value(),
          "cz FLOW FN before its SRFTN anchor was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto first = footnote_text(*extracted.topic, "FTNFTNUNIQ40");
  const auto second = footnote_text(*extracted.topic, "FTNFTNUNIQ41");
  require(first && second,
          "expected two footnotes:\n" + topic_text(*extracted.topic));
  if (!first || !second) return;
  require(contains(*first, "comprises") && contains(*first, "services.") &&
              contains(*second, "recovery."),
          "a footnote lost its words:\n" + topic_text(*extracted.topic));
}

// A footnote anchor that no `SREFTN` closes is not proven to be one.
void an_unclosed_anchor_declines() {
  RecordBuilder builder;
  builder.heading("2.5", "Open");
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"Text."});
  builder.cz({"SRFTNOPEN"});
  builder.row("    ", {"(1)", "Never", "closed."});
  auto extracted = run(builder.build(200), "Open");
  require(!extracted.topic.has_value(),
          "an SRFTN that no SREFTN closes was accepted");
}

// Rows between the anchor and a later `cz FLOW FN` would leave the footnote
// the reader opens at the anchor; the model has no place for them.
void rows_before_a_later_flow_directive_decline() {
  RecordBuilder builder;
  builder.heading("2.6", "Late");
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"Text."});
  builder.cz({"SRFTNLATE"});
  builder.row("    ", {"(1)", "Early", "row."});
  builder.cz({"cz", "FLOW", "FN", "3", "7"});
  builder.row("    ", {"Late", "row."});
  builder.cz({"SREFTN"});
  auto extracted = run(builder.build(201), "Late");
  require(!extracted.topic.has_value(),
          "rows between SRFTN and a later cz FLOW FN were accepted");
}

// A footnote with no `cz FLOW FN` whose text goes on in other blocks:
// SC41-4612-00 `5.9.5` record 856 stores `SRFTNFTNUNIQ84`, the row
// `    (23) This limit also includes:`, a `cz FLOW UL 7 7` list of two items
// and `SREFTN`.  The reader prints the list inside the footnote it opened at
// the anchor.
void a_footnote_that_holds_a_list() {
  RecordBuilder builder;
  builder.heading("5.9.5", "Limits");
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"Cursors", "cannot", "be", "opened."});
  builder.empty();
  builder.cz({"cz", "FLOW", "H3", "3", "3"});
  builder.cz({"SRFTNFTNUNIQ84"});
  builder.row("    ", {"(23)", "This", "limit", "also", "includes:"});
  builder.empty();
  builder.cz({"cz", "FLOW", "UL", "7", "7"});
  builder.cz({"cz", "FLOW", "LI", "7", "11"});
  builder.row("           ", {"Any", "records", "accessed"});
  builder.cz({"cz", "FLOW", "LI", "7", "11"});
  builder.row("           ", {"Any", "rows", "deleted"});
  builder.cz({"SREFTN"});
  auto extracted = run(builder.build(856), "Limits");
  require(extracted.topic.has_value(),
          "a footnote holding a list was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto note = footnote_text(*extracted.topic, "FTNFTNUNIQ84");
  require(note && contains(*note, "includes:"),
          "expected the footnote FTNFTNUNIQ84:\n" +
              topic_text(*extracted.topic));
  const auto text = topic_text(*extracted.topic);
  require(contains(text, "records") && contains(text, "deleted"),
          "the footnote's list lost its words:\n" + text);
}

}  // namespace

int main() {
  a_footnote_that_holds_a_list();
  footnotes_without_a_flow_directive();
  a_flow_directive_before_its_anchor();
  an_unclosed_anchor_declines();
  rows_before_a_later_flow_directive_decline();
  return 0;
}
