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

// A `cz BREAK` inside a verbatim region is a line break of it.  The
// `CZ BREAK` rule prints `<br>` whatever is open, and nothing ends the
// region: SC26-4834-03 `3.169` (DT 19950908105009) serves `cz OFF XMP`,
// `     SQLCODE:  -30041   Database:  DB2`, `cz BREAK 5`,
// `     QMF Function:  All`, `cz OFF EXMP 0 0` as one `<pre width="80">`
// holding both rows with `<br>` between them.
void a_break_inside_a_region_is_a_blank_row() {
  RecordBuilder builder;
  builder.heading("3.169", "DSQ10421");
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"Contact", "your", "administrator."});
  builder.empty();
  builder.cz({"cz", "OFF", "XMP"});
  builder.row("     ", {"SQLCODE:", "-30041"});
  builder.cz({"cz", "BREAK", "5"});
  builder.row("     ", {"QMF", "Function:", "All"});
  builder.cz({"cz", "OFF", "EXMP", "0", "0"});
  auto extracted = run(builder.build(268), "DSQ10421");
  require(extracted.topic.has_value(),
          "a cz BREAK inside cz OFF XMP was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 2 &&
              blocks[1].kind == ProseBlockKindIR::preformatted,
          "the region with a break is not one verbatim block:\n" +
              topic_text(*extracted.topic));
  if (blocks.size() != 2) return;
  const auto text = block_text(blocks[1]);
  require(contains(text, "SQLCODE:") && contains(text, "Function:"),
          "the verbatim block lost a row: '" + text + "'");
}

// A `cz FLOW` inside a verbatim region ends its `<pre>`: the generic
// `CZ FLOW` rule closes whatever `<pre>` is open, and the later closer
// prints nothing.  SC24-5784-00 `3.2.522` (DT 19950910224116) serves
// `cz OFF XMP`, `        WRONG                RIGHT` in `<pre width="80">`,
// then `</pre>` for `cz FLOW P 5 5` and its eight rows as flowing text, and
// nothing for the `cz OFF EXMP 0 0` that follows them.
void a_flow_inside_a_region_ends_it() {
  RecordBuilder builder;
  builder.heading("3.2.522", "DMS454E");
  builder.cz({"cz", "FLOW", "XPL", "3", "3"});
  builder.row("   ", {"Explanation:", "For", "example:"});
  builder.empty();
  builder.cz({"cz", "OFF", "XMP"});
  builder.row("        ", {"WRONG", "RIGHT"});
  builder.empty();
  builder.cz({"cz", "FLOW", "P", "5", "5"});
  builder.row("     ", {"Select", "Select"});
  builder.row("     ", {"end", "end"});
  builder.cz({"cz", "OFF", "EXMP", "0", "0"});
  builder.cz({"cz", "FLOW", "SYSACT", "3", "3"});
  builder.row("   ", {"System", "Action:", "RC=20007."});
  auto extracted = run(builder.build(2840), "DMS454E");
  require(extracted.topic.has_value(),
          "a cz FLOW P inside cz OFF XMP was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 4 &&
              blocks[1].kind == ProseBlockKindIR::preformatted &&
              blocks[2].kind == ProseBlockKindIR::paragraph &&
              blocks[3].kind == ProseBlockKindIR::paragraph,
          "expected paragraph, verbatim block, paragraph, paragraph:\n" +
              topic_text(*extracted.topic));
  if (blocks.size() != 4) return;
  require(contains(block_text(blocks[1]), "WRONG") &&
              !contains(block_text(blocks[1]), "Select"),
          "the region did not end at the cz FLOW P: '" +
              block_text(blocks[1]) + "'");
  require(contains(block_text(blocks[2]), "Select") &&
              contains(block_text(blocks[2]), "end"),
          "the cz FLOW P rows did not flow: '" + block_text(blocks[2]) + "'");
}

// Without its closer the region's end is not the source's, and the topic
// still declines.
void a_flow_inside_an_unclosed_region_declines() {
  RecordBuilder builder;
  builder.heading("3.2.523", "DMS455E");
  builder.cz({"cz", "OFF", "XMP"});
  builder.row("        ", {"WRONG", "RIGHT"});
  builder.cz({"cz", "FLOW", "P", "5", "5"});
  builder.row("     ", {"Select", "Select"});
  auto extracted = run(builder.build(2841), "DMS455E");
  require(!extracted.topic.has_value(),
          "a cz FLOW P inside an unclosed cz OFF XMP was admitted");
}

