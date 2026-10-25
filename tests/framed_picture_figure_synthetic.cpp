// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A picture inside a drawn frame.  The compiler draws the frame of a
// picture figure as display rows -- a top rule, rails, the `PICTURE n`
// placeholder row between the rails, a bottom rule -- and hosted BookServer
// serves those rows verbatim inside its `<pre>`, with the `<img>` standing
// at the selector's column and the selector's covered columns printed blank
// (G544-3305-00 2.3.1, DT 19910508143433:
// `   | <a href="picture-1?mode=zoom"><img ... alt="PICTURE 1"></a>` followed
// by the row's own blanks and its right rail).  The frame is no longer
// dropped: HTML keeps the rows with the image inline, Markdown splits the
// drawing around an image block at that row.

#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "geist/detail/lowering/figure_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "test_failures.hpp"

#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace geist::detail;
TokenWords w(const std::string &text) { return {text.begin(), text.end()}; }
void check(bool value, const std::string &message) {
  if (!value) {
    std::cerr << message << '\n';
    geist_test::record_failure();
  }
}
struct Builder {
  DecodedLogicalRecordSource r;
  std::uint16_t encoded = 0x100;
  void line(std::vector<TokenWords> content, TokenWords prefix = {0x25ba}) {
    r.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    r.tokens.push_back(std::move(prefix));
    for (auto &token : content) {
      r.encoded_tokens.push_back({encoded++, 2});
      r.tokens.push_back(std::move(token));
    }
  }
  void heading() {
    line({w("sh1.1")});
    line({w("ctopicn"), w("10")});
    line({w("cparent"), w("1.0")});
    line({w("cforwardlevel")});
    line({w("cbacklevel")});
    line({w("csummary"), w("10"), w("0"), w("10")});
    line({w("chdlevel"), w(":H2")});
    line({w("csourcefn"), w("SAMPLE")});
    line({w("ST"), w("Example")});
  }
  DecodedLogicalRecordSource finish(std::uint32_t number) {
    r.logical_record = number;
    r.assembled = assemble_logical_record_with_sources(r.tokens);
    r.ir.logical_record = number;
    std::uint32_t byte = 1;
    for (std::size_t t = 0; t < r.tokens.size(); ++t) {
      const auto e = r.encoded_tokens[t];
      const bool spacing = !r.tokens[t].empty() && r.tokens[t][0] < 4;
      r.ir.tokens.push_back({t,
                             e,
                             r.tokens[t],
                             {byte, static_cast<std::uint32_t>(byte + e.width)},
                             spacing,
                             spacing ? r.tokens[t][0] : std::uint16_t{3}});
      byte += e.width;
    }
    r.ir.payload_range = {1, byte};
    assign_display_line_framing(r.ir);
    r.control_segments = decode_control_segments(
        number, r.assembled, r.encoded_tokens, r.ir.display_lines);
    demote_display_line_owned_controls(r);
    return std::move(r);
  }
};

std::optional<DocumentIR> lower(std::vector<DecodedLogicalRecordSource> records,
                                FigureSourceBlockIR *figure = nullptr) {
  std::string error;
  const std::set<std::string> resources{"1", "2"};
  if (figure != nullptr) {
    const auto layout = extract_layout_ir(records);
    const auto ownership = build_ownership_ir(records, layout);
    const auto selectors = extract_selector_catalog_ir(records, &error);
    check(selectors.has_value(), "selector extraction failed: " + error);
    if (!selectors)
      return std::nullopt;
    const auto figures = extract_figure_blocks_ir(records, layout, ownership,
                                                  *selectors, resources);
    check(figures.blocks.size() == 1, "framed figure was not extracted");
    if (figures.blocks.size() != 1)
      return std::nullopt;
    *figure = figures.blocks.front();
  }
  auto doc = try_lower_topic_to_document_ir(
      {"1.1", "Example", ":H2", 10, 20, 22}, records, nullptr, &error, nullptr,
      &resources);
  check(doc.has_value(), "framed picture declined: " + error);
  return doc;
}

std::size_t count(const std::string &text, const std::string &needle) {
  std::size_t n = 0;
  for (auto at = text.find(needle); at != std::string::npos;
       at = text.find(needle, at + needle.size()))
    ++n;
  return n;
}

// The frame of G544-3305-00 2.3.1 record 43, narrowed: `SRFIGMEM1`, the top
// rule, a rail row, `cselect 5 9 PIC1` over `PICTURE 1` between the rails,
// the bottom rule and the caption.
void build_memo(Builder &b, bool label = false) {
  b.heading();
  b.line({w("   "), w("Here is an example.")});
  b.line({w("SRFIGMEM1")});
  b.line({w("    "), w("_______________")});
  b.line({w("   "), w("|"), w("              "), w("|")});
  if (label) {
    b.line({w("cfont"), w("5"), w("4"), w("1")});
    b.line({w("   "), w("|"), w("Memo"), w("         "), w("|")});
  }
  b.line({w("cselect"), w("5"), w("9"), w("PIC1")});
  b.line({w("   "), w("|"), w("PICTURE"), w("1"), w("    "), w("|")});
  b.line({w("   "), w("|_______________|")});
  b.line({w("   "), w("Figure"), w("1."), w("Internal Memo")});
  b.line({w("SREFIG")});
  b.line({w("   "), w("Following prose.")});
}

