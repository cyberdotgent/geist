// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A free-standing ASCII glyph that opens a display row of flattened prose.
//
// The row model reads a one-cell glyph in front of a row's text as the row's
// visual marker -- a change bar in the revision margin, which hosted
// BookServer draws in a column of its own and which the typed render blanks.
// A glyph standing at the row's own origin, after the origin run, is not in
// that margin.  Hosted prints it, so the typed render must keep it too:
//
//  * as a list bullet one level down, when it has a `U+2666` bullet's
//    geometry (ACPZMST1 1.2 `       -   Resource manager` under
//    `   °   Communications partners`, SG24-204 1.1 `       >   OS/2`);
//  * as the first word of the row's text otherwise (N2AH1MST 1.2.4
//    `=      Marginal space`, SC24-5520-00 1.1.30 `* The last entry`).
//
// Everything here is synthetic: the tests build whole topics'
// `DecodedLogicalRecordSource` values by hand and open no book.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/lowering/prose_topic_document_lowering.hpp"
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
using geist::detail::ProseTopicIR;
using geist::detail::TokenWords;

constexpr auto npos = static_cast<std::size_t>(-1);

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "prose_row_glyph_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

bool contains(const std::string &text, const std::string &needle) {
  return text.find(needle) != std::string::npos;
}

// One source token: its decoded words and its encoded width and value.  A
// one-byte token's value is the byte the record stores; the corpus stores
// space runs, bullets and layout glyphs in one byte.
struct Token {
  TokenWords words;
  std::uint8_t width = 2;
  std::uint16_t value = 0;
};

Token word(const std::string &text) {
  return {TokenWords(text.begin(), text.end()), 2, 0};
}

Token byte(const std::string &text, std::uint16_t value) {
  return {TokenWords(text.begin(), text.end()), 1, value};
}

// The one-byte space runs and glyphs of ACPZMST1 record 32: a seven-cell run
// is value 24, a three-cell run 20, a two-cell run 19, `U+2666` 3, `-` 36.
Token spaces(std::size_t count) {
  return byte(std::string(count, ' '), static_cast<std::uint16_t>(16 + count));
}
Token diamond() { return {TokenWords{0x2666}, 1, 3}; }
Token glyph(const std::string &text) { return byte(text, 36); }

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x140;

  // One display line: a one-byte length byte covering `content`.  The length
  // byte's dictionary spelling is the row sentinel.
  void line(std::vector<Token> content) {
    std::uint16_t length = 0;
    for (const auto &token : content) length += token.width;
    record.encoded_tokens.push_back({length, 1});
    record.tokens.push_back(TokenWords{0x25BA});
    for (auto &token : content) {
      const auto value = token.width == 2 ? next_encoded++ : token.value;
      record.encoded_tokens.push_back({value, token.width});
      record.tokens.push_back(std::move(token.words));
    }
  }

  DecodedLogicalRecordSource build(std::uint32_t logical_record) {
    record.logical_record = logical_record;
    record.assembled =
        geist::detail::assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = logical_record;
    std::uint32_t at = 1;
    for (std::size_t token = 0; token < record.tokens.size(); ++token) {
      const auto encoded = record.encoded_tokens[token];
      const auto spacing =
          !record.tokens[token].empty() && record.tokens[token].front() < 4;
      record.ir.tokens.push_back(
          {token, encoded, record.tokens[token],
           {at, static_cast<std::uint32_t>(at + encoded.width)}, spacing,
           spacing ? record.tokens[token].front() : std::uint16_t{3}});
      at += encoded.width;
    }
    record.ir.payload_range = {1, at};
    geist::detail::assign_display_line_framing(record.ir);
    record.control_segments = geist::detail::decode_control_segments(
        record.logical_record, record.assembled, record.encoded_tokens,
        record.ir.display_lines);
    geist::detail::demote_display_line_owned_controls(record);
    geist::detail::assign_display_line_margin_marks(record);
    return std::move(record);
  }
};

const std::string topic_title = "Connectivity Concepts";