// A verbatim region closed by another region's closer.  Any `CZ OFF E..`
// is BookServer's generic closer: it resets the region depth and prints
// nothing, and the `<pre>` stays open up to the next `CZ FLOW`.
// SC33-1369-09 `2.1.1` (DT 19971205131216) opens `cz OFF SYNTAX`, draws its
// boxed diagram and ends with `cz OFF EREV 0 0` and `cz FLOW H3 3 3`, with
// no `ESYNTAX` anywhere; hosted serves the diagram as one
// `<pre width="80">`.  With the region's own closer still to come, or
// another directive before the next `cz FLOW`, the end stays unproven.
void a_region_closed_by_another_regions_closer() {
  const auto topic = [](const std::string &tail) {
    RecordBuilder builder;
    builder.heading("2.1.1", "Sender");
    builder.cz({"cz", "BREAK", "3"});
    builder.cz({"cz", "OFF", "SYNTAX"});
    builder.row("   ", {">>__ALTER", "CHANNEL__><"});
    builder.row("   ", {"(1)", "Valid", "only", "on", "AIX."});
    builder.cz({"cz", "OFF", "EREV", "0", "0"});
    if (tail == "flow") builder.cz({"cz", "FLOW", "H3", "3", "3"});
    if (tail == "own") {
      builder.cz({"cz", "FLOW", "P", "3", "3"});
      builder.row("   ", {"Text."});
      builder.cz({"cz", "OFF", "ESYNTAX", "0", "0"});
    }
    if (tail == "off") builder.cz({"cz", "OFF", "XMP"});
    return builder.build(97);
  };
  for (const auto *tail : {"flow", "end"}) {
    auto extracted = run(topic(tail), "Sender");
    require(extracted.topic.has_value(),
            std::string("a cz OFF SYNTAX closed by cz OFF EREV (") + tail +
                ") was rejected: " + extracted.error);
    if (!extracted.topic) continue;
    const auto &blocks = extracted.topic->blocks;
    require(blocks.size() == 1 &&
                blocks[0].kind == ProseBlockKindIR::preformatted &&
                blocks[0].preformatted_lines.size() == 2,
            "the region is not one two-row verbatim block:\n" +
                topic_text(*extracted.topic));
  }
  for (const auto *tail : {"own", "off"}) {
    auto extracted = run(topic(tail), "Sender");
    require(!extracted.topic.has_value(),
            std::string("a region closed by a foreign closer with an "
                        "unproven end (") +
                tail + ") was admitted");
  }
}

