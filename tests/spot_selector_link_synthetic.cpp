// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A `LNK <SPT>` selector is a link to a spot inside another book.
//
// CICS books reference a `SRSPT<id>` destination in a sibling book with the
// six-alternative tuple `<SPT> <anchor> <> <document number> <level>
// <target>`.  GC33-1162-02 (`dfhle513.boo`) topic `1.6.9.1` record 250 stores
// `cselect 48 28 LNK <SPT> <XDLIPRE> <> <SC33-1165> <00> <DFHA300>` over
// `CICS/ESA Customization Guide`, and hosted BookServer (`GC33-116`, DT
// 19990317155853) serves that phrase as
// `<a href="../../DOCNUM/SC33-1165/SPTXDLIPRE?ScrollTOP=SPTXDLIPRE#SPTXDLIPRE?DocnumLevel=00">`:
// the same `DOCNUM/<document number>/` address an `<HDR>` link uses, with
// the anchor prefixed `SPT` instead of `HDR`.
//
// Like every cross-book reference a single book cannot resolve it, so the
// renderer hands it to the consumer's `resolve_cross_book` hook with the
// spot anchor named, and otherwise marks it unresolved.
//
// Everything here is synthetic: the test builds its own record and opens no
// book.

#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "geist/detail/ir/selector_link_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/lowering/prose_topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::CrossReferenceTargetKindIR;
using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "spot_selector_link_synthetic: " << message << '\n';
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

const char *const topic_title = "Exits";

// A CZ paragraph whose second row starts with a phrase selected by an
// `<SPT>` link: `cselect 3 13 LNK <SPT> <XDLIPRE> <> <SC33-1165> <00>
// <DFHA300>` over `Customization` .. `Guide`.
DecodedLogicalRecordSource spot_topic(const std::vector<std::string> &tuple,
                                      bool latin1_text = false) {
  RecordBuilder builder;
  builder.line({words("sh1.6.9.1")});
  builder.line({words("ctopicn"), words("7")});
  builder.line({words("cparent"), words("1.6.9")});
  builder.line({words("cforwardlevel"), words("1.6.9.2")});
  builder.line({words("cbacklevel"), words("1.6.9")});
  builder.line({words("csummary"), words("9"), words("0"), words("9")});
  builder.line({words("chdlevel"), words(":H4")});
  builder.line({words("csourcefn"), words("DFHLE5A1")});
  builder.line({words("ST"), words("Exits")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  builder.line({words("   "), words("Programming"), words("information"),
                words("is"), words("in"), words("the")});
  // A Latin-1 word decodes to two bytes but stays one word: the control
  // ranges after it are byte ranges, the token map is in words.
  if (latin1_text)
    builder.line({words("   "), TokenWords{'f', 0xFC, 'r'},
                  TokenWords{'H', 0xE4, 'n', 'd', 'e'}, words("und")});
  std::vector<TokenWords> selector{words("cselect"), words("3"),
                                   words("19"), words("LNK")};
  for (const auto &alternative : tuple) selector.push_back(words(alternative));
  builder.line(std::move(selector));
  builder.line({words("   "), words("Customization"), words("Guide.")});
  return builder.build(250);
}

const std::vector<std::string> spot_tuple{"<SPT>",       "<XDLIPRE>", "<>",
                                          "<SC33-1165>", "<00>",
                                          "<DFHA300>"};

struct Extracted {
  std::vector<DecodedLogicalRecordSource> sources;
  std::optional<geist::detail::ProseTopicIR> prose;
  std::string error;
};

Extracted extract(const std::vector<std::string> &tuple,
                  bool latin1_text = false) {
  Extracted out;
  out.sources.push_back(spot_topic(tuple, latin1_text));
  const auto layout = geist::detail::extract_layout_ir(out.sources);
  const auto ownership = geist::detail::build_verified_ownership_ir(
      out.sources, layout, &out.error);
  require(ownership.has_value(),
          "the synthetic topic's ownership is not verifiable: " + out.error);
  if (!ownership) return out;
  out.prose = geist::detail::extract_prose_topic_ir(
      out.sources, layout, *ownership, topic_title, nullptr, &out.error);
  if (out.prose) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(
                out.sources, layout, *ownership, topic_title, nullptr,
                *out.prose, &verify_error),
            "the spot-link topic failed its own verifier: " + verify_error);
  }
  return out;
}

void the_tuple_parses_to_a_book_spot() {
  std::string error;
  const auto link = geist::detail::parse_selector_link(spot_tuple, &error);
  require(link.has_value(), "the <SPT> tuple was rejected: " + error);
  if (!link) return;
  require(link->kind == geist::detail::SelectorLinkKindIR::book_spot,
          "the <SPT> tuple is not a book spot");
  require(link->destination == "DOCNUM/SC33-1165/SPTXDLIPRE",
          "the <SPT> destination is '" + link->destination + "'");
  require(link->document_level == "00",
          "the <SPT> level is '" + link->document_level + "'");
}