void metadata(RecordBuilder &builder) {
  builder.line({word("sh1.2")});
  builder.line({word("ctopicn"), word("7")});
  builder.line({word("cparent"), word("1.0")});
  builder.line({word("cforwardlevel"), word("1.3")});
  builder.line({word("cbacklevel"), word("1.1")});
  builder.line({word("csummary"), word("9"), word("0"), word("9")});
  builder.line({word("chdlevel"), word(":H2")});
  builder.line({word("csourcefn"), word("DVGR1A05")});
  builder.line({word("ST"), word("Connectivity"), word("Concepts")});
}

std::optional<ProseTopicIR> extract(const DecodedLogicalRecordSource &record,
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
  auto topic = geist::detail::extract_prose_topic_ir(
      sources, layout, *ownership, topic_title, nullptr, &error);
  if (topic) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(sources, layout, *ownership,
                                                 topic_title, nullptr, *topic,
                                                 &verify_error),
            "the typed topic does not verify: " + verify_error);
  }
  return topic;
}

std::string markdown(const ProseTopicIR &prose) {
  geist::detail::TopicIdentityIR topic;
  topic.id = "1.2";
  topic.title = topic_title;
  topic.heading_level = 2;
  topic.start_logical_record = 31;
  topic.end_logical_record = 31;
  std::string error;
  const auto document =
      geist::detail::lower_prose_topic_to_document_ir(topic, prose, &error);
  require(document.has_value(), "the typed topic did not lower: " + error);
  if (!document) return {};
  return geist::detail::render_document_markdown(*document);
}

std::string inline_text(const geist::detail::ProseBlockIR &block) {
  std::string text;
  for (const auto &fragment : block.inlines) text += fragment.text;
  return text;
}

std::string body_text(const ProseTopicIR &topic) {
  std::string text;
  for (const auto &block : topic.blocks) text += inline_text(block) + "\n";
  return text;
}

std::size_t block_with(const ProseTopicIR &topic, const std::string &text) {
  for (std::size_t index = 0; index < topic.blocks.size(); ++index)
    if (inline_text(topic.blocks[index]) == text) return index;
  return npos;
}

// -------------------------------------------------------------------------
// A glyph with a bullet's geometry at the row origin is a list bullet.
// -------------------------------------------------------------------------

// ACPZMST1 1.2 record 32: `   °   Communications partners` (origin run token
// 110, `U+2666` 111, gap 112) and then `       -   Resource manager` and
// `       -   User program` (origin run 116, `-` 117 value 36, gap 118).  The
// dash rows stand at the parent item's text column, with no blank line
// between them.
DecodedLogicalRecordSource sub_list_record(bool blank_before_sub_list,
                                           std::size_t sub_origin) {
  RecordBuilder builder;
  metadata(builder);
  builder.line({spaces(3), word("The"), word("following"), word("terms.")});
  builder.line({});
  builder.line({spaces(3), diamond(), spaces(2), word("Domain")});
  builder.line(
      {spaces(3), diamond(), spaces(2), word("Communications"),
       word("partners")});
  if (blank_before_sub_list) builder.line({});
  builder.line({spaces(sub_origin), glyph("-"), spaces(2), word("Resource"),
                word("manager")});
  builder.line({spaces(sub_origin), glyph("-"), spaces(2), word("User"),
                word("program")});
  if (blank_before_sub_list) builder.line({});
  builder.line({spaces(3), diamond(), spaces(2), word("Resources")});
  return builder.build(31);
}

