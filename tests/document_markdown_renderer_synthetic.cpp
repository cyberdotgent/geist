// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "geist/detail/render/markdown_visible_text.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace geist::detail;

bool require(bool condition, const std::string &message) {
  if (!condition)
    std::cerr << "document_markdown_renderer_synthetic: " << message << '\n';
  return condition;
}

InlineIR in(InlineNodeIR node) {
  InlineIR result;
  result.node = std::move(node);
  result.origin.derivation = DocumentDerivationIR::semantic_lowering;
  return result;
}

InlineSequenceIR text(std::string value) {
  return {in(TextInlineIR{std::move(value)})};
}

template <typename T> BlockIR block(T node) {
  BlockIR result;
  result.node = std::move(node);
  result.origin.derivation = DocumentDerivationIR::semantic_lowering;
  return result;
}

TableCellIR cell(InlineSequenceIR content = {}) {
  TableCellIR result;
  result.content = std::move(content);
  result.origin.derivation = DocumentDerivationIR::semantic_lowering;
  return result;
}

TableRowIR row(std::vector<TableCellIR> cells) {
  TableRowIR result;
  result.cells = std::move(cells);
  result.origin.derivation = DocumentDerivationIR::semantic_lowering;
  return result;
}

DocumentNodeOriginIR origin() {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::semantic_lowering;
  return result;
}

bool contains(const std::string &output, const std::string &fragment,
              const std::string &message) {
  return require(output.find(fragment) != std::string::npos, message);
}

} // namespace

