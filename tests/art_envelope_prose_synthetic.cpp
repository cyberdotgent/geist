// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The BUILD 1.3 artwork envelope outside a figure region.
//
// `csart` .. `ceart` around a picture selector and `csartdesc <n>` ..
// `ceartdesc` around the `cartdesc` lines that describe picture <n> are not
// written only inside `SRFIG` .. `SREFIG` figures.  BUILD 1.3 books also put
// them in running text, two ways:
//
//   GA27-4106-02 D.0 (DT 19980408153335), between paragraphs:
//     csart / `cselect 3 11 PIC16` / `     PICTURE 16` / ceart /
//     csartdesc 16 / SRPIC16 / `cartdesc   REQTEXT` / ceartdesc /
//     `    Danger: Before you begin ...`
//   hosted: the image with `alt="   REQTEXT"`, then
//     `<a name="PIC16">    <B>Danger:</B> Before you begin ...`.
//   SC41-5101-01 D.2.1.1 (DT 19971120215738) writes the same inside a list
//   item, between `Press the Shift key with the` and `key. The Diacritic
//   mode symbol ...`.
//
//   SC41-5612-02 PREFACE.3 (DT 19980522111551), as its own region:
//     csart / `cz OFF ARTALT` / `cselect 3 9 PIC1` / `   PICTURE 1` / ceart /
//     csartdesc 1 / ceartdesc / `cz OFF EHP0 0 0`
//   hosted: the picture row alone in a `<pre width="80">`, `alt="PICTURE 1"`.
//
// The envelope controls own their opcode cells and the picture number, the
// `cartdesc` text is the picture's alternative text (never printed), and text
// the decoder glued behind a closer is the body that follows.  Before, every
// such topic declined ("body control csart is outside the prose model").
//
// Everything here is synthetic: the test builds the topic record by hand.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "geist/detail/lowering/prose_topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "art_envelope_prose_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

bool contains(const std::string &text, const std::string &needle) {
  return text.find(needle) != std::string::npos;
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;

  void line(std::vector<TokenWords> content) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(TokenWords{0x25BA});
    for (auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(std::move(token));
    }
  }

  void heading(const std::string &id, const std::string &title_word) {
    line({words("sh" + id)});
    line({words("ctopicn"), words("7")});
    line({words("cparent"), words("1.0")});
    line({words("cforwardlevel"), words("1.2")});
    line({words("cbacklevel"), words("1.0")});
    line({words("csummary"), words("9"), words("0"), words("9")});
    line({words("chdlevel"), words(":H2")});
    line({words("csourcefn"), words("RBAFZMST")});
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

struct Rendered {
  std::optional<geist::detail::ProseTopicIR> topic;
  std::string error;
  std::string html;
  std::string markdown;
};

Rendered render(DecodedLogicalRecordSource record, const std::string &title) {
  Rendered result;
  std::vector<DecodedLogicalRecordSource> records;
  records.push_back(std::move(record));
  require(records.front().ir.display_lines_parse,
          "the synthetic topic's display lines did not parse");
  const auto layout = geist::detail::extract_layout_ir(records);
  std::string error;
  const auto ownership =
      geist::detail::build_verified_ownership_ir(records, layout, &error);
  require(ownership.has_value(), "ownership is not verifiable: " + error);
  if (!ownership) return result;
  const std::set<std::string> resource_ids{"1", "53"};
  result.topic = geist::detail::extract_prose_topic_ir(
      records, layout, *ownership, title, nullptr, &result.error,
      &resource_ids);
  if (!result.topic) return result;
  require(geist::detail::verify_prose_topic_ir(records, layout, *ownership,
                                               title, nullptr, *result.topic,
                                               &error, &resource_ids),
          "the extracted topic failed its own verifier: " + error);
  const auto document = geist::detail::lower_prose_topic_to_document_ir(
      {"1.1", title}, *result.topic, &error);
  require(document.has_value(), "the topic did not lower: " + error);
  if (!document) return result;
  require(geist::detail::verify_prose_topic_document_ir(*result.topic,
                                                        *document, &error),
          "the lowered topic failed verification: " + error);
  result.html = geist::detail::render_document_html_blocks(*document, {});
  result.markdown = geist::detail::render_document_markdown(*document);
  return result;
}

// GA27-4106-02 D.0 and SC41-5101-01 D.2.1.1, reduced: the envelope between
// two runs of text.  `described` is the operand of `csartdesc`.
DecodedLogicalRecordSource running_text_topic(const std::string &described) {
  RecordBuilder builder;
  builder.heading("D.2.1.1", "Example");
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("Press"), words("the"), words("Shift"),
                words("key"), words("with"), words("the")});
  builder.line({words("csart")});
  builder.line({words("cselect"), words("8"), words("10"), words("PIC53")});
  builder.line({words("        "), words("PICTURE"), words("53")});
  builder.line({words("ceart")});
  builder.line({words("csartdesc"), words(described)});
  builder.line({words("SRPIC" + described)});
  builder.line({words("cartdesc"), words("       "), words("REQTEXT")});
  builder.line({words("ceartdesc")});
  builder.line({words("   "), words("key."), words("The"), words("symbol"),
                words("appears.")});
  return builder.build(361);
}