void check_sub_list(const ProseTopicIR &prose, const std::string &shape) {
  const auto parent = block_with(prose, "Communications partners");
  const auto first = block_with(prose, "Resource manager");
  const auto second = block_with(prose, "User program");
  const auto domain = block_with(prose, "Domain");
  const auto after = block_with(prose, "Resources");
  require(parent != npos && first != npos && second != npos &&
              domain != npos && after != npos,
          shape + ": the sub-list rows lost text or merged: '" +
              body_text(prose) + "'");
  if (parent == npos || first == npos || second == npos || domain == npos ||
      after == npos)
    return;
  const auto &blocks = prose.blocks;
  require(blocks[first].kind == ProseBlockKindIR::list_item &&
              blocks[second].kind == ProseBlockKindIR::list_item,
          shape + ": the `-` rows are not list items");
  require(blocks[first].parent_block == parent &&
              blocks[second].parent_block == parent,
          shape + ": the `-` items are not nested in the item above them");
  require(blocks[parent].body_blocks ==
              std::vector<std::size_t>{first, second},
          shape + ": the parent item does not own its sub-list");
  require(blocks[first].list_ordinal == blocks[second].list_ordinal &&
              blocks[first].list_ordinal != blocks[parent].list_ordinal,
          shape + ": the sub-items do not form one list of their own");
  require(blocks[domain].list_ordinal == blocks[parent].list_ordinal &&
              blocks[after].list_ordinal == blocks[parent].list_ordinal &&
              blocks[after].parent_block == npos,
          shape + ": the outer list was split around its sub-list");
  const auto rendered = markdown(prose);
  require(contains(rendered, "- Communications partners\n") &&
              contains(rendered, "  - Resource manager\n") &&
              contains(rendered, "  - User program\n") &&
              contains(rendered, "\n- Resources"),
          shape + ": the nested list did not render: " + rendered);
}

void a_dash_row_under_an_item_is_a_nested_item() {
  const auto record = sub_list_record(false, 7);
  std::string error;
  const auto prose = extract(record, error);
  require(prose.has_value(), "the sub-list topic was rejected: " + error);
  if (prose) check_sub_list(*prose, "adjacent sub-list");
}

// QSYSNEWG 1.9 record 37 sets a blank line before and after the `-` rows;
// hosted (DT 19910524085706) prints them inside the item above.
void a_dash_list_after_a_blank_line_is_still_nested() {
  const auto record = sub_list_record(true, 7);
  std::string error;
  const auto prose = extract(record, error);
  require(prose.has_value(),
          "the blank-separated sub-list topic was rejected: " + error);
  if (prose) check_sub_list(*prose, "blank-separated sub-list");
}

// A `-` item that stands neither at its parent's origin nor at its text
// column is a level the model cannot place: the topic declines rather than
// guessing a structure.
void a_misaligned_dash_item_declines() {
  const auto record = sub_list_record(false, 9);
  std::string error;
  const auto prose = extract(record, error);
  require(!prose.has_value(),
          "a sub-item outside its parent's text column was typed: '" +
              (prose ? body_text(*prose) : std::string()) + "'");
}

// DREICMST 1.7.2.1 sets its `-` items under a paragraph: they are a list of
// their own, nested in nothing.
void a_dash_list_after_a_paragraph_is_a_list() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({spaces(7), word("Check"), word("the"), word("following:")});
  builder.line({});
  builder.line({spaces(11), glyph("-"), spaces(2), word("Possible"),
                word("errors")});
  builder.line({spaces(11), glyph("-"), spaces(2), word("That"),
                word("COLVERSION")});
  const auto record = builder.build(31);
  std::string error;
  const auto prose = extract(record, error);
  require(prose.has_value(), "the dash-list topic was rejected: " + error);
  if (!prose) return;
  const auto first = block_with(*prose, "Possible errors");
  const auto second = block_with(*prose, "That COLVERSION");
  require(first != npos && second != npos,
          "the dash rows lost text: '" + body_text(*prose) + "'");
  if (first == npos || second == npos) return;
  require(prose->blocks[first].kind == ProseBlockKindIR::list_item &&
              prose->blocks[second].kind == ProseBlockKindIR::list_item &&
              prose->blocks[first].parent_block == npos &&
              prose->blocks[first].list_ordinal ==
                  prose->blocks[second].list_ordinal,
          "the dash rows under a paragraph are not one list");
  require(contains(markdown(*prose), "\n- Possible errors\n- That COLVERSION"),
          "the dash list did not render: " + markdown(*prose));
}

