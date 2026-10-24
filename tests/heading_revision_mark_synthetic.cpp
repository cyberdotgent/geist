// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A topic heading prints the revision mark its `ST` line stores in front of
// the topic number.
//
// The cell glued directly after the `ST` opcode is the title's revision
// column (doc/boo-spec/logical-controls.adoc, the title line).  Hosted
// BookServer prints the mark there in front of the number, never inside the
// title: SC41-5401-00 2.1.2.5 (DT 19970421115044)
// `<H3>| 2.1.2.5   CSLBUFSIZE (Console buffer size)</H3>`, ACPZMST1 5.4
// (DT 19920319123146) `<H2>| 5.4   /etc/inittab ...</H2>` for a stored
// `U+2502`, SC33-0668-00 2.5.4.1 `<h6>| 2.5.4.1   101</h6>`.
//
// HTML prints the mark as hosted does; Markdown prints it as plain text in
// the same place.  A mark that is no drawn glyph does not verify.

#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "test_failures.hpp"

#include <iostream>
#include <string>
#include <utility>

namespace {

using namespace geist::detail;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "heading_revision_mark_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

DocumentIR heading_document(std::optional<char32_t> mark) {
  DocumentIR document;
  document.topic.id = "2.1.2.5";
  document.topic.title = "CSLBUFSIZE (Console buffer size)";
  HeadingBlockIR heading;
  heading.level = 3;
  InlineIR text;
  text.node = TextInlineIR{"2.1.2.5 CSLBUFSIZE (Console buffer size)"};
  text.origin.derivation = DocumentDerivationIR::semantic_lowering;
  heading.content.push_back(std::move(text));
  heading.revision_mark = mark;
  BlockIR block;
  block.node = std::move(heading);
  block.origin.derivation = DocumentDerivationIR::semantic_lowering;
  document.blocks.push_back(std::move(block));
  return document;
}

void test_heading_mark() {
  for (const auto &[mark, printed] :
       {std::pair<char32_t, std::string>{U'|', "|"},
        std::pair<char32_t, std::string>{char32_t{0x2502}, "|"},
        std::pair<char32_t, std::string>{U'#', "#"}}) {
    const auto document = heading_document(mark);
    std::string error;
    require(verify_document_ir(document, &error),
            "a heading with a revision mark does not verify: " + error);
    const auto html = render_document_html_blocks(document, {});
    require(html.find(">" + printed + " 2.1.2.5 CSLBUFSIZE") !=
                std::string::npos,
            "HTML does not print the mark before the number: " + html);
    const auto markdown = render_document_markdown(document);
    require(markdown.find("### " + escape_markdown_text(printed) + " " +
                          escape_markdown_text("2.1.2.5 CSLBUFSIZE")) !=
                std::string::npos,
            "Markdown does not print the mark before the number: " +
                markdown);
    require(format_document_ir(document).find("revision_mark=") !=
                std::string::npos,
            "the formatted IR does not show the revision mark");
  }

  const auto plain = heading_document(std::nullopt);
  const auto html = render_document_html_blocks(plain, {});
  require(html.find(">2.1.2.5 CSLBUFSIZE") != std::string::npos,
          "a heading without a mark changed: " + html);
  require(format_document_ir(plain).find("revision_mark=") ==
              std::string::npos,
          "a heading without a mark formats one");

  for (const char32_t bad : {U' ', char32_t{0}, char32_t{0xFFFF}}) {
    const auto document = heading_document(bad);
    require(!verify_document_ir(document),
            "a heading revision mark that is no drawn glyph verifies");
  }
}

} // namespace

int main() {
  test_heading_mark();
  geist_test::exit_with_failures();
  std::cout << "heading revision mark checks passed\n";
  return 0;
}
