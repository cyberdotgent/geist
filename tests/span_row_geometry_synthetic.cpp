// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A span's extent is measured against the row's own display cells (issue #67,
// the span-geometry classes).
//
// Two facts of the format meet here, and both are read off the record's
// carried display-line framing rather than off the flattened decoded string.
//
//  * A control's opcode is the first token of its own display line.  A word
//    that stands *after* the line's origin run is display text at a column,
//    however control-shaped it is spelled.  SH12-565 4.7.5.3 record 403 line
//    33 is `   SRVMODE` under `cfont 3 7 P`; reading `SRVMODE` as a control
//    cost the row its first word, left the row's cells three columns short of
//    the operand and sank the topic.  Hosted (SH12-5657-04 DT 19941206115523)
//    serves `SRVMODE is a common prefix ...` as plain display text and puts no
//    anchor on it.
//
//  * The cells in front of a row's text -- origin run, bullet glyph, change
//    bar, ordinal label -- are structure the reader draws itself.  A font span
//    confined to them decorates that structure and has nowhere to land once
//    the row is lowered into a list item.  SC09-138 8.1.10.4 record 1359
//    stores `cfont 3 1 X 7 4 X 12 7 X` over `   °   char *dsname` while the
//    sibling row above it stores only `cfont 7 4 X 12 7 X` over the identical
//    shape.
//
// Everything here is synthetic: the tests build whole topics'
// `DecodedLogicalRecordSource` values by hand and open no book.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "geist/detail/lowering/font_style_inlines.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "span_row_geometry_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

// A token glued to the word before it: the decoder's attach prefix, which is
// how a row spells `*dsname` as `*` + `dsname` with no column between them.
TokenWords glued(const std::string &text) {
  TokenWords token{1};
  token.insert(token.end(), text.begin(), text.end());
  return token;
}

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;

  // One display line: a length byte covering `content`, whose tokens are two
  // bytes each.  The length byte's dictionary spelling is the row sentinel.
  void line(std::vector<TokenWords> content) {
    line_with_prefix(TokenWords{0x25BA}, std::move(content));
  }

  void line_with_prefix(TokenWords prefix, std::vector<TokenWords> content) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(std::move(prefix));
    for (auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(std::move(token));
    }
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
    // The production decode order (logical.cpp).
    geist::detail::demote_display_line_owned_controls(record);
    return std::move(record);
  }
};

void metadata(RecordBuilder &builder, const std::string &topic,
              const std::vector<std::string> &title) {
  builder.line({words("sh" + topic)});
  builder.line({words("ctopicn"), words("7")});
  builder.line({words("cparent"), words("1.0")});
  builder.line({words("cforwardlevel"), words("1.2")});
  builder.line({words("cbacklevel"), words("1.0")});
  builder.line({words("csummary"), words("9"), words("0"), words("9")});
  builder.line({words("chdlevel"), words(":H2")});
  builder.line({words("csourcefn"), words("DVGR1A05")});
  std::vector<TokenWords> heading{words("ST")};
  for (const auto &word : title) heading.push_back(words(word));
  builder.line(std::move(heading));
}

std::optional<geist::detail::ProseTopicIR> extract(
    const DecodedLogicalRecordSource &record, const std::string &title,
    std::string &error) {
  const std::vector<DecodedLogicalRecordSource> sources{record};
  const auto layout = geist::detail::extract_layout_ir(sources);
  const auto ownership =
      geist::detail::build_verified_ownership_ir(sources, layout, &error);
  if (!ownership) {
    require(false, "the synthetic topic's ownership is not verifiable: " +
                       error);
    return std::nullopt;
  }
  return geist::detail::extract_prose_topic_ir(sources, layout, *ownership,
                                               title, nullptr, &error);
}

bool verify(const DecodedLogicalRecordSource &record, const std::string &title,
            const geist::detail::ProseTopicIR &topic, std::string &error) {
  const std::vector<DecodedLogicalRecordSource> sources{record};
  const auto layout = geist::detail::extract_layout_ir(sources);
  const auto ownership =
      geist::detail::build_verified_ownership_ir(sources, layout, &error);
  return ownership && geist::detail::verify_prose_topic_ir(
                          sources, layout, *ownership, title, nullptr, topic,
                          &error);
}

std::string body_text(const geist::detail::ProseTopicIR &topic) {
  std::string text;
  for (const auto &block : topic.blocks)
    for (const auto &fragment : block.inlines) text += fragment.text;
  return text;
}

bool carries_opcode(const DecodedLogicalRecordSource &record,
                    const std::string &opcode) {
  return std::any_of(record.control_segments.begin(),
                     record.control_segments.end(),
                     [&](const auto &segment) {
                       return geist::detail::ascii_lower(segment.opcode) ==
                              geist::detail::ascii_lower(opcode);
                     });
}

// -------------------------------------------------------------------------
// A control-shaped word after the origin run is display text.
// -------------------------------------------------------------------------

DecodedLogicalRecordSource control_shaped_word_record() {
  RecordBuilder builder;
  metadata(builder, "1.1", {"Server", "Running", "Mode"});
  // `cfont 3 7 P` addresses columns 3..9 of the row below it, which is where
  // that row's own cells put `SRVMODE`.
  builder.line({words("cfont"), words("3"), words("7"), words("P")});
  builder.line({words("   "), words("SRVMODE")});
  builder.line({words("   "), words("Alpha"), words("beta"), words("gamma")});
  return builder.build(31);
}

void a_control_shaped_word_after_the_origin_run_is_display_text() {
  const auto record = control_shaped_word_record();
  require(record.ir.display_lines_parse,
          "the synthetic topic's display lines did not parse");

  // The premise: the flattened splitter really does read `SRVMODE` as an
  // opcode, so the fixture reproduces the fault rather than passing by
  // accident.  What pins the fix is that the framing withdraws it again.
  const auto raw = geist::detail::decode_control_segments(
      record.logical_record, record.assembled, record.encoded_tokens,
      record.ir.display_lines);
  const auto raw_carries = std::any_of(
      raw.begin(), raw.end(), [](const auto &segment) {
        return segment.opcode == "SRVMODE";
      });
  require(raw_carries,
          "the fixture no longer reproduces the split this pins: the "
          "splitter did not read `SRVMODE` as a control opcode at all");
  require(!carries_opcode(record, "SRVMODE"),
          "`SRVMODE` stands after its display line's origin run and is "
          "display text, but the framing left it as a control opcode");

  std::string error;
  const auto prose = extract(record, "Server Running Mode", error);
  require(prose.has_value(),
          "a topic whose row opens with a control-shaped word was rejected: " +
              error);
  if (!prose) return;

  const auto body = body_text(*prose);
  require(body.find("SRVMODE") != std::string::npos,
          "the row's first word was lost; the body carries '" + body + "'");
  require(prose->anchors.empty(),
          "a display word was still claimed as an anchor: the topic carries " +
              std::to_string(prose->anchors.size()) + " anchor(s)");

  // The `cfont` operand names the columns the row's own cells put `SRVMODE`
  // in, so the word comes back styled rather than plain.
  bool styled = false;
  for (const auto &block : prose->blocks)
    for (const auto &fragment : block.inlines)
      if (fragment.text.find("SRVMODE") != std::string::npos &&
          fragment.style != geist::detail::FontStyleIR::unknown)
        styled = true;
  require(styled,
          "the `cfont 3 7 P` triple did not land on `SRVMODE`; the row's "
          "columns and the operand's columns still disagree");
}