void frame_rows_stay_with_the_image_inline() {
  Builder b;
  build_memo(b);
  FigureSourceBlockIR figure;
  auto d = lower({b.finish(43)}, &figure);
  if (!d)
    return;
  std::string error;
  check(verify_document_ir(*d, &error), "framed document invalid: " + error);
  const auto html = render_document_html_fragment(*d);
  // The rails are the source's own rows, inside one `<pre>`.
  check(html.find("    _______________\n") != std::string::npos,
        "frame top rule dropped: " + html);
  check(html.find("   |               |\n") != std::string::npos,
        "frame rail row dropped: " + html);
  check(html.find("   |_______________|") != std::string::npos,
        "frame bottom rule dropped: " + html);
  // The image stands at the selector column; the covered columns print
  // blank and the right rail keeps its place after them.
  const auto img = html.find("<img");
  check(img != std::string::npos && html.rfind("   | ", img) + 5 == img,
        "image does not stand at the selector column: " + html);
  const auto img_end = html.find('>', img);
  check(img_end != std::string::npos &&
            html.compare(img_end + 1, 14, "              ") == 0 &&
            html.compare(img_end + 15, 1, "|") == 0,
        "placeholder columns are not blank before the right rail: " + html);
  check(html.find("alt=\"PICTURE 1\"") != std::string::npos,
        "image lost its placeholder alt text: " + html);
  check(html.find("<figure") == std::string::npos,
        "framed picture still served as a figure: " + html);
  check(count(html, "<pre") == 1, "frame rows split across blocks: " + html);
  check(html.find("PICTURE 1</") == std::string::npos &&
            html.find(">PICTURE 1") == std::string::npos,
        "placeholder words printed beside the image: " + html);
  check(html.find("id=\"FIGMEM1\"") != std::string::npos,
        "figure anchor lost: " + html);
  check(html.find("Figure 1. Internal Memo") != std::string::npos,
        "caption lost: " + html);
  check(html.find("Following prose.") != std::string::npos,
        "prose after the figure lost: " + html);

  const auto md = render_document_markdown(*d);
  // Markdown splits the drawing around an image block at the picture row:
  // rows above, the image, then the picture row (blanked) and the rows
  // below.
  const auto top = md.find("    _______________\n");
  const auto image = md.find("![PICTURE 1](<resource:1>)");
  const auto blanked = md.find("   |               |\n", image);
  const auto bottom = md.find("   |_______________|");
  check(top != std::string::npos && image != std::string::npos &&
            blanked != std::string::npos && bottom != std::string::npos &&
            top < image && image < blanked && blanked < bottom,
        "markdown does not split the frame around the image: " + md);
  check(md.find("PICTURE 1 ") == std::string::npos &&
            md.find("|PICTURE") == std::string::npos,
        "markdown printed the placeholder inside the frame: " + md);
  check(md.find("<img") == std::string::npos,
        "markdown fell back to raw HTML for an unstyled frame: " + md);

  // The verifier re-lowers the figure and rejects a changed image cell.
  std::string lowering_error;
  const auto canonical =
      lower_figure_block_to_document_blocks(figure, &lowering_error);
  check(canonical.has_value(), "framed figure re-lowering failed: " +
                                   lowering_error);
  if (!canonical)
    return;
  auto mutated = *canonical;
  bool changed = false;
  for (auto &block : mutated)
    if (auto *pre = std::get_if<PreformattedBlockIR>(&block.node))
      for (auto &row : pre->styled_lines)
        for (auto &in : row)
          if (auto *picture = std::get_if<ImageInlineIR>(&in.node)) {
            picture->resource = "resource:2";
            changed = true;
          }
  check(changed, "framed figure lowered no inline image");
  check(!verify_figure_document_blocks(figure, mutated),
        "figure verifier ignored a mutated inline image");
}

// A styled label inside the frame keeps its CFONT style in the frame row.
void styled_label_inside_the_frame() {
  Builder b;
  build_memo(b, true);
  auto d = lower({b.finish(43)});
  if (!d)
    return;
  const auto html = render_document_html_fragment(*d);
  check(html.find("   | <em class=\"geist-emphasis\">Memo</em>          |") !=
            std::string::npos,
        "frame label lost its row position or style: " + html);
  check(count(html, "<pre") == 1, "frame rows split across blocks: " + html);
}

// DocumentIR: an image inside a styled preformatted row occupies exactly the
// cells it names; the row still flattens to its literal source row.
void image_cells_keep_row_geometry() {
  DocumentIR doc;
  doc.topic.id = "1.1";
  BlockIR block;
  block.origin.derivation = DocumentDerivationIR::synthesized;
  PreformattedBlockIR pre;
  pre.lines = {"| PICTURE 1 |"};
  DocumentNodeOriginIR line;
  line.derivation = DocumentDerivationIR::synthesized;
  pre.line_origins = {line};
  ImageInlineIR image{"resource:1", "PICTURE 1"};
  image.cells = "PICTURE 1";
  pre.styled_lines = {{{TextInlineIR{"| "}, line},
                       {image, line},
                       {TextInlineIR{" |"}, line}}};
  block.node = pre;
  doc.blocks.push_back(block);
  std::string error;
  check(verify_document_ir(doc, &error),
        "image over its own cells rejected: " + error);
  auto wrong = doc;
  std::get<ImageInlineIR>(
      std::get<PreformattedBlockIR>(wrong.blocks[0].node).styled_lines[0][1]
          .node)
      .cells = "PICTURE 12";
  check(!verify_document_ir(wrong, &error),
        "image claiming other cells accepted");
  auto bare = doc;
  std::get<ImageInlineIR>(
      std::get<PreformattedBlockIR>(bare.blocks[0].node).styled_lines[0][1]
          .node)
      .cells.clear();
  check(!verify_document_ir(bare, &error),
        "image without cells accepted inside a preformatted row");
}
} // namespace

int main() {
  image_cells_keep_row_geometry();
  frame_rows_stay_with_the_image_inline();
  styled_label_inside_the_frame();
  return 0;
}
