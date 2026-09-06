// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The BUILD 1.3 artwork envelope around a picture figure.
//
// Books built by BookManager BUILD 1.3 (SG24-4815-01 `ez30ad01.boo`, build
// date 11/08/96, and the other AS/400 redbooks of its shelf) wrap every
// picture figure in five controls the earlier compilers never wrote.  Topic
// `1.1`, record 25, in decoded order:
//
//   SRFIG0372WOR                      figure anchor
//   csart                             artwork opens
//   cselect 3 9 PIC1     PICTURE 1    the picture and its placeholder
//   ceart                             artwork closes
//   csartdesc 1                       description of picture 1 opens
//   SRPIC1                            the picture's own anchor
//   cartdesc                          a blank description line
//   cartdesc   This graphic shows all the current countries in the world and
//   cartdesc   indicates by different shades ...
//   ceartdesc                         description closes; the caption follows
//      Figure 1. Internet World Map
//   SREFIG
//
// `csart` .. `ceart` and `csartdesc` .. `ceartdesc` are BookMaster's
// `:artwork` and `:artdesc` .. `:eartdesc`: the description is what a reader
// that cannot show the picture shows instead.  Before the envelope was
// understood, `SRPIC1` was a structural control inside the region and every
// such figure declined, which sent 42 of the book's 186 topics to the
// verbatim best-effort route with `csart` and `cartdesc` printed as text.
//
// The model admits the envelope as one picture figure: the `SRPIC<n>` anchor
// opens the picture, the `cartdesc` lines are the figure's description and
// become the image's alternative text, and the envelope's boundaries own
// their opcode cells and nothing else.  ez302400 `3.3` record 211 writes the
// same envelope with an empty description (`csartdesc 1`, `SRPIC1`,
// `ceartdesc`).  An envelope that names a picture other than the region's
// still fails closed.
//
// An uncaptioned picture closes its figure *before* its description, in
// three shapes the compiler writes (`Region` in figure_block_ir.cpp):
//
//   SC21-8295-03 `1.1` record 46      cartdesc REQTEXT / SREFIG / ceartdesc
//   DFHP7A00 `3.2.1` records 569-570  cartdesc ... / SREFIG / cartdesc ... /
//                                     ceartdesc
//   QB3AWG04 `APPENDIX1.2.1.1` r. 628 ceart / SREFIG / csartdesc 2 / SRPIC2 /
//                                     ceartdesc
//
// The description belongs to the picture it names wherever the marker falls
// in it, so the region owns the envelope's tail and the topic stays typed.
// Read as prose, `ceartdesc` was a body control outside the prose model and
// the whole topic went to the verbatim route.
//
// Everything here is synthetic: the test builds whole topic records by hand
// with `assemble_logical_record_with_sources` and opens no book, because
// `libgeist` may depend on no book but `packet.boo` (issue #59) and none of
// the BUILD 1.3 books is it.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "geist/detail/lowering/figure_document_lowering.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "art_description_figure_synthetic: " << message << '\n';
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

  // The eight metadata controls and the `ST` title every prose topic opens
  // with.
  void heading(const std::string &id, const std::string &title_word) {
    line({words("sh" + id)});
    line({words("ctopicn"), words("10")});
    line({words("cparent"), words("1.0")});
    line({words("cforwardlevel"), words("1.2")});
    line({words("cbacklevel"), words("1.0")});
    line({words("csummary"), words("26"), words("0"), words("26")});
    line({words("chdlevel"), words(":H2")});
    line({words("csourcefn"), words("0372VIEW")});
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
  // Picture 1 is in the book's resource catalog, as it is in every BUILD
  // 1.3 book that draws it.
  const std::set<std::string> resource_ids{"1", "2"};
  result.topic = geist::detail::extract_prose_topic_ir(
      result.records, layout, *ownership, title, nullptr, &result.error,
      &resource_ids);
  if (result.topic) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(
                result.records, layout, *ownership, title, nullptr,
                *result.topic, &verify_error, &resource_ids),
            "the extracted topic failed its own verifier: " + verify_error);
  }
  return result;
}

enum class Description { described, empty };

// SG24-4815-01 `1.1` record 25, reduced to the envelope.  `described`
// carries the two description lines and the blank one before them;
// `empty` is the ez302400 `3.3` shape.  `described_picture` is the operand
// of `csartdesc`, which names the picture the description belongs to.
DecodedLogicalRecordSource internet_topic(Description description,
                                          const std::string &described_picture) {
  RecordBuilder builder;
  builder.heading("1.1", "Internet");
  builder.line({words("SRFIGFIGUNIQ1")});
  builder.line({words("csart")});
  builder.line({words("cselect"), words("3"), words("9"), words("PIC1")});
  builder.line({words("   "), words("PICTURE"), words("1")});
  builder.line({words("ceart")});
  builder.line({words("csartdesc"), words(described_picture)});
  builder.line({words("SRPIC1")});
  if (description == Description::described) {
    builder.line({words("cartdesc")});
    builder.line({words("cartdesc"), words("  "), words("This"),
                  words("graphic"), words("shows"), words("the"),
                  words("world.")});
    builder.line({words("cartdesc"), words("  "), words("White"), words("is"),
                  words("full"), words("access.")});
  }
  builder.line({words("ceartdesc")});
  builder.line({words("   "), words("Figure"), words("1."), words("Internet"),
                words("World"), words("Map")});
  builder.line({words("SREFIG")});
  return builder.build(25);
}