// An anchor before the first row of a parameter's body stands at that
// parameter's entry, not at the end of the topic.  SC31-6552-00 `4.4.3`
// record 2021 stores `cz FLOW PT 3 7` `   ASIRFMSG=ALLSSCP`, `cz BREAK 7`,
// `SRSPTSOAROL` and `       specifies that messages are issued in all
// SSCPs.`; hosted (DT 19950503072955) serves `<a name="SPTSOAROL">` on that
// body row, inside the first entry and before the second.
void an_anchor_before_a_parameter_body_stays_at_its_entry() {
  RecordBuilder builder;
  builder.heading("4.4.3", "ASIRFMSG");
  builder.cz({"cz", "FLOW", "PARML", "3", "3"});
  builder.cz({"cz", "FLOW", "PT", "3", "7"});
  builder.row("   ", {"ASIRFMSG=ALLSSCP"});
  builder.cz({"cz", "BREAK", "7"});
  builder.line({words("SRSPTSOAROL")});
  builder.row("       ", {"specifies", "all", "SSCPs."});
  builder.cz({"cz", "FLOW", "PT", "3", "7"});
  builder.row("   ", {"ASIRFMSG=NONE"});
  builder.cz({"cz", "BREAK", "7"});
  builder.row("       ", {"specifies", "none."});
  builder.cz({"cz", "OFF", "EPARML", "0", "0"});
  auto extracted = run(builder.build(2021), "ASIRFMSG");
  require(extracted.topic.has_value(),
          "a parameter list with an anchored body was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &topic = *extracted.topic;
  const auto anchor =
      std::find_if(topic.anchors.begin(), topic.anchors.end(),
                   [](const auto &candidate) {
                     return candidate.id == "SPTSOAROL";
                   });
  require(anchor != topic.anchors.end(), "the SPTSOAROL anchor is missing");
  if (anchor == topic.anchors.end()) return;
  require(anchor->position < topic.blocks.size() &&
              topic.blocks[anchor->position].kind ==
                  ProseBlockKindIR::definition_entry &&
              contains(block_text(topic.blocks[anchor->position]), "ALLSSCP"),
          "the anchor before the first body row is not at its entry; it "
          "stands at block " +
              std::to_string(anchor->position) + " of " +
              std::to_string(topic.blocks.size()));
}

std::size_t count_kind(const geist::detail::ProseTopicIR &topic,
                       ProseBlockKindIR kind);

// Verbatim rows keep their highlighting and links.  BookServer prints every
// row of a `<pre>` with its `CFONT` and `CSELECT` markup: GC35-0316-00
// `3.1.6` (DT 19980403120326) serves the `except` of a row its
// `cz OFF EXMP` closer carries as `<I>except</I>` inside the example's
// `<pre width="80">`, and the rows after a named closer (`cz OFF EDL`) keep
// their links in the `<pre width="132">` it opens.
void verbatim_rows_keep_highlighting_and_links() {
  RecordBuilder builder;
  builder.heading("3.1.6", "Batch");
  builder.cz({"cz", "OFF", "XMP"});
  builder.cz({"cfont", "5", "6", "2"});
  builder.row("     ", {"except", "this"});
  builder.cz({"cz", "OFF", "EXMP", "0", "0"});
  builder.cz({"cz", "FLOW", "DL", "3", "3"});
  builder.cz({"cz", "FLOW", "DT", "3", "13"});
  builder.row("   ", {"Term", "Its", "body."});
  builder.cz({"cz", "OFF", "EDL", "0", "0"});
  builder.cz({"cselect", "7", "5", "HDRBATCH"});
  builder.row("   ", {"See", "Batch", "mode."});
  auto extracted = run(builder.build(316), "Batch");
  require(extracted.topic.has_value(),
          "styled verbatim rows were rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  const auto styled = [](const geist::detail::ProseBlockIR &block) {
    return block.kind == ProseBlockKindIR::preformatted &&
           !block.preformatted_line_inline_ends.empty();
  };
  require(!blocks.empty() && styled(blocks.front()) &&
              std::any_of(blocks.front().inlines.begin(),
                          blocks.front().inlines.end(),
                          [](const auto &in) {
                            return in.text == "except" &&
                                   in.kind == geist::detail::
                                                  ProseInlineKindIR::emphasis;
                          }),
          "the example row lost its highlighting:\n" +
              topic_text(*extracted.topic));
  require(!blocks.empty() && styled(blocks.back()) &&
              std::any_of(blocks.back().inlines.begin(),
                          blocks.back().inlines.end(),
                          [](const auto &in) {
                            return in.kind == geist::detail::ProseInlineKindIR::
                                                  cross_reference &&
                                   in.text == "Batch";
                          }),
          "the row after the named closer is not a verbatim row with its "
          "link:\n" +
              topic_text(*extracted.topic));
}

// Region depth inside a verbatim region.  A generic region opened inside it
// only counts the reader's depth and prints nothing: SC24-5718-00 `B.10.5`
// (DT 19951023144552) opens `cz OFF XMP`, then `cz OFF CGRAPHIC` with the
// OS/2 `DIR` screen and closes with `cz OFF EXMP 2 2` alone -- one
// `<pre width="80">` on hosted.  A foreign closer may be followed by a new
// opener of the same region: SC24-5760-00 `1.2.27.1` (DT 19950909201627)
// ends its first `SYNTAX` with `cz OFF EHP1 0 0` and opens the second at
// once, closing that one with `cz OFF ESYNTAX`.
void nested_openers_and_foreign_closers() {
  {
    RecordBuilder builder;
    builder.heading("B.10.5", "PIPE");
    builder.cz({"cz", "FLOW", "P", "3", "3"});
    builder.row("   ", {"The", "example:"});
    builder.cz({"cz", "OFF", "XMP"});
    builder.cz({"cz", "OFF", "CGRAPHIC"});
    builder.row("       ", {"'PIPE',"});
    builder.row("       ", {"'os/2", "|',"});
    builder.cz({"cz", "OFF", "EXMP", "2", "2"});
    builder.cz({"cz", "FLOW", "H4", "3", "3"});
    auto extracted = run(builder.build(1471), "PIPE");
    require(extracted.topic.has_value(),
            "a CGRAPHIC opened inside an XMP was rejected: " +
                extracted.error);
    if (extracted.topic)
      require(count_kind(*extracted.topic, ProseBlockKindIR::preformatted) ==
                      1 &&
                  extracted.topic->blocks.back().preformatted_lines.size() == 2,
              "the nested region is not one two-row block:\n" +
                  topic_text(*extracted.topic));
  }
  {
    RecordBuilder builder;
    builder.heading("1.2.27.1", "DIAG");
    builder.cz({"cz", "OFF", "SYNTAX"});
    builder.row("   ", {">>__DIAG__><"});
    builder.cz({"cz", "OFF", "EHP1", "0", "0"});
    builder.cz({"cz", "OFF", "SYNTAX"});
    builder.row("   ", {">>__MORE__><"});
    builder.cz({"cz", "OFF", "ESYNTAX", "0", "0"});
    builder.cz({"cz", "FLOW", "H4", "3", "3"});
    auto extracted = run(builder.build(526), "DIAG");
    require(extracted.topic.has_value(),
            "a SYNTAX ended by EHP1 before a second SYNTAX was rejected: " +
                extracted.error);
    if (extracted.topic)
      require(count_kind(*extracted.topic, ProseBlockKindIR::preformatted) ==
                  2,
              "expected two syntax blocks:\n" + topic_text(*extracted.topic));
  }
  {
    // A TABDATA pair inside an XMP: SC33-1362-01 `1.1.1.1.1` record 61
    // opens `cz OFF XMP` and at once `cz OFF TABDATA`, writes the RPG
    // declaration rows, and closes with `cz OFF ETABDATA 2 2` and
    // `cz OFF EXMP 2 2` (record 64 lines 12-13); hosted (DT
    // 19950831082352) serves the rows as one `<pre width="80">`.
    RecordBuilder builder;
    builder.heading("1.1.1.1.1", "RPG");
    builder.cz({"cz", "BREAK", "3"});
    builder.cz({"cz", "OFF", "XMP"});
    builder.cz({"cz", "OFF", "TABDATA"});
    builder.row("          ", {"D*", "MQDLH", "Structure"});
    builder.row("          ", {"D", "  ", "DLSID", "      ", "1"});
    builder.cz({"cz", "OFF", "ETABDATA", "2", "2"});
    builder.cz({"cz", "OFF", "EXMP", "2", "2"});
    builder.cz({"cz", "FLOW", "H4", "3", "3"});
    auto extracted = run(builder.build(61), "RPG");
    require(extracted.topic.has_value(),
            "a TABDATA pair inside an XMP was rejected: " + extracted.error);
    if (extracted.topic)
      require(count_kind(*extracted.topic, ProseBlockKindIR::preformatted) ==
                      1 &&
                  extracted.topic->blocks.back().preformatted_lines.size() == 2,
              "the nested region is not one two-row block:\n" +
                  topic_text(*extracted.topic));
  }
}

// `cz FLOW LP` outside a definition list.  Before the first item of an item
// list the part is the list's lead-in (SC26-9920-00 `1.1.1.6.1`, DT
// 20000114153724: `<ol type=1 start=1><br>   Capturing data ...` before the
// first `<li>`); in a parameter list it is a block after the entry before
// it (SC24-5760-00 `1.2.20.6`).  A heading tag past the dialect's levels
// flows (SC24-5760-00 `4.9.1.1`, `cz FLOW H7 3 5`).
void list_parts_outside_definition_lists() {
  RecordBuilder builder;
  builder.heading("1.1.1.6.1", "Process");
  builder.cz({"cz", "FLOW", "OL", "3", "3"});
  builder.cz({"cz", "FLOW", "LP", "3", "3"});
  builder.row("   ", {"Capturing", "data"});
  builder.cz({"cz", "FLOW", "LI", "3", "7"});
  builder.row("   ", {"1.", "The", "Capture", "program", "reads."});
  builder.cz({"cz", "OFF", "EOL", "0", "0"});
  builder.cz({"cz", "FLOW", "PARML", "3", "3"});
  builder.cz({"cz", "FLOW", "PT", "3", "7"});
  builder.row("   ", {"SXORGCTA"});
  builder.cz({"cz", "BREAK", "7"});
  builder.row("       ", {"A", "fullword", "field."});
  builder.cz({"cz", "FLOW", "LP", "3", "3"});
  builder.row("   ", {"The", "start", "of", "a", "table."});
  builder.cz({"cz", "OFF", "EPARML", "0", "0"});
  builder.cz({"cz", "FLOW", "H7", "3", "5"});
  builder.row("     ", {"Example:", "a", "prtid."});
  auto extracted = run(builder.build(78), "Process");
  require(extracted.topic.has_value(),
          "list parts outside a definition list were rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(!blocks.empty() && blocks.front().kind == ProseBlockKindIR::paragraph &&
              block_text(blocks.front()) == "Capturing data",
          "the OL lead-in is not a paragraph before the items:\n" +
              topic_text(*extracted.topic));
  const auto part = std::find_if(blocks.begin(), blocks.end(),
                                 [](const auto &block) {
                                   return block_text(block) ==
                                          "The start of a table.";
                                 });
  require(part != blocks.end() && part->kind == ProseBlockKindIR::paragraph &&
              part->parent_block == static_cast<std::size_t>(-1),
          "the parameter-list part is not a block of its own:\n" +
              topic_text(*extracted.topic));
  require(block_text(blocks.back()) == "Example: a prtid." &&
              blocks.back().kind == ProseBlockKindIR::paragraph,
          "the H7 rows do not flow:\n" + topic_text(*extracted.topic));
}

// A term with no row before the list's end draws nothing: GC31-8553-00
// `4.9` (DT 19970730115116) ends its parameter list with a row-less
// `cz FLOW PT 3 7`, a row-less `cz BREAK 3` and `cz OFF EPARML 0 0`, and
// hosted serves only `<p>` and `<br>` there.
void a_trailing_rowless_parameter_term_draws_nothing() {
  RecordBuilder builder;
  builder.heading("4.9", "Parameters");
  builder.cz({"cz", "FLOW", "PARML", "3", "3"});
  builder.cz({"cz", "FLOW", "PT", "3", "7"});
  builder.row("   ", {"RECONFIG"});
  builder.cz({"cz", "BREAK", "7"});
  builder.row("       ", {"Performs", "reconfiguration."});
  builder.cz({"cz", "FLOW", "PT", "3", "7"});
  builder.cz({"cz", "BREAK", "3"});
  builder.cz({"cz", "OFF", "EPARML", "0", "0"});
  builder.cz({"cz", "FLOW", "H3", "3", "3"});
  builder.row("   ", {"Examples"});
  auto extracted = run(builder.build(447), "Parameters");
  require(extracted.topic.has_value(),
          "a trailing row-less PT was rejected: " + extracted.error);
  if (extracted.topic)
    require(count_kind(*extracted.topic, ProseBlockKindIR::definition_entry) ==
                1,
            "the row-less PT drew an entry:\n" + topic_text(*extracted.topic));
}

// A parameter whose term is the syntax diagram after it.  GC24-5837-01
// `5.2.8.3` (DT 19990503161545) stores a row-less `cz FLOW PT 7 11`,
// `cz OFF SYNTAX`, the `MODENUM` diagram, `cz OFF ESYNTAX 4 4`, then
// `cz BREAK 7` and the description.  BookServer prints nothing for the PT,
// opens `<pre width="80">` for the region, and since the closer leaves the
// `<pre>` open and the break only prints `<br>`, the description rows are
// served inside that same `<pre>`.
void a_parameter_term_that_is_a_syntax_diagram() {
  RecordBuilder builder;
  builder.heading("5.2.8.3", "Options");
  builder.cz({"cz", "FLOW", "PARML", "7", "7"});
  builder.cz({"cz", "FLOW", "PT", "7", "11"});
  builder.cz({"cz", "OFF", "SYNTAX"});
  builder.row("           ", {">>__MODENUM__2__><"});
  builder.cz({"cz", "OFF", "ESYNTAX", "4", "4"});
  builder.cz({"cz", "BREAK", "7"});
  builder.row("       ", {"specifies", "the", "mode."});
  builder.cz({"cz", "FLOW", "PT", "7", "11"});
  builder.row("       ", {"LIBTYPE"});
  builder.cz({"cz", "BREAK", "11"});
  builder.row("           ", {"specifies", "the", "format."});
  builder.cz({"cz", "OFF", "EPARML", "4", "4"});
  auto extracted = run(builder.build(1344), "Options");
  require(extracted.topic.has_value(),
          "a row-less PT before its syntax diagram was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &topic = *extracted.topic;
  require(count_kind(topic, ProseBlockKindIR::definition_entry) == 2,
          "expected two parameter entries:\n" + topic_text(topic));
  const auto entry = std::find_if(
      topic.blocks.begin(), topic.blocks.end(), [](const auto &block) {
        return block.kind == ProseBlockKindIR::definition_entry;
      });
  require(entry != topic.blocks.end() && entry->inlines.empty() &&
              entry->body_blocks.size() == 1 &&
              topic.blocks[entry->body_blocks.front()].kind ==
                  ProseBlockKindIR::preformatted,
          "the empty term does not own the diagram:\n" + topic_text(topic));
  if (entry == topic.blocks.end() || entry->body_blocks.size() != 1) return;
  const auto &diagram = topic.blocks[entry->body_blocks.front()];
  require(diagram.preformatted_lines.size() == 3 &&
              contains(diagram.preformatted_lines.back(), "specifies the mode."),
          "the description after the break is not a row of the diagram:\n" +
              topic_text(topic));
}

std::size_t count_kind(const geist::detail::ProseTopicIR &topic,
                       ProseBlockKindIR kind) {
  std::size_t count = 0;
  for (const auto &block : topic.blocks)
    if (block.kind == kind) ++count;
  return count;
}

// A `cz FLOW PT` whose rows run on at its indent column carries its own
// description.  BookServer reads PT as the generic `CZ FLOW` (a `<p>` and
// the rows): SC31-8212-00 `2.3.4.2.3` (DT 19950906051034) stores
// `cz FLOW PT 5 8`, `     RECEIVE_FILL_BUFFER` and three rows at column 8
// with no `cz BREAK`, and hosted serves
// `<p>     <tt>RECEIVE_FILL_BUFFER</tt>` followed by those rows.
void a_parameter_term_with_rows_at_its_indent() {
  RecordBuilder builder;
  builder.heading("2.3.4.2.3", "Fields");
  builder.cz({"cz", "FLOW", "PARML", "5", "5"});
  builder.cz({"cz", "FLOW", "PT", "5", "8"});
  builder.row("     ", {"RECEIVE_FILL_BUFFER"});
  builder.row("        ", {"The", "program", "receives", "data."});
  builder.empty();
  builder.cz({"cz", "FLOW", "PT", "5", "8"});
  builder.row("     ", {"RECEIVE_FILL_LL"});
  builder.row("        ", {"The", "program", "receives", "a", "record."});
  builder.empty();
  builder.cz({"cz", "OFF", "EPARML", "2", "2"});
  auto extracted = run(builder.build(877), "Fields");
  require(extracted.topic.has_value(),
          "parameter terms with rows at their indent were rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 2 &&
              blocks[0].kind == ProseBlockKindIR::definition_entry &&
              blocks[1].kind == ProseBlockKindIR::definition_entry,
          "expected two parameter entries:\n" + topic_text(*extracted.topic));
  if (blocks.size() != 2) return;
  require(blocks[0].term_inline_count > 0 &&
              blocks[0].term_inline_count < blocks[0].inlines.size(),
          "the first entry has no separate description: '" +
              block_text(blocks[0]) + "'");
}

// A term whose body never comes is a term alone.  GC24-5851-01 `11.5.2`
// (DT 19990506135028) stores `cz FLOW PT 7 11`, `     parms`, an empty
// `cz FLOW P 11 11` and the next `cz FLOW PT`; hosted serves
// `<p><br> |     <var>parms</var>` and nothing else for it.  GC41-5435-03
// `9.2` hands a `cz FLOW DT 3 13` term (`Examples:`) its body as two
// `cz OFF XMP` regions and no `cz FLOW DD`; hosted serves
// `<dt>   <B>Examples:</B>` and the regions after the `<dd>`.
void terms_without_a_separate_body() {
  RecordBuilder builder;
  builder.heading("11.5.2", "Operands");
  builder.cz({"cz", "FLOW", "PARML", "7", "7"});
  builder.cz({"cz", "FLOW", "PT", "7", "11"});
  builder.row("     ", {"parms"});
  builder.empty();
  builder.cz({"cz", "FLOW", "P", "11", "11"});
  builder.cz({"cz", "FLOW", "PT", "7", "11"});
  builder.row("     ", {"-dp"});
  builder.cz({"cz", "BREAK", "11"});
  builder.row("         ", {"Activates", "tracing."});
  builder.cz({"cz", "OFF", "EPARML", "0", "0"});
  builder.cz({"cz", "FLOW", "DL", "3", "3"});
  builder.cz({"cz", "FLOW", "DT", "3", "13"});
  builder.row("   ", {"Examples:"});
  builder.empty();
  builder.cz({"cz", "OFF", "XMP"});
  builder.row("               ", {"AddType", ".shtml"});
  builder.cz({"cz", "OFF", "EXMP", "10", "10"});
  builder.cz({"cz", "OFF", "XMP"});
  builder.row("               ", {"AddType", ".htmls"});
  builder.cz({"cz", "OFF", "EXMP", "10", "10"});
  builder.cz({"cz", "FLOW", "DT", "3", "13"});
  builder.row("   ", {"Alone:"});
  builder.cz({"cz", "OFF", "EDL", "0", "0"});
  auto extracted = run(builder.build(790), "Operands");
  require(extracted.topic.has_value(),
          "terms without a separate body were rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &topic = *extracted.topic;
  require(count_kind(topic, ProseBlockKindIR::definition_entry) == 4,
          "expected four entries:\n" + topic_text(topic));
  require(count_kind(topic, ProseBlockKindIR::preformatted) == 2,
          "the term's regions are not two verbatim blocks:\n" +
              topic_text(topic));
  const auto text = topic_text(topic);
  require(contains(text, "parms") && contains(text, "Alone:") &&
              contains(text, "Activates"),
          "a term lost its text:\n" + text);
  // A term alone lowers to an entry with no definition and renders.
  geist::detail::TopicIdentityIR identity;
  identity.id = "11.5.2";
  identity.title = "Operands";
  identity.heading_level = 2;
  identity.start_logical_record = 790;
  identity.end_logical_record = 790;
  std::string error;
  const auto document =
      geist::detail::lower_prose_topic_to_document_ir(identity, topic, &error);
  require(document.has_value(), "terms alone did not lower: " + error);
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "parms") && contains(markdown, "Alone:"),
          "a term alone did not render: " + markdown);
}

// A list that never writes an item holds only its paragraphs.  BookServer
// prints `<ul>` for `CZ FLOW SL` and `</ul>` for its closer, and the rows of
// the `cz FLOW P` between them as text: GA27-3960-05 `5.24.4` (DT
// 19961015143739) stores `cz FLOW SL 3 3`, `cz FLOW P 7 7` with four rows and
// `cz OFF ESL 0 0`, served as `<ul>` and `<br> |     If, in the frame
// received, ...`.
void a_list_without_items_holds_its_paragraphs() {
  RecordBuilder builder;
  builder.heading("5.24.4", "Example");
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"The", "action", "would", "be:"});
  builder.empty();
  builder.cz({"cz", "FLOW", "SL", "3", "3"});
  builder.cz({"cz", "FLOW", "P", "7", "7"});
  builder.row("       ", {"If", "the", "condition", "is", "true,"});
  builder.row("       ", {"the", "frame", "is", "discarded."});
  builder.empty();
  builder.cz({"cz", "OFF", "ESL", "0", "0"});
  auto extracted = run(builder.build(546), "Example");
  require(extracted.topic.has_value(),
          "an SL without items was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 2 &&
              blocks[0].kind == ProseBlockKindIR::paragraph &&
              blocks[1].kind == ProseBlockKindIR::paragraph &&
              contains(block_text(blocks[1]), "discarded."),
          "the item-less list is not its paragraph:\n" +
              topic_text(*extracted.topic));
}

