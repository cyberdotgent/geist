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
#include "geist/detail/render/html_visible_text.hpp"
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

InlineIR text_inline(const std::string &value) {
  InlineIR text;
  text.node = TextInlineIR{value};
  text.origin.derivation = DocumentDerivationIR::semantic_lowering;
  return text;
}

template <typename T> BlockIR body_block(T node) {
  BlockIR block;
  block.node = std::move(node);
  block.origin.derivation = DocumentDerivationIR::semantic_lowering;
  return block;
}

// Body marks: a revised run inside a paragraph, a revised list item and a
// revised table row.  HTML marks each with `geist-revised` and the mark;
// Markdown has no margin and prints the text only.
void test_body_marks() {
  DocumentIR document;
  document.topic.id = "1.0";
  InlineSequenceIR paragraph;
  paragraph.push_back(text_inline("Plain. "));
  RevisedInlineIR revised;
  revised.mark = U'#';
  revised.children.push_back(text_inline("Changed."));
  InlineIR wrapper;
  wrapper.node = revised;
  wrapper.origin.derivation = DocumentDerivationIR::semantic_lowering;
  paragraph.push_back(wrapper);
  document.blocks.push_back(body_block(ParagraphBlockIR{paragraph}));

  ListBlockIR list;
  ListItemIR item;
  item.content.push_back(text_inline("Defining the font size"));
  item.origin.derivation = DocumentDerivationIR::semantic_lowering;
  item.revision_mark = char32_t{0x2502};
  list.items.push_back(item);
  document.blocks.push_back(body_block(std::move(list)));

  TableBlockIR table;
  TableRowIR row;
  row.origin.derivation = DocumentDerivationIR::semantic_lowering;
  row.revision_mark = char32_t{0x2502};
  TableCellIR cell;
  cell.origin.derivation = DocumentDerivationIR::semantic_lowering;
  cell.content.push_back(text_inline("Y"));
  row.cells.push_back(cell);
  table.rows.push_back(row);
  document.blocks.push_back(body_block(std::move(table)));

  std::string error;
  require(verify_document_ir(document, &error),
          "a document with body marks does not verify: " + error);
  const auto html = render_document_html_blocks(document, {});
  require(html.find("Plain. <span class=\"geist-revised\" "
                    "data-geist-revision-mark=\"#\">Changed.</span>") !=
              std::string::npos,
          "HTML does not mark the revised run: " + html);
  require(html.find("<li class=\"geist-list-item geist-revised\" "
                    "data-geist-revision-mark=\"|\">") != std::string::npos,
          "HTML does not mark the revised item: " + html);
  require(html.find("<tr class=\"geist-table-row geist-revised\" "
                    "data-geist-revision-mark=\"|\">") != std::string::npos,
          "HTML does not mark the revised row: " + html);
  // The conservation check reads the fragment's visible text: the marks are
  // attributes, never text, and the revised words are still there.
  const auto visible = html_visible_text(html);
  require(visible.find("Plain. Changed.") != std::string::npos &&
              visible.find("Defining the font size") != std::string::npos,
          "the visible text of the revised fragment lost words: " + visible);
  const auto markdown = render_document_markdown(document);
  require(markdown.find(escape_markdown_text("Plain. Changed.")) !=
              std::string::npos,
          "Markdown does not print the revised run as plain text: " +
              markdown);
  require(markdown.find('#') == std::string::npos ||
              markdown.find("\\#") == std::string::npos,
          "Markdown prints the body mark");
  const auto formatted = format_document_ir(document);
  require(formatted.find("revised=") != std::string::npos &&
              formatted.find("revision_mark=") != std::string::npos,
          "the formatted IR does not show the body marks");

  // A revised run holds ordinary inlines only, and at least one.
  auto nested = document;
  {
    auto &content =
        std::get<ParagraphBlockIR>(nested.blocks.front().node).content;
    auto &outer = std::get<RevisedInlineIR>(content.back().node);
    outer.children.push_back(wrapper);
  }
  require(!verify_document_ir(nested), "a nested revised run verifies");
  auto empty = document;
  std::get<RevisedInlineIR>(
      std::get<ParagraphBlockIR>(empty.blocks.front().node).content.back().node)
      .children.clear();
  require(!verify_document_ir(empty), "an empty revised run verifies");
  auto blank = document;
  std::get<ListBlockIR>(blank.blocks[1].node).items.front().revision_mark =
      U' ';
  require(!verify_document_ir(blank), "a blank item revision mark verifies");
}

} // namespace

int main() {
  test_heading_mark();
  test_body_marks();
  geist_test::exit_with_failures();
  std::cout << "heading revision mark checks passed\n";
  return 0;
}