int main() {
  DocumentIR document;
  document.topic.id = "MARKDOWN";
  document.topic.title = "Markdown renderer";

  InlineSequenceIR rich;
  rich.push_back(in(TextInlineIR{"literal * [x] &copy; <tag>"}));
  rich.push_back(in(HardBreakInlineIR{}));
  rich.push_back(in(EmphasisInlineIR{"em_phasis"}));
  rich.push_back(in(TextInlineIR{" and "}));
  rich.push_back(in(CodeInlineIR{"edge ``ticks`` edge"}));
  rich.push_back(in(TextInlineIR{" then "}));
  rich.push_back(in(CrossReferenceInlineIR{
      {CrossReferenceTargetKindIR::external,
       "https://example.test/chapter one?q=a&b"},
      "a [link]"}));
  // The displayed label and the selector control naming its destination are
  // distinct source claims. Only the former belongs in the rendered-word
  // trace; the latter remains attached to the typed reference itself.
  rich.back().origin.slices.push_back({10, 1, 2, 3, 100, 104});
  auto &sourced_reference =
      std::get<CrossReferenceInlineIR>(rich.back().node);
  sourced_reference.targets.front().control_origin = origin();
  sourced_reference.targets.front().control_origin->detail =
      "synthetic selector target";
  sourced_reference.targets.front().control_origin->slices.push_back(
      {10, 0, 0, 1, 80, 90});
  rich.push_back(in(TextInlineIR{"; "}));
  rich.push_back(in(CrossReferenceInlineIR{
      {CrossReferenceTargetKindIR::topic, "Chapter One"}, "topic"}));
  rich.push_back(in(TextInlineIR{"; "}));
  rich.push_back(in(CrossReferenceInlineIR{
      {CrossReferenceTargetKindIR::anchor, "section one"}, "section"}));
  rich.push_back(in(TextInlineIR{"; "}));
  rich.push_back(in(CrossReferenceInlineIR{
      {CrossReferenceTargetKindIR::resource, "manuals/a b.pdf"}, "manual"}));
  rich.push_back(in(TextInlineIR{" and "}));
  rich.push_back(in(ImageInlineIR{"image one(2).png", "alt [image]"}));
  rich.push_back(in(TextInlineIR{" plus "}));
  rich.push_back(in(OpaqueInlineIR{"legacy*span", "raw `value`"}));

  document.blocks.push_back(block(HeadingBlockIR{2, text("Heading #1")}));
  document.blocks.push_back(block(ParagraphBlockIR{rich}));
  document.blocks.push_back(block(AnchorBlockIR{"A\"<&'\n"}));

  ListBlockIR unordered;
  unordered.items = {{text("first - item"), origin()},
                     {text("second"), origin()}};
  document.blocks.push_back(block(std::move(unordered)));
  ListBlockIR ordered;
  ordered.ordered = true;
  ordered.items = {{text("one"), origin()}, {text("two"), origin()}};
  document.blocks.push_back(block(std::move(ordered)));
  ListBlockIR source_numbered;
  source_numbered.ordered = true;
  source_numbered.items = {{text("nine"), origin(), 9},
                           {text("ten"), origin(), 10},
                           {text("twelve"), origin(), 12}};
  document.blocks.push_back(block(std::move(source_numbered)));

  DefinitionListBlockIR definitions;
  definitions.entries = {
      {text("Term"), text("Definition [literal]"), origin()}};
  document.blocks.push_back(block(std::move(definitions)));

  TableBlockIR table;
  table.header_rows = 2;
  table.rows = {
      row({cell(text("Primary")), cell()}),
      row({cell(text("Secondary")), cell(text("Value"))}),
      row({cell(text("pipe | value")), cell({in(CodeInlineIR{"a|`b`"})})})};
  document.blocks.push_back(block(std::move(table)));

  TableBlockIR headerless;
  headerless.rows = {row({cell(text("body")), cell()})};
  document.blocks.push_back(block(std::move(headerless)));

  document.blocks.push_back(block(ThematicBreakBlockIR{}));

  document.blocks.push_back(
      block(PreformattedBlockIR{{"source", "```", "tail without newline"}}));

  // A preformatted block that carries a cross reference cannot be a fence: a
  // fence renders the anchor as inert text, and hosted BookServer keeps the
  // anchors of a drawn body inside the `<pre>` of the very same rows.  The
  // row's own bytes are untouched -- only `&`, `<` and `>` are escaped, and
  // nothing occupying a column is inserted -- so the drawn box rules stay in
  // their columns and the marked span becomes the anchor's text.
  PreformattedBlockIR linked;
  linked.lines = {"   | see <Notices> & more    |", "   |___________________|"};
  VerbatimLinkIR in_book;
  in_book.begin = 9;   // the `<` of `<Notices>`
  in_book.end = 18;    // past the `>`
  in_book.kind = VerbatimLinkKindIR::in_book;
  in_book.target = "HDRNOTICES";
  linked.line_links = {{in_book}, {}};
  document.blocks.push_back(block(std::move(linked)));
  document.blocks.push_back(
      block(NoteBlockIR{text("Caution"), text("Do *this* safely")}));

  PublicationListBlockIR publications;
  publications.entries = {
      {text("Book [A]"),
       {text("First paragraph."), text("Second paragraph.")},
       origin()}};
  document.blocks.push_back(block(std::move(publications)));

  FigureBlockIR figure;
  figure.resource = "figures/a b(1).png";
  figure.caption = text("Figure * one");
  document.blocks.push_back(block(std::move(figure)));

  // A book resource is named by its picture, exactly as hosted BookServer
  // names it (`alt="PICTURE 69"`, GG24-395 3.3.8 DT 19941215160749).
  FigureBlockIR book_figure;
  book_figure.resource = "resource:69";
  book_figure.caption = text("Figure * two");
  document.blocks.push_back(block(std::move(book_figure)));
  document.blocks.push_back(
      block(FootnoteBlockIR{"note ] one", text("Footnote body")}));

  IndexGroupBlockIR index;
  index.heading = text("Index A-Z");
  index.entries = {{text("Alpha [entry]"), "#alpha one", origin()},
                   {text("Beta"), "other(topic)", origin()}};
  document.blocks.push_back(block(std::move(index)));
  MenuBlockIR menu;
  menu.items = {{{CrossReferenceTargetKindIR::topic, "2.1.1"},
                 "Displaying [Status]", origin()},
                {{CrossReferenceTargetKindIR::topic, "2.1.2"}, "Checking",
                 origin()}};
  document.blocks.push_back(block(std::move(menu)));
  document.blocks.push_back(
      block(OpaqueBlockIR{"control*kind", "raw ``` payload\nsecond line"}));

  std::string error;
  if (!require(verify_document_ir(document, &error), error))
    return 1;
  auto invalid_target = document;
  auto &invalid_inlines =
      std::get<ParagraphBlockIR>(invalid_target.blocks[1].node).content;
  std::get<CrossReferenceInlineIR>(invalid_inlines[6].node)
      .targets.front().target.kind =
      static_cast<CrossReferenceTargetKindIR>(99);
  error.clear();
  if (!require(!verify_document_ir(invalid_target, &error) &&
                   error == "inline node is not canonical",
               "verifier admitted an invalid cross-reference target kind"))
    return 1;
  const auto formatted = format_document_ir(document);
  if (!contains(formatted,
                "xref=\"a [link]\" claim={kind=external value=\"https://example.test/chapter one?q=a&b\"",
                "formatter omitted external target kind") ||
      !contains(formatted,
                "control_origin={ origin=semantic:\"synthetic selector "
                "target\" source=(lr=10 seg=0 tok=0:1 bytes=80:90)}",
                "formatter omitted cross-reference target provenance") ||
      !contains(formatted, "claim={kind=topic value=\"Chapter One\"",
                "formatter omitted topic target kind") ||
      !contains(formatted, "claim={kind=anchor value=\"section one\"",
                "formatter omitted anchor target kind") ||
      !contains(formatted, "claim={kind=resource value=\"manuals/a b.pdf\"",
                "formatter omitted resource target kind") ||
      !contains(formatted, "item=ordinal=9 [text=\"nine\"",
                "formatter omitted an explicit list source ordinal") ||
      !contains(formatted, "item=ordinal=12 [text=\"twelve\"",
                "formatter omitted a nonconsecutive list source ordinal"))
    return 1;
  auto empty_menu = document;
  std::get<MenuBlockIR>(empty_menu.blocks[empty_menu.blocks.size() - 2].node)
      .items.clear();
  if (!require(!verify_document_ir(empty_menu, &error) &&
                   error == "menu is empty",
               "verifier admitted an empty menu block"))
    return 1;
  auto anchor_menu = document;
  std::get<MenuBlockIR>(anchor_menu.blocks[anchor_menu.blocks.size() - 2].node)
      .items.front()
      .target.kind = CrossReferenceTargetKindIR::anchor;
  if (!require(!verify_document_ir(anchor_menu, &error) &&
                   error == "menu item is incomplete",
               "verifier admitted a non-topic menu item target"))
    return 1;
  auto invalid_target_origin = document;
  auto &invalid_reference = std::get<CrossReferenceInlineIR>(
      std::get<ParagraphBlockIR>(invalid_target_origin.blocks[1].node)
          .content[6]
          .node);
  invalid_reference.targets.front().control_origin->derivation =
      DocumentDerivationIR::decoded;
  invalid_reference.targets.front().control_origin->slices.clear();
  error.clear();
  if (!require(!verify_document_ir(invalid_target_origin, &error) &&
                   error == "decoded node origin names no source slice",
               "verifier admitted source-free target provenance"))
    return 1;
  auto no_claims = document;
  std::get<CrossReferenceInlineIR>(
      std::get<ParagraphBlockIR>(no_claims.blocks[1].node).content[6].node)
      .targets.clear();
  error.clear();
  if (!require(!verify_document_ir(no_claims, &error) &&
                   error == "inline node is not canonical",
               "verifier admitted a reference with no target claims"))
    return 1;
  auto incomplete_book_claim = document;
  auto& incomplete = std::get<CrossReferenceInlineIR>(
      std::get<ParagraphBlockIR>(incomplete_book_claim.blocks[1].node)
          .content[6]
          .node)
                         .targets.front()
                         .target;
  incomplete.kind = CrossReferenceTargetKindIR::book_heading;
  incomplete.alternatives = {"HDR", "", "", "", "", "TARGET"};
  incomplete.value = "TARGET";
  error.clear();
  if (!require(!verify_document_ir(incomplete_book_claim, &error) &&
                   error == "inline node is not canonical",
               "verifier admitted an incomplete cross-book claim"))
    return 1;
  CrossReferenceTargetIR canonical_heading{
      CrossReferenceTargetKindIR::book_heading, "TARGET"};
  canonical_heading.alternatives = {"HDR", "INSTALL", "unused", "SC00-0000",
                                    "ANY", "TARGET"};
  canonical_heading.document_number = "SC00-0000";
  canonical_heading.document_level = "ANY";
  canonical_heading.heading_anchor = "INSTALL";
  const auto rejects_heading_mutation = [&](auto mutate,
                                             const std::string& message) {
    auto candidate = document;
    auto& target = std::get<CrossReferenceInlineIR>(
        std::get<ParagraphBlockIR>(candidate.blocks[1].node).content[6].node)
                       .targets.front()
                       .target;
    target = canonical_heading;
    mutate(target);
    std::string mutation_error;
    return require(!verify_document_ir(candidate, &mutation_error) &&
                       mutation_error == "inline node is not canonical",
                   message);
  };
  if (!rejects_heading_mutation(
          [](auto& target) {
            target.kind = CrossReferenceTargetKindIR::book_contents;
          },
          "verifier admitted a tuple whose kind token disagrees") ||
      !rejects_heading_mutation(
          [](auto& target) { target.alternatives.resize(5); },
          "verifier admitted a short LNK tuple") ||
      !rejects_heading_mutation(
          [](auto& target) { target.alternatives.push_back("x");
                             target.alternatives.push_back("y"); },
          "verifier admitted a long LNK tuple") ||
      !rejects_heading_mutation(
          [](auto& target) { target.alternatives[0] = "BOOK"; },
          "verifier ignored the LNK kind slot") ||
      !rejects_heading_mutation(
          [](auto& target) { target.alternatives[1] = "OTHER"; },
          "verifier ignored the LNK heading slot") ||
      !rejects_heading_mutation(
          [](auto& target) { target.alternatives[3] = "SC00-9999"; },
          "verifier ignored the LNK document slot") ||
      !rejects_heading_mutation(
          [](auto& target) { target.alternatives[4] = "OLD"; },
          "verifier ignored the LNK level slot") ||
      !rejects_heading_mutation(
          [](auto& target) { target.alternatives[5] = "OTHER"; },
          "verifier ignored the LNK identity slot") ||
      !rejects_heading_mutation(
          [](auto& target) { target.document_number = "SC00-9999"; },
          "verifier ignored derived document number") ||
      !rejects_heading_mutation(
          [](auto& target) { target.document_level = "OLD"; },
          "verifier ignored derived document level") ||
      !rejects_heading_mutation(
          [](auto& target) { target.heading_anchor = "OTHER"; },
          "verifier ignored derived heading anchor") ||
      !rejects_heading_mutation(
          [](auto& target) { target.url = "https://unexpected.test"; },
          "verifier admitted an irrelevant URL field"))
    return 1;
  auto ordinary_with_lnk_metadata = document;
  std::get<CrossReferenceInlineIR>(
      std::get<ParagraphBlockIR>(ordinary_with_lnk_metadata.blocks[1].node)
          .content[6]
          .node)
      .targets.front()
      .target.document_number = "SC00-0000";
  error.clear();
  if (!require(!verify_document_ir(ordinary_with_lnk_metadata, &error) &&
                   error == "inline node is not canonical",
               "verifier admitted LNK metadata on an ordinary target"))
    return 1;
  if (!contains(formatted,
                "menu items=[target=\"2.1.1\" label=\"Displaying [Status]\"",
                "formatter omitted the menu block items"))
    return 1;
  const auto fallback_output = render_document_markdown(document);
  if (!contains(fallback_output, "[topic](<Chapter%20One>)",
                "context-free topic fallback changed semantic identity") ||
      // The reader-generated `Subtopics:` lead and `<id> ` label prefix are
      // renderer expansions of the typed menu block; the unresolved
      // destination is the legacy `#<id>` form.
      !contains(fallback_output,
                "Subtopics:\n\n"
                "- [2\\.1\\.1 Displaying \\[Status\\]](<#2.1.1>)\n"
                "- [2\\.1\\.2 Checking](<#2.1.2>)",
                "context-free menu block expansion changed"))
    return 1;

  DocumentMarkdownRendererOptions options;
  options.resolve_cross_reference =
      [](const CrossReferenceTargetIR &target)
      -> std::optional<std::string> {
    if (target.kind == CrossReferenceTargetKindIR::topic)
      return "topics/chapter-one.md";
    if (target.kind == CrossReferenceTargetKindIR::resource)
      return "assets/manual.pdf";
    return std::nullopt;
  };
  DocumentRenderTraceIR render_trace;
  const auto output = render_document_markdown(document, options, &render_trace);

  CrossReferenceTargetIR unresolved_book{
      CrossReferenceTargetKindIR::book_contents, "BOOKTARGET"};
  unresolved_book.alternatives = {"BOOK", "", "", "SC00-0000", "ANY",
                                  "BOOKTARGET"};
  unresolved_book.document_number = "SC00-0000";
  unresolved_book.document_level = "ANY";
  CrossReferenceInlineIR claimed;
  claimed.targets = {{unresolved_book, std::nullopt},
                     {{CrossReferenceTargetKindIR::external,
                       "https://example.test/second"},
                      std::nullopt}};
  claimed.targets[0].control_origin = origin();
  claimed.targets[0].control_origin->slices.push_back(
      {20, 0, 0, 1, 200, 210});
  claimed.targets[1].control_origin = origin();
  claimed.targets[1].control_origin->slices.push_back(
      {20, 1, 1, 2, 300, 310});
  claimed.label = "one label";
  claimed.label_code = true;
  DocumentIR claim_document;
  claim_document.topic.id = "CLAIMS";
  claim_document.topic.title = "Claims";
  claim_document.blocks.push_back(
      block(ParagraphBlockIR{{in(std::move(claimed))}}));
  DocumentMarkdownRendererOptions claim_options;
  claim_options.resolve_cross_reference = [](const auto& target)
      -> std::optional<std::string> {
    if (target.kind == CrossReferenceTargetKindIR::external)
      return "resolved-second.md";
    return std::nullopt;
  };
  DocumentRenderTraceIR claims_trace;
  const auto claims_output =
      render_document_markdown(claim_document, claim_options, &claims_trace);
  const auto destination_trace = std::find_if(
      claims_trace.spans.begin(), claims_trace.spans.end(),
      [](const auto& span) { return span.reason == "link destination"; });
  if (!contains(claims_output, "[`one label`](<resolved-second.md>)",
                "multi-claim resolver order or styled label failed") ||
      !require(claims_output.find("one label") ==
                   claims_output.rfind("one label"),
               "multi-claim Markdown repeated its visible label") ||
      !contains(render_document_markdown(claim_document),
                "[`one label`](<#>)",
                "unresolved cross-book claim gained a fallback") ||
      !require(destination_trace != claims_trace.spans.end() &&
                   destination_trace->origin &&
                   destination_trace->origin->slices.size() == 1 &&
                   destination_trace->origin->slices.front().byte_begin == 300,
               "resolved destination trace did not use the selected claim"))
    return 1;
  const auto label_trace = std::find_if(
      render_trace.spans.begin(), render_trace.spans.end(),
      [](const auto &span) { return span.reason == "link label"; });
  if (!require(label_trace != render_trace.spans.end() &&
                   label_trace->origin.has_value() &&
                   label_trace->origin->slices.size() == 1 &&
                   label_trace->origin->slices.front().byte_begin == 100 &&
                   label_trace->origin->slices.front().byte_end == 104,
               "rendered link-label trace included selector-control bytes"))
    return 1;
  if (!require(output == render_document_markdown(document, options),
               "rendering the same typed IR was not deterministic") ||
      !require(!output.empty() && output.back() == '\n',
               "typed output has no canonical final newline") ||
      !contains(output, "## Heading \\#1", "heading/text escaping failed") ||
      !contains(output,
                "literal \\* \\[x\\] &amp;copy; \\<tag\\>  \n"
                "*em\\_phasis*",
                "prose escaping or explicit hard break failed") ||
      !contains(output, "```edge ``ticks`` edge```",
                "variable-length inline code delimiter failed") ||
      !contains(output,
                "[a \\[link\\]](<https://example.test/chapter%20one?q=a&b>)",
                "external link label/destination escaping failed") ||
      !contains(output, "[topic](<topics/chapter-one.md>)",
                "topic destination resolution failed") ||
      !contains(output, "[section](<#section%20one>)",
                "anchor destination resolution failed") ||
      !contains(output, "[manual](<assets/manual.pdf>)",
                "resource destination resolution failed") ||
      !contains(output, "![alt \\[image\\]](<image%20one(2).png>)",
                "inline image escaping failed") ||
      !contains(output, "`` legacy*span: raw `value` ``",
                "opaque inline did not use a safe code span") ||
      !contains(output, "<a id=\"A&quot;&lt;&amp;&#39;&#xA;\"></a>",
                "HTML anchor attribute escaping failed") ||
      !contains(output, "- first \\- item\n- second",
                "unordered list structure failed") ||
      !contains(output, "1. one\n1. two", "ordered list structure failed") ||
      !contains(output, "9. nine\n10. ten\n12. twelve",
                "ordered list source ordinals were not preserved") ||
      !contains(output, "- **Term:** Definition \\[literal\\]",
                "definition list structure failed"))
    return 1;

  auto invalid_list = document;
  auto &unordered_with_ordinal =
      std::get<ListBlockIR>(invalid_list.blocks[3].node);
  unordered_with_ordinal.items.front().source_ordinal = 1;
  error.clear();
  if (!require(!verify_document_ir(invalid_list, &error) &&
                   error == "unordered list item has a source ordinal",
               "verifier admitted an ordinal on an unordered list"))
    return 1;

  invalid_list = document;
  auto &partially_numbered =
      std::get<ListBlockIR>(invalid_list.blocks[4].node);
  partially_numbered.items.front().source_ordinal = 1;
  error.clear();
  if (!require(!verify_document_ir(invalid_list, &error) &&
                   error == "ordered list source ordinals are incomplete",
               "verifier admitted partially explicit source ordinals"))
    return 1;

  invalid_list = document;
  auto &nonincreasing = std::get<ListBlockIR>(invalid_list.blocks[5].node);
  nonincreasing.items[1].source_ordinal = 9;
  error.clear();
  if (!require(
          !verify_document_ir(invalid_list, &error) &&
              error ==
                  "ordered list source ordinals are not strictly increasing",
          "verifier admitted repeated source ordinals"))
    return 1;

  if (!contains(output,
                "| Primary<br>Secondary | <br>Value |\n"
                "| --- | --- |\n"
                "| pipe \\| value | `` a\\|`b` `` |",
                "multi-header table aggregation or cell escaping failed") ||
      !contains(output, "|  |  |\n| --- | --- |\n| body |  |",
                "headerless table/empty cell policy failed") ||
      !contains(output, "| body |  |\n\n---\n\n````",
                "semantic thematic break was not rendered between blocks") ||
      !contains(output, "````\nsource\n```\ntail without newline\n````",
                "variable-length preformatted fence failed") ||
      !contains(output,
                "<pre>\n"
                "   | see <a href=\"#HDRNOTICES\">&lt;Notices&gt;</a> "
                "&amp; more    |\n"
                "   |___________________|\n"
                "</pre>",
                "linked preformatted block did not render as a <pre> with "
                "the anchor inside its row") ||
      !contains(output, "> **Caution:** Do \\*this\\* safely",
                "note structure failed") ||
      !contains(output,
                "- **Book \\[A\\]**\n\n  First paragraph\\.\n\n"
                "  Second paragraph\\.",
                "publication structure failed") ||
      // The image's alt text names the picture, never the caption: hosted
      // BookServer serves `alt="/bookmgr/monetcoq.jpg"` for an external image
      // (XWEBDEMO 1.4.1) and `alt="PICTURE 69"` for a book resource
      // (GG24-395 3.3.8), with the caption served separately below it.
      !contains(output,
                "![figures/a b\\(1\\)\\.png](<figures/a%20b(1).png>)\n\n"
                "*Figure \\* one*",
                "figure resource, alt text, or caption failed") ||
      !contains(output,
                "![PICTURE 69](<resource:69>)\n\n*Figure \\* two*",
                "book-resource figure alt text failed") ||
      !contains(output, "[^note%20%5D%20one]: Footnote body",
                "footnote label normalization failed") ||
      !contains(output,
                "**Index A\\-Z**\n\n"
                "- [Alpha \\[entry\\]](<#alpha%20one>)\n"
                "- [Beta](<other(topic)>)",
                "index group structure failed") ||
      !contains(output,
                "Subtopics:\n\n"
                "- [2\\.1\\.1 Displaying \\[Status\\]]"
                "(<topics/chapter-one.md>)\n"
                "- [2\\.1\\.2 Checking](<topics/chapter-one.md>)",
                "resolved menu block structure failed") ||
      !contains(output,
                "**Opaque control\\*kind content:**\n\n"
                "````\nraw ``` payload\nsecond line\n````",
                "opaque block escaping/fencing failed"))
    return 1;

  // Compound containers retain real block boundaries. These shapes are the
  // minimum needed by message fields: prose may continue in a second
  // paragraph and then open a list without being flattened into one line.
  DocumentIR compound;
  compound.topic.id = "COMPOUND";
  compound.topic.title = "Compound content";

  ListBlockIR nested;
  ListItemIR nested_item;
  nested_item.content = text("Nested child.");
  nested_item.origin = origin();
  nested.items.push_back(std::move(nested_item));

  ListItemIR compound_item;
  compound_item.origin = origin();
  compound_item.blocks.push_back(
      block(ParagraphBlockIR{text("First paragraph.")}));
  compound_item.blocks.push_back(
      block(ParagraphBlockIR{text("Second paragraph.")}));
  compound_item.blocks.push_back(block(std::move(nested)));
  compound_item.blocks.push_back(
      block(PreformattedBlockIR{{" |   row one", "   row two"}}));
  ListBlockIR compound_list;
  compound_list.items.push_back(std::move(compound_item));
  compound.blocks.push_back(block(std::move(compound_list)));

  DefinitionEntryIR compound_definition;
  compound_definition.term = text("Meaning:");
  compound_definition.origin = origin();
  compound_definition.definition_blocks.push_back(
      block(ParagraphBlockIR{text("Definition first.")}));
  compound_definition.definition_blocks.push_back(
      block(ParagraphBlockIR{text("Definition second.")}));
  DefinitionListBlockIR compound_definitions;
  compound_definitions.entries.push_back(std::move(compound_definition));
  DefinitionEntryIR empty_definition;
  empty_definition.empty_term = true;
  empty_definition.origin = origin();
  empty_definition.definition_blocks.push_back(
      block(ParagraphBlockIR{text("Untitled definition.")}));
  compound_definitions.entries.push_back(std::move(empty_definition));
  compound.blocks.push_back(block(std::move(compound_definitions)));

  ListBlockIR cell_list;
  ListItemIR cell_item;
  cell_item.content = text("Cell list item.");
  cell_item.origin = origin();
  cell_list.items.push_back(std::move(cell_item));
  TableCellIR compound_cell;
  compound_cell.origin = origin();
  compound_cell.blocks.push_back(
      block(ParagraphBlockIR{text("Cell paragraph.")}));
  compound_cell.blocks.push_back(block(std::move(cell_list)));
  TableBlockIR compound_table;
  compound_table.rows = {row({std::move(compound_cell)})};
  compound.blocks.push_back(block(std::move(compound_table)));

  error.clear();
  if (!require(verify_document_ir(compound, &error),
               "compound DocumentIR was rejected: " + error))
    return 1;
  const auto compound_output = render_document_markdown(compound);
  if (!contains(compound_output,
                "- First paragraph\\.\n\n"
                "    Second paragraph\\.\n\n"
                "    - Nested child\\.",
                "compound list item lost paragraph/list structure") ||
      !contains(compound_output,
                "\n\n    ```\n     |   row one\n       row two\n    ```",
                "compound preformatted rows were not indented verbatim") ||
      !require(compound_output.find(" \n") == std::string::npos &&
                   compound_output.find("\n    \n") == std::string::npos,
               "compound block replay appended trailing whitespace") ||
      !contains(compound_output,
                "<dl class=\"geist-definition-list\">"
                "<dt class=\"geist-definition-term\">Meaning:</dt>"
                "<dd class=\"geist-definition-description\">"
                "<p class=\"geist-paragraph\">Definition first.</p>"
                "<p class=\"geist-paragraph\">Definition second.</p></dd>"
                "<dt class=\"geist-definition-term\"></dt>"
                "<dd class=\"geist-definition-description\">"
                "<p class=\"geist-paragraph\">Untitled definition.</p>"
                "</dd></dl>",
                "compound definition lost paragraph structure") ||
      !require(compound_output.find("**:**") == std::string::npos,
               "empty definition term fabricated Markdown punctuation") ||
      !contains(compound_output, "<table",
                "compound table did not use its block-capable HTML form") ||
      !contains(compound_output, "Cell paragraph.",
                "compound table lost its paragraph") ||
      !contains(compound_output, "<ul",
                "compound table lost its nested list") ||
      !contains(compound_output, "Cell list item.",
                "compound table lost its list item") ||
      !contains(compound_output, "</table>",
                "compound table HTML form was not closed"))
    return 1;

  // A hard break inside a paragraph is a CommonMark hard line break (two
  // trailing spaces), and the line after it never becomes a setext heading
  // underline: `=` is not otherwise escaped, so a line that starts with it
  // is guarded. Plain rows after a break are emitted unchanged.
  {
    DocumentIR broken;
    broken.topic.id = "BREAKS";
    InlineSequenceIR lines;
    lines.push_back(in(TextInlineIR{"HELP MSG (where number"}));
    lines.push_back(in(HardBreakInlineIR{}));
    lines.push_back(in(TextInlineIR{"and type)"}));
    lines.push_back(in(HardBreakInlineIR{}));
    lines.push_back(in(TextInlineIR{"==="}));
    broken.blocks.push_back(block(ParagraphBlockIR{std::move(lines)}));
    DocumentRenderTraceIR break_trace;
    const auto output = render_document_markdown(broken, {}, &break_trace);
    if (!contains(output, "HELP MSG \\(where number  \nand type\\)  \n\\===",
                  "hard breaks did not render as guarded Markdown line "
                  "breaks: " + output) ||
        !require(markdown_visible_text(output, break_trace)
                         .find("HELP MSG (where number\nand type)\n===") !=
                     std::string::npos,
                 "hard break visible text drifted: " +
                     markdown_visible_text(output, break_trace)))
      return 1;
  }

  // Two emphasis runs with nothing between them cannot both be Markdown
  // delimiter runs: `*ft***,**` glues the closer of one to the opener of the
  // next, and CommonMark then leaves `**,**` as literal asterisks. Such runs
  // are written as inline HTML, which pairs unambiguously; a run with text
  // or a space on both sides keeps its Markdown delimiters.
  {
    DocumentIR abutting;
    abutting.topic.id = "ABUT";
    InlineSequenceIR line;
    line.push_back(in(EmphasisInlineIR{"fn", EmphasisKindIR::emphasis}));
    line.push_back(in(TextInlineIR{" "}));
    line.push_back(in(EmphasisInlineIR{"ft", EmphasisKindIR::emphasis}));
    line.push_back(in(EmphasisInlineIR{",", EmphasisKindIR::strong}));
    line.push_back(in(TextInlineIR{" tail"}));
    abutting.blocks.push_back(block(ParagraphBlockIR{std::move(line)}));
    DocumentRenderTraceIR abut_trace;
    const auto output = render_document_markdown(abutting, {}, &abut_trace);
    if (!contains(output,
                  "*fn* <em class=\"geist-emphasis\">ft</em>"
                  "<strong class=\"geist-strong\">,</strong> tail",
                  "abutting emphasis runs were not written as HTML: " +
                      output) ||
        !require(markdown_visible_text(output, abut_trace).find("fn ft, tail") !=
                     std::string::npos,
                 "abutting emphasis visible text drifted: " +
                     markdown_visible_text(output, abut_trace)))
      return 1;
  }

  // A run naming a phrase (`E`, `P`, `C`, `V`, `Q`) is HTML's business only: Markdown
  // writes it exactly as the same run without one, in its own syntax and
  // in the HTML it embeds for abutting emphasis and styled literal rows.
  {
    const auto markdown_of = [](bool phrased) {
      const auto phrase = [&](PhraseKindIR kind) {
        return phrased ? kind : PhraseKindIR::none;
      };
      DocumentIR document;
      document.topic.id = "PHRASE";
      InlineSequenceIR line;
      line.push_back(in(EmphasisInlineIR{"Guide", EmphasisKindIR::emphasis,
                                         phrase(PhraseKindIR::citation)}));
      line.push_back(in(EmphasisInlineIR{",", EmphasisKindIR::strong}));
      line.push_back(in(TextInlineIR{" press "}));
      line.push_back(in(CodeInlineIR{"ENTER", phrase(PhraseKindIR::keyboard)}));
      line.push_back(in(TextInlineIR{" or "}));
      line.push_back(in(EmphasisInlineIR{"Title", EmphasisKindIR::emphasis,
                                         phrase(PhraseKindIR::citation)}));
      line.push_back(in(TextInlineIR{" to "}));
      line.push_back(in(EmphasisInlineIR{"mem", EmphasisKindIR::emphasis,
                                         phrase(PhraseKindIR::variable)}));
      line.push_back(in(TextInlineIR{" as "}));
      line.push_back(in(CodeInlineIR{"*LIBL", phrase(PhraseKindIR::definition)}));
      document.blocks.push_back(block(ParagraphBlockIR{std::move(line)}));
      PreformattedBlockIR rows;
      rows.lines = {"  NOSTAE"};
      rows.line_origins = {origin()};
      rows.styled_lines = {{in(TextInlineIR{"  "}),
                            in(CodeInlineIR{"NOSTAE",
                                            phrase(PhraseKindIR::sample)})}};
      document.blocks.push_back(block(std::move(rows)));
      return render_document_markdown(document);
    };
    const auto plain = markdown_of(false);
    const auto phrased = markdown_of(true);
    if (!require(phrased == plain,
                 "a phrase changed Markdown:\n" + plain + "\n---\n" +
                     phrased) ||
        !require(phrased.find("<em class=\"geist-emphasis\">Guide</em>") !=
                         std::string::npos &&
                     phrased.find("`ENTER`") != std::string::npos &&
                     phrased.find("*Title*") != std::string::npos &&
                     phrased.find("*mem*") != std::string::npos &&
                     phrased.find("`*LIBL`") != std::string::npos &&
                     phrased.find("<code class=\"geist-monospace\">NOSTAE"
                                  "</code>") != std::string::npos,
                 "phrase runs lost their Markdown presentation: " + phrased))
      return 1;
  }

  // A compound definition list is embedded as one HTML block. Inside a list
  // item every line of that block gets the item's indentation, which the
  // CommonMark parser strips again: the indentation splits the HTML into
  // per-line spans, but the visible text is still that of the one block
  // (S20H-1642-01 2.3, GC35-0316-00: a `<pre>` row inside a definition).
  {
    DocumentIR nested;
    nested.topic.id = "NESTED-HTML";
    PreformattedBlockIR rows;
    rows.lines = {"  - CONFIG.SYS", "  - WIN.INI"};
    DefinitionEntryIR entry;
    entry.term = text("Dump");
    entry.origin = origin();
    entry.definition_blocks.push_back(block(ParagraphBlockIR{text("saves")}));
    entry.definition_blocks.push_back(block(std::move(rows)));
    DefinitionListBlockIR definitions;
    definitions.entries.push_back(std::move(entry));
    ListItemIR item{{}, origin()};
    item.blocks.push_back(block(ParagraphBlockIR{text("Select it.")}));
    item.blocks.push_back(block(std::move(definitions)));
    ListBlockIR steps;
    steps.ordered = true;
    steps.items.push_back(std::move(item));
    nested.blocks.push_back(block(std::move(steps)));
    DocumentRenderTraceIR nested_trace;
    const auto output = render_document_markdown(nested, {}, &nested_trace);
    std::string visible;
    try {
      visible = markdown_visible_text(output, nested_trace);
    } catch (const std::exception &error) {
      std::cerr << "document_markdown_renderer_synthetic: nested HTML block "
                   "has no visible text: "
                << error.what() << "\n" << output << '\n';
      return 1;
    }
    if (!require(visible.find("- CONFIG.SYS") != std::string::npos &&
                     visible.find("- WIN.INI") != std::string::npos &&
                     visible.find("saves") != std::string::npos,
                 "nested HTML block visible text drifted: " + visible))
      return 1;
  }

  std::cout << "document Markdown renderer synthetic checks passed\n";
  return 0;
}
