// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A generated `:INDEX` topic's term fields, decided on the record's own
// display-line framing and on the delimiter the book declares (issue #78).
//
// Two things a reader of the flattened decoded string gets wrong here.
//
// 1. The one-byte token in front of a control is not a boundary sentinel; it
//    is the display line's *length byte*, and a token reader resolves it
//    through the dictionary like any other byte.  Books spell it differently:
//    SC31-605 record 720 spells the `cidelm` line's length byte `U+25BA`,
//    SH20-918 record 608 spells the same byte `U+2666` -- the bullet glyph.
//    Nothing about the glyph decides anything; the framing does, and the
//    checked accessor refuses to hand a length byte back as display text.
//    The field delimiter is the operand `cidelm` declares, not the byte.
//
// 2. A `citerm` whose term field is empty is a term the book wrote with no
//    text of its own.  The field is present -- its two delimiter words stand
//    adjacent, with no cell between them -- and the entries below it are its
//    children.  SH20-918 record 636 line 18 is exactly `citerm <D><D>1`, the
//    only one in the corpus's 29 INDEX topics, and rejecting it dropped that
//    book's whole 668-entry index.
//
// Everything here is synthetic: the tests build `DecodedLogicalRecordSource`
// values by hand and open no book.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/lowering/generated_toc_index_document_lowering.hpp"
#include "geist/detail/ir/generated_toc_index_ir.hpp"
#include "geist/detail/core/internal.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <variant>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenFramingRole;
using geist::detail::TokenWords;

// The row sentinel and the bullet, as the code page decodes them.
constexpr std::uint16_t sentinel_glyph = 0x25BA;
constexpr std::uint16_t bullet_glyph = 0x2666;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "generated_index_term_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;

  void append(std::uint16_t encoded, std::uint8_t width, TokenWords value) {
    record.encoded_tokens.push_back({encoded, width});
    record.tokens.push_back(std::move(value));
  }

  // One display line: its length byte, then tokens whose widths sum to it.
  // The length byte's own dictionary spelling is `spelling`, which is exactly
  // the evidence this test is about.
  void line(TokenWords spelling, std::vector<TokenWords> content) {
    std::uint16_t bytes = 0;
    for (const auto &token : content)
      bytes = static_cast<std::uint16_t>(bytes + 2);
    append(bytes, 1, std::move(spelling));
    for (auto &token : content) append(next_encoded++, 2, std::move(token));
  }

  DecodedLogicalRecordSource build(std::uint32_t logical_record) {
    record.logical_record = logical_record;
    record.assembled =
        geist::detail::assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = logical_record;
    record.ir.tokens.clear();
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
    return std::move(record);
  }
};

// `citerm <D>term<D><level>[<D><target>...]` as one display line.  `term`
// empty writes the two delimiter words into one token, exactly as SH20-918
// writes them.
std::vector<TokenWords> index_term(const std::string &term,
                                   const std::string &level,
                                   const std::string &target) {
  std::vector<TokenWords> content;
  content.push_back(words("citerm"));
  if (term.empty()) {
    // SH20-918's own shape: one token holding both delimiter words, the
    // attach control that keeps the level against them, then the level.
    content.push_back({sentinel_glyph, sentinel_glyph});
    content.push_back({1});
    TokenWords level_field;
    for (const auto ch : level)
      level_field.push_back(static_cast<std::uint16_t>(ch));
    content.push_back(std::move(level_field));
  } else {
    TokenWords opener{sentinel_glyph};
    for (const auto ch : term) opener.push_back(static_cast<std::uint16_t>(ch));
    content.push_back(std::move(opener));
    TokenWords level_field{1, sentinel_glyph};
    for (const auto ch : level)
      level_field.push_back(static_cast<std::uint16_t>(ch));
    content.push_back(std::move(level_field));
  }
  if (!target.empty()) {
    TokenWords target_field{1, sentinel_glyph};
    for (const auto ch : target)
      target_field.push_back(static_cast<std::uint16_t>(ch));
    content.push_back(std::move(target_field));
  }
  return content;
}

