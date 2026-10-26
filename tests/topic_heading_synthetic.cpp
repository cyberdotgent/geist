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
// Every `ST` line heads the topic.  A second `ST` inside the body prints a
// second heading with the same level and the same topic number.  SC31-8212-00
// 2.3.3 (DT 19950906051034) opens with an empty `ST`, a styled row `open`,
// the anchor `SRHDRHWEOP6` and `ST  Purpose`; hosted serves `<H2> 2.3.3
// </H2>`, `   <B>open</B>` and `<a name="HDRHWEOP6"><H2> 2.3.3
// Purpose</H2></a>`.
//
// The `ST` payload is the whole rest of its display line.  A title whose
// first word is spelled like a control is still the title: SC34-425
// 1.7.3.2.5 record 383 line 8 is `ST  SREPLACE Command`, which the flattened
// splitter cuts after the bare `ST`; hosted (DT 19921112160049) heads the
// topic `<H4> 1.7.3.2.5   SREPLACE Command</H4>`.
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
#include <tuple>
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
// `length_spellings` overrides the dictionary spelling of chosen lines'
// length bytes (by line index), as real dictionaries spell them.
DecodedLogicalRecordSource
build(const std::vector<Line> &lines, std::uint32_t logical_record,
      const std::vector<std::pair<std::size_t, TokenWords>> &length_spellings =
          {}) {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const auto &content = lines[index];
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    TokenWords length{0x25BA};
    for (const auto &spelling : length_spellings)
      if (spelling.first == index) length = spelling.second;
    record.tokens.push_back(length);
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

// The document's blocks in order: `hN:<text>` for a heading, `a:<id>` for
// an anchor, `p` for a paragraph and `?` for anything else.
std::string outline(const std::vector<Line> &lines, const std::string &id,
                    const std::string &catalog_title, std::string *rejection,
                    const std::vector<std::pair<std::size_t, TokenWords>>
                        &length_spellings = {}) {
  std::vector<DecodedLogicalRecordSource> records;
  records.push_back(build(lines, 2, length_spellings));
  geist::detail::TopicIdentityIR identity;
  identity.id = id;
  identity.title = catalog_title;
  identity.start_logical_record = 2;
  identity.end_logical_record = 2;
  const auto document = geist::detail::try_lower_topic_to_document_ir(
      identity, records, nullptr, rejection);
  if (!document) return "<declined>";
  std::string result;
  for (const auto &block : document->blocks) {
    if (!result.empty()) result += '|';
    std::visit(
        [&](const auto &node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, geist::detail::HeadingBlockIR>)
            result += "h" + std::to_string(node.level) + ":" +
                      inline_text(node.content);
          else if constexpr (std::is_same_v<T, geist::detail::AnchorBlockIR>)
            result += "a:" + node.id;
          else if constexpr (std::is_same_v<T,
                                            geist::detail::ParagraphBlockIR>)
            result += "p";
          else
            result += "?";
        },
        block.node);
  }
  return result;
}

void expect_outline(const std::string &label, const std::vector<Line> &lines,
                    const std::string &id, const std::string &catalog_title,
                    const std::string &expected,
                    const std::vector<std::pair<std::size_t, TokenWords>>
                        &length_spellings = {}) {
  std::string rejection;
  const auto found =
      outline(lines, id, catalog_title, &rejection, length_spellings);
  require(found == expected, label + ": blocks '" + found + "' (" +
                                 rejection + "), expected '" + expected + "'");
}

// A topic whose body carries a second `ST` line after an anchor.
std::vector<Line> second_title_topic(const Line &first_title,
                                     const Line &second_title) {
  return {
      {words("SH2.3.3")},
      {words("ctopicn"), words("164")},
      {words("cparent"), words("2.3")},
      {words("cforwardlevel"), words("2.3.4")},
      {words("cbacklevel"), words("2.3.2")},
      {words("csummary"), words("9"), words("0"), words("9")},
      {words("chdlevel"), words(":H2")},
      {words("csourcefn"), words("DPML0O61")},
      first_title,
      {words("cfont"), words("3"), words("4"), words("2")},
      {words("   "), words("open")},
      {words("SRHDRPURPOSE")},
      second_title,
      {},
      {words("   "), words("Opens"), words("a"), words("session.")},
  };
}

