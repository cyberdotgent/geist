// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "geist/detail/ir/prose/prose_topic_internal.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace geist::detail;

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "topic_title_display_line_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t encoded = 0x100;

  void line(const std::vector<std::string>& words) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * words.size()), 1});
    record.tokens.push_back({0x25ba});
    for (const auto& word : words) {
      record.encoded_tokens.push_back({encoded++, 2});
      record.tokens.emplace_back(word.begin(), word.end());
    }
  }

  DecodedLogicalRecordSource build() {
    record.logical_record = 617;
    record.assembled = assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = 617;
    std::uint32_t byte = 1;
    for (std::size_t token = 0; token < record.tokens.size(); ++token) {
      const auto value = record.encoded_tokens[token];
      record.ir.tokens.push_back(
          {token, value, record.tokens[token],
           {byte, static_cast<std::uint32_t>(byte + value.width)}, false, 3});
      byte += value.width;
    }
    record.ir.payload_range = {1, byte};
    assign_display_line_framing(record.ir);
    record.control_segments = decode_control_segments(
        617, record.assembled, record.encoded_tokens, record.ir.display_lines);
    demote_display_line_owned_controls(record);
    return std::move(record);
  }
};

void check_title(const std::vector<std::string>& title_words,
                 const std::string& expected,
                 bool next_line_anchor = false) {
  RecordBuilder builder;
  builder.line({"sh6.4.4"});
  builder.line({"ctopicn", "263"});
  builder.line({"cparent", "6.4"});
  builder.line({"cforwardlevel"});
  builder.line({"cbacklevel", "6.4.3"});
  builder.line({"csummary", "20", "0", "20"});
  builder.line({"chdlevel", ":H4"});
  builder.line({"csourcefn", "HCPA4SRV"});
  auto title = title_words;
  title.insert(title.begin(), "ST");
  builder.line(title);
  if (!expected.empty()) builder.line({});
  if (next_line_anchor) builder.line({"SRBODY"});
  builder.line({"The", "body", "survives"});
  const std::vector<DecodedLogicalRecordSource> records{builder.build()};
  require(records.front().ir.display_lines_parse,
          "synthetic display framing did not parse");
  TopicIdentityIR identity;
  identity.id = "6.4.4";
  identity.title = expected.empty() ? "Catalog title" : expected;
  identity.heading_level = ":H4";
  identity.topic_number = 263;
  identity.start_logical_record = 617;
  identity.end_logical_record = 618;
  std::string error;
  const auto document = try_lower_topic_to_document_ir(
      identity, records, nullptr, &error);
  require(document.has_value(), "topic declined: " + error);
  if (!document) return;
  const auto markdown = render_document_markdown(*document);
  const auto heading_end = markdown.find('\n');
  const auto heading = markdown.substr(0, heading_end);
  require(heading == "#### 6\\.4\\.4" +
                         (expected.empty() ? "" : " " + expected),
          "unexpected heading: " + heading);
  require(markdown.find("The body survives") != std::string::npos,
          "body lost: " + markdown);
  const auto tail = markdown.substr(heading_end);
  if (!expected.empty())
    require(tail.find(expected) == std::string::npos,
            "title duplicated into body: " + markdown);
  if (next_line_anchor)
    require(tail.find("id=\"BODY\"") != std::string::npos,
            "real next-line anchor was swallowed by title: " + markdown);
  const auto html = render_document_html_fragment(*document);
  if (!expected.empty()) {
    const auto title_at = html.find(expected);
    require(title_at != std::string::npos &&
                html.find(expected, title_at + expected.size()) ==
                    std::string::npos,
            "HTML lost or duplicated title: " + html);
    require(title_at < html.find("</h4>"),
            "HTML title extends outside heading: " + html);
  } else {
    require(html.find("6.4.4</h4>") != std::string::npos,
            "empty HTML heading absorbed body text: " + html);
  }
}

// Without framing, a segment end remains a boundary. The line-builder input
// isolates that guard: the two ordinary tokens would form one title if the
// new continuation rule ignored the absence of a proven display line.
void unknown_framing_keeps_the_segment_boundary() {
  using namespace geist::detail::prose_internal;
  RecordBuilder builder;
  builder.line({"Update", "SRVBLDS"});
  auto record = builder.build();
  record.ir.display_lines_parse = false;
  const std::vector<DecodedLogicalRecordSource> records{std::move(record)};
  Ledger ledger(records);
  std::vector<Item> items(3);
  items[0].token = view_token(records, 0, 1);
  items[0].title_start = true;
  items[1].kind = ItemKind::segment_end;
  items[2].token = view_token(records, 0, 2);
  LineBuild output;
  std::string error;
  require(build_lines(records, items, ledger, output, &error),
          "unframed boundary check declined: " + error);
  require(output.title == "Update",
          "unknown framing merged segments into title: " + output.title);
  require(ledger.at(0, 2).role == ProseTokenRoleIR::text,
          "unframed following segment was swallowed into heading");
}
} // namespace

int main() {
  // Issue #89: SRVBLDS is a title word inside the ST line, not an anchor.
  check_title({"Update", "the", "SRVBLDS", "Table", "After", "Manual",
               "Build", "Processing"},
              "Update the SRVBLDS Table After Manual Build Processing");
  check_title({"Ordinary", "title"}, "Ordinary title", true);
  check_title({"Update", "SRVBLDS", "and", "SRVAPPS", "Tables"},
              "Update SRVBLDS and SRVAPPS Tables");
  check_title({}, "", true);
  check_title({"/etc/inittab", "File", "Definitions"},
              "/etc/inittab File Definitions");
  unknown_framing_keeps_the_segment_boundary();
  return 0;
}