// An `:INDEX` envelope whose every display line's length byte is spelled with
// the bullet glyph -- the shape SH20-918 writes and SC31-605 does not.
DecodedLogicalRecordSource index_record(
    const std::vector<std::vector<TokenWords>> &terms) {
  RecordBuilder builder;
  const TokenWords bullet{bullet_glyph};
  builder.line(bullet, {words("SHINDEX")});
  builder.line(bullet, {words("chdlevel"), words(":INDEX")});
  builder.line(bullet, {words("ST"), words("Index")});
  builder.line(bullet, {words("cidelm"), TokenWords{sentinel_glyph}});
  builder.line(bullet, {words("cgpsep"), TokenWords{sentinel_glyph, 'A'}});
  for (const auto &term : terms) builder.line(bullet, term);
  builder.line(bullet, {words("cendindex")});
  return builder.build(41);
}

const geist::detail::GeneratedIndexGroupIR *only_group(
    const geist::detail::GeneratedTocIndexTopicIR &topic) {
  return topic.groups.size() == 1 ? &topic.groups.front() : nullptr;
}

// The length byte in front of `cidelm` is a length byte whatever it spells,
// and the delimiter is the operand the book declares.
void the_bullet_in_the_sentinel_slot_is_a_length_byte() {
  const auto record = index_record({index_term("Alpha", "1", "TOPICA")});
  require(record.ir.display_lines_parse,
          "the synthetic :INDEX record's display lines did not parse");
  for (const auto &line : record.ir.display_lines) {
    require(record.ir.tokens[line.prefix_token].framing ==
                TokenFramingRole::line_length,
            "a display line's opening token was not stamped as its length "
            "byte");
    require(record.ir.tokens[line.prefix_token].decoded_words.size() == 1 &&
                record.ir.tokens[line.prefix_token].decoded_words.front() ==
                    bullet_glyph,
            "the fixture no longer spells its length bytes with the bullet "
            "glyph, which is the whole point of this record");
    require(geist::detail::display_text_words(record, line.prefix_token) ==
                nullptr,
            "the checked accessor handed back the bullet spelling of a length "
            "byte; the glyph in that slot is structure, not a boundary word");
  }

  std::string error;
  const auto topic =
      geist::detail::extract_generated_toc_index_topic_ir({record}, nullptr,
                                                          &error);
  require(topic.has_value(),
          "an :INDEX record whose length bytes spell the bullet glyph was "
          "rejected: " + error);
  if (!topic) return;
  require(topic->delimiter == sentinel_glyph,
          "the field delimiter was not read from the CIDELM operand");
  const auto *group = only_group(*topic);
  require(group != nullptr && group->terms.size() == 1 &&
              group->terms.front().term == "Alpha" &&
              group->terms.front().targets.size() == 1,
          "the ordinary term of the bullet-spelled record did not parse");
}