// SC41-5612-02 PREFACE.3, reduced.
DecodedLogicalRecordSource artalt_topic() {
  RecordBuilder builder;
  builder.heading("PREFACE.3", "Conventions");
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("The"), words("conventions"),
                words("apply:")});
  builder.line({words("csart")});
  builder.line({words("cz"), words("OFF"), words("ARTALT")});
  builder.line({words("cselect"), words("3"), words("9"), words("PIC1")});
  builder.line({words("   "), words("PICTURE"), words("1")});
  builder.line({words("ceart")});
  builder.line({words("csartdesc"), words("1")});
  builder.line({words("cartdesc"), words("   "), words("Mixed"),
                words("data.")});
  builder.line({words("ceartdesc")});
  builder.line({words("cz"), words("OFF"), words("EHP0"), words("0"),
                words("0")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("After"), words("the"), words("picture.")});
  return builder.build(35);
}

void an_envelope_in_running_text_describes_its_picture() {
  const auto rendered = render(running_text_topic("53"), "Example");
  require(rendered.topic.has_value(),
          "the envelope in running text declined the topic: " +
              rendered.error);
  if (!rendered.topic) return;
  require(rendered.topic->picture_descriptions.count("53") == 1 &&
              rendered.topic->picture_descriptions.at("53") == "REQTEXT",
          "the description of picture 53 was not collected");
  for (const auto *output : {&rendered.html, &rendered.markdown}) {
    require(contains(*output, "resource:53"),
            "the picture was not drawn: " + *output);
    require(contains(*output, "Press the Shift key with the") &&
                contains(*output, "The symbol appears"),
            "the text around the envelope was lost: " + *output);
    require(!contains(*output, "csart") && !contains(*output, "cartdesc") &&
                !contains(*output, "ceart"),
            "an envelope opcode was printed: " + *output);
  }
  require(contains(rendered.html, "alt=\"REQTEXT\"") &&
              contains(rendered.html, "id=\"PIC53\""),
          "the picture's description or anchor is missing: " + rendered.html);
  require(contains(rendered.markdown, "![REQTEXT]("),
          "the Markdown image does not carry the description: " +
              rendered.markdown);
}

void an_artalt_region_holds_its_picture() {
  const auto rendered = render(artalt_topic(), "Conventions");
  require(rendered.topic.has_value(),
          "the cz OFF ARTALT region declined the topic: " + rendered.error);
  if (!rendered.topic) return;
  require(contains(rendered.html, "resource:1") &&
              contains(rendered.html, "alt=\"Mixed data.\""),
          "the region's picture or its description is missing: " +
              rendered.html);
  require(contains(rendered.html, "The conventions apply:") &&
              contains(rendered.html, "After the picture."),
          "the text around the region was lost: " + rendered.html);
  require(!contains(rendered.html, "ARTALT") &&
              !contains(rendered.html, "EHP0"),
          "a region directive was printed: " + rendered.html);
}

void a_description_of_a_picture_not_drawn_fails_closed() {
  const auto rendered = render(running_text_topic("54"), "Example");
  require(!rendered.topic.has_value(),
          "a description of a picture the topic does not draw was admitted");
}

} // namespace

int main() {
  an_envelope_in_running_text_describes_its_picture();
  an_artalt_region_holds_its_picture();
  a_description_of_a_picture_not_drawn_fails_closed();
  geist_test::exit_with_failures();
  std::cout << "art_envelope_prose_synthetic: ok\n";
  return 0;
}