// A control that does open its own display line is untouched: the rule reads
// the framing, and the framing says this word is the line's first token.
void a_control_opening_its_display_line_stays_a_control() {
  RecordBuilder builder;
  metadata(builder, "1.2", {"Anchored", "Topic"});
  builder.line({words("SRHDRABC")});
  builder.line({words("   "), words("Alpha"), words("beta")});
  const auto record = builder.build(32);
  require(record.ir.display_lines_parse,
          "the anchored topic's display lines did not parse");
  require(carries_opcode(record, "SRHDRABC"),
          "an opcode standing immediately after its line's length byte is a "
          "control and was withdrawn");

  std::string error;
  const auto prose = extract(record, "Anchored Topic", error);
  require(prose.has_value(), "the anchored topic was rejected: " + error);
  if (!prose) return;
  require(prose->anchors.size() == 1,
          "the topic's own `SRHDR` anchor was lost");
  require(body_text(*prose).find("SRHDRABC") == std::string::npos,
          "an anchor control was rendered as display text");
}

// -------------------------------------------------------------------------
// A font span confined to the row's structural cells.
// -------------------------------------------------------------------------

// `   °    char *name` -- three origin columns, the bullet at column 3, the
// gap that follows it, `char` at column 8 and `*name` at column 13.  Those are
// the columns the row's own cells put the words in, which is what a `CFONT`
// operand names.
void bullet_row(RecordBuilder &builder, const std::string &name,
                bool style_the_bullet) {
  std::vector<TokenWords> font{words("cfont")};
  if (style_the_bullet) {
    font.push_back(words("3"));
    font.push_back(words("1"));
    font.push_back(words("X"));
  }
  for (const auto *triple : {"8", "4", "X", "13", "6", "X"})
    font.push_back(words(triple));
  builder.line(std::move(font));
  builder.line({words("   "), TokenWords{0x2666}, words("   "), words("char"),
                words("*"), glued(name)});
}

void a_font_span_on_the_bullet_is_dropped_and_the_row_kept() {
  RecordBuilder builder;
  metadata(builder, "1.3", {"Fields"});
  bullet_row(builder, "aaaaa", false);
  bullet_row(builder, "bbbbb", true);
  const auto record = builder.build(33);
  require(record.ir.display_lines_parse,
          "the bullet topic's display lines did not parse");

  std::string error;
  const auto prose = extract(record, "Fields", error);
  require(prose.has_value(),
          "a list row whose `cfont` also styles its bullet glyph was "
          "rejected: " + error);
  if (!prose) return;

  const auto body = body_text(*prose);
  require(body.find("*aaaaa") != std::string::npos &&
              body.find("*bbbbb") != std::string::npos,
          "a bullet row lost its words; the body carries '" + body + "'");
  require(prose->blocks.size() == 2,
          "the two bullet rows did not lower into two blocks");
  // Both word triples of both rows land on the columns the rows' own cells
  // put the words in; only the bullet triple is gone.  The two rows carry the
  // same style, so the words a row styles may reach the block as one inline
  // or as several; what is pinned is that the row's words are styled and its
  // bullet is not there at all.
  for (std::size_t index = 0; index < prose->blocks.size(); ++index) {
    std::string styled;
    for (const auto &fragment : prose->blocks[index].inlines)
      if (fragment.style != geist::detail::FontStyleIR::unknown)
        styled += fragment.text;
    const std::string name = index == 0 ? "*aaaaa" : "*bbbbb";
    require(styled.find("char") != std::string::npos &&
                styled.find(name) != std::string::npos,
            "the `cfont` word triples of bullet row " + std::to_string(index) +
                " did not land on its words; the styled text is '" + styled +
                "'");
  }
  for (const auto &block : prose->blocks)
    require(block.kind == geist::detail::ProseBlockKindIR::list_item,
            "a bullet row did not lower into a list item");
  // The bullet glyph itself is never a word of the block: the reader draws
  // the list marker, so a style on it has nowhere to go and is dropped.
  require(body.find('\xE2') == std::string::npos && body.find('?') ==
                                                        std::string::npos,
          "the bullet glyph leaked into the block text: '" + body + "'");
}

// Fail closed on anything wider: a span that starts in the structure and
// reaches into the text means the row's columns and the operand's columns
// disagree, which is exactly what this check exists to catch.
void a_span_reaching_from_the_bullet_into_the_text_still_fails() {
  RecordBuilder builder;
  metadata(builder, "1.4", {"Fields"});
  // `cfont 3 10 X`: from the bullet at column 3 through `char` at 8..11.
  builder.line({words("cfont"), words("3"), words("10"), words("X")});
  builder.line({words("   "), TokenWords{0x2666}, words("   "), words("char"),
                words("*"), glued("ccccc")});
  const auto record = builder.build(34);
  std::string error;
  const auto prose = extract(record, "Fields", error);
  require(!prose.has_value(),
          "a span running from the row's bullet into its text was admitted; "
          "the row's columns are not proven");
  require(error.find("span covers the bullet") != std::string::npos,
          "the topic was rejected for the wrong reason: " + error);
}

// Each font instruction has the coordinate origin of its own following
// display row.  Consecutive instructions must not leave the first row open
// and apply both sets of triples to the second row.
void consecutive_font_instructions_bind_to_distinct_native_rows() {
  RecordBuilder builder;
  metadata(builder, "1.5", {"Commands"});
  builder.line({words("cfont"), words("9"), words("5"), words("2"),
                words("25"), words("4"), words("2")});
  builder.line({words("         "), words("SUPVR"), words("           "),
                words("FOPT")});
  builder.line({words("cfont"), words("9"), words("4"), words("E"),
                words("25"), words("5"), words("E")});
  builder.line({words("         "), words("MODE"), words("            "),
                words("IODEV")});
  const auto record = builder.build(35);

  std::string error;
  const auto prose = extract(record, "Commands", error);
  require(prose.has_value(),
          "consecutive row-local font instructions were combined: " + error);
  if (!prose) return;
  const auto body = body_text(*prose);
  require(body.find("SUPVR") != std::string::npos &&
              body.find("FOPT") != std::string::npos &&
              body.find("MODE") != std::string::npos &&
              body.find("IODEV") != std::string::npos,
          "a consecutively styled row lost text: '" + body + "'");
  for (const auto &block : prose->blocks)
    for (const auto &inline_node : block.inlines) {
      if (inline_node.text.find("SUPVR") != std::string::npos ||
          inline_node.text.find("FOPT") != std::string::npos)
        require(inline_node.style == geist::detail::FontStyleIR::highlight_2,
                "the first row received the following row's font");
      if (inline_node.text.find("MODE") != std::string::npos ||
          inline_node.text.find("IODEV") != std::string::npos)
        require(geist::detail::font_style_is_example(inline_node.style),
                "the second row retained the preceding row's font");
    }
}