std::optional<geist::detail::DocumentIR> lower(
    const geist::detail::ProseTopicIR &topic, const std::string &id) {
  geist::detail::TopicIdentityIR identity;
  identity.id = id;
  identity.title = "Topic";
  identity.heading_level = 2;
  std::string error;
  auto document =
      geist::detail::lower_prose_topic_to_document_ir(identity, topic, &error);
  require(document.has_value(), "topic " + id + " did not lower: " + error);
  return document;
}

// An item or a definition that owns no row of its own.  BookServer prints
// `<li>` (or `<dd>`) and whatever follows: GC28-1251-08 `EDITION` (DT
// 19980123093613) serves the row-less `cz FLOW LI 7 7` between two address
// lines as `<li> <li>        FAX ...`, an empty item; GA27-4105-03
// `5.2.2.1` (DT 19980701154012) follows `cz FLOW DT 3 13`
// `Service agreement` with a row-less `cz FLOW DD 13 13` and a
// `cz FLOW P 13 13` paragraph, served as `<dd>` and that paragraph.
void items_and_definitions_without_rows() {
  RecordBuilder builder;
  builder.heading("EDITION", "Edition");
  builder.cz({"cz", "FLOW", "SL", "3", "3"});
  builder.cz({"cz", "FLOW", "LI", "7", "7"});
  builder.row("       ", {"United", "States"});
  builder.cz({"cz", "FLOW", "LI", "7", "7"});
  builder.cz({"cz", "FLOW", "LI", "7", "7"});
  builder.row("       ", {"FAX:", "1+914"});
  builder.cz({"cz", "OFF", "ESL", "0", "0"});
  builder.cz({"cz", "FLOW", "UL", "3", "3"});
  builder.cz({"cz", "FLOW", "LI", "3", "7"});
  builder.cz({"cz", "FLOW", "DL", "7", "7"});
  builder.cz({"cz", "FLOW", "DT", "7", "13"});
  builder.row("       ", {"Lines", "Two."});
  builder.cz({"cz", "OFF", "EDL", "0", "0"});
  builder.cz({"cz", "OFF", "EUL", "0", "0"});
  builder.cz({"cz", "FLOW", "DL", "3", "3"});
  builder.cz({"cz", "FLOW", "DT", "3", "13"});
  builder.row("   ", {"Service", "agreement"});
  builder.cz({"cz", "FLOW", "DD", "13", "13"});
  builder.cz({"cz", "FLOW", "P", "13", "13"});
  builder.row("             ", {"An", "E1", "line", "signals."});
  builder.empty();
  builder.cz({"cz", "FLOW", "DT", "3", "13"});
  builder.row("   ", {"Hunt", "groups"});
  builder.cz({"cz", "FLOW", "DD", "13", "13"});
  builder.cz({"cz", "FLOW", "DT", "3", "13"});
  builder.row("   ", {"Numbers"});
  builder.cz({"cz", "FLOW", "DD", "13", "13"});
  builder.row("             ", {"Two", "per", "line."});
  builder.cz({"cz", "OFF", "EDL", "0", "0"});
  auto extracted = run(builder.build(23), "Edition");
  require(extracted.topic.has_value(),
          "row-less items and definitions were rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &topic = *extracted.topic;
  require(count_kind(topic, ProseBlockKindIR::list_item) == 4,
          "expected four list items:\n" + topic_text(topic));
  require(count_kind(topic, ProseBlockKindIR::definition_entry) == 4,
          "expected four definition entries:\n" + topic_text(topic));
  for (const auto &block : topic.blocks)
    if (block.kind == ProseBlockKindIR::definition_entry &&
        contains(block_text(block), "Service"))
      require(block.body_blocks.size() == 1,
              "the paragraph after the empty DD is not its body");
  const auto document = lower(topic, "EDITION");
  if (!document) return;
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "FAX:") && contains(markdown, "signals.") &&
              contains(markdown, "Hunt"),
          "the row-less shapes did not render: " + markdown);

  // Row-less items as the last rows of the topic: SC31-8569-00 D.2.1.1 (DT
  // 19980126130303) ends `cz FLOW LI 7 7` + a row, two row-less
  // `cz FLOW LI 7 7` and `cz OFF ESL 0 0`, served `<li> <li> </ul>`.
  RecordBuilder trailing;
  trailing.heading("D.2.1.1", "Message");
  trailing.cz({"cz", "FLOW", "SL", "3", "3"});
  trailing.cz({"cz", "FLOW", "LI", "7", "7"});
  trailing.row("       ", {"IST1416I", "FAILED"});
  trailing.cz({"cz", "FLOW", "LI", "7", "7"});
  trailing.cz({"cz", "FLOW", "LI", "7", "7"});
  trailing.cz({"cz", "OFF", "ESL", "0", "0"});
  trailing.cz({"cz", "FLOW", "H4", "3", "3"});
  auto last = run(trailing.build(2825), "Message");
  require(last.topic.has_value(),
          "trailing row-less items were rejected: " + last.error);
  if (last.topic) {
    require(count_kind(*last.topic, ProseBlockKindIR::list_item) == 3,
            "expected three list items:\n" + topic_text(*last.topic));
    const auto lowered = lower(*last.topic, "D.2.1.1");
    if (lowered)
      require(contains(geist::detail::render_document_markdown(*lowered),
                       "IST1416I"),
              "the trailing items did not render");
  }
}