// The empty term field, its child, and the whole lowering.
void a_term_the_book_wrote_empty_keeps_its_children() {
  const auto record = index_record({index_term("Alpha", "1", "TOPICA"),
                                    index_term("", "1", ""),
                                    index_term("See Alpha", "2", "")});
  std::string error;
  const auto topic =
      geist::detail::extract_generated_toc_index_topic_ir({record}, nullptr,
                                                          &error);
  require(topic.has_value(),
          "an index whose term field is empty was rejected: " + error);
  if (!topic) return;
  const auto *group = only_group(*topic);
  require(group != nullptr && group->terms.size() == 3,
          "the empty term was dropped instead of kept");
  if (group == nullptr || group->terms.size() != 3) return;
  require(group->terms[1].term.empty() && group->terms[1].level == 1 &&
              group->terms[1].targets.empty(),
          "the empty term did not survive as a textless level-1 parent");
  require(group->terms[2].term == "See Alpha" && group->terms[2].level == 2,
          "the child of the empty term was lost");
  require(geist::detail::verify_generated_toc_index_topic_ir({record}, nullptr,
                                                             *topic, &error),
          "the extracted index failed its own verifier: " + error);

  geist::detail::TopicIdentityIR identity;
  identity.id = "INDEX";
  identity.title = "Index";
  identity.start_logical_record = 41;
  identity.end_logical_record = 41;
  const auto document =
      geist::detail::lower_generated_toc_index_topic_to_document_ir(
          identity, *topic, &error);
  require(document.has_value(),
          "the index with a textless entry did not lower: " + error);
  if (!document) return;
  const geist::detail::ListBlockIR *list = nullptr;
  for (const auto &block : document->blocks)
    if (const auto *candidate = std::get_if<geist::detail::ListBlockIR>(
            &block.node))
      list = candidate;
  require(list != nullptr && list->items.size() == 3,
          "the lowered index does not carry one item per term");
  if (list == nullptr || list->items.size() != 3) return;
  require(list->items[1].content.empty() && list->items[1].empty_content,
          "the textless entry was given words the line does not carry, or "
          "did not declare itself empty");
  require(list->items[1].depth == 0 && list->items[2].depth == 1,
          "the child no longer hangs under the textless entry");
  require(geist::detail::verify_document_ir(*document, &error),
          "the lowered document failed verification: " + error);
}

// Fail closed: an empty *rendering* is not an empty field.  A term of spaces
// is what a misdeclared delimiter produces, and a textless term that carries
// a target would be a link with no label.
void an_unproven_empty_term_still_declines() {
  {
    std::vector<TokenWords> spaced;
    spaced.push_back(words("citerm"));
    spaced.push_back({sentinel_glyph, ' ', ' '});
    spaced.push_back({1, sentinel_glyph, '1'});
    std::string error;
    const auto topic = geist::detail::extract_generated_toc_index_topic_ir(
        {index_record({spaced})}, nullptr, &error);
    require(!topic.has_value(),
            "a term field of spaces was admitted as a textless term");
    require(error.find("CITERM term field carries no term text") !=
                std::string::npos,
            "a spacing-only term rejected for the wrong reason: " + error);
  }
  {
    std::string error;
    const auto topic = geist::detail::extract_generated_toc_index_topic_ir(
        {index_record({index_term("", "1", "TOPICA")})}, nullptr, &error);
    require(!topic.has_value(),
            "a textless term carrying a target was admitted");
    require(error.find("carries a target") != std::string::npos,
            "a textless term with a target rejected for the wrong reason: " +
                error);
  }
}

// An `:INDEX` envelope with the body lines given verbatim: no `cgpsep` is
// added, so a test decides where (and whether) the groups start.
DecodedLogicalRecordSource index_body(
    const std::vector<std::vector<TokenWords>> &body) {
  RecordBuilder builder;
  const TokenWords bullet{bullet_glyph};
  builder.line(bullet, {words("SHINDEX")});
  builder.line(bullet, {words("chdlevel"), words(":INDEX")});
  builder.line(bullet, {words("ST"), words("Index")});
  builder.line(bullet, {words("cidelm"), TokenWords{sentinel_glyph}});
  for (const auto &line : body) builder.line(bullet, line);
  return builder.build(42);
}

std::vector<TokenWords> group_line(const std::string &label) {
  TokenWords field{sentinel_glyph};
  for (const auto ch : label) field.push_back(static_cast<std::uint16_t>(ch));
  return {words("cgpsep"), std::move(field)};
}

std::optional<geist::detail::DocumentIR> lower(
    const DecodedLogicalRecordSource &record,
    const geist::detail::GeneratedTocIndexTopicIR &topic) {
  geist::detail::TopicIdentityIR identity;
  identity.id = "INDEX";
  identity.title = "Index";
  identity.start_logical_record = record.logical_record;
  identity.end_logical_record = record.logical_record;
  std::string error;
  auto document = geist::detail::lower_generated_toc_index_topic_to_document_ir(
      identity, topic, &error);
  require(document.has_value(), "the index did not lower: " + error);
  if (document)
    require(geist::detail::verify_document_ir(*document, &error),
            "the lowered index failed verification: " + error);
  return document;
}