// A selector owns the destination while CFONT independently owns the label's
// presentation.  The two controls make compatible claims over the same
// source cells; retaining the link must not erase the font semantics.
void a_font_span_inside_a_selector_preserves_both_semantics() {
  RecordBuilder builder;
  metadata(builder, "1.6", {"Reference"});
  builder.line({words("cselect"), words("3"), words("5"), words("HDRNEXT")});
  builder.line({words("cfont"), words("3"), words("5"), words("2")});
  builder.line({words("   "), words("Next")});
  const auto record = builder.build(36);

  std::string error;
  const auto prose = extract(record, "Reference", error);
  require(prose.has_value(),
          "a styled selector label was rejected: " + error);
  if (!prose) return;
  const auto found = std::find_if(
      prose->blocks.front().inlines.begin(), prose->blocks.front().inlines.end(),
      [](const auto &inline_node) {
        return inline_node.kind ==
                   geist::detail::ProseInlineKindIR::cross_reference &&
               inline_node.text == "Next";
      });
  require(found != prose->blocks.front().inlines.end(),
          "the styled selector did not produce its link label");
  if (found != prose->blocks.front().inlines.end())
    require(found->style == geist::detail::FontStyleIR::highlight_2,
            "the selector kept its destination but discarded the label's "
            "CFONT style");
}

// Columns count display cells, while partial-token provenance counts bytes in
// the UTF-8 projection.  Styling the final non-ASCII cell of a linked word
// must therefore split at byte 3 and retain the two-byte encoded character.
void styled_selector_columns_map_to_utf8_source_offsets() {
  RecordBuilder builder;
  metadata(builder, "1.7", {"UTF-8", "Reference"});
  builder.line({words("cselect"), words("3"), words("4"), words("HDRCAFE")});
  builder.line({words("cfont"), words("6"), words("1"), words("2")});
  builder.line({words("   "), TokenWords{'C', 'a', 'f', 0x00e9}});
  const auto record = builder.build(37);

  std::string error;
  const auto prose = extract(record, "UTF-8 Reference", error);
  require(prose.has_value(),
          "UTF-8 styled selector geometry was rejected: " + error);
  if (!prose) return;
  const auto found = std::find_if(
      prose->blocks.front().inlines.begin(), prose->blocks.front().inlines.end(),
      [](const auto &inline_node) {
        return inline_node.kind ==
                   geist::detail::ProseInlineKindIR::cross_reference &&
               inline_node.style == geist::detail::FontStyleIR::highlight_2;
      });
  require(found != prose->blocks.front().inlines.end(),
          "the linked UTF-8 cell lost its font style");
  if (found == prose->blocks.front().inlines.end()) return;
  require(found->text == "\xC3\xA9",
          "the one-cell span did not select the complete UTF-8 character");
  require(found->slices.size() == 1 &&
              found->slices.front().character_begin == 3 &&
              found->slices.front().character_end == 5,
          "display-cell geometry was confused with UTF-8 byte offsets");
}

// Some bibliography labels are claimed by one LNK selector per edition,
// with a footnote selector nested over only part of the same cells.  These
// are alternatives for one displayed label, not repeated text and not a
// reason to discard all but one destination.  This also exercises the split
// between cell coordinates and UTF-8 byte provenance at the nested edge.
void overlapping_selectors_preserve_every_target_once() {
  RecordBuilder builder;
  metadata(builder, "1.7.1", {"Alternative", "References"});
  builder.line({words("cselect"), words("3"), words("4"), words("LNK"),
                words("<BOOK>"), words("<>"), words("<>"),
                words("<SC31-6105>"), words("<>"), words("<CNMYQ00>")});
  builder.line({words("cselect"), words("3"), words("4"), words("LNK"),
                words("<BOOK>"), words("<>"), words("<>"),
                words("<SC31-6006>"), words("<>"), words("<CNMYY00>")});
  builder.line(
      {words("cselect"), words("6"), words("1"), words("FTNMERBIB")});
  builder.line({words("cfont"), words("6"), words("1"), words("2")});
  builder.line({words("   "), TokenWords{'C', 'a', 'f', 0x00e9}});
  const auto record = builder.build(42);

  std::string error;
  const auto prose = extract(record, "Alternative References", error);
  require(prose.has_value(),
          "overlapping selector claims were rejected: " + error);
  if (!prose) return;
  require(verify(record, "Alternative References", *prose, error),
          "the extracted multi-target source IR did not verify: " + error);
  require(body_text(*prose) == "Caf\xC3\xA9",
          "overlapping selectors duplicated or lost their visible label: '" +
              body_text(*prose) + "'");

  const auto &inlines = prose->blocks.front().inlines;
  require(inlines.size() == 2,
          "the nested selector/font boundaries did not produce two label "
          "runs");
  if (inlines.size() != 2) return;
  const auto &outer = inlines[0];
  const auto &nested = inlines[1];
  require(outer.text == "Caf" && outer.targets.size() == 2,
          "the outer label run does not retain its two edition targets");
  require(nested.text == "\xC3\xA9" && nested.targets.size() == 3,
          "the nested label run does not retain both editions and its "
          "footnote target");
  if (outer.targets.size() == 2) {
    require(outer.targets[0].kind ==
                geist::detail::CrossReferenceTargetKindIR::book_contents &&
                outer.targets[0].value == "CNMYQ00" &&
                outer.targets[1].kind ==
                    geist::detail::CrossReferenceTargetKindIR::book_contents &&
                outer.targets[1].value == "CNMYY00",
            "distinct edition destinations were reordered or collapsed");
    require(outer.targets[0].alternatives ==
                std::vector<std::string>({"BOOK", "", "", "SC31-6105",
                                          "", "CNMYQ00"}) &&
                outer.targets[1].alternatives ==
                    std::vector<std::string>({"BOOK", "", "",
                                              "SC31-6006", "", "CNMYY00"}),
            "an LNK alternative tuple was not retained verbatim");
    require(outer.targets[0].source.token_begin <
                outer.targets[0].source.token_end &&
                outer.targets[1].source.token_begin <
                    outer.targets[1].source.token_end &&
                outer.targets[0].source.token_begin !=
                    outer.targets[1].source.token_begin,
            "distinct selector controls lost their individual provenance");
  }
  if (nested.targets.size() == 3) {
    require(nested.targets[2].value == "FTNMERBIB" &&
                nested.targets[2].alternatives.empty(),
            "the nested in-book selector was not retained after the LNK "
            "alternatives");
    require(nested.style == geist::detail::FontStyleIR::highlight_2,
            "the nested run lost its independent font claim");
    require(nested.slices.size() == 1 &&
                nested.slices.front().character_begin == 3 &&
                nested.slices.front().character_end == 5,
            "the nested UTF-8 cell does not own its exact byte range");
  }
}

