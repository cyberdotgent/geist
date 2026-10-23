// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A `LNK <IMAGE>` selector in running prose is a link whose content starts
// with an image.
//
// XWEBDEMO (`xwebdemo.boo`) topic `1.0` record 5 stores
// `cselect 3 26 LNK <IMAGE> <INTERNET> </bookmgr/product.gif>
// <http://booksrv2.raleigh.ibm.com/cgi-bin/bookmgr/bookmgr.cmd/library> <>
// <ICON>` over the 26 columns `IBM BookManager BookServer` that open the
// next display row, and hosted BookServer (DT 19970423182524) serves
//
//   <a href="http://booksrv2.raleigh.ibm.com/cgi-bin/bookmgr/bookmgr.cmd/library">
//   <img src="/bookmgr/product.gif" alt="/bookmgr/product.gif">IBM
//   BookManager BookServer</a> is a specialized World Wide Web server that
//
// Alternative 3 is the image, alternative 4 the link, and the selected
// columns stay the link's text after the image.  When alternative 3 is empty
// (the same book's `1.4.1` figure, `<IMAGE> <INTERNET> <>
// </bookmgr/monetcoq.jpg> <> <MONET1>`) alternative 4 is both: hosted serves
// `<a href="/bookmgr/monetcoq.jpg"><img src="/bookmgr/monetcoq.jpg" ...>`.
//
// What either URL means is the consumer's business: the image goes through
// `resolve_resource` exactly as an external figure image does, the link
// through `resolve_external`.
//
// Everything here is synthetic: the test builds its own record and opens no
// book.

#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "geist/detail/ir/selector_link_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/lowering/drawn_word_conservation.hpp"
#include "geist/detail/lowering/prose_topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "geist/detail/render/html_visible_text.hpp"
#include "geist/detail/render/markdown_visible_text.hpp"
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
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "image_selector_link_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

bool contains(const std::string &text, const std::string &needle) {
  return text.find(needle) != std::string::npos;
}

std::size_t count(const std::string &text, const std::string &needle) {
  std::size_t found = 0;
  for (auto at = text.find(needle); at != std::string::npos;
       at = text.find(needle, at + needle.size()))
    ++found;
  return found;
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

const char *const topic_title = "Introduction to BookServer";
const char *const library =
    "http://booksrv2.raleigh.ibm.com/cgi-bin/bookmgr/bookmgr.cmd/library";

const std::vector<std::string> icon_tuple{
    "<IMAGE>", "<INTERNET>", "</bookmgr/product.gif>",
    std::string("<") + library + ">", "<>", "<ICON>"};

// The opening paragraph of XWEBDEMO 1.0: a CZ paragraph whose first row
// starts with the 26 columns the `<IMAGE>` selector covers.
DecodedLogicalRecordSource image_topic(const std::vector<std::string> &tuple) {
  RecordBuilder builder;
  builder.line({words("sh1.0")});
  builder.line({words("ctopicn"), words("4.")});
  builder.line({words("cparent")});
  builder.line({words("cforwardlevel"), words("2.0")});
  builder.line({words("cbacklevel"), words("FIGURES")});
  builder.line({words("csummary"), words("17"), words("4"), words("17")});
  builder.line({words("chdlevel"), words(":H1")});
  builder.line({words("csourcefn"), words("XWEBDEMO")});
  builder.line({words("ST"), words("Introduction"), words("to"),
                words("BookServer")});
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3")});
  std::vector<TokenWords> selector{words("cselect"), words("3"), words("26"),
                                   words("LNK")};
  for (const auto &alternative : tuple) selector.push_back(words(alternative));
  builder.line(std::move(selector));
  builder.line({words("   "), words("IBM"), words("BookManager"),
                words("BookServer"), words("is"), words("a"),
                words("specialized"), words("server.")});
  return builder.build(5);
}

struct Extracted {
  std::vector<DecodedLogicalRecordSource> sources;
  std::optional<geist::detail::ProseTopicIR> prose;
  std::string error;
};

Extracted extract(const std::vector<std::string> &tuple) {
  Extracted out;
  out.sources.push_back(image_topic(tuple));
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
            "the image-link topic failed its own verifier: " + verify_error);
  }
  return out;
}

std::optional<geist::detail::DocumentIR>
lower(const std::vector<std::string> &tuple) {
  const auto extracted = extract(tuple);
  require(extracted.prose.has_value(),
          "the <IMAGE> selector topic was rejected: " + extracted.error);
  if (!extracted.prose) return std::nullopt;
  geist::detail::TopicIdentityIR identity;
  identity.id = "1.0";
  identity.title = topic_title;
  identity.heading_level = 1;
  identity.start_logical_record = 5;
  identity.end_logical_record = 5;
  std::string error;
  auto document = geist::detail::lower_prose_topic_to_document_ir(
      identity, *extracted.prose, &error);
  require(document.has_value(), "the image-link topic did not lower: " + error);
  if (!document) return std::nullopt;
  require(geist::detail::verify_document_ir(*document, &error),
          "the image-link document did not verify: " + error);
  return document;
}