// A note BookServer cannot tell from a paragraph.  `CZ FLOW NT` and
// `CZ FLOW NOTE` match only the generic `CZ FLOW`: the rows flow as a
// paragraph, and a reader sees a note only where the first word is a label.
// SC09-2671-00 `H.0` (DT 19990222160950) opens its `cz FLOW NT` with
// `Note on Skipping Releases:`, served as plain flowing text; a note whose
// row is a lone `See also:` is the same.
void a_note_without_a_label_is_a_paragraph() {
  RecordBuilder builder;
  builder.heading("H.0", "Migration");
  builder.cz({"cz", "FLOW", "NT", "3", "10"});
  builder.row("   ", {"Note", "on", "Skipping", "Releases:", "If", "you",
                      "skip", "releases."});
  builder.empty();
  builder.cz({"cz", "OFF", "ENT", "0", "0"});
  builder.cz({"cz", "FLOW", "NOTE", "3", "3"});
  builder.row("   ", {"See", "also:"});
  builder.empty();
  builder.cz({"cz", "FLOW", "P", "3", "3"});
  builder.row("   ", {"End."});
  auto extracted = run(builder.build(900), "Migration");
  require(extracted.topic.has_value(),
          "notes without a label were rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 3 &&
              blocks[0].kind == ProseBlockKindIR::paragraph &&
              blocks[1].kind == ProseBlockKindIR::paragraph &&
              contains(block_text(blocks[0]), "Skipping") &&
              contains(block_text(blocks[1]), "See also:"),
          "the label-less notes are not paragraphs:\n" +
              topic_text(*extracted.topic));
}