// Numeric display words can repeat the preceding CFONT operand byte for byte.
// Native line framing, not decoded-text equality, bounds operand ownership.
void repeated_numeric_words_after_a_font_instruction_stay_display_text() {
  RecordBuilder builder;
  metadata(builder, "1.8", {"Numeric", "Row"});
  builder.line({words("cfont"), words("7"), words("1"), words("2"),
                words("9"), words("1"), words("2"), words("11"),
                words("1"), words("2"), words("13"), words("1"),
                words("2")});
  builder.line_with_prefix(
      words("  "), {words("       "), words("3"), words("7"), words("2"),
                     words("2"), words("and"), words("provide"),
                     words("data")});
  const auto record = builder.build(38);

  std::string error;
  const auto prose = extract(record, "Numeric Row", error);
  require(prose.has_value(),
          "row words duplicating CFONT operands were rejected: " + error);
  if (!prose) return;
  require(body_text(*prose).find("3 7 2 2 and provide data") !=
              std::string::npos,
          "numeric display words were reassigned to the preceding operand: '" +
              body_text(*prose) + "'");
}

// Exact CFONT coverage distinguishes a drawn leading glyph inside the native
// display line from the line's preceding physical control slot.
void a_styled_leading_bar_is_display_text() {
  RecordBuilder builder;
  metadata(builder, "1.9", {"Styled", "Bar"});
  builder.line({words("cfont"), words("3"), words("1"), words("2")});
  builder.line({words("   "), words("|"), words("        "), words("An"),
                words("OR"), words("sign")});
  // The production ambiguity exists only for the compact one-byte bar: that
  // is also the encoding used by revision-margin bars.  RecordBuilder uses
  // width-two dictionary tokens for ordinary line content, so pin this cell
  // to the source width carried by SC26-457 1.2 and IBMMMSTR 1.5.733.
  const auto bar = std::find(builder.record.tokens.begin(),
                             builder.record.tokens.end(), words("|"));
  require(bar != builder.record.tokens.end(), "the bar token is absent");
  if (bar != builder.record.tokens.end())
    builder.record.encoded_tokens[static_cast<std::size_t>(
        std::distance(builder.record.tokens.begin(), bar))]
        .width = 1;
  const auto record = builder.build(39);

  std::string error;
  const auto prose = extract(record, "Styled Bar", error);
  require(prose.has_value(),
          "a CFONT-covered leading bar was rejected: " + error);
  if (!prose) return;
  bool styled_bar = false;
  for (const auto &block : prose->blocks)
    for (const auto &inline_node : block.inlines)
      if (inline_node.text.find('|') != std::string::npos &&
          inline_node.style == geist::detail::FontStyleIR::highlight_2)
        styled_bar = true;
  require(styled_bar, "the leading bar was retained without its CFONT claim");
  require(body_text(*prose).find("| An OR sign") != std::string::npos,
          "the leading bar lost the following source text: '" +
              body_text(*prose) + "'");
}

// A width-2 unmapped dictionary token at the native row end is displayed
// text, which the reader prints as its substitution byte 0xFF (`ÿ`):
// EDCGV1R2 4.1 (DT 19891031000000) `<samp>\xff</samp>`.
void styled_terminal_punctuation_is_display_text() {
  RecordBuilder builder;
  metadata(builder, "1.10", {"Styled", "Punctuation"});
  builder.line({words("cfont"), words("3"), words("8"), words("E"),
                words("11"), words("1"), words("E")});
  builder.line(
      {words("   "), words("Question"), TokenWords{1, 0xFFFF}});
  const auto record = builder.build(40);

  std::string error;
  const auto prose = extract(record, "Styled Punctuation", error);
  require(prose.has_value(),
          "CFONT-covered terminal punctuation was rejected: " + error);
  if (!prose) return;
  require(body_text(*prose).find("Question\xC3\xBF") != std::string::npos,
          "the terminal punctuation cell was suppressed: '" +
              body_text(*prose) + "'");
}

// Punctuation at the end of a native display row stays in that row when its
// bound span distinguishes it from an inferred marker slot.
void a_styled_terminal_marker_finishes_the_open_row() {
  RecordBuilder builder;
  metadata(builder, "1.11", {"Styled", "Terminal", "Marker"});
  builder.line({words("cfont"), words("3"), words("5"), words("E"),
                words("9"), words("1"), words("E")});
  builder.line({words("   "), words("LTORG"), words(",")});
  const auto record = builder.build(41);

  std::string error;
  const auto prose = extract(record, "Styled Terminal Marker", error);
  require(prose.has_value(),
          "a CFONT-covered terminal marker was rejected: " + error);
  if (!prose) return;
  require(body_text(*prose).find("LTORG ,") != std::string::npos,
          "the terminal marker was suppressed: '" + body_text(*prose) +
              "'");
}

// CFONT code A names the book's APL font/code page.  It changes the graphic
// character represented by a stored byte rather than merely decorating the
// CP037 projection: byte 0x8A is U+00AB there, but U+2191 in IBM code page
// 293.  An otherwise identical unstyled row pins that the remapping is
// span-local, and the following XMP row pins the pointer's native geometry.
void an_apl_font_span_decodes_its_own_character_cells() {
  const auto stored_arrow = geist::detail::EbcdicCodec::cp037().decode_word(0x8A);
  require(stored_arrow == 0x00AB,
          "the fixture's stored 0x8A byte no longer has the CP037 projection "
          "this regression exercises");
  RecordBuilder builder;
  metadata(builder, "1.12", {"APL", "Arrow"});
  builder.line({words("   "), TokenWords{stored_arrow}});
  builder.line({words("cfont"), words("3"), words("1"), words("A")});
  // RecordBuilder starts at the production decoder's TokenWords boundary, so
  // derive both cells from the actual CP037 source byte instead of spelling
  // the projected Unicode scalar directly.
  builder.line({words("   "), TokenWords{stored_arrow}});
  builder.line({words("cfont"), words("3"), words("1"), words("E")});
  builder.line({words("   "), words("T")});
  const auto record = builder.build(42);

  std::string error;
  const auto prose = extract(record, "APL Arrow", error);
  require(prose.has_value(),
          "an APL-code-page font span was rejected: " + error);
  if (!prose) return;
  require(verify(record, "APL Arrow", *prose, error),
          "the APL span's source-byte claims are not conserved: " + error);

  const auto body = body_text(*prose);
  require(body.find("\xE2\x86\x91") != std::string::npos,
          "the APL byte was not decoded as an upwards arrow: '" + body +
              "'");
  require(body.find("\xC2\xAB") != std::string::npos,
          "the same CP037 character outside the APL span was remapped: '" +
              body + "'");
  bool positioned = false;
  for (const auto &block : prose->blocks)
    if (block.kind == geist::detail::ProseBlockKindIR::preformatted)
      for (const auto &line : block.preformatted_lines)
        if (line == "   \xE2\x86\x91") positioned = true;
  require(positioned,
          "the APL diagnostic arrow lost its native display column");
}

// -------------------------------------------------------------------------
// A wide in-row gap stored as two space runs (issue #91).
// -------------------------------------------------------------------------

// One space-run token spells at most 63 cells, so a comment-box row whose
// gap is wider stores it as a 63-cell run followed by a short one.  SC33-033
// A.3.2 record 872 line 18 is `     C*` + 63 + 3 + `*  02100000` under
// `cfont 5 2 E 74 1 E 77 8 E`: one 85-column display line, served by hosted
// (DT 19930422134757) as one row with the `*` styled at column 74.
std::vector<TokenWords> comment_box_row(const std::string &sequence) {
  return {words("     "), words("C*"), words(std::string(63, ' ')),
          words("   "), words("*"), words(" "), words(sequence)};
}

