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

#include "geist/detail/display_lines.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/prose_topic_ir.hpp"
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
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(TokenWords{0x25BA});
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

} // namespace

int main() {
  a_control_shaped_word_after_the_origin_run_is_display_text();
  a_control_opening_its_display_line_stays_a_control();
  a_font_span_on_the_bullet_is_dropped_and_the_row_kept();
  a_span_reaching_from_the_bullet_into_the_text_still_fails();
  return 0;
}