// Where the SREFIG marker falls in an uncaptioned picture's description.
enum class Tail {
  after_description,   // SC21-8295-03: SREFIG, ceartdesc
  inside_description,  // DFHP7A00: SREFIG between two cartdesc lines
  before_description,  // QB3AWG04: SREFIG, csartdesc, SRPIC, ceartdesc
};

// The uncaptioned shape: `cz OFF FIG` .. `cz OFF EFIG` around the figure as
// every BUILD 1.3 book writes it, and no caption line.
DecodedLogicalRecordSource uncaptioned_topic(Tail tail) {
  RecordBuilder builder;
  builder.heading("1.1", "Configuration");
  builder.line({words("SRFIGFIGUNIQ1")});
  builder.line({words("cz"), words("OFF"), words("FIG")});
  builder.line({words("csart")});
  builder.line({words("cselect"), words("3"), words("9"), words("PIC1")});
  builder.line({words("   "), words("PICTURE"), words("1")});
  builder.line({words("ceart")});
  if (tail == Tail::before_description) {
    builder.line({words("SREFIG")});
    builder.line({words("csartdesc"), words("1")});
    builder.line({words("SRPIC1")});
    builder.line({words("ceartdesc")});
  } else {
    builder.line({words("csartdesc"), words("1")});
    builder.line({words("SRPIC1")});
    builder.line({words("cartdesc"), words("  "), words("The"), words("first"),
                  words("line.")});
    builder.line({words("SREFIG")});
    if (tail == Tail::inside_description)
      builder.line({words("cartdesc"), words("  "), words("The"),
                    words("second."), words("")});
    builder.line({words("ceartdesc")});
  }
  builder.line({words("cz"), words("OFF"), words("EFIG"), words("0"),
                words("0")});
  return builder.build(46);
}

// SC21-8295-03 `A.0` record 717, reduced: two pictures under one caption,
// each in its own envelope.
DecodedLogicalRecordSource two_picture_topic() {
  RecordBuilder builder;
  builder.heading("A.0", "Overview");
  builder.line({words("SRFIGFIGUNIQ1")});
  builder.line({words("cz"), words("OFF"), words("FIG")});
  for (const auto *n : {"1", "2"}) {
    builder.line({words("csart")});
    builder.line({words("cselect"), words("3"), words("9"),
                  words(std::string("PIC") + n)});
    builder.line({words("   "), words("PICTURE"), words(n)});
    builder.line({words("ceart")});
    builder.line({words("csartdesc"), words(n)});
    builder.line({words(std::string("SRPIC") + n)});
    builder.line({words("cartdesc"), words("  "), words("Picture"),
                  words(std::string(n) + ".")});
    builder.line({words("ceartdesc")});
  }
  builder.line({words("   "), words("Figure"), words("1."), words("Internet"),
                words("World"), words("Map")});
  builder.line({words("SREFIG")});
  builder.line({words("cz"), words("OFF"), words("EFIG"), words("0"),
                words("0")});
  return builder.build(717);
}

// DFHPA608 `1.2.6` record 137, reduced: the description carries a cross
// reference as an escaped `cvselect` line over the text line after it.
DecodedLogicalRecordSource escaped_selector_topic() {
  RecordBuilder builder;
  builder.heading("1.1", "Internet");
  builder.line({words("SRFIGFIGUNIQ1")});
  builder.line({words("csart")});
  builder.line({words("cselect"), words("3"), words("9"), words("PIC1")});
  builder.line({words("   "), words("PICTURE"), words("1")});
  builder.line({words("ceart")});
  builder.line({words("csartdesc"), words("1")});
  builder.line({words("SRPIC1")});
  builder.line({words("cartdesc"), words("cvselect"), words("13"), words("8"),
                words("FIGDSTAB")});
  builder.line({words("cartdesc"), words("  "), words("Figure"), words("7"),
                words("shows"), words("sharing.")});
  builder.line({words("ceartdesc")});
  builder.line({words("   "), words("Figure"), words("1."), words("Internet"),
                words("World"), words("Map")});
  builder.line({words("SREFIG")});
  return builder.build(137);
}

