// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The topic header envelope opens with the topic-start control; the seven
// metadata controls after it (`ctopicn`, `cparent`, `cforwardlevel`,
// `cbacklevel`, `csummary`, `chdlevel`, `csourcefn`) are each optional and
// carry no order, none of them draws anything, and a topic without
// `chdlevel` is headed at the top level.  An empty `ST` heads the topic
// with its id alone.
//
//   SC09-138 FRONT record 2   `SHFRONT`, `ctopicn 1`, `cparent`,
//                             `cforwardlevel ABSTRACT`, `cbacklevel`,
//                             `csummary 0 0 0`, eleven `c.rev` lines, `ST`,
//                             `c.sp 1`, two `SI` lines
//
// Hosted BookServer serves that topic (DT 19910321130500) as
// `<h1> FRONT </h1>` and an empty body.  A metadata control written twice,
// visible text ahead of the title, a metadata control after the title and a
// topic with no title at all still decline.
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
    std::cerr << "topic_header_envelope_rule_synthetic: " << message << '\n';
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
  auto record = build(lines, 2);
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

Line revision(const std::string &id, const std::string &bar) {
  return {words("c.rev"), words(id), words(bar)};
}

// SC09-138 FRONT record 2, line for line.
std::vector<Line> front_matter_topic() {
  return {
      {words("SHFRONT")},
      {words("ctopicn"), words("1")},
      {words("cparent")},
      {words("cforwardlevel"), words("ABSTRACT")},
      {words("cbacklevel")},
      {words("csummary"), words("0"), words("0"), words("0")},
      revision("REL2", "|"),
      revision("REL3", "|"),
      revision("ADC", "|"),
      revision("GE", "G"),
      revision("DUNNO", "?"),
      {words("ST")},
      {words("c.sp"), words("1")},
      {words("SI"), words("run-time,"), words("options")},
      {words("SI"), words("TSO,"), words("system"), words("services")},
  };
}

// A numbered topic with a prose body; `drop` names metadata opcodes left
// out, `before_summary` is moved in front of `csummary`.
std::vector<Line> numbered_topic(const std::vector<std::string> &drop,
                                 const std::string &before_summary = {},
                                 const std::string &title = "Recording") {
  const std::vector<Line> metadata = {
      {words("ctopicn"), words("7")},
      {words("cparent"), words("6.11")},
      {words("cforwardlevel"), words("6.11.9")},
      {words("cbacklevel"), words("6.11.7")},
      {words("csummary"), words("2"), words("0"), words("2")},
      {words("chdlevel"), words(":H3")},
      {words("csourcefn"), words("HCPLOGR")},
  };
  const auto opcode = [](const Line &line) {
    return std::string(line.front().begin(), line.front().end());
  };
  std::vector<Line> lines = {{words("sh6.11.8")}};
  std::vector<Line> kept;
  for (const auto &line : metadata) {
    bool dropped = false;
    for (const auto &name : drop) dropped = dropped || opcode(line) == name;
    if (!dropped) kept.push_back(line);
  }
  if (!before_summary.empty()) {
    for (std::size_t index = 0; index < kept.size(); ++index) {
      if (opcode(kept[index]) != before_summary) continue;
      auto moved = kept[index];
      kept.erase(kept.begin() + static_cast<std::ptrdiff_t>(index));
      for (std::size_t at = 0; at < kept.size(); ++at)
        if (opcode(kept[at]) == "csummary") {
          kept.insert(kept.begin() + static_cast<std::ptrdiff_t>(at),
                      std::move(moved));
          break;
        }
      break;
    }
  }
  for (auto &line : kept) lines.push_back(std::move(line));
  Line title_line = {words("ST")};
  if (!title.empty()) title_line.push_back(words(title));
  lines.push_back(std::move(title_line));
  lines.push_back({words("   "), words("An"), words("installation"),
                   words("may"), words("write.")});
  lines.push_back({words("   "), words("Closing"), words("row.")});
  return lines;
}

void admitted(const std::string &label, const std::vector<Line> &lines,
              const std::string &title, const std::string &level,
              const std::string &form) {
  auto extracted = run(lines, title);
  require(extracted.topic.has_value(),
          label + ": the topic was rejected: " + extracted.error);
  if (!extracted.topic) return;
  require(extracted.topic->heading_level == level,
          label + ": heading level '" + extracted.topic->heading_level +
              "', expected '" + level + "'");
  require(extracted.topic->heading_form == form,
          label + ": heading form '" + extracted.topic->heading_form +
              "', expected '" + form + "'");
  require(extracted.topic->title == title,
          label + ": title '" + extracted.topic->title + "', expected '" +
              title + "'");
  const auto text = topic_text(*extracted.topic);
  for (const auto *metadata :
       {"ctopicn", "cparent", "csummary", "chdlevel", "csourcefn", "HCPLOGR",
        "6.11.9", "ABSTRACT", "rev", "DUNNO"})
    require(text.find(metadata) == std::string::npos,
            label + ": envelope metadata '" + metadata +
                "' leaked into the text: '" + text + "'");
}

