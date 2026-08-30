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

#include "geist/detail/display_lines.hpp"
#include "geist/detail/document_ir.hpp"
#include "geist/detail/generated_toc_index_document_lowering.hpp"
#include "geist/detail/generated_toc_index_ir.hpp"
#include "geist/detail/internal.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
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

} // namespace

int main() {
  the_bullet_in_the_sentinel_slot_is_a_length_byte();
  a_term_the_book_wrote_empty_keeps_its_children();
  an_unproven_empty_term_still_declines();
  return 0;
}
