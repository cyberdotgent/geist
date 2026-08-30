#include "geist/detail/document_ir.hpp"
#include "geist/detail/document_lowering.hpp"
#include "geist/detail/document_markdown_renderer.hpp"

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

  document.blocks.push_back(
      block(PreformattedBlockIR{{"source", "```", "tail without newline"}}));
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
  std::get<CrossReferenceInlineIR>(invalid_inlines[6].node).target.kind =
      static_cast<CrossReferenceTargetKindIR>(99);
  error.clear();
  if (!require(!verify_document_ir(invalid_target, &error) &&
                   error == "inline node is not canonical",
               "verifier admitted an invalid cross-reference target kind"))
    return 1;
  const auto formatted = format_document_ir(document);
  if (!contains(formatted,
                "xref=external:\"https://example.test/chapter one?q=a&b\"",
                "formatter omitted external target kind") ||
      !contains(formatted, "xref=topic:\"Chapter One\"",
                "formatter omitted topic target kind") ||
      !contains(formatted, "xref=anchor:\"section one\"",
                "formatter omitted anchor target kind") ||
      !contains(formatted, "xref=resource:\"manuals/a b.pdf\"",
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
  const auto output = render_document_markdown(document, options);
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
      !contains(output, "````\nsource\n```\ntail without newline\n````",
                "variable-length preformatted fence failed") ||
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

  // The migration adapter deliberately retains exact legacy behavior; typed
  // output above is judged by semantic structure, not byte parity with it.
  TopicIdentityIR identity;
  const std::vector<std::string> records = {":h1.Legacy", ":p.Body"};
  const auto legacy = lower_legacy_topic_to_document_ir(identity, records);
  if (!require(render_document_markdown(legacy) == "# Legacy\n\nBody\n",
               "sole whole-topic legacy adapter changed behavior"))
    return 1;

  std::cout << "document Markdown renderer synthetic checks passed\n";
  return 0;
}