// A list part inside an item list continues the item before it.
// `CZ FLOW LP` prints `<br>` and its rows wherever it stands: GC33-0790-04
// `3.49.61` (DT 19990426180546) stores `cz FLOW OL 3 3`, `cz FLOW LI 3 7`
// `   1.  change the specified routing module,`, `cz FLOW LP 3 3` `   or`
// and the second item, served as `<li> change ...`, `<br>   or`, `<li> ...`
// inside one `<ol>`: the numbering runs on.
void a_list_part_inside_an_item_list_continues_the_item() {
  RecordBuilder builder;
  builder.heading("3.49.61", "Routing");
  builder.cz({"cz", "FLOW", "OL", "3", "3"});
  builder.cz({"cz", "FLOW", "LI", "3", "7"});
  builder.row("   ", {"1.", "change", "the", "module,"});
  builder.cz({"cz", "FLOW", "LP", "3", "3"});
  builder.row("   ", {"or"});
  builder.cz({"cz", "FLOW", "LI", "3", "7"});
  builder.row("   ", {"2.", "associate", "the", "region."});
  builder.cz({"cz", "OFF", "EOL", "0", "0"});
  auto extracted = run(builder.build(3221), "Routing");
  require(extracted.topic.has_value(),
          "a cz FLOW LP inside an OL was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 3 && blocks[0].kind == ProseBlockKindIR::list_item &&
              blocks[1].kind == ProseBlockKindIR::paragraph &&
              blocks[1].parent_block == 0 &&
              blocks[2].kind == ProseBlockKindIR::list_item &&
              blocks[2].list_ordinal == blocks[0].list_ordinal,
          "the list part is not inside the first item:\n" +
              topic_text(*extracted.topic));
}

