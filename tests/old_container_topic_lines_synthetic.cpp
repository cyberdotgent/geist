// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Topic lines of containers older than directory version 1.2 (doc/boo-spec,
// topics, "Topic Lines In Older Containers").  The five version 1.0/1.1
// books write their topic headers and bodies with control lines that a
// version 1.2 book spells differently, and the reader draws none of them:
//
//   * `CSUMMARY <id> <script>`: EDCGV1R2 3.4.6 record 68 display line 5 is
//     `CSUMMARY 3.4.6 .SP 1#.SK 1#T7#.SK 1#T1#.SK 2`; the CONTENTS topic
//     writes two such lines (record 9 lines 5 and 6).  Hosted BookServer
//     (DT 19891031000000) serves 3.4.6 as the heading and the body only.
//   * `C.<word>` lines: the reader skips every display line whose control
//     name starts with `C.`, operands included.  The version 1.0/1.1 books
//     write `C.SK <n>` (7,689 lines), `C.SP <n>`, `C.CP`, `C.PA` and
//     `C.CC <n>`; EDCGV1R2 3.4.6 record 68 line 11 is `C.SK 1`, standing
//     between `SI Transaction Processing Facility (TPF)` and the first body
//     row, and line 27 is `C.SK 2` at the topic's end.  Hosted serves the
//     body rows with no `C.SK` and no count.
//
// Everything here is synthetic: the test builds whole topic records by hand.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;
using Line = std::vector<TokenWords>;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "old_container_topic_lines_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

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

struct Extracted {
  std::vector<DecodedLogicalRecordSource> records;
  std::optional<geist::detail::ProseTopicIR> topic;
  std::string error;
};

Extracted run(const std::vector<Line> &lines, const std::string &title) {
  Extracted result;
  auto record = build(lines, 68);
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
  result.topic = geist::detail::extract_prose_topic_ir(
      result.records, layout, *ownership, title, nullptr, &result.error);
  if (result.topic) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(
                result.records, layout, *ownership, title, nullptr,
                *result.topic, &verify_error),
            "the extracted topic failed its own verifier: " + verify_error);
  }
  return result;
}

std::string topic_text(const geist::detail::ProseTopicIR &topic) {
  std::string text;
  for (const auto &block : topic.blocks)
    for (const auto &item : block.inlines) text += item.text + " ";
  return text;
}

const Line summary_3_4_6 = {words("CSUMMARY"), words("3.4.6"), words(".SP"),
                            words("1#.SK"),    words("1#T7#.SK"),
                            words("1#T1#.SK"), words("2")};

// EDCGV1R2 3.4.6, record 68: the envelope, the title and a body.  `summary`
// stands where the book writes its `CSUMMARY` lines; `body` replaces the
// body rows when given.
const Line title_3_4_6 = {words("ST"), words(" "), words("Transaction"),
                          words("Processing"), words("Facility")};

std::vector<Line> topic(const std::vector<Line> &summary,
                        const std::vector<Line> &body = {},
                        const Line &title = title_3_4_6) {
  std::vector<Line> lines = {
      {words("SH3.4.6")},
      {words("CTOPIC#"), words("38")},
      {words("CPARENT"), words("3.4")},
      {words("CFORWARDLEVEL")},
      {words("CBACKLEVEL"), words("3.4.5")},
  };
  lines.insert(lines.end(), summary.begin(), summary.end());
  const std::vector<Line> rest = {
      {words("CHDLEVEL"), words(":H3")},
      {words("CSOURCEFN"), words("EDCGI")},
      title,
  };
  lines.insert(lines.end(), rest.begin(), rest.end());
  if (body.empty()) {
    lines.push_back({words("   "), words("You"), words("can"), words("compile"),
                     words("application"), words("programs")});
    lines.push_back({words("   "), words("for"), words("TPF.")});
  } else {
    lines.insert(lines.end(), body.begin(), body.end());
  }
  return lines;
}

