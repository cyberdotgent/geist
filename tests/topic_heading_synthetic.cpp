// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The topic heading a typed topic is served under, end to end: synthetic
// topic records go through the typed dispatcher, and the heading block of the
// resulting document is compared with the heading hosted BookServer serves.
//
// Heading number suppression.  The heading is the topic id, then the `ST`
// title -- except where the title equals the id ignoring case, when the id
// is not printed at all.  Hosted BookServer serves `NOTICES` topics titled
// `Notices` as `<h1>    Notices</h1>` in every one of 18 sampled books
// (GC23-0469-02 DT 19930208105051, SG24-4424-01, SC41-8106-02, ...), and
// `FIGURES`/`TABLES` topics titled `Figures`/`Tables` the same way
// (ESE4MSTR, SC31-7563-02).  A title that merely starts with the id keeps the
// number.
//
// Everything here is synthetic: the test builds whole topic records by hand.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;
using Line = std::vector<TokenWords>;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "topic_heading_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

// One logical record; every line is a length byte followed by its tokens.
DecodedLogicalRecordSource build(const std::vector<Line> &lines,
                                 std::uint32_t logical_record) {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;
  for (const auto &content : lines) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(TokenWords{0x25BA});
    for (const auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(token);
    }
  }
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
  return record;
}

// A topic `id` with a metadata envelope, the title line `title_line` and a
// two-row prose body.
std::vector<Line> topic(const std::string &id, const std::string &level,
                        const Line &title_line) {
  return {
      {words("SH" + id)},
      {words("ctopicn"), words("3")},
      {words("cparent")},
      {words("cforwardlevel"), words("1.0")},
      {words("cbacklevel")},
      {words("csummary"), words("2"), words("0"), words("2")},
      {words("chdlevel"), words(level)},
      {words("csourcefn"), words("FRONTM")},
      title_line,
      {words("   "), words("An"), words("installation"), words("may"),
       words("write.")},
      {words("   "), words("Closing"), words("row.")},
  };
}

struct Heading {
  bool found = false;
  std::uint32_t level = 0;
  std::string text;
};

std::string inline_text(const geist::detail::InlineSequenceIR &content) {
  std::string text;
  for (const auto &item : content)
    std::visit(
        [&](const auto &node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, geist::detail::TextInlineIR>)
            text += node.text;
        },
        item.node);
  return text;
}

std::vector<Heading> headings(const std::vector<Line> &lines,
                              const std::string &id,
                              const std::string &catalog_title,
                              std::string *rejection) {
  std::vector<DecodedLogicalRecordSource> records;
  records.push_back(build(lines, 2));
  require(records.front().ir.display_lines_parse,
          id + ": the synthetic topic's display lines did not parse");
  geist::detail::TopicIdentityIR identity;
  identity.id = id;
  identity.title = catalog_title;
  identity.start_logical_record = 2;
  identity.end_logical_record = 2;
  const auto document = geist::detail::try_lower_topic_to_document_ir(
      identity, records, nullptr, rejection);
  std::vector<Heading> result;
  if (!document) return result;
  for (const auto &block : document->blocks)
    if (const auto *heading =
            std::get_if<geist::detail::HeadingBlockIR>(&block.node))
      result.push_back({true, heading->level, inline_text(heading->content)});
  return result;
}

void expect_heading(const std::string &label, const std::vector<Line> &lines,
                    const std::string &id, const std::string &catalog_title,
                    const std::string &expected) {
  std::string rejection;
  const auto found = headings(lines, id, catalog_title, &rejection);
  require(!found.empty(), label + ": no typed heading (" + rejection + ")");
  if (found.empty()) return;
  require(found.front().text == expected,
          label + ": heading '" + found.front().text + "', expected '" +
              expected + "'");
}

} // namespace

int main() {
  // The title spells the id: no number.
  expect_heading("NOTICES / Notices",
                 topic("NOTICES", ":NOTICES",
                       {words("ST"), words("Notices")}),
                 "NOTICES", "Notices", "Notices");
  expect_heading("FIGURES / Figures, chdlevel :H1",
                 topic("FIGURES", ":H1", {words("ST"), words("Figures")}),
                 "FIGURES", "Figures", "Figures");
  // Letter case is ignored in both directions.
  expect_heading("index / INDEX",
                 topic("index", ":H1", {words("ST"), words("INDEX")}),
                 "index", "INDEX", "INDEX");

  // Anything else keeps the number.
  expect_heading("PREFACE / About This Book",
                 topic("PREFACE", ":PREFACE",
                       {words("ST"), words("About"), words("This"),
                        words("Book")}),
                 "PREFACE", "About This Book", "PREFACE About This Book");
  expect_heading("NOTICES / Notices and trademarks",
                 topic("NOTICES", ":NOTICES",
                       {words("ST"), words("Notices"), words("and"),
                        words("trademarks")}),
                 "NOTICES", "Notices and trademarks",
                 "NOTICES Notices and trademarks");
  expect_heading("1.1 / Introduction",
                 topic("1.1", ":H2", {words("ST"), words("Introduction")}),
                 "1.1", "Introduction", "1.1 Introduction");
  return 0;
}