// A `cz BREAK` after a parameter term that owns no row prints only `<br>`.
// GC31-8368-00 `3.2.2` (DT 19970123022321) heads a parameter list with
// `cz FLOW PT 14 21` `Reason`, a row-less `cz BREAK 21`, and the next term
// `Code` with its body `Explanation`; hosted serves `Reason`, `<br>` and
// then `Code`.  The first term stands alone.
void a_rowless_break_after_a_parameter_term() {
  RecordBuilder builder;
  builder.heading("3.2.2", "Codes");
  builder.cz({"cz", "FLOW", "PARML", "14", "14"});
  builder.cz({"cz", "FLOW", "PT", "14", "21"});
  builder.row("              ", {"Reason"});
  builder.empty();
  builder.cz({"cz", "BREAK", "21"});
  builder.cz({"cz", "FLOW", "PT", "14", "21"});
  builder.row("              ", {"Code"});
  builder.cz({"cz", "BREAK", "21"});
  builder.row("                     ", {"Explanation"});
  builder.cz({"cz", "OFF", "EPARML", "11", "11"});
  auto extracted = run(builder.build(159), "Codes");
  require(extracted.topic.has_value(),
          "a row-less cz BREAK after PT was rejected: " + extracted.error);
  if (!extracted.topic) return;
  require(count_kind(*extracted.topic, ProseBlockKindIR::definition_entry) == 2,
          "expected two parameter entries:\n" + topic_text(*extracted.topic));
}

}  // namespace

int main() {
  a_rowless_break_after_a_parameter_term();
  a_list_part_inside_an_item_list_continues_the_item();
  a_note_without_a_label_is_a_paragraph();
  items_and_definitions_without_rows();
  a_list_without_items_holds_its_paragraphs();
  a_parameter_term_with_rows_at_its_indent();
  terms_without_a_separate_body();
  a_break_inside_a_region_is_a_blank_row();
  a_flow_inside_a_region_ends_it();
  a_flow_inside_an_unclosed_region_declines();
  a_region_closed_by_another_regions_closer();
  an_anchor_before_a_parameter_body_stays_at_its_entry();
  a_parameter_term_that_is_a_syntax_diagram();
  verbatim_rows_keep_highlighting_and_links();
  nested_openers_and_foreign_closers();
  list_parts_outside_definition_lists();
  a_trailing_rowless_parameter_term_draws_nothing();
  an_empty_region_draws_nothing();
  rows_after_an_empty_region_are_verbatim();
  return 0;
}