std::size_t count_headings(const geist::detail::DocumentIR &document) {
  std::size_t headings = 0;
  for (const auto &block : document.blocks)
    if (std::holds_alternative<geist::detail::HeadingBlockIR>(block.node))
      ++headings;
  return headings;
}

// SC41-5612-02 INDEX writes two terms before its first `cgpsep`; hosted
// serves them above the first divider with no divider of their own.
void terms_before_the_first_group_stand_without_a_divider() {
  const auto record = index_body({index_term("' (apostrophe)", "1", "2.3.1"),
                                  index_term("- (subtraction)", "1", "2.15.3"),
                                  group_line("Special Characters"),
                                  index_term("/ (divide)", "1", "2.15.3"),
                                  {words("cendindex")}});
  std::string error;
  const auto topic = geist::detail::extract_generated_toc_index_topic_ir(
      {record}, nullptr, &error);
  require(topic.has_value(),
          "terms before the first CGPSEP were rejected: " + error);
  if (!topic) return;
  require(topic->groups.size() == 2 && !topic->groups[0].separated &&
              topic->groups[0].label.empty() &&
              topic->groups[0].terms.size() == 2 &&
              topic->groups[1].separated &&
              topic->groups[1].label == "Special Characters" &&
              topic->groups[1].terms.size() == 1,
          "the leading terms did not form one unlabelled group");
  require(geist::detail::verify_generated_toc_index_topic_ir({record}, nullptr,
                                                             *topic, &error),
          "the index failed its own verifier: " + error);
  const auto document = lower(record, *topic);
  if (!document) return;
  // The title and the one real divider, nothing for the leading group.
  require(count_headings(*document) == 2,
          "the leading group was given a divider heading");
}

// eyua1b01 INDEX is `cidelm` straight followed by `cendindex`; BookServer
// serves such an index as its heading over an empty body (SC33-0668-02).
void an_index_with_no_terms_is_its_heading() {
  const auto record = index_body({{words("cendindex")}});
  std::string error;
  const auto topic = geist::detail::extract_generated_toc_index_topic_ir(
      {record}, nullptr, &error);
  require(topic.has_value(), "an empty index was rejected: " + error);
  if (!topic) return;
  require(topic->groups.empty(), "an empty index grew a group");
  const auto document = lower(record, *topic);
  if (document)
    require(count_headings(*document) == 1 && document->blocks.size() == 1,
            "an empty index lowered to more than its heading");

  // Without its `cendindex` the body is not proven complete.
  const auto open = index_body({});
  require(!geist::detail::extract_generated_toc_index_topic_ir({open}, nullptr,
                                                               &error),
          "an index body with no CENDINDEX was admitted as empty");
}

// A plain line after a term prints on that term's line (SH12-2230-00-de
// record 942: `citerm <D>RGZPFM (...)<D>1<D>` then `    Befehl  10.9`).
void a_plain_line_continues_the_term_before_it() {
  const auto record = index_body(
      {group_line("R"), index_term("RGZPFM (reorganize)", "1", ""),
       {words("    "), words("Befehl"), words("10.9")},
       index_term("RMVDIRE", "1", "2.3.1"),
       {words("cendindex")},
       {words("   "), words("U."), words("S."), words("A.")}});
  std::string error;
  const auto topic = geist::detail::extract_generated_toc_index_topic_ir(
      {record}, nullptr, &error);
  require(topic.has_value(),
          "a plain text line inside an index was rejected: " + error);
  if (!topic) return;
  const auto *group = only_group(*topic);
  require(group != nullptr && group->terms.size() == 2,
          "the text line was not attached to a term");
  if (group == nullptr || group->terms.size() != 2) return;
  require(group->terms[0].continuations.size() == 1 &&
              group->terms[0].continuations[0].text.find("Befehl") == 0 &&
              group->terms[0].continuations[0].text.find("10.9") !=
                  std::string::npos &&
              group->terms[0].targets.empty(),
          "the continuation line did not stay plain text on its term");
  require(group->terms[1].continuations.size() == 1 &&
              group->terms[1].continuations[0].text.find("U.") == 0,
          "the line after CENDINDEX did not attach to the last term");
  const auto document = lower(record, *topic);
  if (!document) return;
  const auto rendered = geist::detail::format_document_ir(*document);
  require(rendered.find("Befehl") != std::string::npos,
          "the lowered index lost the continuation text");

  // A line that opens with a word is an unknown control, not text.
  const auto control = index_body(
      {group_line("R"), index_term("RGZPFM", "1", ""),
       {words("cfoo"), words("bar")}, {words("cendindex")}});
  require(!geist::detail::extract_generated_toc_index_topic_ir({control},
                                                               nullptr, &error),
          "an unknown control line was admitted as index text");
  // And a text line with no term before it has nothing to continue.
  const auto orphan = index_body(
      {{words("    "), words("orphan")}, {words("cendindex")}});
  require(!geist::detail::extract_generated_toc_index_topic_ir({orphan},
                                                               nullptr, &error),
          "a text line with no term before it was admitted");
}

