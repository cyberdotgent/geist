// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// DFHCAP34 topic 1.0 is a :H0 chapter menu with a c.sp 1 p skip.
// Build the record synthetically; the original book is not redistributable.
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/ir/book_topic_catalog_ir.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "test_failures.hpp"
#include <iostream>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
using namespace geist::detail;
namespace {
void require(bool condition, const std::string& message) {
  if (!condition) { std::cerr << message << '\n'; geist_test::record_failure(); }
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


BookTopicCatalogIR catalog() {
  std::vector<geist::TopicInfo> topics;
  for (const auto& item : std::vector<std::pair<std::string, std::string>>{
      {"1.1", "Preparing your application to run"},
      {"1.2", "Language considerations"}}) {
    geist::TopicInfo topic;
    topic.id = item.first; topic.title = item.second;
    topic.heading_level = ":H1";
    topics.push_back(topic);
  }
  return *build_book_topic_catalog_ir(topics, {});
}
void chapter_menu(const std::vector<std::string>& skip, bool admitted,
                  const std::string& level = ":H0") {
  RecordBuilder builder;
  builder.line({words("sh1.0")});
  builder.line({words("ctopicn"), words("22")});
  builder.line({words("cparent")});
  builder.line({words("cforwardlevel"), words("2.0")});
  builder.line({words("cbacklevel"), words("CHANGES")});
  builder.line({words("csummary"), words("1"), words("2"), words("1")});
  builder.line({words("chdlevel"), words(level)});
  builder.line({words("csourcefn"), words("DFHP3A00")});
  builder.line({words("ST"), words("Getting"), words("started")});
  std::vector<TokenWords> control{words("c.sp")};
  for (const auto& operand : skip) control.push_back(words(operand));
  builder.line(std::move(control));
  builder.line({words("cmenu")});
  builder.line({words("cmitem"), words("1.1"), words("Preparing"),
                words("your"), words("application"), words("to"), words("run")});
  builder.line({words("cmitem"), words("1.2"), words("Language"), words("considerations")});
  builder.line({words("cemenu")});
  builder.line({words("cz"), words("BREAK"), words("3")});
  const auto book = catalog();
  TopicIdentityIR identity; identity.id = "1.0"; identity.title = "Getting started";
  identity.heading_level = level;
  std::string error;
  const auto document = try_lower_topic_to_document_ir(identity, {builder.build(72)}, &book, &error);
  require(document.has_value() == admitted, "chapter menu admission mismatch: " + error);
  if (!document) return;
  geist::HtmlRenderOptions options;
  options.resolve_topic = [](const std::string& id) { return "/book/topic/" + id; };
  const auto html = render_document_html_fragment(*document, options);
  require(html.find("<h1 class=\"geist-heading\">1.0 Getting started</h1>") != std::string::npos,
          "chapter heading was not rendered as H1: " + html);
  require(html.find("href=\"/book/topic/1.1\"") != std::string::npos &&
          html.find("href=\"/book/topic/1.2\"") != std::string::npos,
          "chapter subtopic destinations were not preserved: " + html);
  require(html.find("Preparing your application to run") != std::string::npos &&
          html.find("Language considerations") != std::string::npos,
          "chapter menu labels were lost: " + html);
  require(html.find("1 p") == std::string::npos && html.find("c.sp") == std::string::npos,
          "spacing operands leaked into HTML: " + html);
}
}
int main() {
  for (const auto& skip : std::vector<std::vector<std::string>>{
      {"1", "p"}, {"1", "c"}, {"3p", "p", "c"}, {"8mm", "p", "c"}, {"1"}})
    for (const auto& level : {":H0", ":H1"})
      chapter_menu(skip, true, level);
  chapter_menu({"1", "p", "visible"}, false);
  chapter_menu({"p"}, false);
}