DecodedLogicalRecordSource comment_box_record(bool spanned) {
  RecordBuilder builder;
  metadata(builder, "1.13", {"Comment", "Box"});
  builder.line({words("cfont"), words("5"), words("70"), words("E"),
                words("77"), words("8"), words("E")});
  builder.line({words("     "), words("C" + std::string(69, '*')),
                words(" "), words("00700000")});
  if (spanned) {
    builder.line({words("cfont"), words("5"), words("2"), words("E"),
                  words("74"), words("1"), words("E"), words("77"),
                  words("8"), words("E")});
  } else {
    builder.line({words("cfont"), words("5"), words("2"), words("E")});
  }
  builder.line(comment_box_row("02100000"));
  return builder.build(43);
}

void a_two_run_gap_inside_a_spanned_row_keeps_the_row_whole() {
  const auto record = comment_box_record(true);
  require(record.ir.display_lines_parse,
          "the comment-box topic's display lines did not parse");
  // The premise: the stored display line really is one 85-column row with
  // the closing `*` at the span's column 74.
  std::string native;
  if (const auto *lines = geist::detail::record_display_lines(record))
    for (const auto &line : *lines) {
      const auto text = geist::detail::display_line_text(record, line);
      if (text.find("02100000") != std::string::npos) native = text;
    }
  require(native.size() == 85 && native[74] == '*',
          "the fixture's display line is not the 85-column comment-box row: '" +
              native + "'");

  std::string error;
  const auto prose = extract(record, "Comment Box", error);
  require(prose.has_value(),
          "a comment-box row whose gap is stored as two runs was rejected: " +
              error);
  if (!prose) return;
  require(verify(record, "Comment Box", *prose, error),
          "the comment-box row's source claims are not conserved: " + error);

  bool whole = false;
  for (const auto &block : prose->blocks)
    if (block.kind == geist::detail::ProseBlockKindIR::preformatted)
      for (const auto &line : block.preformatted_lines)
        if (line == native) whole = true;
  require(whole, "the comment-box row was not kept as its native display "
                 "line; the body carries '" + body_text(*prose) + "'");
  // The closing `*` keeps its example font: either as its own styled cell
  // or inside the styled run that carries it with the sequence number.
  bool styled_star = false;
  const auto tail = native.size() > 74 ? native.substr(74) : std::string();
  for (const auto &block : prose->blocks)
    for (const auto &fragment : block.inlines)
      if (geist::detail::font_style_is_example(fragment.style) &&
          (fragment.text == "*" ||
           (!tail.empty() && fragment.text.find(tail) != std::string::npos)))
        styled_star = true;
  require(styled_star,
          "the `cfont 74 1 E` triple did not land on the closing `*`");
}

// Without a span past the cells already written, nothing corroborates that
// the pair stands inside the row: the pair keeps its fill/origin reading and
// the operand that does exist still lands on its own cells.
void a_two_run_gap_without_a_reaching_span_keeps_the_pair_reading() {
  const auto record = comment_box_record(false);
  std::string error;
  const auto prose = extract(record, "Comment Box", error);
  if (!prose) return;  // declining is also acceptable; admitting a wider row
                       // on no evidence is not.
  for (const auto &block : prose->blocks)
    for (const auto &line : block.preformatted_lines)
      require(line.find("C*") == std::string::npos ||
                  line.find("02100000") == std::string::npos,
              "an unspanned two-run gap was held open as one row: '" + line +
                  "'");
}

// -------------------------------------------------------------------------
// A styled `,` / `?` that ends its row in front of the next control (#91).
// -------------------------------------------------------------------------

// SC24-546 D.0 record 1118 line 7 is `              LTORG ,` under
// `cfont 14 5 E 20 1 E`, and the very next display line is the following
// row's `cfont`.  The `,` is a two-byte dictionary word that no control
// segment claims, spelled like decoder punctuation; hosted (DT
// 19940323131240) serves `<samp>LTORG</samp> <samp>,</samp>`.
// SC09-1714-00 1.4 record 221 line 5 stores the same shape with a one-byte
// `,` (token 78, value 29, inside the line) after `source_binding`, under
// the row's own `cfont ... 47 1 P`; hosted (DT 19940917171754) serves
// `<var>source_binding</var> <kbd>,</kbd>`.  `one_byte` stores the comma that
// way.
DecodedLogicalRecordSource styled_terminal_record(bool styled,
                                                  bool one_byte = false) {
  RecordBuilder builder;
  metadata(builder, "1.14", {"Literal", "Pool"});
  builder.line({words("cfont"), words("3"), words("5"), words("E"),
                words("9"), words("1"), words("E")});
  builder.line({words("   "), words("SPACE"), words("3")});
  if (styled)
    builder.line({words("cfont"), words("3"), words("5"), words("E"),
                  words("9"), words("1"), words("E")});
  else
    builder.line({words("cfont"), words("3"), words("5"), words("E")});
  builder.line({words("   "), words("LTORG"), words(",")});
  if (one_byte) {
    auto &encoded = builder.record.encoded_tokens;
    encoded.back() = {29, 1};
    encoded[encoded.size() - 4].value -= 1;
  }
  // The next line's length byte is the byte the book's dictionary spells as
  // a seven-cell space run (record 1118 token 63, value 23).
  builder.line_with_prefix(words("       "),
                           {words("cfont"), words("3"), words("5"),
                            words("E")});
  builder.line({words("   "), words("TITLE")});
  return builder.build(44);
}

void a_styled_row_final_comma_before_a_control_is_display_text(
    bool one_byte = false) {
  const auto record = styled_terminal_record(true, one_byte);
  std::string error;
  const auto prose = extract(record, "Literal Pool", error);
  require(prose.has_value(),
          "a row ending in a CFONT-covered `,` in front of the next control "
          "was rejected: " + error);
  if (!prose) return;
  require(verify(record, "Literal Pool", *prose, error),
          "the styled terminal `,` is not conserved: " + error);
  std::string rows;
  for (const auto &block : prose->blocks)
    for (const auto &line : block.preformatted_lines) rows += line + "\n";
  require((rows + body_text(*prose)).find("LTORG ,") != std::string::npos,
          "the styled terminal `,` was dropped: '" + body_text(*prose) + "'");
}

// The same `,` under no span is not corroborated as a drawn cell: it stays
// decoder padding, and the row is not widened on its account.
void an_unstyled_row_final_comma_before_a_control_stays_padding(
    bool one_byte = false) {
  const auto record = styled_terminal_record(false, one_byte);
  std::string error;
  const auto prose = extract(record, "Literal Pool", error);
  if (!prose) return;
  std::string rows;
  for (const auto &block : prose->blocks)
    for (const auto &line : block.preformatted_lines) rows += line + "\n";
  require((rows + body_text(*prose)).find("LTORG ,") == std::string::npos,
          "an unstyled terminal `,` was admitted as display text: '" +
              body_text(*prose) + "'");
}

// -------------------------------------------------------------------------
// A selector label stored glued to the word in front of it (#91).
// -------------------------------------------------------------------------