void admitted(const std::string &label, const std::vector<Line> &lines,
              const std::string &expected,
              const std::vector<std::string> &hidden) {
  auto extracted = run(lines, "Transaction Processing Facility");
  require(extracted.topic.has_value(),
          label + ": the topic was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto text = topic_text(*extracted.topic);
  require(text.find(expected) != std::string::npos,
          label + ": the body '" + expected + "' is missing: '" + text + "'");
  for (const auto &word : hidden)
    require(text.find(word) == std::string::npos,
            label + ": control text '" + word + "' is drawn: '" + text + "'");
}

void summary_lines() {
  const std::vector<std::string> hidden = {".SP", ".SK", "T7#", "3.4.6"};
  admitted("CSUMMARY with a SCRIPT operand", topic({summary_3_4_6}),
           "You can compile application programs for TPF.", hidden);
  // EDCGV1R2 CONTENTS record 9: the summary continues on a second
  // `CSUMMARY <id>` line.
  admitted("two CSUMMARY lines",
           topic({{words("CSUMMARY"), words("3.4.6"), words("#1L17#1L16")},
                  {words("CSUMMARY"), words("3.4.6"), words("1#3L52#3L27")}}),
           "You can compile application programs for TPF.",
           {"#1L17", "3L52"});
}

void script_lines() {
  const std::string body =
      "You can compile application programs for TPF. For more information";
  const auto row1 = Line{words("   "), words("You"), words("can"),
                         words("compile"), words("application"),
                         words("programs")};
  const auto row2 = Line{words("   "), words("for"), words("TPF.")};
  const auto row3 = Line{words("   "), words("For"), words("more"),
                         words("information")};
  const auto index = Line{words("SI"), words("Transaction"),
                          words("Processing"), words("Facility")};
  const auto lines = [&](const Line &first, const Line &second,
                         const Line &last) {
    return topic({summary_3_4_6},
                 {index, first, row1, row2, second, row3, last});
  };
  // EDCGV1R2 3.4.6: `C.SK 1` after the index line, `C.SK 1` between the
  // paragraphs and `C.SK 2` closing the topic.
  admitted("C.SK lines",
           lines({words("C.SK"), words("1")}, {words("C.SK"), words("1")},
                 {words("C.SK"), words("2")}),
           body, {"C.SK", "SK", " 1 ", " 2 "});
  // EDCGV1R2 COVER and EHMPMST 2.1: the page controls with and without a
  // depth.
  admitted("C.PA, C.CP and C.CC lines",
           lines({words("C.PA")}, {words("C.CC"), words("4")},
                 {words("C.CP"), words("12DV")}),
           body, {"C.PA", "C.CC", "C.CP", "12DV", " 4 "});
  // Any other `C.` control draws nothing either, whatever its operands.
  admitted("an unlisted C. control",
           lines({words("C.TR"), words("A"), words("B")},
                 {words("C.SK"), words("1")}, {words("C.IN"), words("+3")}),
           body, {"C.TR", "C.IN", "+3", " A B"});
}

// The styled text of every inline of `style`, joined by a blank.
std::string styled(const geist::detail::ProseTopicIR &topic,
                   geist::detail::FontStyleIR style) {
  std::string text;
  for (const auto &block : topic.blocks)
    for (const auto &item : block.inlines)
      if (item.style == style) text += (text.empty() ? "" : " ") + item.text;
  return text;
}

void one_triple_font_lines() {
  using geist::detail::FontStyleIR;
  // EDCGV1R2 3.4.6 record 68 lines 17-19: `CFONT 31 7 X` and `CFONT 39 7 X`,
  // one triple per line, both styling the next drawn row; hosted serves
  // `<tt>#pragma</tt> <tt>linkage</tt>`.  Lines 23-26: three `C` lines over
  // `TPF General Information`, served as three `<cite>` runs.
  const std::vector<Line> body = {
      {words("CFONT"), words("31"), words("7"), words("X")},
      {words("CFONT"), words("39"), words("7"), words("X")},
      {words("   "), words("information"), words("provided"), words("in"),
       words("the"), words("#pragma"), words("linkage"), words("statements")},
      {words("C.SK"), words("1")},
      {words("CFONT"), words("40"), words("3"), words("C")},
      {words("CFONT"), words("44"), words("7"), words("C")},
      {words("CFONT"), words("52"), words("11"), words("C")},
      {words("   "), words("For"), words("more"), words("information"),
       words("on"), words("TPF,"), words("consult"), words("TPF"),
       words("General"), words("Information,"), words("GH20-7521.")},
  };
  auto extracted =
      run(topic({summary_3_4_6}, body), "Transaction Processing Facility");
  require(extracted.topic.has_value(),
          "one-triple CFONT lines: the topic was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto example = styled(*extracted.topic, FontStyleIR::example_phrase);
  require(example == "#pragma linkage" || example == "#pragma linkage ",
          "one-triple CFONT lines: the X spans style '" + example + "'");
  const auto citation = styled(*extracted.topic, FontStyleIR::citation);
  require(citation == "TPF General Information",
          "one-triple CFONT lines: the C spans style '" + citation + "'");
  const auto text = topic_text(*extracted.topic);
  require(text.find("CFONT") == std::string::npos &&
              text.find(" 31 ") == std::string::npos,
          "one-triple CFONT lines: control text is drawn: '" + text + "'");
}

void dash_in_title() {
  // EDCIV1R2 2.1 record 44 line 8: `ST  Chapter 3.  Preparing for the
  // Installation ── MVS`, the dash stored as one token of two U+2500 words
  // (token 37).  The catalog string projects the run as `__`; hosted
  // BookServer (DT 19891031000000) heads the topic `<H1> 2.1   Chapter 3.
  // Preparing for the Installation __ MVS</H1>`.  EDCUSER 4.6.2.1 is the
  // same with a one-byte dash token after `__ctrace`.
  const TokenWords dash = {0x2500, 0x2500};
  const Line title = {words("ST"),           words(" "),
                      words("Chapter"),      words("3."),
                      words("Preparing"),    words("for"),
                      words("the"),          words("Installation"),
                      dash,                  words("MVS")};
  auto extracted = run(topic({summary_3_4_6}, {}, title),
                       "Chapter 3.  Preparing for the Installation __ MVS");
  require(extracted.topic.has_value(),
          "dash in title: the topic was rejected: " + extracted.error);
  if (!extracted.topic) return;
  require(extracted.topic->title ==
              "Chapter 3. Preparing for the Installation __ MVS",
          "dash in title: the title is '" + extracted.topic->title + "'");
  require(topic_text(*extracted.topic).find("You can compile") !=
              std::string::npos,
          "dash in title: the body is missing");
}

}  // namespace

int main() {
  dash_in_title();
  summary_lines();
  script_lines();
  one_triple_font_lines();
  return 0;
}
