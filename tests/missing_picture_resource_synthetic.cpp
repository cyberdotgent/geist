// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A picture figure whose picture the book does not store.
//
// GC30-3072-04 (`SNA Concepts And Products`, 1993) topic `2.2` draws its
// figure as
//
//   SRFIGEUSERS
//   cselect 3 9 PIC1        PICTURE 1
//      Figure 1. End Users in a Network
//   SREFIG
//
// but the book has no resource catalog entry 1: none of its pictures were
// stored.  Hosted BookServer (DT 19930512100721) still serves the figure,
// with its "under construction" stand-in in place of the picture and the
// placeholder as the alternative text:
//
//   <a name="FIGEUSERS">   <a href="/bookmgr/construc.gif">
//   <img src="/bookmgr/construc.gif" alt="PICTURE 1"></a>         </a>
//
// GX27-3990-01 2.10 and C.0 do the same for pictures 18-20 and 24-27.  The
// figure is therefore typed: the picture is kept, marked as one the book
// does not store, and drawn as an unresolved image without asking the
// consumer for an object that does not exist.  Before, the whole topic
// declined to the verbatim route ("picture resource 1 is not in the resource
// catalog").
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
#include <variant>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "missing_picture_resource_synthetic: " << message << '\n';
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

  void line(std::vector<TokenWords> content,
            TokenWords length_spelling = TokenWords{0x25BA}) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(std::move(length_spelling));
    for (auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(std::move(token));
    }
  }

  void heading(const std::string &id, const std::string &title_word) {
    line({words("sh" + id)});
    line({words("ctopicn"), words("10")});
    line({words("cparent"), words("2.0")});
    line({words("cforwardlevel"), words("2.3")});
    line({words("cbacklevel"), words("2.1")});
    line({words("csummary"), words("34"), words("0"), words("34")});
    line({words("chdlevel"), words(":H2")});
    line({words("csourcefn"), words("KADL4NET")});
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

// GC30-3072-04 2.2, reduced: a paragraph, the figure, a paragraph.
DecodedLogicalRecordSource end_users_topic(const std::string &picture) {
  RecordBuilder builder;
  builder.heading("2.2", "Users");
  builder.line({words("   "), words("Two"), words("end"), words("users.")});
  builder.line({words("SRFIGEUSERS")});
  builder.line({words("cselect"), words("3"), words("9"),
                words("PIC" + picture)});
  builder.line({words("   "), words("PICTURE"), words(picture)});
  builder.line({words("   "), words("Figure"), words("1."), words("End"),
                words("Users")});
  builder.line({words("SREFIG")});
  builder.line({words("   "), words("Following"), words("text.")});
  return builder.build(39);
}

struct Rendered {
  std::optional<geist::detail::ProseTopicIR> topic;
  std::string error;
  std::string html;
  std::string markdown;
  std::vector<std::string> asked;
};

Rendered render(const std::string &picture) {
  Rendered result;
  std::vector<DecodedLogicalRecordSource> records;
  records.push_back(end_users_topic(picture));
  require(records.front().ir.display_lines_parse,
          "the synthetic topic's display lines did not parse");
  const auto layout = geist::detail::extract_layout_ir(records);
  std::string error;
  const auto ownership =
      geist::detail::build_verified_ownership_ir(records, layout, &error);
  require(ownership.has_value(), "ownership is not verifiable: " + error);
  if (!ownership) return result;
  // The catalog stores picture 1 only.
  const std::set<std::string> resource_ids{"1"};
  result.topic = geist::detail::extract_prose_topic_ir(
      records, layout, *ownership, "Users", nullptr, &result.error,
      &resource_ids);
  if (!result.topic) return result;
  require(geist::detail::verify_prose_topic_ir(records, layout, *ownership,
                                               "Users", nullptr, *result.topic,
                                               &error, &resource_ids),
          "the extracted topic failed its own verifier: " + error);
  const auto document = geist::detail::lower_prose_topic_to_document_ir(
      {"2.2", "Users"}, *result.topic, &error);
  require(document.has_value(), "the topic did not lower: " + error);
  if (!document) return result;
  require(geist::detail::verify_prose_topic_document_ir(*result.topic,
                                                        *document, &error),
          "the lowered topic failed verification: " + error);
  geist::HtmlRenderOptions options;
  auto *asked = &result.asked;
  options.resolve_resource =
      [asked](const std::string &id) -> std::optional<std::string> {
    asked->push_back(id);
    return "/object/" + id;
  };
  result.html = geist::detail::render_document_html_blocks(*document, options);
  result.markdown = geist::detail::render_document_markdown(*document);
  return result;
}

void a_picture_the_book_does_not_store_is_kept_unresolved() {
  const auto rendered = render("3");
  require(rendered.topic.has_value(),
          "the figure of an unstored picture was rejected: " + rendered.error);
  if (!rendered.topic) return;
  const auto &figures = rendered.topic->figures.blocks;
  require(figures.size() == 1, "the figure is not one figure block");
  if (figures.size() == 1)
    require(figures.front().target == "3" &&
                figures.front().missing_resource,
            "the figure does not name picture 3 as unstored");
  require(rendered.asked.empty(),
          "the consumer was asked for an object the book does not store");
  require(contains(rendered.html, "geist-image--unresolved") &&
              contains(rendered.html, "alt=\"PICTURE 3\"") &&
              contains(rendered.html, "src=\"resource:3\""),
          "the unstored picture is not an unresolved image: " + rendered.html);
  require(contains(rendered.html, "Figure 1. End Users") &&
              contains(rendered.html, "Following text."),
          "the caption or the following text was lost: " + rendered.html);
  require(contains(rendered.markdown, "![PICTURE 3](<resource:3>)"),
          "the Markdown image changed: " + rendered.markdown);
}

void a_stored_picture_is_still_resolved() {
  const auto rendered = render("1");
  require(rendered.topic.has_value(),
          "the stored picture was rejected: " + rendered.error);
  if (!rendered.topic) return;
  require(rendered.topic->figures.blocks.size() == 1 &&
              !rendered.topic->figures.blocks.front().missing_resource,
          "a stored picture was marked unstored");
  require(rendered.asked == std::vector<std::string>{"1"},
          "the consumer was not asked for the stored picture");
  require(contains(rendered.html, "src=\"/object/1\"") &&
              !contains(rendered.html, "geist-image--unresolved"),
          "the stored picture did not resolve: " + rendered.html);
}

} // namespace

int main() {
  a_picture_the_book_does_not_store_is_kept_unresolved();
  a_stored_picture_is_still_resolved();
  geist_test::exit_with_failures();
  std::cout << "missing_picture_resource_synthetic: ok\n";
  return 0;
}