// The revision margin keeps its reading: a change bar is the only cell in
// front of its row's origin, and hosted prints it in the margin, not as a
// bullet.
void a_change_bar_in_the_margin_is_no_bullet() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({spaces(3), word("Plain"), word("text.")});
  builder.line({spaces(1), glyph("|"), spaces(2), word("Revised"),
                word("text.")});
  const auto record = builder.build(31);
  std::string error;
  const auto prose = extract(record, error);
  require(prose.has_value(), "the change-bar topic was rejected: " + error);
  if (!prose) return;
  require(!contains(body_text(*prose), "|") &&
              contains(body_text(*prose), "Revised text."),
          "the change bar was admitted as text: '" + body_text(*prose) + "'");
  require(std::none_of(prose->blocks.begin(), prose->blocks.end(),
                       [](const auto &block) {
                         return block.kind == ProseBlockKindIR::list_item;
                       }),
          "the change bar opened a list item");
}

// The revised row inside a paragraph carries its margin mark through the IR.
// BLGR3MST EDITION and N2AB1MST 2.8 are served by hosted BookServer with
// ` | ` in front of each revised row and three blanks in front of the rows
// around it; the paragraph is revised in part, so the mark wraps the revised
// row's text only.  Markdown has no margin and prints the paragraph as
// before; HTML marks the run.
void a_revised_row_keeps_its_mark() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({spaces(3), word("Plain"), word("text.")});
  builder.line({spaces(1), glyph("|"), spaces(2), word("Revised"),
                word("text.")});
  builder.line({spaces(3), word("More"), word("text.")});
  const auto record = builder.build(31);
  require(record.ir.display_lines.size() == 12 &&
              record.ir.display_lines[10].margin_mark == U'|',
          "the barred line does not store its margin mark");
  std::string error;
  const auto prose = extract(record, error);
  require(prose.has_value(), "the revised topic was rejected: " + error);
  if (!prose) return;
  const auto block = block_with(*prose, "Plain text. Revised text. More text.");
  require(block != npos, "the three rows are not one paragraph: '" +
                             body_text(*prose) + "'");
  if (block == npos) return;
  std::string marked;
  std::string plain;
  for (const auto &fragment : prose->blocks[block].inlines)
    (fragment.revision_mark == U'|' ? marked : plain) += fragment.text;
  require(marked == "Revised text.",
          "the revised row's text is not marked: '" + marked + "'");
  require(plain == "Plain text.  More text.",
          "unrevised rows carry a mark: '" + plain + "'");

  geist::detail::TopicIdentityIR topic;
  topic.id = "1.2";
  topic.title = topic_title;
  const auto document =
      geist::detail::lower_prose_topic_to_document_ir(topic, *prose, &error);
  require(document.has_value(), "the revised topic did not lower: " + error);
  if (!document) return;
  require(geist::detail::verify_document_ir(*document, &error),
          "the revised document does not verify: " + error);
  const auto html =
      geist::detail::render_document_html_blocks(*document, {});
  require(contains(html, "Plain text. <span class=\"geist-revised\" "
                         "data-geist-revision-mark=\"|\">Revised text."
                         "</span> More text."),
          "HTML does not mark the revised run: " + html);
  require(contains(markdown(*prose),
                   geist::detail::escape_markdown_text(
                       "Plain text. Revised text. More text.")),
          "Markdown changed the revised paragraph: " + markdown(*prose));
}

// Only the bullet's own geometry makes a list item: a glyph followed by a
// wider gap is a definition-style row (N2AH1MST 1.2.4 `=      Marginal`).
void a_glyph_before_a_wide_gap_is_no_bullet() {
  RecordBuilder builder;
  metadata(builder);
  builder.line({spaces(7), word("Symbols:")});
  builder.line({spaces(7), glyph("="), spaces(5), word("Marginal"),
                word("space")});
  const auto record = builder.build(31);
  std::string error;
  const auto prose = extract(record, error);
  if (!prose) return;
  require(std::none_of(prose->blocks.begin(), prose->blocks.end(),
                       [](const auto &block) {
                         return block.kind == ProseBlockKindIR::list_item;
                       }),
          "a glyph before a wide gap opened a list item");
}

// -------------------------------------------------------------------------
// Any other glyph at the row origin is the row's first display word.
// -------------------------------------------------------------------------