void declined(const std::string &label, const std::vector<Line> &lines,
              const std::string &title) {
  auto extracted = run(lines, title);
  require(!extracted.topic.has_value(),
          label + ": a topic the envelope rule declines was admitted");
}

}  // namespace

int main() {
  // The complete envelope, read as before.
  admitted("complete envelope", numbered_topic({}), "Recording", "h3", "h3");

  // SC09-138 FRONT: no chdlevel, no csourcefn, empty ST, csummary 0 0 0.
  {
    const auto label = std::string("front matter topic");
    auto extracted = run(front_matter_topic(), "");
    require(extracted.topic.has_value(),
            label + ": the topic was rejected: " + extracted.error);
    if (extracted.topic) {
      const auto &topic = *extracted.topic;
      require(topic.heading_level == "h1",
              label + ": heading level '" + topic.heading_level + "'");
      require(topic.heading_form.empty(),
              label + ": heading form '" + topic.heading_form + "'");
      require(topic.title.empty(), label + ": title '" + topic.title + "'");
      require(topic_text(topic).find_first_not_of(' ') == std::string::npos,
              label + ": the body is not empty: '" + topic_text(topic) + "'");
      require(topic.index_terms.size() == 2,
              label + ": " + std::to_string(topic.index_terms.size()) +
                  " index terms, expected 2");
    }
  }

  admitted("no chdlevel", numbered_topic({"chdlevel"}), "Recording", "h1",
           "");
  admitted("no csourcefn", numbered_topic({"csourcefn"}), "Recording", "h3",
           "h3");
  admitted("neither chdlevel nor csourcefn",
           numbered_topic({"chdlevel", "csourcefn"}), "Recording", "h1", "");
  admitted("only the topic number",
           numbered_topic({"cparent", "cforwardlevel", "cbacklevel",
                           "csummary", "chdlevel", "csourcefn"}),
           "Recording", "h1", "");
  admitted("chdlevel ahead of csummary", numbered_topic({}, "chdlevel"),
           "Recording", "h3", "h3");
  admitted("csourcefn ahead of csummary, no chdlevel",
           numbered_topic({"chdlevel"}, "csourcefn"), "Recording", "h1", "");

  // A metadata control written twice.
  {
    auto lines = numbered_topic({});
    lines.insert(lines.begin() + 7, Line{words("chdlevel"), words(":H2")});
    declined("repeated chdlevel", lines, "Recording");
  }
  {
    auto lines = numbered_topic({"csourcefn"});
    lines.insert(lines.begin() + 2, Line{words("ctopicn"), words("8")});
    declined("repeated ctopicn", lines, "Recording");
  }
  // Visible text where the metadata stops and the title has not begun.
  {
    auto lines = numbered_topic({"chdlevel", "csourcefn"});
    lines.insert(lines.begin() + 6,
                 Line{words("   "), words("Stray"), words("words.")});
    declined("text ahead of the title", lines, "Recording");
  }
  // A metadata control the envelope ended before.
  {
    auto lines = numbered_topic({"csourcefn"});
    lines.insert(lines.begin() + 8, Line{words("csourcefn"),
                                         words("HCPLOGR")});
    declined("metadata after the title", lines, "Recording");
  }
  // No title line at all: the reader emits a heading only for an `ST`
  // line, so the topic is its body alone (EDCUSER 8.4.2.1, hosted DT
  // 19900523000000).
  {
    auto lines = numbered_topic({"chdlevel"});
    lines.erase(lines.begin() + 7);
    auto extracted = run(lines, "Recording");
    require(extracted.topic.has_value(),
            "no ST: the topic was rejected: " + extracted.error);
    if (extracted.topic) {
      require(extracted.topic->untitled && extracted.topic->title.empty(),
              "no ST: the topic was given a title");
      require(topic_text(*extracted.topic).find("An installation may write.") !=
                  std::string::npos,
              "no ST: the body is missing: '" +
                  topic_text(*extracted.topic) + "'");
    }
  }
  // The envelope must open with the topic-start control.
  {
    auto lines = numbered_topic({});
    lines.erase(lines.begin());
    declined("no topic-start control", lines, "Recording");
  }
  return 0;
}
