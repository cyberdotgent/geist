// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/picture_placement_ir.hpp"
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
std::optional<DocumentIR>
lower(std::vector<DecodedLogicalRecordSource> records) {
  std::string error;
  const std::set<std::string> resources{"1", "2"};
  auto doc = try_lower_topic_to_document_ir(
      {"1.1", "Example", ":H2", 10, 20, 22}, records, nullptr, &error, nullptr,
      &resources);
  check(doc.has_value(), "ordered picture declined: " + error);
  return doc;
}

std::optional<DocumentIR>
lower(std::vector<DecodedLogicalRecordSource> records,
      FigureSourceBlockIR* source_figure) {
  std::string error;
  const std::set<std::string> resources{"1", "2"};
  const auto layout = extract_layout_ir(records);
  const auto ownership = build_ownership_ir(records, layout);
  const auto selectors = extract_selector_catalog_ir(records, &error);
  check(selectors.has_value(), "selector extraction failed: " + error);
  if (!selectors) return std::nullopt;
  const auto figures = extract_figure_blocks_ir(
      records, layout, ownership, *selectors, resources);
  check(figures.blocks.size() == 1,
        "synthetic ordered figure was not uniquely extracted");
  if (figures.blocks.size() != 1) return std::nullopt;
  *source_figure = figures.blocks.front();
  return lower(std::move(records));
}
void ordered(const std::string &output,
             const std::vector<std::string> &expected) {
  std::size_t after = 0;
  for (const auto &value : expected) {
    const auto at = output.find(value, after);
    check(at != std::string::npos, "lost/reordered '" + value + "': " + output);
    if (at == std::string::npos)
      return;
    check(output.find(value, at + value.size()) == std::string::npos,
          "duplicated '" + value + "'");
    after = at + value.size();
  }
}
void label_and_number_only_caption() {
  Builder b;
  b.heading();
  b.line({w("SRFIGEXAMPLE")});
  b.line({w("cz"), w("OFF"), w("FIG")});
  b.line({w("cselect"), w("3"), w("9"), w("PIC1")});
  b.line({w("   "), w("PICTURE"), w("1")});
  b.line({});
  b.line({w("cfont"), w("3"), w("4"), w("1"), w("8"), w("5"), w("1"), w("14"),
          w("3"), w("1")});
  b.line({w("   "), w("9402"), w("Model"), w("236")});
  b.line({});
  b.line({w("   "), w("Figure"), w("1.")});
  b.line({w("SREFIG")});
  b.line({w("cz"), w("FLOW"), w("P"), w("3"), w("3")});
  b.line({w("   "), w("Following prose.")});
  auto d = lower({b.finish(20)});
  if (!d)
    return;
  const auto html = render_document_html_fragment(*d);
  ordered(html, {"resource:1", "9402", "Model", "236", "Figure 1.",
                 "Following prose."});
  check(html.find("<em class=\"geist-emphasis\">9402") != std::string::npos,
        "model label lost CFONT emphasis");
  ordered(render_document_markdown(*d), {"resource:1", "9402", "Model", "236",
                                         "Figure 1", "Following prose"});
}
void caption_before_image_and_cross_record() {
  Builder a;
  a.heading();
  a.line({w("SRFIGEXAMPLE")});
  a.line({w("cz"), w("OFF"), w("FIG")});
  a.line({w("   "), w("Figure  7."), w("Scenario begins")});
  a.line({w("              "), w("and continues.")});
  a.line({w("cselect"), w("3"), w("9"), w("PIC1")});
  Builder b;
  b.line({w("   "), w("PICTURE"), w("1")});
  b.line({w("SREFIG")}, {1, '.'});
  b.line({w("cz"), w("FLOW"), w("P"), w("3"), w("3")});
  b.line({w("   "), w("Following prose.")});
  auto d = lower({a.finish(20), b.finish(21)});
  if (!d)
    return;
  check(render_document_html_fragment(*d).find("Scenario begins and continues.") !=
            std::string::npos,
        "caption continuation used normalized rather than native columns");
  ordered(
      render_document_html_fragment(*d),
      {"Scenario begins", "and continues.", "resource:1", "Following prose."});
  ordered(render_document_markdown(*d), {"Scenario begins", "and continues",
                                         "resource:1", "Following prose"});
}
void label_before_image_and_colon_caption() {
  Builder b;
  b.heading();
  b.line({w("SRFIGEXAMPLE")});
  b.line({w("cz"), w("OFF"), w("FIG")});
  b.line({w("   "), w("Panel label")});
  b.line({w("cselect"), w("3"), w("9"), w("PIC1")});
  b.line({w("   "), w("PICTURE"), w("1")});
  b.line({w("csartdesc"), w("1")});
  b.line({w("SRPIC1")});
  b.line({w("cartdesc"), w("Description before formatting.")});
  b.line({w("cvfont"), w("3"), w("9"), w("X")});
  b.line({w("cartdesc"), w("Description after formatting.")});
  b.line({w("ceartdesc")});
  b.line({w("SI"), TokenWords{'S', 'N', 'A', 0xb0, 'p', 's'}});
  b.line({w(" | "), w("Figure"), w("2:"), w("Gateway diagram")});
  b.line({w("SREFIG")});
  auto d = lower({b.finish(20)});
  if (!d)
    return;
  for (const auto &out :
       {render_document_html_fragment(*d), render_document_markdown(*d)}) {
    ordered(out, {"Panel label", "resource:1", "Gateway diagram"});
    check(out.find("SNA") == std::string::npos,
          "index operand leaked into figure");
    check(out.find("| Figure") == std::string::npos,
          "revision margin leaked into caption text");
    check(out.find("geist-paragraph\">cvfont") == std::string::npos &&
              out.find("\n\ncvfont") == std::string::npos,
          "description formatting leaked into visible figure text");
  }
}
void cross_book_label_retains_lnk_contract() {
  Builder b;
  b.heading();
  b.line({w("SRFIGEXAMPLE")});
  b.line({w("cz"), w("OFF"), w("FIG")});
  b.line({w("cselect"), w("3"), w("6"), w("LNK"), w("<BOOK>"), w("<>"),
          w("<>"), w("<SC00-0000>"), w("<ANY>"), w("<TARGET>")});
  b.line({w("   "), w("Manual")});
  b.line({w("cselect"), w("3"), w("9"), w("PIC1")});
  b.line({w("   "), w("PICTURE"), w("1")});
  b.line({w("SREFIG")});
  FigureSourceBlockIR source;
  auto d = lower({b.finish(20)}, &source);
  if (!d) return;
  const auto html = render_document_html_fragment(*d);
  check(html.find("geist-link--book-contents geist-link--unresolved") !=
            std::string::npos &&
            html.find(">Manual</a>") != std::string::npos,
        "picture text LNK lost its typed unresolved cross-book target: " +
            html);
  check(render_document_markdown(*d).find("[Manual](<#>)") !=
            std::string::npos,
        "picture text LNK gained an invented Markdown destination");

  std::string error;
  const auto canonical = lower_figure_block_to_document_blocks(source, &error);
  check(canonical.has_value(), "ordered figure re-lowering failed: " + error);
  if (!canonical) return;
  const auto find_reference = [](std::vector<BlockIR>& blocks)
      -> InlineIR* {
    for (auto& block : blocks) {
      if (auto* paragraph = std::get_if<ParagraphBlockIR>(&block.node))
        for (auto& in : paragraph->content)
          if (std::holds_alternative<CrossReferenceInlineIR>(in.node))
            return &in;
      if (auto* figure = std::get_if<FigureBlockIR>(&block.node))
        for (auto& in : figure->caption)
          if (std::holds_alternative<CrossReferenceInlineIR>(in.node))
            return &in;
    }
    return nullptr;
  };
  auto mutated = *canonical;
  auto* reference = find_reference(mutated);
  check(reference != nullptr,
        "ordered figure did not lower its LNK label to a reference");
  if (reference == nullptr) return;
  reference->origin.detail += " mutated";
  check(!verify_figure_document_blocks(source, mutated),
        "figure verifier ignored mutated inline label provenance");
  mutated = *canonical;
  reference = find_reference(mutated);
  auto& claim =
      std::get<CrossReferenceInlineIR>(reference->node).targets.front();
  check(claim.control_origin.has_value(),
        "ordered figure LNK lost control provenance");
  if (claim.control_origin) claim.control_origin->detail += " mutated";
  check(!verify_figure_document_blocks(source, mutated),
        "figure verifier ignored mutated selector provenance");
}
void placement_uses_columns_and_never_borrows_neighbouring_text() {
  std::vector<DisplayLineCellIR> cells;
  for (const auto word : TokenWords{0x2666, ' ', ' ', 'P', 'I', 'C', 'T', 'U',
                                    'R', 'E', ' ', '1', ' ', ' ', 'x'})
    cells.push_back({word});
  const auto span = picture_placement_span(cells, 2, 12, "1");
  check(span && span->begin == 2 && span->end == 14,
        "padded picture span did not use display columns");
  check(!picture_placement_span(cells, 2, 13, "1"),
        "picture span swallowed neighbouring text");
  check(!picture_placement_span(cells, 3, 8, "1"),
        "picture span borrowed its missing number");
  check(!picture_placement_span(cells, 3, 9, "2"),
        "picture span ignored the selected resource identity");
  check(!picture_placement_span(cells, 3, 0, "1") &&
            !picture_placement_span(cells, cells.size(), 9, "1"),
        "empty/out-of-range picture span was admitted");
}
} // namespace
int main() {
  label_and_number_only_caption();
  caption_before_image_and_cross_record();
  label_before_image_and_colon_caption();
  cross_book_label_retains_lnk_contract();
  placement_uses_columns_and_never_borrows_neighbouring_text();
  return 0;
}