// The SC31-8212-00 2.3.3 shape in the CZ dialect: the repeated title stands
// before the first directive, three empty rows follow it, and a heading
// directive with no rows of its own comes before the body paragraph.
std::vector<Line> cz_second_title_topic() {
  return {
      {words("SH2.3.3")},
      {words("ctopicn"), words("164")},
      {words("cparent"), words("2.3")},
      {words("cforwardlevel"), words("2.3.4")},
      {words("cbacklevel"), words("2.3.2")},
      {words("csummary"), words("9"), words("0"), words("9")},
      {words("chdlevel"), words(":H2")},
      {words("csourcefn"), words("DPML0O61")},
      {words("ST")},
      {words("cfont"), words("3"), words("4"), words("2")},
      {words("   "), words("open")},
      {words("SRHDRPURPOSE")},
      {words("ST"), words("  "), words("Purpose")},
      {},
      {},
      {},
      {words("cz"), words("BREAK"), words("3")},
      {words("cz"), words("FLOW"), words("H3"), words("3"), words("3")},
      {words("cz"), words("BREAK"), words("3")},
      {words("cz"), words("FLOW"), words("P"), words("3"), words("3")},
      {words("   "), words("Opens"), words("a"), words("session.")},
  };
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

  // A second ST heads the topic again, at the same level, after the anchor
  // that precedes it.
  expect_outline("empty ST, then ST Purpose",
                 second_title_topic({words("ST")},
                                    {words("ST"), words("  "),
                                     words("Purpose")}),
                 "2.3.3", "", "h2:2.3.3|p|a:HDRPURPOSE|h2:2.3.3 Purpose|p");
  expect_outline("ST Open, then ST Purpose",
                 second_title_topic({words("ST"), words("Open")},
                                    {words("ST"), words("Purpose"),
                                     words("and"), words("use")}),
                 "2.3.3", "Open",
                 "h2:2.3.3 Open|p|a:HDRPURPOSE|h2:2.3.3 Purpose and use|p");

  // The same in the CZ dialect: the anchor before the repeated title stays
  // in front of it although only empty rows follow, and the empty `cz FLOW
  // H3` draws nothing.
  expect_outline("CZ dialect: empty ST, then ST Purpose",
                 cz_second_title_topic(), "2.3.3", "",
                 "h2:2.3.3|p|a:HDRPURPOSE|h2:2.3.3 Purpose|p");

  // A control-shaped first title word does not empty the title.
  expect_outline("ST  SREPLACE Command",
                 topic("1.7.3.2.5", ":H4",
                       {words("ST"), words("  "), words("SREPLACE"),
                        words("Command")}),
                 "1.7.3.2.5", "SREPLACE Command",
                 "h4:1.7.3.2.5 SREPLACE Command|p");
  expect_outline("ST  SRPI connectivity method",
                 topic("2.21.2", ":H2",
                       {words("ST"), words("  "), words("SRPI"),
                        words("connectivity"), words("method")}),
                 "2.21.2", "SRPI connectivity method",
                 "h2:2.21.2 SRPI connectivity method|p");
  // A bare `ST` before a control line is still an empty title.
  expect_outline("bare ST",
                 second_title_topic({words("ST")},
                                    {words("ST"), words("Purpose")}),
                 "2.3.3", "", "h2:2.3.3|p|a:HDRPURPOSE|h2:2.3.3 Purpose|p");
  // A bare `ST` display line followed directly by body rows: the title is
  // the `ST` line alone, so it is empty and the topic is headed by its id;
  // the rows after it are body, although the flattened segment that opens
  // with the opcode runs on across them.  GC24-5611-01 FRONT record 2
  // line 6 is `ST` (one token) and line 7 the cover's drawn rule; hosted
  // (DT 19931102100502) serves `<h1> FRONT </h1>` and then the cover.
  expect_outline("bare ST line, body rows follow",
                 topic("FRONT", ":H1", {words("ST")}), "FRONT", "",
                 "h1:FRONT|p");
  // The same in a message topic: SC38-3058-02 11.2.4.17 (DT 19960430074838)
  // `<MSGNO> 11.2.4.17 </MSGNO>` and then the message body.
  expect_outline("bare ST line, message topic",
                 topic("11.2.4.17", ":MSGNO", {words("ST")}), "11.2.4.17",
                 "", "h6:11.2.4.17|p");
  // The same where the `ST` line is glued to `csourcefn`: SC38-3058-02
  // 11.2.4.17 record 474 spells the `ST` line's length byte (value 1) `.`
  // and the two empty lines' bytes (value 0) as the bare spacing control,
  // and the flattened segment `csourcefn ADNL2PRO. ST` runs on across them.
  {
    auto lines = topic("11.2.4.17", ":MSGNO", {words("ST")});
    lines.insert(lines.begin() + 9, Line{});
    lines.insert(lines.begin() + 9, Line{});
    expect_outline("glued bare ST line, empty rows follow", lines,
                   "11.2.4.17", "", "h6:11.2.4.17|p",
                   {{8, TokenWords{1, '.'}},
                    {9, TokenWords{1}},
                    {10, TokenWords{1}}});
  }

  // A `?` the dictionary spells as a word of its own ends the title as
  // stored: SC33-1661-01 1.1.1 record 33 line 9 is `ST  What is DTP?`, the
  // `?` a word after a spacing token; hosted (DT 19991018104951) heads the
  // topic `<H2> 1.1.1   What is DTP?</H2>`.
  expect_heading("ST  What is DTP?",
                 topic("1.1.1", ":H2",
                       {words("ST"), words("  "), words("What"), words("is"),
                        words("DTP"), TokenWords{1}, words("?")}),
                 "1.1.1", "What is DTP", "1.1.1 What is DTP?");

  // The reader takes the level from a fixed column of the `chdlevel` line,
  // skipping the one character after the opcode's blank whatever it is: the
  // foil book GG24-4137-00 writes `chdlevel .H3` (record 306 line 6) and
  // hosted (DT 19941221141316) heads 2.21.1 `<H3> 2.21.1   FOIL: ...</H3>`.
  expect_outline("chdlevel .H3",
                 topic("2.21.1", ".H3",
                       {words("ST"), words("  "), words("FOIL:"),
                        words("Background")}),
                 "2.21.1", "FOIL: Background", "h3:2.21.1 FOIL: Background|p");
  // Any level other than H1-H6 and MSGNO heads the topic at the top level:
  // SC31-6171-00 LEGEND `chdlevel :LEGEND` (DT 19930505133732) is served
  // `<h1> LEGEND   Notices</h1>`, SC31-6173-01 LEGEND the same.
  expect_outline("chdlevel :LEGEND",
                 topic("LEGEND", ":LEGEND", {words("ST"), words("Notices")}),
                 "LEGEND", "Notices", "h1:LEGEND Notices|p");

  // An `SRLEN` envelope anchor names the topic by the rest of its display
  // line, even where a word of it is spelled like a control.  SC24-5780-01
  // 3.146 record 1261 stores `SRLEN SI (Macro)` between `csummary` and
  // `chdlevel`; hosted (DT 19961023165238) serves `<a name="LEN SI
  // (Macro)">` and no text before the heading.  SC31-6210-01 2.13.5
  // (`SRLEN SRCLO`) and SC31-7066-00 2.1.258 (`SRLEN SRATIO (NPDA)`) are
  // served `<a name="LEN SRCLO">` and `<a name="LEN SRATIO (NPDA)">`.
  for (const auto &[label, payload, expected] :
       std::vector<std::tuple<std::string, Line, std::string>>{
           {"SRLEN SI (Macro)",
            {words("SRLEN"), words("SI"), words("(Macro)")},
            "h2:3.146 SI (Macro)|a:LEN SI (Macro)|p"},
           {"SRLEN .SI -- Segment Include",
            {words("SRLEN"), words(".SI"), words("--"), words("Segment"),
             words("Include")},
            "h2:3.146 SI (Macro)|a:LEN .SI -- Segment Include|p"},
           {"SRLEN SRATIO (NPDA)",
            {words("SRLEN"), words("SRATIO"), words("(NPDA)")},
            "h2:3.146 SI (Macro)|a:LEN SRATIO (NPDA)|p"}}) {
    auto lines = topic("3.146", ":H2",
                       {words("ST"), words("  "), words("SI"),
                        words("(Macro)")});
    lines.insert(lines.begin() + 6, payload);
    expect_outline(label, lines, "3.146", "SI (Macro)", expected);
  }
  // The envelope `SRMSG` row names its message by the rest of the row, too:
  // dknm3a02 3.1906 record 2192 stores `SRMSG SRTM 01` between `csummary`
  // and `chdlevel`, and the splitter cut it before `SRTM`.  The reader
  // forms an `SR` row's anchor from the row text after `SR`, verbatim.
  {
    auto lines = topic("3.1906", ":MSGNO",
                       {words("ST"), words("  "), words("SRTM"), words("01")});
    lines.insert(lines.begin() + 6,
                 Line{words("SRMSG"), words("SRTM"), words("01")});
    expect_outline("SRMSG SRTM 01", lines, "3.1906", "SRTM 01",
                   "h6:3.1906 SRTM 01|a:MSG SRTM 01|p");
  }
  // The title is its display line: a subject-index line after it is an
  // index entry, not more title.  SG24-4675-00 1.1.2 record 35 line 8 is
  // `ST  Is Your Storage Service Cost Effective?`, line 9 is empty (its
  // length byte spelled as the attach control and `,`), line 10 is
  // `SI costs`; the one-word term was read as title and the entry left
  // empty.  Hosted (DT 19960308132239) heads the topic `1.1.2   Is Your
  // Storage Service Cost Effective?` and shows no `costs` there.
  // dfht4c00 3.3.2 record 219 line 7 is an `SI` line with no term at all;
  // like any index line it draws nothing.
  {
    // The title's own `?` stands after a spacing token, as stored (record
    // 35 tokens 34-35); this check is about the index line, so only the
    // title's words and the absence of `costs` are compared.
    auto lines = topic("1.1.2", ":H3",
                       {words("ST"), words("  "), words("Is"), words("Your"),
                        words("Storage"), words("Service"), words("Cost"),
                        words("Effective"), TokenWords{1}, words("?")});
    lines.insert(lines.begin() + 9, Line{words("SI"), words("costs")});
    lines.insert(lines.begin() + 9, Line{});
    std::string rejection;
    const auto found =
        outline(lines, "1.1.2", "Is Your Storage Service Cost Effective?",
                &rejection, {{9, TokenWords{1}}, {10, TokenWords{1, ','}}});
    require(found.rfind("h3:1.1.2 Is Your Storage Service Cost Effective", 0) ==
                    0 &&
                found.find("costs") == std::string::npos &&
                found.size() > 2 && found.compare(found.size() - 2, 2, "|p") == 0,
            "SI line after the title: blocks '" + found + "' (" + rejection +
                ")");
  }
  {
    auto lines = topic("1.1.2", ":H3",
                       {words("ST"), words("  "), words("Is"), words("Your"),
                        words("Storage"), words("Service"), words("Cost"),
                        words("Effective")});
    lines.insert(lines.begin() + 9, Line{words("SI")});
    lines.insert(lines.begin() + 9, Line{});
    expect_outline("SI line with no term", lines, "1.1.2",
                   "Is Your Storage Service Cost Effective",
                   "h3:1.1.2 Is Your Storage Service Cost Effective|p",
                   {{9, TokenWords{1}}, {10, TokenWords{1, ','}}});
  }
  return 0;
}