void a_spot_tuple_without_its_anchor_or_book_fails_closed() {
  std::string error;
  require(!geist::detail::parse_selector_link(
              {"<SPT>", "<>", "<>", "<SC33-1165>", "<00>", "<DFHA300>"},
              &error),
          "an <SPT> tuple naming no anchor was admitted");
  require(!geist::detail::parse_selector_link(
              {"<SPT>", "<XDLIPRE>", "<>", "<>", "<00>", "<DFHA300>"},
              &error),
          "an <SPT> tuple naming no document number was admitted");
}

void a_spot_selector_links_its_phrase_to_the_other_book() {
  const auto extracted = extract(spot_tuple);
  require(extracted.prose.has_value(),
          "the <SPT> selector topic was rejected: " + extracted.error);
  if (!extracted.prose) return;

  geist::detail::TopicIdentityIR identity;
  identity.id = "1.6.9.1";
  identity.title = topic_title;
  identity.heading_level = 4;
  identity.start_logical_record = 250;
  identity.end_logical_record = 250;
  std::string error;
  const auto document = geist::detail::lower_prose_topic_to_document_ir(
      identity, *extracted.prose, &error);
  require(document.has_value(), "the spot-link topic did not lower: " + error);
  if (!document) return;
  require(geist::detail::verify_document_ir(*document, &error),
          "the spot-link document did not verify: " + error);

  std::vector<geist::HtmlCrossBookReference> asked;
  geist::HtmlRenderOptions options;
  options.resolve_cross_book =
      [&asked](const geist::HtmlCrossBookReference &reference)
      -> std::optional<std::string> {
    asked.push_back(reference);
    return "/shelf/SC33-1165/SPT" + reference.spot_anchor;
  };
  const auto html = geist::detail::render_document_html_blocks(*document,
                                                               options);
  require(asked.size() == 1, "the renderer asked " +
                                 std::to_string(asked.size()) +
                                 " cross-book questions, not one");
  if (asked.size() == 1) {
    require(asked.front().document_number == "SC33-1165" &&
                asked.front().document_level == "00" &&
                asked.front().spot_anchor == "XDLIPRE" &&
                asked.front().heading_anchor.empty() &&
                asked.front().target == "DFHA300",
            "the cross-book reference does not name the spot");
  }
  require(contains(html, "href=\"/shelf/SC33-1165/SPTXDLIPRE\"") &&
              contains(html, "Customization Guide"),
          "the resolved spot link is not on its phrase: " + html);

  const auto unresolved =
      geist::detail::render_document_html_blocks(*document, {});
  require(contains(unresolved, "geist-link--unresolved"),
          "an unresolved spot link is not marked unresolved: " + unresolved);
  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown, "Customization Guide") &&
              !contains(markdown, "XDLIPRE") && !contains(markdown, "<SPT>"),
          "the Markdown spot link lost its phrase or leaked its tuple: " +
              markdown);
}

// SH12-2230-00-de 1.2.1 record 32: German text with umlauts precedes
// `cselect 70 3 LNK <BOOK> <> <> <SH12-2227> <> <0695>`.  Each umlaut is one
// word but two decoded bytes, and reading the selector's byte range as a word
// range swallowed `<BOOK>` into the operands, leaving five alternatives.
void latin1_text_before_the_selector_keeps_all_alternatives() {
  const auto record = spot_topic(spot_tuple, true);
  const geist::detail::ControlSegmentIR *selector = nullptr;
  for (const auto &segment : record.control_segments)
    if (segment.kind == geist::detail::BookControlKind::select)
      selector = &segment;
  require(selector != nullptr, "the synthetic selector was not decoded");
  if (selector == nullptr) return;
  std::vector<std::string> alternatives;
  const auto tokens = geist::detail::selector_link_alternative_tokens(
      record, *selector, &alternatives);
  require(alternatives == spot_tuple,
          "the alternatives after Latin-1 text are " +
              std::to_string(alternatives.size()) + ", not the six stored");
  require(tokens.size() == spot_tuple.size(),
          "the alternatives claimed the wrong tokens");
  const auto extracted = extract(spot_tuple, true);
  require(extracted.prose.has_value(),
          "the selector after Latin-1 text was rejected: " + extracted.error);
}

} // namespace

int main() {
  latin1_text_before_the_selector_keeps_all_alternatives();
  the_tuple_parses_to_a_book_spot();
  a_spot_tuple_without_its_anchor_or_book_fails_closed();
  a_spot_selector_links_its_phrase_to_the_other_book();
  geist_test::exit_with_failures();
  std::cout << "spot_selector_link_synthetic: ok\n";
  return 0;
}