void the_tuple_names_its_image_and_its_link() {
  std::string error;
  const auto link = geist::detail::parse_selector_link(icon_tuple, &error);
  require(link.has_value(), "the <IMAGE> tuple was rejected: " + error);
  if (!link) return;
  require(link->kind == geist::detail::SelectorLinkKindIR::external_image,
          "the <IMAGE> tuple is not an external image");
  require(link->destination == library,
          "the <IMAGE> link is '" + link->destination + "'");
  require(link->image == "/bookmgr/product.gif",
          "the <IMAGE> image is '" + link->image + "'");

  const auto figure = geist::detail::parse_selector_link(
      {"<IMAGE>", "<INTERNET>", "<>", "</bookmgr/monetcoq.jpg>", "<>",
       "<MONET1>"},
      &error);
  require(figure && figure->image == "/bookmgr/monetcoq.jpg" &&
              figure->destination == "/bookmgr/monetcoq.jpg",
          "an <IMAGE> tuple with an empty alternative 3 does not show and "
          "link its alternative 4");
}

void an_image_selector_leads_its_link_with_the_image() {
  const auto document = lower(icon_tuple);
  if (!document) return;

  std::vector<std::string> resources;
  std::vector<std::string> externals;
  geist::HtmlRenderOptions options;
  options.resolve_resource =
      [&resources](const std::string &resource) -> std::optional<std::string> {
    resources.push_back(resource);
    return "/media" + resource;
  };
  options.resolve_external =
      [&externals](const std::string &url) -> std::optional<std::string> {
    externals.push_back(url);
    return std::nullopt;
  };
  const auto html =
      geist::detail::render_document_html_blocks(*document, options);
  require(resources.size() == 1 && resources.front() == "/bookmgr/product.gif",
          "the image was not handed to resolve_resource once");
  require(externals.size() == 1 && externals.front() == library,
          "the link was not handed to resolve_external once");
  const auto expected =
      std::string("href=\"") + library +
      "\"><img class=\"geist-image\" src=\"/media/bookmgr/product.gif\" "
      "alt=\"/bookmgr/product.gif\">IBM BookManager BookServer</a> is a "
      "specialized server.";
  require(contains(html, expected),
          "the image is not the start of the link's content: " + html);
  require(count(html, "<img") == 1, "the image is not drawn once: " + html);
  require(!contains(html, "ICON") && !contains(html, "INTERNET"),
          "the selector tuple leaked into the page: " + html);

  const auto markdown = geist::detail::render_document_markdown(*document);
  require(contains(markdown,
                   std::string("[![/bookmgr/product\\.gif]"
                               "(</bookmgr/product.gif>)"
                               "IBM BookManager BookServer](<") +
                       library + ">) is a specialized server"),
          "the Markdown link does not lead with the image: " + markdown);
}

// The label keeps its words: the image's alt text is a run of its own in
// the visible text of both renderings, so the URL does not swallow the first
// word of the label (`gif` + `IBM`).
void the_label_words_survive_beside_the_image() {
  const auto document = lower(icon_tuple);
  if (!document) return;
  const auto has_word = [](const std::string &visible,
                           const std::string &word) {
    const auto words = geist::detail::conservation_words(visible);
    return std::find(words.begin(), words.end(), word) != words.end();
  };
  const auto html = geist::detail::render_document_html_blocks(*document, {});
  require(has_word(geist::detail::html_visible_text(html), "ibm"),
          "the HTML label lost 'IBM' to the image's alt text");
  geist::detail::DocumentRenderTraceIR trace;
  const auto markdown =
      geist::detail::render_document_markdown(*document, {}, &trace);
  require(has_word(geist::detail::markdown_visible_text(markdown, trace),
                   "ibm"),
          "the Markdown label lost 'IBM' to the image's alt text");
}

void an_image_tuple_without_a_url_fails_closed() {
  std::string error;
  require(!geist::detail::parse_selector_link(
              {"<IMAGE>", "<INTERNET>", "<ICON>", "<>", "<>", "<X>"}, &error),
          "an <IMAGE> tuple naming no link was admitted");
  require(!geist::detail::parse_selector_link(
              {"<IMAGE>", "<INTERNET>", "<product>",
               std::string("<") + library + ">", "<>", "<ICON>"},
              &error),
          "an <IMAGE> tuple whose image is not a URL was admitted");
}

} // namespace

int main() {
  the_tuple_names_its_image_and_its_link();
  an_image_selector_leads_its_link_with_the_image();
  the_label_words_survive_beside_the_image();
  an_image_tuple_without_a_url_fails_closed();
  geist_test::exit_with_failures();
  std::cout << "image_selector_link_synthetic: ok\n";
  return 0;
}