// Index targets are topic ids, and those may contain `/` and parentheses.
void a_target_may_name_a_slashed_or_parenthesized_topic() {
  const auto record = index_body(
      {group_line("G"), index_term("parameters", "1", "GENDCESTB/SRCMBR"),
       index_term("abend code 1064", "1", "ISM(VM)"), {words("cendindex")}});
  std::string error;
  const auto topic = geist::detail::extract_generated_toc_index_topic_ir(
      {record}, nullptr, &error);
  require(topic.has_value(),
          "a slashed or parenthesized target was rejected: " + error);
  if (!topic) return;
  const auto *group = only_group(*topic);
  require(group != nullptr && group->terms.size() == 2 &&
              group->terms[0].targets.size() == 1 &&
              group->terms[0].targets[0].topic_id == "GENDCESTB/SRCMBR" &&
              group->terms[1].targets[0].topic_id == "ISM(VM)",
          "the target ids were not kept as written");
  const auto quoted = index_body(
      {group_line("G"), index_term("x", "1", "A'B"), {words("cendindex")}});
  require(!geist::detail::extract_generated_toc_index_topic_ir({quoted},
                                                               nullptr, &error),
          "a target with a quote was admitted");
}

// A paragraph opened after `cendindex` and never filled draws nothing
// (SC33-6733-01 INDEX: `cz FLOW P 3 3`); before the end it still declines.
void an_empty_trailing_paragraph_after_the_index_draws_nothing() {
  const auto cz_flow = std::vector<TokenWords>{
      words("cz"), words("FLOW"), words("P"), words("3"), words("3")};
  const auto after = index_body({group_line("Y"),
                                 index_term("year", "1", "4.4"),
                                 {words("cendindex")}, cz_flow});
  std::string error;
  const auto admitted = geist::detail::extract_generated_toc_index_topic_ir(
      {after}, nullptr, &error);
  require(admitted.has_value(),
          "an empty paragraph after CENDINDEX was rejected: " + error);
  const auto inside = index_body({group_line("Y"), cz_flow,
                                  index_term("year", "1", "4.4"),
                                  {words("cendindex")}});
  require(!geist::detail::extract_generated_toc_index_topic_ir({inside},
                                                               nullptr, &error),
          "a paragraph opened inside the index body was admitted");
}

} // namespace

int main() {
  the_bullet_in_the_sentinel_slot_is_a_length_byte();
  a_term_the_book_wrote_empty_keeps_its_children();
  an_unproven_empty_term_still_declines();
  terms_before_the_first_group_stand_without_a_divider();
  an_index_with_no_terms_is_its_heading();
  a_plain_line_continues_the_term_before_it();
  a_target_may_name_a_slashed_or_parenthesized_topic();
  an_empty_trailing_paragraph_after_the_index_draws_nothing();
  return 0;
}