const geist::detail::FigureSourceBlockIR *
the_figure(const Extracted &extracted, const std::string &label,
           const std::optional<std::string> &expected_caption =
               std::string("Figure 1. Internet World Map")) {
  require(extracted.topic.has_value(),
          label + ": the topic was rejected: " + extracted.error);
  if (!extracted.topic) return nullptr;
  const auto &figures = extracted.topic->figures.blocks;
  require(figures.size() == 1,
          label + ": the envelope did not become one figure block; the topic "
                  "has " +
              std::to_string(figures.size()) + " and declined " +
              std::to_string(extracted.topic->figures.declined.size()));
  if (figures.size() != 1) return nullptr;
  const auto &figure = figures.front();
  require(figure.body_kind == geist::detail::FigureBodyKindIR::picture,
          label + ": the figure is not a picture figure");
  require(figure.target_kind ==
                  geist::detail::FigureTargetKindIR::book_resource &&
              figure.target == "1",
          label + ": the figure does not draw picture 1: '" + figure.target +
              "'");
  require(figure.placeholder_text == "PICTURE 1",
          label + ": placeholder changed: '" + figure.placeholder_text + "'");
  if (expected_caption)
    require(figure.caption && figure.caption->text == *expected_caption,
            label + ": caption changed: '" +
                (figure.caption ? figure.caption->text : "") + "'");
  else
    require(!figure.caption,
            label + ": an uncaptioned figure grew a caption: '" +
                figure.caption->text + "'");
  require(!figure.spot_anchors.empty() &&
              figure.spot_anchors.front().id == "PIC1" &&
              figure.spot_anchors.front().at_body_start,
          label + ": the SRPIC1 anchor was not admitted as the picture's "
                  "anchor");
  // The envelope's opcodes are controls; nothing of it is displayed.
  for (const auto &cell : figure.cells)
    require(cell.role != geist::detail::FigureCellRoleIR::body_content &&
                cell.role != geist::detail::FigureCellRoleIR::caption_content ||
                cell.word != 'c',
            label + ": an envelope opcode leaked into displayed content");
  return &figure;
}

// The lowering puts the picture's anchor in front of the image and the
// description on the image.
void check_lowering(const geist::detail::FigureSourceBlockIR &figure,
                    const std::string &expected_description,
                    const std::string &label) {
  std::string error;
  const auto blocks =
      geist::detail::lower_figure_block_to_document_blocks(figure, &error);
  require(blocks.has_value(), label + ": lowering failed: " + error);
  if (!blocks) return;
  require(blocks->size() == 3,
          label + ": expected anchor, picture anchor and figure, got " +
              std::to_string(blocks->size()) + " blocks");
  if (blocks->size() != 3) return;
  const auto *figure_anchor =
      std::get_if<geist::detail::AnchorBlockIR>(&(*blocks)[0].node);
  const auto *picture_anchor =
      std::get_if<geist::detail::AnchorBlockIR>(&(*blocks)[1].node);
  const auto *image =
      std::get_if<geist::detail::FigureBlockIR>(&(*blocks)[2].node);
  require(figure_anchor != nullptr && figure_anchor->id == "FIGFIGUNIQ1",
          label + ": the figure anchor is not first");
  require(picture_anchor != nullptr && picture_anchor->id == "PIC1" &&
              (*blocks)[1].origin.fidelity ==
                  geist::detail::DocumentFidelityIR::typed,
          label + ": the picture anchor is not an exact block before the "
                  "image");
  require(image != nullptr && image->resource == "resource:1",
          label + ": the image block does not draw resource 1");
  require(image != nullptr && image->description == expected_description,
          label + ": the image description is '" +
              (image ? image->description : "") + "'");
  require(geist::detail::verify_figure_document_blocks(figure, *blocks,
                                                       &error),
          label + ": the lowering failed its own verifier: " + error);
}

void a_described_picture_is_one_figure_with_its_description() {
  const std::string label = "described";
  auto extracted = run(internet_topic(Description::described, "1"),
                       "Internet");
  const auto *figure = the_figure(extracted, label);
  if (figure == nullptr) return;
  require(figure->description ==
              "This graphic shows the world. White is full access.",
          label + ": description changed: '" + figure->description + "'");
  // The description lines are the figure's, not prose of the topic.
  require(extracted.topic->blocks.empty(),
          label + ": the description leaked into prose blocks: the topic "
                  "has " +
              std::to_string(extracted.topic->blocks.size()));
  check_lowering(*figure, "This graphic shows the world. White is full access.",
                 label);
}

void an_empty_description_is_still_the_picture() {
  const std::string label = "empty";
  auto extracted = run(internet_topic(Description::empty, "1"), "Internet");
  const auto *figure = the_figure(extracted, label);
  if (figure == nullptr) return;
  require(figure->description.empty(),
          label + ": an empty envelope produced a description: '" +
              figure->description + "'");
  check_lowering(*figure, "", label);
}