// SC24-5527-02 4.2.3 record 399 stores `inTable` as one two-byte dictionary
// word under `cselect 57 9 TBLXREXBL`, and hosted (DT 19921218151459) serves
// `in<a ...>Table 4-8</a>`: the label begins inside the stored word and
// crosses the blank between its own words.  The row's `cfont 3 2 2` lands on
// a whole word and proves the row's columns.
//
// Row cells: origin 0-2, `Enter` 3-7, `the` 9-11, `list` 13-16, `inTable`
// 18-24 (`Table` at 20), `4-8` 26-28, `now` 30-32.
enum class GluedFont { none, corroborating, torn };

DecodedLogicalRecordSource glued_end_record(const std::string &select_begin,
                                            bool corroborated, bool one_token);

DecodedLogicalRecordSource glued_label_record(const std::string &select_length,
                                              GluedFont font, bool one_token) {
  RecordBuilder builder;
  metadata(builder, "1.15", {"Build", "List"});
  builder.line({words("cselect"), words("20"), words(select_length),
                words("TBLXREXBL")});
  if (font == GluedFont::corroborating)
    builder.line({words("cfont"), words("3"), words("5"), words("2")});
  else if (font == GluedFont::torn)
    builder.line({words("cfont"), words("3"), words("5"), words("2"),
                  words("20"), words("9"), words("1")});
  std::vector<TokenWords> row{words("   "), words("Enter"), words("the"),
                              words("list")};
  if (one_token) {
    row.push_back(words("inTable"));
  } else {
    row.push_back(words("in"));
    row.push_back(glued("Table"));
  }
  row.push_back(words("4-8"));
  row.push_back(words("now"));
  builder.line(std::move(row));
  return builder.build(45);
}

void a_selector_label_glued_to_a_word_is_a_link() {
  const auto record = glued_label_record("9", GluedFont::corroborating, true);
  std::string error;
  const auto prose = extract(record, "Build List", error);
  require(prose.has_value(),
          "a selector label glued to the word before it was rejected: " +
              error);
  if (!prose) return;
  require(verify(record, "Build List", *prose, error),
          "the glued selector label is not conserved: " + error);
  require(body_text(*prose).find("Enter the list inTable 4-8 now") !=
              std::string::npos,
          "the glued row lost its text: '" + body_text(*prose) + "'");
  bool label = false;
  for (const auto &block : prose->blocks)
    for (const auto &fragment : block.inlines)
      if (fragment.kind == geist::detail::ProseInlineKindIR::cross_reference &&
          fragment.text == "Table 4-8")
        label = true;
  require(label, "the link does not carry exactly the label `Table 4-8`");
}

// Without another span on whole words the row's own stored display line
// proves its columns: every word cell stands where the line stores it
// (SH09-8107-00 1.2.6.4, `listed in Table 6and round up` under the single
// `cselect 19 7 TBLDIRECT`, served `listed in <a ...>Table 6</a>and round
// up`, DT 19920529121056).
void a_glued_label_on_its_stored_columns_needs_no_second_span() {
  std::string error;
  require(extract(glued_label_record("9", GluedFont::none, true),
                  "Build List", error).has_value(),
          "a glued label on its stored columns was rejected: " + error);
  require(extract(glued_end_record("13", false, true), "Build List", error)
              .has_value(),
          "an end-glued label on its stored columns was rejected: " + error);
}

// A label that also ends inside a word is torn at both ends.
void a_glued_label_torn_at_both_ends_still_fails() {
  std::string error;
  require(!extract(glued_label_record("11", GluedFont::corroborating, true),
                   "Build List", error),
          "a selector torn at both ends was admitted");
}

// The rule reads the stored word: a split between two stored tokens is not
// the glued shape and keeps its rejection.
void a_split_between_two_tokens_still_fails() {
  std::string error;
  require(!extract(glued_label_record("9", GluedFont::corroborating, false),
                   "Build List", error),
          "a selector torn between two stored tokens was admitted");
}

// Only selectors are witnessed in this shape; a font span stays fail-closed.
void a_torn_font_span_over_a_glued_word_still_fails() {
  std::string error;
  require(!extract(glued_label_record("9", GluedFont::torn, true),
                   "Build List", error),
          "a torn font span over a glued word was admitted");
}

// The mirror shape: the label's *end* is glued to the word after it.
// SC33-6705-01 6.12.2.1 record 605 token 228 is the one word `Guideprovides`
// under `cselect 3 16 LNK ...`, served (DT 19991011131428)
// `<a ...><cite>Operations</cite> <cite>Guide</cite></a>provides`.
//
// Row cells: origin 0-2, `Enter` 3-7, `the` 9-11, `Table` 13-17, `4-8now`
// 19-24 (`now` at 22), `please` 26-31.
DecodedLogicalRecordSource glued_end_record(const std::string &select_begin,
                                            bool corroborated,
                                            bool one_token) {
  RecordBuilder builder;
  metadata(builder, "1.15", {"Build", "List"});
  builder.line({words("cselect"), words(select_begin), words("9"),
                words("TBLXREXBL")});
  if (corroborated)
    builder.line({words("cfont"), words("3"), words("5"), words("2")});
  std::vector<TokenWords> row{words("   "), words("Enter"), words("the"),
                              words("Table")};
  if (one_token) {
    row.push_back(words("4-8now"));
  } else {
    row.push_back(words("4-8"));
    row.push_back(glued("now"));
  }
  row.push_back(words("please"));
  builder.line(std::move(row));
  return builder.build(45);
}

void a_selector_label_glued_to_the_next_word_is_a_link() {
  const auto record = glued_end_record("13", true, true);
  std::string error;
  const auto prose = extract(record, "Build List", error);
  require(prose.has_value(),
          "a selector label glued to the word after it was rejected: " +
              error);
  if (!prose) return;
  require(verify(record, "Build List", *prose, error),
          "the end-glued selector label is not conserved: " + error);
  require(body_text(*prose).find("Enter the Table 4-8now please") !=
              std::string::npos,
          "the end-glued row lost its text: '" + body_text(*prose) + "'");
  bool label = false;
  for (const auto &block : prose->blocks)
    for (const auto &fragment : block.inlines)
      if (fragment.kind == geist::detail::ProseInlineKindIR::cross_reference &&
          fragment.text == "Table 4-8")
        label = true;
  require(label, "the link does not carry exactly the label `Table 4-8`");
}

void an_end_glued_label_keeps_its_guards() {
  std::string error;
  require(!extract(glued_end_record("14", true, true), "Build List", error),
          "a selector torn at both ends was admitted");
  require(!extract(glued_end_record("13", true, false), "Build List", error),
          "a selector torn between two stored tokens was admitted");
}

// -------------------------------------------------------------------------
// A leading glyph glued to more text, under a span of that whole word (#91).
// -------------------------------------------------------------------------

