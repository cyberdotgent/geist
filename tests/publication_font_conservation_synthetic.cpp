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
#include "geist/detail/render/html_visible_text.hpp"
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
  } else if (style == "blank_")
    // SC24-5527-02 COMMENTS record 978: `_` spans over blank columns.
    line({"cfont", "3", "5", "2", "11", "3", "_"});
  else if (style == "worded_")
    line({"cfont", "3", "17", "_"});
  else if (style == "overlap_")
    line({"cfont", "3", "5", "2", "3", "17", "_"});
  else if (style == "past_")
    line({"cfont", "3", "5", "2", "14", "4", "_"});
  else
    line({"cfont", "3", "5", style});
  if (style == "blank_" || style == "past_")
    line({"   ", "Alpha", "     ", "YES"});
  else
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
  if (expected_markup == "!") {
    // The typed route must decline rather than drop the underscores.
    const auto document = try_lower_topic_to_document_ir(
        {"1.0", "Related Publications", ":H2", 1, 1, 2}, records, nullptr,
        &error, nullptr);
    require(!document.has_value(),
            "unmodelled font code " + style + " was typed: " +
                (document ? render_document_html_fragment(*document) : ""));
  } else if (!expected_markup.empty()) {
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
      // Markdown keeps its own conventions whatever element HTML chose.
      if (style == "C" || style == "V")
        require(render_document_markdown(*document).find("*Alpha*") !=
                    std::string::npos,
                "Markdown flattened source " + style + " styling: " +
                    render_document_markdown(*document));
      if (style == "E" || style == "P" || style == "X" || style == "4")
        require(render_document_markdown(*document).find("`Alpha`") !=
                        std::string::npos &&
                    render_document_markdown(*document).find('<') ==
                        std::string::npos,
                "Markdown did not keep " + style + " a code span: " +
                    render_document_markdown(*document));
      // The conservation reader must see the slot as visible text.
      if (style == "blank_") {
        std::string visible;
        try {
          visible = html_visible_text(html);
        } catch (const std::exception &e) {
          visible = std::string("threw: ") + e.what();
        }
        require(visible.find("Alpha ___YES") != std::string::npos,
                "underline slot is not visible text: " + visible);
      }
      // Markdown has no underline; the slot is written as inline HTML.
      if (style == "blank_")
        require(render_document_markdown(*document).find(
                    "<u class=\"geist-underline\">___</u>") !=
                    std::string::npos,
                "Markdown lost the underline of a `_` slot: " +
                    render_document_markdown(*document));
    }
  }
}
} // namespace

int main() {
  // HTML serves each phrase code in the element hosted BookServer uses
  // (doc/boo-spec/markup.adoc, the message-entry census): `C` (CIT)
  // `<cite>`, `E` (XMP) `<samp>`, `P` (PK) `<kbd>`, `V` (PV) `<var>`.  `X`
  // (XPH) and `4` (HP4) are hosted `<tt>`, obsolete in HTML5, and stay
  // `<code>`.
  check("C", true, false,
        "<cite class=\"geist-emphasis geist-citation\">Alpha</cite>");
  check("E", true, false,
        "<samp class=\"geist-monospace geist-sample\">Alpha</samp>");
  check("P", true, false,
        "<kbd class=\"geist-monospace geist-keyboard\">Alpha</kbd>");
  check("X", true, false, "<code class=\"geist-monospace\">Alpha</code>");
  check("4", true, false, "<code class=\"geist-monospace\">Alpha</code>");
  check("V", true, false,
        "<var class=\"geist-emphasis geist-variable\">Alpha</var>");
  check("2", true, false, "<strong class=\"geist-strong\">Alpha</strong>");
  // `K` (H4) is italic, not bold: hosted BookServer serves every K span of
  // 30 sampled books as `<I>`, e.g. SC09-138 1.1.3 (DT 19910321130500)
  // `cfont 3 6 K 10 12 K` -> `<I>Online</I> <I>Publications</I>`.
  check("K", true, false, "<em class=\"geist-emphasis\">Alpha</em>");
  // `T` (TP) and `Y` (MDQUAL) are hosted `<B>`, `B` (CAUTION) and `F`
  // (CAUTIONTEXT) `<b>`, `D` (DANGER) and `O` (DANGERTEXT) `<strong>`: one
  // bold element per covered word in every sampled book (doc/boo-spec/
  // markup.adoc, the bold-code census), e.g. GC23-0084-02 5.4.1
  // (DT 19920925130324) `cfont 5 1 T 6 1 T 7 1 T` ->
  // `<B>(</B><B>1</B><B>)</B>`.
  for (const auto *bold : {"T", "Y", "B", "F", "D", "O"})
    check(bold, true, false, "<strong class=\"geist-strong\">Alpha</strong>");
  check("_", true, false, "");
  // `_` (UNDERSCORE) underscores every column it covers, blank or not: hosted
  // BookServer serves SC24-5527-02 COMMENTS (DT 19921218151459) record 978
  // `cfont ... 29 3 _ 32 3 2 37 3 _ ...` as `<u>___</u><B>YES</B>  <u>___</u>
  // <B>NO</B>`, and SL26-0018-02 COVER `cfont 3 17 _` as
  // `<u>CMS/TSO_Pipelines</u>`.
  check("blank_", true, false,
        "<strong class=\"geist-strong\">Alpha</strong> "
        "<u class=\"geist-underline\">___</u>YES");
  check("worded_", true, false,
        "<u class=\"geist-underline\">Alpha_(SC31-1234)</u>");
  // Where `_` overlaps another style, hosted underscores only the first gap
  // (SG24-3910-01 2.1, SH35-0069-06 4.1.2.2); a slot past the row's last
  // column is padded by hosted.  Neither is modelled: both fail closed.
  check("overlap_", true, false, "!");
  check("past_", true, false, "!");
  check("mixed", true, false, "");
  check("0", true, true, "");
  check("C", false, true, "");
  check("prefixC", true, true, "");
  return 0;
}