std::optional<ProseTopicIR> glyph_row_topic(std::vector<Token> row) {
  RecordBuilder builder;
  metadata(builder);
  builder.line({spaces(7), word("Symbols"), word("follow:")});
  builder.line({});
  builder.line(std::move(row));
  const auto record = builder.build(31);
  std::string error;
  auto prose = extract(record, error);
  require(prose.has_value(), "the glyph-row topic was rejected: " + error);
  return prose;
}

// N2AH1MST 1.2.4 record 148 token 306: `=` after a seven-cell origin run,
// then a five-cell gap and `Marginal space ...`.  Hosted (DT
// 19910329000100) prints `       =      Marginal space (track) situation`.
void a_glyph_before_a_wide_gap_is_display_text() {
  const auto prose = glyph_row_topic(
      {spaces(7), glyph("="), spaces(5), word("Marginal"), word("space")});
  if (!prose) return;
  require(contains(body_text(*prose), "= Marginal space"),
          "the `=` term was dropped: '" + body_text(*prose) + "'");
}

// SC24-5520-00 1.1.30 record 272 token 29: `*` directly before `The last
// entry ...`; hosted prints `       * The last entry is indicated ...`.
void a_glyph_before_its_text_is_display_text() {
  const auto prose = glyph_row_topic(
      {spaces(7), glyph("*"), word("The"), word("last"), word("entry")});
  if (!prose) return;
  require(contains(body_text(*prose), "* The last entry"),
          "the leading `*` was dropped: '" + body_text(*prose) + "'");
}

// GC23-046 F.0 record 284 tokens 298-299: `(` and `)` as two one-byte
// tokens before `MVS/SP`; hosted prints `    ( ) MVS/SP and System/370 ...`.
void a_glyph_pair_opening_a_row_is_display_text() {
  const auto prose = glyph_row_topic(
      {spaces(4), glyph("("), glyph(")"), word("MVS"), word("trademark")});
  if (!prose) return;
  require(contains(body_text(*prose), "( ) MVS trademark"),
          "the `( )` pair was dropped: '" + body_text(*prose) + "'");
}

// GG24-395 3.4.3 record 685: a change bar at column 1, then `-` and
// `POSIX Support"`; hosted prints ` | - POSIX Support" in topic 3.2.1.` --
// the bar stays in the margin and the dash is text.
void a_glyph_behind_a_margin_bar_is_display_text() {
  const auto prose = glyph_row_topic({spaces(1), glyph("|"), glyph("-"),
                                      word("POSIX"), word("Support")});
  if (!prose) return;
  require(contains(body_text(*prose), "- POSIX Support") &&
              !contains(body_text(*prose), "|"),
          "the margin bar or the dash behind it was misread: '" +
              body_text(*prose) + "'");
}

// The revision margin keeps its reading: GC23-046 7.2.1 record 200 token
// 281 is `|` at column 1 directly before `If you obtained ...`.
void a_margin_bar_before_its_text_stays_a_marker() {
  const auto prose = glyph_row_topic(
      {spaces(1), glyph("|"), word("If"), word("you"), word("obtained")});
  if (!prose) return;
  require(contains(body_text(*prose), "If you obtained") &&
              !contains(body_text(*prose), "|"),
          "the margin bar was admitted as text: '" + body_text(*prose) + "'");
}

} // namespace

int main() {
  a_revised_row_keeps_its_mark();
  a_dash_row_under_an_item_is_a_nested_item();
  a_dash_list_after_a_blank_line_is_still_nested();
  a_misaligned_dash_item_declines();
  a_dash_list_after_a_paragraph_is_a_list();
  a_change_bar_in_the_margin_is_no_bullet();
  a_glyph_before_a_wide_gap_is_no_bullet();
  a_glyph_before_a_wide_gap_is_display_text();
  a_glyph_before_its_text_is_display_text();
  a_glyph_pair_opening_a_row_is_display_text();
  a_glyph_behind_a_margin_bar_is_display_text();
  a_margin_bar_before_its_text_stays_a_marker();
  return 0;
}