// `csartdesc 2` describes a picture the region does not draw.
void a_description_of_another_picture_fails_closed() {
  auto extracted = run(internet_topic(Description::described, "2"),
                       "Internet");
  require(!extracted.topic.has_value(),
          "an envelope describing picture 2 around picture 1 was admitted; "
          "it must fail closed");
  require(extracted.error.find("different picture") != std::string::npos,
          "the decline does not name the mismatch: " + extracted.error);
}

// The marker falls after, inside, or before the description: the region
// owns the envelope's tail and the description is still the picture's.
void an_uncaptioned_picture_owns_the_envelope_behind_its_marker() {
  struct Case {
    Tail tail;
    const char *label;
    const char *description;
  };
  const Case cases[] = {
      {Tail::after_description, "tail-after", "The first line."},
      {Tail::inside_description, "tail-inside", "The first line. The second."},
      {Tail::before_description, "tail-before", ""},
  };
  for (const auto &item : cases) {
    const std::string label = item.label;
    auto extracted = run(uncaptioned_topic(item.tail), "Configuration");
    const auto *figure = the_figure(extracted, label, std::nullopt);
    if (figure == nullptr) continue;
    require(figure->description == item.description,
            label + ": description changed: '" + figure->description + "'");
    // The tail is the figure's: the span ends at `ceartdesc`, the last
    // segment of the envelope, not at the marker.
    const auto &record = extracted.records.front();
    const auto &end =
        record.control_segments[figure->span.end.segment_index];
    require(end.kind == geist::detail::BookControlKind::art_description_end,
            label + ": the figure's span ends at '" + end.opcode +
                "', not at ceartdesc");
    require(extracted.topic->blocks.empty(),
            label + ": the envelope tail leaked into prose blocks: the "
                    "topic has " +
                std::to_string(extracted.topic->blocks.size()));
    check_lowering(*figure, item.description, label);
  }
}

// Two pictures, two envelopes: each picture keeps its own description and
// its own anchor, in front of its own image.
void each_picture_of_a_figure_keeps_its_own_envelope() {
  const std::string label = "two-pictures";
  auto extracted = run(two_picture_topic(), "Overview");
  const auto *figure = the_figure(extracted, label);
  if (figure == nullptr) return;
  require(figure->description == "Picture 1.",
          label + ": first description changed: '" + figure->description +
              "'");
  require(figure->additional_pictures.size() == 1 &&
              figure->additional_pictures.front().target == "2" &&
              figure->additional_pictures.front().description == "Picture 2.",
          label + ": the second picture did not keep its description");
  require(extracted.topic->blocks.empty(),
          label + ": the envelopes leaked into prose blocks");
  std::string error;
  const auto blocks =
      geist::detail::lower_figure_block_to_document_blocks(*figure, &error);
  require(blocks.has_value(), label + ": lowering failed: " + error);
  if (!blocks) return;
  std::vector<std::string> shape;
  for (const auto &block : *blocks) {
    if (const auto *anchor =
            std::get_if<geist::detail::AnchorBlockIR>(&block.node))
      shape.push_back("#" + anchor->id);
    else if (const auto *image =
                 std::get_if<geist::detail::FigureBlockIR>(&block.node))
      shape.push_back(image->resource + "|" + image->description);
    else
      shape.push_back("?");
  }
  const std::vector<std::string> expected{
      "#FIGFIGUNIQ1", "#PIC1", "resource:1|Picture 1.", "#PIC2",
      "resource:2|Picture 2."};
  std::string actual;
  for (const auto &item : shape) actual += item + " ";
  require(shape == expected,
          label + ": the lowering is not anchor, PIC1, image 1, PIC2, image "
                  "2 but: " +
              actual);
  require(geist::detail::verify_figure_document_blocks(*figure, *blocks,
                                                       &error),
          label + ": the lowering failed its own verifier: " + error);
}

// The escaped `cvselect` line is the description's link, not its text.
void an_escaped_selector_line_is_not_description_text() {
  const std::string label = "escaped-selector";
  auto extracted = run(escaped_selector_topic(), "Internet");
  const auto *figure = the_figure(extracted, label);
  if (figure == nullptr) return;
  require(figure->description == "Figure 7 shows sharing.",
          label + ": description changed: '" + figure->description + "'");
  check_lowering(*figure, "Figure 7 shows sharing.", label);
}

}  // namespace

int main() {
  each_picture_of_a_figure_keeps_its_own_envelope();
  an_escaped_selector_line_is_not_description_text();
  a_described_picture_is_one_figure_with_its_description();
  an_empty_description_is_still_the_picture();
  a_description_of_another_picture_fails_closed();
  an_uncaptioned_picture_owns_the_envelope_behind_its_marker();
  return 0;
}
