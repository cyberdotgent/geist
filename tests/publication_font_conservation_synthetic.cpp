// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/publication_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/layout_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "test_failures.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace geist::detail;
void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
    geist_test::record_failure();
  }
}

DecodedLogicalRecordSource source(const std::string &style, bool framed) {
  DecodedLogicalRecordSource record;
  record.logical_record = 1;
  std::uint16_t key = 0x100;
  const auto line = [&](const std::vector<std::string> &words) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(words.size() * 2), 1});
    record.tokens.push_back({0x2534});
    for (const auto &word : words) {
      record.encoded_tokens.push_back({key++, 2});
      record.tokens.emplace_back(word.begin(), word.end());
    }
  };
  line({"sh1.0"});
  line({"ctopicn", "1"});
  line({"cparent", "1"});
  line({"cforwardlevel"});
  line({"cbacklevel"});
  line({"csummary", "1", "0", "1"});
  line({"chdlevel", ":H2"});
  line({"csourcefn", "SAMPLE"});
  line({"ST", "  ", "Related", "Publications"});
  if (style == "mixed")
    line({"cfont", "3", "5", "0", "9", "11", "C"});
  else if (style == "prefixC") {
    // The arbitrary dictionary spelling of a native length byte can form a
    // syntactically valid FONT segment. It is not an actual instruction.
    line({"3", "5", "C"});
    record.tokens[record.tokens.size() - 4] = {'c', 'f', 'o', 'n', 't'};
  } else
    line({"cfont", "3", "5", style});
  line({"   ", "Alpha", "(SC31-1234)"});
  record.assembled = assemble_logical_record_with_sources(record.tokens);
  record.ir.logical_record = 1;
  std::uint32_t byte = 0;
  for (std::size_t token = 0; token < record.tokens.size(); ++token) {
    const auto encoded = record.encoded_tokens[token];
    record.ir.tokens.push_back({token,
                                encoded,
                                record.tokens[token],
                                {byte, byte + encoded.width},
                                false,
                                3});
    byte += encoded.width;
  }
  record.ir.payload_range = {0, byte};
  assign_display_line_framing(record.ir);
  record.control_segments = decode_control_segments(
      1, record.assembled, record.encoded_tokens, record.ir.display_lines);
  demote_display_line_owned_controls(record);
  if (!framed) {
    record.ir.display_lines_parse = false;
    for (auto &token : record.ir.tokens)
      token.framing = TokenFramingRole::unframed;
  }
  return record;
}

void check(const std::string &style, bool framed, bool admitted,
           const std::string &expected_markup) {
  const std::vector<DecodedLogicalRecordSource> records{source(style, framed)};
  const auto layout = extract_layout_ir(records);
  std::string error;
  const auto ownership = build_verified_ownership_ir(records, layout, &error);
  require(ownership.has_value(), "font fixture ownership failed: " + error);
  if (!ownership)
    return;
  const auto publication =
      extract_publication_catalog_ir(records, layout, *ownership);
  require(publication.has_value() == admitted,
          "text-only publication admission lost native font code " + style);
  if (!expected_markup.empty()) {
    TypedLoweringTraceIR trace;
    const auto document = try_lower_topic_to_document_ir(
        {"1.0", "Related Publications", ":H2", 1, 1, 2}, records, nullptr,
        &error, &trace);
    require(document.has_value(),
            "styled publication has no typed route: " + error);
    if (document) {
      const auto html = render_document_html_fragment(*document);
      require(trace.family != "publication catalog" &&
                  html.find(expected_markup) != std::string::npos,
              "publication route flattened source styling: " + html);
      if (style == "C")
        require(render_document_markdown(*document).find("*Alpha*") !=
                    std::string::npos,
                "Markdown flattened source citation styling");
    }
  }
}
} // namespace

int main() {
  check("C", true, false, "<em class=\"geist-emphasis\">Alpha</em>");
  check("2", true, false, "<strong class=\"geist-strong\">Alpha</strong>");
  // `K` (H4) is italic, not bold: hosted BookServer serves every K span of
  // 30 sampled books as `<I>`, e.g. SC09-138 1.1.3 (DT 19910321130500)
  // `cfont 3 6 K 10 12 K` -> `<I>Online</I> <I>Publications</I>`.
  check("K", true, false, "<em class=\"geist-emphasis\">Alpha</em>");
  check("_", true, false, "");
  check("mixed", true, false, "");
  check("0", true, true, "");
  check("C", false, true, "");
  check("prefixC", true, true, "");
  return 0;
}
