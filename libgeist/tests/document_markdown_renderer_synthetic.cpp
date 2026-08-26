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
  rich.push_back(in(CrossReferenceInlineIR{"chapter one?q=a&b", "a [link]"}));
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
  document.blocks.push_back(
      block(FootnoteBlockIR{"note ] one", text("Footnote body")}));

  IndexGroupBlockIR index;
  index.heading = text("Index A-Z");
  index.entries = {{text("Alpha [entry]"), "#alpha one", origin()},
                   {text("Beta"), "other(topic)", origin()}};
  document.blocks.push_back(block(std::move(index)));
  document.blocks.push_back(
      block(OpaqueBlockIR{"control*kind", "raw ``` payload\nsecond line"}));

  std::string error;
  if (!require(verify_document_ir(document, &error), error))
    return 1;
  const auto output = render_document_markdown(document);
  if (!require(output == render_document_markdown(document),
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
      !contains(output, "[a \\[link\\]](<chapter%20one?q=a&b>)",
                "link label/destination escaping failed") ||
      !contains(output, "![alt \\[image\\]](<image%20one(2).png>)",
                "inline image escaping failed") ||
      !contains(output, "`` legacy*span: raw `value` ``",
                "opaque inline did not use a safe code span") ||
      !contains(output, "<a id=\"A&quot;&lt;&amp;&#39;&#xA;\"></a>",
                "HTML anchor attribute escaping failed") ||
      !contains(output, "- first \\- item\n- second",
                "unordered list structure failed") ||
      !contains(output, "1. one\n1. two", "ordered list structure failed") ||
      !contains(output, "- **Term:** Definition \\[literal\\]",
                "definition list structure failed"))
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
      !contains(output,
                "![Figure \\* one](<figures/a%20b(1).png>)\n\n"
                "*Figure \\* one*",
                "figure resource, alt text, or caption failed") ||
      !contains(output, "[^note%20%5D%20one]: Footnote body",
                "footnote label normalization failed") ||
      !contains(output,
                "**Index A\\-Z**\n\n"
                "- [Alpha \\[entry\\]](<#alpha%20one>)\n"
                "- [Beta](<other(topic)>)",
                "index group structure failed") ||
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