// SC24-546 B.1.3.1 record 1026 line 10 stores `'` (one byte) + attach `.`
// + ` A<.B><.C>` under `cfont 5 2 E 8 9 E ...`; hosted (DT 19940323131240)
// serves `<samp>'.</samp> <samp>A<.B><.C></samp>`.
DecodedLogicalRecordSource glued_glyph_record(bool word_span) {
  RecordBuilder builder;
  metadata(builder, "1.16", {"DBCS", "Symbols"});
  if (word_span)
    builder.line({words("cfont"), words("5"), words("2"), words("E"),
                  words("8"), words("5"), words("E")});
  else
    builder.line({words("cfont"), words("8"), words("5"), words("E")});
  builder.line({words("     "), words("'"), glued("."), words("A<.B>"),
                words("->"), words("Incorrect")});
  const auto quote = std::find(builder.record.tokens.begin(),
                               builder.record.tokens.end(), words("'"));
  require(quote != builder.record.tokens.end(), "the quote token is absent");
  if (quote != builder.record.tokens.end())
    builder.record.encoded_tokens[static_cast<std::size_t>(
        std::distance(builder.record.tokens.begin(), quote))]
        .width = 1;
  return builder.build(46);
}

std::string rows_and_body(const geist::detail::ProseTopicIR &topic) {
  std::string rows;
  for (const auto &block : topic.blocks)
    for (const auto &line : block.preformatted_lines) rows += line + "\n";
  return rows + body_text(topic);
}

void a_glyph_glued_to_text_under_its_word_span_is_display_text() {
  const auto record = glued_glyph_record(true);
  std::string error;
  const auto prose = extract(record, "DBCS Symbols", error);
  require(prose.has_value(),
          "a leading glyph whose glued word a span covers was rejected: " +
              error);
  if (!prose) return;
  require(verify(record, "DBCS Symbols", *prose, error),
          "the styled glued glyph is not conserved: " + error);
  require(rows_and_body(*prose).find("'. A<.B>") != std::string::npos,
          "the glued glyph word was dropped: '" + rows_and_body(*prose) +
              "'");
  bool styled = false;
  for (const auto &block : prose->blocks)
    for (const auto &fragment : block.inlines)
      if (geist::detail::font_style_is_example(fragment.style) &&
          fragment.text.find("'.") != std::string::npos)
        styled = true;
  require(styled, "the `cfont 5 2 E` triple did not land on `'.`");
}

// -------------------------------------------------------------------------
// A glyph glued to the text after it is never a row marker.
// -------------------------------------------------------------------------

// A row marker holds a display column of its own, so the span is not what
// proves the glyph: with no span on its word the glued `'.` is still drawn
// text, and the `cfont 8 5 E` on `A<.B>` still lands on that word.
void a_glued_glyph_without_its_word_span_is_display_text() {
  const auto record = glued_glyph_record(false);
  std::string error;
  const auto prose = extract(record, "DBCS Symbols", error);
  require(prose.has_value(),
          "an unspanned glued leading glyph was rejected: " + error);
  if (!prose) return;
  require(verify(record, "DBCS Symbols", *prose, error),
          "the unspanned glued glyph is not conserved: " + error);
  require(rows_and_body(*prose).find("'. A<.B>") != std::string::npos,
          "an unspanned glued leading glyph was dropped: '" +
              rows_and_body(*prose) + "'");
}

// SH20-918 4.6.4 record 436 token 181 stores `"` (one byte, value 45) at
// the row origin with `.` and `TEXT` attached behind it and no span on the
// row; hosted (DT 19910520154851) prints `".TEXT"`.  SG24-204 5.3.3 record
// 230 token 73 is the same shape, `(` + `.` + `.` + `\cicscli\bin`, served
// `(..\cicscli\bin)`.  This row spells SC24-546's `<.D.A>` in that shape.
DecodedLogicalRecordSource glued_row_glyph_record(TokenWords glyph,
                                                  std::size_t glyph_width) {
  RecordBuilder builder;
  metadata(builder, "1.18", {"Mixed", "Strings"});
  builder.line({words("     "), glyph, glued("."), glued("D"), glued("."),
                glued("A>"), words("is"), words("valid")});
  const auto at = std::find(builder.record.tokens.begin(),
                            builder.record.tokens.end(), glyph);
  require(at != builder.record.tokens.end(), "the glyph token is absent");
  if (at != builder.record.tokens.end())
    builder.record.encoded_tokens[static_cast<std::size_t>(
        std::distance(builder.record.tokens.begin(), at))]
        .width = static_cast<std::uint8_t>(glyph_width);
  return builder.build(48);
}

void a_glued_glyph_opening_an_unstyled_row_is_display_text() {
  const auto record = glued_row_glyph_record(words("<"), 1);
  std::string error;
  const auto prose = extract(record, "Mixed Strings", error);
  require(prose.has_value(),
          "a glued glyph opening an unstyled row was rejected: " + error);
  if (!prose) return;
  require(verify(record, "Mixed Strings", *prose, error),
          "the glued row glyph is not conserved: " + error);
  require(body_text(*prose).find("<.D.A> is valid") != std::string::npos,
          "the glued row glyph was dropped: '" + body_text(*prose) + "'");
}

// A decoder placeholder run carries no character, so gluing changes nothing:
// it stays the row's marker (ACPZMST1 3.11 record 180).
void a_glued_placeholder_run_stays_a_marker() {
  const auto record = glued_row_glyph_record(TokenWords{0x2502}, 1);
  std::string error;
  const auto prose = extract(record, "Mixed Strings", error);
  require(prose.has_value(),
          "a row opened by a glued placeholder was rejected: " + error);
  if (!prose) return;
  require(body_text(*prose).find(".D.A> is valid") != std::string::npos,
          "the row after a glued placeholder lost its text: '" +
              body_text(*prose) + "'");
  require(body_text(*prose).find("\xe2\x94\x82") == std::string::npos &&
              body_text(*prose).find("|.D") == std::string::npos,
          "a glued placeholder run was admitted as text: '" +
              body_text(*prose) + "'");
}

// The display line's own length byte is the row-control slot whatever word
// the dictionary spells for it and whatever is attached behind it: SG24-204
// 5.3.3 record 229 opens a row with the length byte value 27, spelled `(`,
// and hosted prints no `(` there.
void a_length_byte_spelled_as_a_glued_glyph_stays_the_slot() {
  RecordBuilder builder;
  metadata(builder, "1.19", {"Length", "Byte"});
  builder.line({words("   "), words("TCP"), words("support")});
  builder.line_with_prefix(words("("),
                           {glued("IP"), words("is"), words("provided")});
  const auto record = builder.build(49);
  std::string error;
  const auto prose = extract(record, "Length Byte", error);
  require(prose.has_value(),
          "a row opened by a length byte spelled `(` was rejected: " + error);
  if (!prose) return;
  require(body_text(*prose).find('(') == std::string::npos,
          "a length byte spelled `(` was admitted as text: '" +
              body_text(*prose) + "'");
}

// -------------------------------------------------------------------------
// `Z` (PVDEF) presents as PKDEF does (#91), but keeps a style of its own:
// hosted serves `Q` and `Z` alike as `<dfn>`, and only `Q` has the census
// behind it that lets HTML follow.
// -------------------------------------------------------------------------

// SC26-457 1.3 and SC26-4905-00 1.3 store
// `cfont 3 7 P 11 10 Z 22 4 P 27 1 P 29 10 Z 39 1 P`; hosted serves
// `<kbd>COMMAND</kbd> <dfn>parameters</dfn> ...` in both books.
// Code `0` (`H0`) opens `<I>` in the reader's fixed code table, as `K`
// (`H4`) does; hosted serves SH19-6758-01 PREFACE.4 (DT 19930122124212)
// `cfont 12 11 0 24 3 0 ...` as `<I>Customizing</I> <I>and</I> ...`.
void an_h0_span_is_an_italic_phrase() {
  RecordBuilder builder;
  metadata(builder, "1.18", {"Related", "Books"});
  builder.line({words("cfont"), words("12"), words("11"), words("0"),
                words("24"), words("3"), words("0")});
  // `Customizing` stands at column 12 and `and` at column 24.
  builder.line({words("   "), words("Part"), words("1."), words(" "),
                words("Customizing"), words("and")});
  const auto record = builder.build(48);
  std::string error;
  const auto prose = extract(record, "Related Books", error);
  require(prose.has_value(), "an H0 span was rejected: " + error);
  if (!prose) return;
  bool italic = false;
  for (const auto &block : prose->blocks)
    for (const auto &fragment : block.inlines)
      if (fragment.text.find("Customizing") != std::string::npos &&
          fragment.style == geist::detail::FontStyleIR::italic_phrase)
        italic = true;
  require(italic, "the H0 span did not become the italic phrase");
}

void a_pvdef_span_is_a_defined_term() {
  RecordBuilder builder;
  metadata(builder, "1.17", {"Command", "Structure"});
  builder.line({words("cfont"), words("3"), words("7"), words("P"),
                words("11"), words("10"), words("Z")});
  builder.line({words("   "), words("COMMAND"), words("parameters")});
  const auto record = builder.build(47);
  std::string error;
  const auto prose = extract(record, "Command Structure", error);
  require(prose.has_value(), "a PVDEF span was rejected: " + error);
  if (!prose) return;
  bool defined = false;
  for (const auto &block : prose->blocks)
    for (const auto &fragment : block.inlines)
      if (fragment.text == "parameters" &&
          fragment.style == geist::detail::FontStyleIR::variable_define &&
          geist::detail::font_style_phrase_kind(fragment.style) ==
              geist::detail::PhraseKindIR::none)
        defined = true;
  require(defined, "the PVDEF span did not stay its own phraseless style");

  // `Q` and `Z` cells join one run, as cells of one presentation do; the
  // run names no phrase, so `Z` never reaches `<dfn>` through a neighbour.
  RecordBuilder joined;
  metadata(joined, "1.17", {"Command", "Structure"});
  joined.line({words("cfont"), words("3"), words("4"), words("Q"),
               words("7"), words("6"), words("Z")});
  joined.line({words("   "), words("*CURRENTLY")});
  const auto both = extract(joined.build(47), "Command Structure", error);
  require(both.has_value(), "a Q/Z run was rejected: " + error);
  if (!both) return;
  bool one_run = false;
  for (const auto &block : both->blocks)
    for (const auto &fragment : block.inlines)
      if (fragment.text.find("RENTLY") != std::string::npos)
        one_run = fragment.text.find("*CUR") != std::string::npos &&
                  geist::detail::font_style_phrase_kind(fragment.style) ==
                      geist::detail::PhraseKindIR::none;
  require(one_run, "a Q/Z run split or named a phrase");
}


// -------------------------------------------------------------------------
// A font span inside another span of the same style.
// -------------------------------------------------------------------------

// SC09-1905-00 1.2.2 record 48 line 6 is `cfont 3 5 2 9 6 2 14 1 2` over
// ` | Usage Notes:`: the colon at column 14 is styled twice, once by the
// `Notes:` span and once on its own.  Hosted (DT 19940711122820) nests the
// two `<B>` elements (`<B>Usage</B> <B>Notes<B>:</B></B>`), which draws the
// same bold text.  A second span of the same style adds nothing; one of a
// different style is still unproven and declines.
void a_nested_span_of_the_same_style(bool same_style, bool expect_typed) {
  RecordBuilder builder;
  metadata(builder, "1.2.2", {"Commands"});
  builder.line({words("cfont"), words("3"), words("5"), words("2"),
                words("9"), words("6"), words("2"), words("14"), words("1"),
                words(same_style ? "2" : "1")});
  builder.line({words("   "), words("Usage"), words("Notes:")});
  const auto record = builder.build(48);
  std::string error;
  const auto prose = extract(record, "Commands", error);
  if (!expect_typed) {
    require(!prose.has_value(),
            "overlapping spans of different styles were admitted");
    return;
  }
  require(prose.has_value(),
          "a span nested in a same-style span was rejected: " + error);
  if (!prose) return;
  require(verify(record, "Commands", *prose, error),
          "the nested span topic does not verify: " + error);
  require(body_text(*prose).find("Notes:") != std::string::npos,
          "the styled words were lost: '" + body_text(*prose) + "'");
}
} // namespace

int main() {
  a_control_shaped_word_after_the_origin_run_is_display_text();
  a_control_opening_its_display_line_stays_a_control();
  a_font_span_on_the_bullet_is_dropped_and_the_row_kept();
  a_span_reaching_from_the_bullet_into_the_text_still_fails();
  consecutive_font_instructions_bind_to_distinct_native_rows();
  a_font_span_inside_a_selector_preserves_both_semantics();
  styled_selector_columns_map_to_utf8_source_offsets();
  overlapping_selectors_preserve_every_target_once();
  repeated_numeric_words_after_a_font_instruction_stay_display_text();
  a_styled_leading_bar_is_display_text();
  styled_terminal_punctuation_is_display_text();
  a_styled_terminal_marker_finishes_the_open_row();
  an_apl_font_span_decodes_its_own_character_cells();
  a_two_run_gap_inside_a_spanned_row_keeps_the_row_whole();
  a_two_run_gap_without_a_reaching_span_keeps_the_pair_reading();
  a_styled_row_final_comma_before_a_control_is_display_text();
  a_styled_row_final_comma_before_a_control_is_display_text(true);
  an_unstyled_row_final_comma_before_a_control_stays_padding();
  an_unstyled_row_final_comma_before_a_control_stays_padding(true);
  a_selector_label_glued_to_a_word_is_a_link();
  a_glued_label_on_its_stored_columns_needs_no_second_span();
  a_glued_label_torn_at_both_ends_still_fails();
  a_split_between_two_tokens_still_fails();
  a_torn_font_span_over_a_glued_word_still_fails();
  a_selector_label_glued_to_the_next_word_is_a_link();
  an_end_glued_label_keeps_its_guards();
  a_glyph_glued_to_text_under_its_word_span_is_display_text();
  a_glued_glyph_without_its_word_span_is_display_text();
  a_glued_glyph_opening_an_unstyled_row_is_display_text();
  a_glued_placeholder_run_stays_a_marker();
  a_length_byte_spelled_as_a_glued_glyph_stays_the_slot();
  a_pvdef_span_is_a_defined_term();
  an_h0_span_is_an_italic_phrase();
  a_nested_span_of_the_same_style(true, true);
  a_nested_span_of_the_same_style(false, false);
  return 0;
}
