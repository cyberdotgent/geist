// The native HTML renderer over a synthetic typed Document IR (issue #46).
//
// Three things are asserted here that the format depends on:
//
//   * escaping is a property of the renderer.  Every text and attribute
//     position is fed source bytes containing `<`, `>`, `&`, `"` and `'`, and
//     no route -- including the preformatted one the Markdown renderer spells
//     as literal `<pre>` -- may pass them through;
//   * an id a link targets is the id that is actually emitted, with and
//     without a configured prefix.  A previous divergence between the two
//     silently produced 243 broken cross-file destinations that still read as
//     ordinary text; and
//   * the documented class scheme is complete in both directions: every class
//     the renderer emits is in the shipped example stylesheet, and the
//     stylesheet names no class the renderer cannot emit.

#include "geist/detail/document_html_renderer.hpp"
#include "geist/detail/document_ir.hpp"

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace geist::detail;

int failures = 0;

bool require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "document_html_renderer_synthetic: " << message << '\n';
    ++failures;
  }
  return condition;
}

bool contains(const std::string &output, const std::string &fragment,
              const std::string &message) {
  return require(output.find(fragment) != std::string::npos,
                 message + " (missing: " + fragment + ")");
}

bool absent(const std::string &output, const std::string &fragment,
            const std::string &message) {
  return require(output.find(fragment) == std::string::npos,
                 message + " (present: " + fragment + ")");
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

DocumentNodeOriginIR origin() {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::semantic_lowering;
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

// Every `class="..."` value in the output, split into individual classes.
std::set<std::string> emitted_classes(const std::string &html) {
  std::set<std::string> classes;
  std::size_t at = 0;
  const std::string marker = "class=\"";
  while ((at = html.find(marker, at)) != std::string::npos) {
    at += marker.size();
    const auto end = html.find('"', at);
    if (end == std::string::npos)
      break;
    std::istringstream values(html.substr(at, end - at));
    std::string value;
    while (values >> value)
      classes.insert(value);
    at = end;
  }
  return classes;
}

// Every `.geist-...` selector the example stylesheet names.
std::set<std::string> stylesheet_classes(const std::string &css) {
  std::set<std::string> classes;
  std::size_t at = 0;
  while ((at = css.find(".geist-", at)) != std::string::npos) {
    auto end = at + 1;
    while (end < css.size() &&
           (std::isalnum(static_cast<unsigned char>(css[end])) != 0 ||
            css[end] == '-' || css[end] == '_'))
      ++end;
    classes.insert(css.substr(at + 1, end - at - 1));
    at = end;
  }
  return classes;
}

DocumentIR build_document() {
  DocumentIR document;
  document.topic.id = "HTML<1>";
  document.topic.title = "HTML renderer";
  document.named_destinations = {"SPTE083I"};

  InlineSequenceIR rich;
  rich.push_back(in(TextInlineIR{"literal & <tag> \"quoted\" 'single'"}));
  rich.push_back(in(HardBreakInlineIR{}));
  rich.push_back(in(EmphasisInlineIR{"em<phasis>", EmphasisKindIR::emphasis}));
  rich.push_back(in(EmphasisInlineIR{"str&ong", EmphasisKindIR::strong}));
  rich.push_back(
      in(EmphasisInlineIR{"both<&>", EmphasisKindIR::strong_emphasis}));
  rich.push_back(in(CodeInlineIR{"a < b && c > d"}));
  rich.push_back(in(CrossReferenceInlineIR{
      {CrossReferenceTargetKindIR::external,
       "https://example.test/a b?q=a&b=\"c\""},
      "a <link>"}));
  rich.push_back(in(CrossReferenceInlineIR{
      {CrossReferenceTargetKindIR::topic, "Chapter <One>"}, "topic"}));
  rich.push_back(in(CrossReferenceInlineIR{
      {CrossReferenceTargetKindIR::anchor, "SECTION&ONE"}, "section"}));
  rich.push_back(in(CrossReferenceInlineIR{
      {CrossReferenceTargetKindIR::resource, "resource:69"}, "manual"}));
  rich.push_back(in(ImageInlineIR{"resource:70", "alt \"<image>\" & more"}));
  rich.push_back(in(OpaqueInlineIR{"legacy<span>", "raw & value"}));

  document.blocks.push_back(block(HeadingBlockIR{2, text("Heading <#1> & co")}));
  document.blocks.push_back(block(ParagraphBlockIR{rich}));

  // Every anchor role, each with a source id that would break an attribute if
  // it were not escaped.
  document.blocks.push_back(
      block(AnchorBlockIR{"A\"<&'x", AnchorRoleIR::cross_reference}));
  document.blocks.push_back(
      block(AnchorBlockIR{"FIGFIGUNIQ1", AnchorRoleIR::figure}));
  document.blocks.push_back(
      block(AnchorBlockIR{"TBLTBLUNIQ1", AnchorRoleIR::table}));
  document.blocks.push_back(
      block(AnchorBlockIR{"FTNFTNUNIQ1", AnchorRoleIR::local}));

  ListBlockIR unordered;
  unordered.items = {{text("first & item"), origin()},
                     {text("one deep"), origin(), std::nullopt, 1},
                     {text("nested <deep>"), origin(), std::nullopt, 2}};
  ListItemIR empty_item;
  empty_item.origin = origin();
  empty_item.empty_content = true;
  empty_item.depth = 1;
  unordered.items.push_back(empty_item);
  document.blocks.push_back(block(std::move(unordered)));

  ListBlockIR ordered;
  ordered.ordered = true;
  ordered.items = {{text("nine"), origin(), 9}, {text("twelve"), origin(), 12}};
  document.blocks.push_back(block(std::move(ordered)));

  DefinitionListBlockIR definitions;
  definitions.entries = {
      {text("Term <T>"), text("Definition & meaning"), origin()}};
  document.blocks.push_back(block(std::move(definitions)));

  TableBlockIR table;
  table.header_rows = 1;
  table.rows = {row({cell(text("Primary <P>")), cell(text("Secondary"))}),
                row({cell(text("body & cell")), cell()})};
  document.blocks.push_back(block(std::move(table)));

  // A headerless source table must not acquire a `<thead>`: an invented
  // header is a claim the source did not make.
  TableBlockIR headerless;
  headerless.rows = {row({cell(text("only <body>")), cell()})};
  document.blocks.push_back(block(std::move(headerless)));

  document.blocks.push_back(block(
      PreformattedBlockIR{{"drawn & <row>", "  +---------+", "tail"}}));

  // Drawn rows carrying every kind of cross reference a selector can name.
  // The link is a pair of byte offsets into the row, never a byte inserted
  // into it, so the column arithmetic has to survive escaping.
  PreformattedBlockIR linked;
  linked.lines = {"   | see <Notices> & more    |",
                  "   | see OtherBook           |",
                  "   | see OtherHead           |",
                  "   | see TheWeb              |"};
  VerbatimLinkIR in_book;
  in_book.begin = 9;
  in_book.end = 18;
  in_book.kind = VerbatimLinkKindIR::in_book;
  in_book.target = "HDRNOTICES";
  VerbatimLinkIR contents;
  contents.begin = 9;
  contents.end = 18;
  contents.kind = VerbatimLinkKindIR::book_contents;
  contents.target = "HCPB9";
  contents.document_number = "GC24-5518";
  contents.document_level = "ANY";
  contents.alternatives = {"BOOK", "", "", "GC24-5518", "ANY", "HCPB9"};
  VerbatimLinkIR heading = contents;
  heading.kind = VerbatimLinkKindIR::book_heading;
  heading.heading_anchor = "HDRINTRO";
  VerbatimLinkIR url;
  url.begin = 9;
  url.end = 15;
  url.kind = VerbatimLinkKindIR::external_url;
  url.url = "https://example.test/x?a=1&b=\"2\"";
  linked.line_links = {{in_book}, {contents}, {heading}, {url}};
  document.blocks.push_back(block(std::move(linked)));

  document.blocks.push_back(
      block(NoteBlockIR{text("Caution <!>"), text("Do this & that")}));

  PublicationListBlockIR publications;
  publications.entries = {{text("Book <A>"),
                           {text("First & paragraph."), text("Second.")},
                           origin()}};
  document.blocks.push_back(block(std::move(publications)));

  FigureBlockIR figure;
  figure.resource = "resource:69";
  figure.caption = text("Figure <one> & two");
  document.blocks.push_back(block(std::move(figure)));

  document.blocks.push_back(
      block(FootnoteBlockIR{"FTN<1>", text("Footnote body & more")}));

  IndexGroupBlockIR index;
  index.heading = text("Index A-Z & more");
  index.entries = {{text("Alpha <entry>"), "#ALPHA&ONE", origin()},
                   {text("Beta"), "other<topic>", origin()}};
  document.blocks.push_back(block(std::move(index)));

  MenuBlockIR menu;
  menu.items = {{{CrossReferenceTargetKindIR::topic, "2.1.1"},
                 "Displaying <Status> & more", origin()}};
  document.blocks.push_back(block(std::move(menu)));

  document.blocks.push_back(
      block(OpaqueBlockIR{"control<kind>", "raw & <payload>\nsecond line"}));

  // The one block the pipeline could not prove the structure of.  Degraded
  // output is always emitted; what this asserts is that a consumer can find
  // it programmatically and trace it back.
  auto degraded = block(PreformattedBlockIR{{"unproven & <region>"}});
  degraded.origin.fidelity = DocumentFidelityIR::degraded;
  degraded.origin.degradation_code = "fixed-table-verbatim";
  document.blocks.push_back(std::move(degraded));

  return document;
}

std::string read_file(const std::string &path) {
  std::ifstream stream(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

} // namespace

int main() {
  const auto document = build_document();
  std::string error;
  if (!require(verify_document_ir(document, &error), error))
    return 1;

  const auto fragment = render_document_html_fragment(document);

  // The fragment root states the topic and the fidelity of the whole topic,
  // and is itself the topic's destination.  A second id the source names this
  // same topic by exists in the output too, so a reference using it lands.
  if (!contains(fragment,
                "<div class=\"geist-topic\" id=\"HTML&lt;1&gt;\" "
                "data-geist-topic=\"HTML&lt;1&gt;\" "
                "data-geist-severity=\"typed-degraded\">",
                "fragment root identity or severity is wrong") ||
      !contains(fragment,
                "<span class=\"geist-topic-destination\" id=\"SPTE083I\">"
                "</span>",
                "a further named destination of the topic was not emitted"))
    return 1;

  // Escaping, in every position source bytes can reach.
  contains(fragment, "Heading &lt;#1&gt; &amp; co", "heading text not escaped");
  contains(fragment, "literal &amp; &lt;tag&gt; \"quoted\" 'single'",
           "paragraph text not escaped");
  contains(fragment, "<em class=\"geist-emphasis\">em&lt;phasis&gt;</em>",
           "emphasis text not escaped");
  contains(fragment, "<strong class=\"geist-strong\">str&amp;ong</strong>",
           "strong text not escaped");
  contains(fragment,
           "<strong class=\"geist-strong-emphasis\"><em>both&lt;&amp;&gt;"
           "</em></strong>",
           "strong emphasis text not escaped");
  contains(fragment,
           "<code class=\"geist-monospace\">a &lt; b &amp;&amp; c &gt; d"
           "</code>",
           "code text not escaped");
  contains(fragment, "id=\"A&quot;&lt;&amp;&#39;x\"",
           "anchor id attribute not escaped");
  contains(fragment,
           "href=\"https://example.test/a b?q=a&amp;b=&quot;c&quot;\"",
           "external link destination not escaped");
  contains(fragment, "alt=\"alt &quot;&lt;image&gt;&quot; &amp; more\"",
           "image alt attribute not escaped");
  contains(fragment, "data-geist-opaque-kind=\"legacy&lt;span&gt;\"",
           "opaque inline kind attribute not escaped");
  contains(fragment, "raw &amp; &lt;payload&gt;",
           "opaque block content not escaped");
  // The verbatim route is the one the Markdown renderer emits as literal
  // `<pre>`.  Its content is escaped here, not passed through.
  contains(fragment, "<pre class=\"geist-preformatted\">drawn &amp; &lt;row&gt;",
           "preformatted line not escaped");
  absent(fragment, "<tag>", "raw source markup reached the output");
  absent(fragment, "<payload>", "raw source markup reached the output");
  absent(fragment, "<Notices>", "raw source markup reached the output");

  // A link inside a drawn row is a pair of byte offsets into the row.  The
  // marked columns become the anchor text and every other column stays where
  // it was, escaping included.
  contains(fragment,
           "   | see <a class=\"geist-link geist-link--in-book\" "
           "href=\"#HDRNOTICES\">&lt;Notices&gt;</a> &amp; more    |",
           "in-book verbatim link placement or escaping is wrong");

  // Every link kind carries its modifier class.
  for (const auto *kind : {"topic", "anchor", "resource", "external",
                           "in-book", "book-contents", "book-heading",
                           "external-url"})
    contains(fragment, std::string("geist-link--") + kind,
             "a link kind modifier class was not emitted");

  // A cross-book reference has no destination a single book can prove.  With
  // no resolver it is a dead link that says so, never a dead link presented
  // as live.
  contains(fragment,
           "<a class=\"geist-link geist-link--book-contents "
           "geist-link--unresolved\" href=\"#\">",
           "unresolved cross-book link is not marked unresolved");

  // The id a link targets is the id that is emitted.
  contains(fragment, "href=\"#SECTION&amp;ONE\"",
           "anchor reference destination is wrong");
  contains(fragment, "<a class=\"geist-link geist-link--anchor\" "
                     "href=\"#ALPHA&amp;ONE\">",
           "index entry anchor destination is wrong");
  for (const auto *role : {"cross-reference", "figure", "table", "local"})
    contains(fragment, std::string("geist-anchor--") + role,
             "an anchor role modifier class was not emitted");

  // A headerless source table gets no invented header row.
  contains(fragment, "<table class=\"geist-table\"><thead",
           "a table with header rows lost its thead");
  contains(fragment,
           "<table class=\"geist-table\"><tbody class=\"geist-table-body\">"
           "<tr class=\"geist-table-row\"><td class=\"geist-table-cell\">"
           "only &lt;body&gt;",
           "a headerless table did not render as body rows alone");

  // Degraded output is emitted, and it is machine-findable.
  contains(fragment,
           "<pre class=\"geist-preformatted\" data-geist-degraded=\"true\" "
           "data-geist-degradation=\"fixed-table-verbatim\">"
           "unproven &amp; &lt;region&gt;</pre>",
           "degraded block is not identifiable or lost its content");

  // Source ordinals and nesting depth survive.
  contains(fragment, "<li class=\"geist-list-item\" value=\"9\">",
           "an explicit ordered-list source ordinal was lost");
  contains(fragment, "<li class=\"geist-list-item\" data-geist-depth=\"2\">",
           "a list item nesting depth was lost");
  contains(fragment,
           "<li class=\"geist-list-item\" data-geist-depth=\"1\" "
           "data-geist-empty=\"true\">",
           "a source-declared empty list item was not marked");

  // Determinism.
  require(fragment == render_document_html_fragment(document),
          "rendering the same typed IR twice was not deterministic");

  // Resolvers, each reached for its own kind of destination.
  geist::HtmlRenderOptions options;
  options.id_prefix = "bk-";
  options.resolve_topic = [](const std::string &id)
      -> std::optional<std::string> { return "/topics/" + id; };
  options.resolve_anchor = [](const std::string &id)
      -> std::optional<std::string> {
    if (id == "SECTION&ONE")
      return std::nullopt; // falls back to the emitted id
    return "/anchors/" + id;
  };
  options.resolve_resource = [](const std::string &id)
      -> std::optional<std::string> {
    if (id == "70")
      return std::string(); // deliberately unresolvable
    return "/assets/" + id;
  };
  options.resolve_external = [](const std::string &url)
      -> std::optional<std::string> { return "/out?u=" + url; };
  options.resolve_cross_book =
      [](const geist::HtmlCrossBookReference &reference)
      -> std::optional<std::string> {
    if (reference.document_number != "GC24-5518")
      return std::nullopt;
    if (reference.alternatives.size() != 6)
      return std::nullopt;
    return "/shelf/" + reference.document_number + "/" +
           reference.document_level +
           (reference.heading_anchor.empty()
                ? std::string()
                : "#" + reference.heading_anchor);
  };
  const auto resolved = render_document_html_fragment(document, options);

  contains(resolved, "href=\"/topics/Chapter &lt;One&gt;\"",
           "topic resolver was not used");
  contains(resolved, "href=\"/assets/69\"", "resource resolver was not used");
  contains(resolved, "href=\"/out?u=https://example.test/a b?q=a&amp;b="
                     "&quot;c&quot;\"",
           "external resolver was not used");
  contains(resolved, "href=\"/shelf/GC24-5518/ANY\"",
           "cross-book resolver was not used for a contents reference");
  contains(resolved, "href=\"/shelf/GC24-5518/ANY#HDRINTRO\"",
           "cross-book resolver did not receive the heading anchor");
  contains(resolved, "href=\"/anchors/HDRNOTICES\"",
           "anchor resolver was not used for a link inside a drawn row");
  // A resolver that answers with nothing must produce a marked dead link, not
  // a live-looking one.
  contains(resolved,
           "<img class=\"geist-image geist-image--unresolved\" src=\"#\"",
           "a resolver returning nothing did not mark the image unresolved");

  // The prefix is applied to the emitted id *and* to the href generated for
  // it, so a reference and its destination cannot diverge.
  contains(resolved, "id=\"bk-A&quot;&lt;&amp;&#39;x\"",
           "id prefix was not applied to an emitted anchor id");
  contains(resolved, "href=\"#bk-SECTION&amp;ONE\"",
           "id prefix was not applied to the href of an unresolved anchor");
  contains(resolved, "id=\"bk-HTML&lt;1&gt;\"",
           "id prefix was not applied to the topic root id");
  contains(resolved, "id=\"bk-SPTE083I\"",
           "id prefix was not applied to a named topic destination");

  // The complete document reuses the fragment renderer: the fragment bytes
  // appear in it unchanged.
  geist::HtmlDocumentOptions page;
  page.title = "Title <&> \"quoted\"";
  page.stylesheets = {"/css/geist.css?v=1&x=2"};
  page.inline_stylesheet = ".geist-topic { color: red } /* </style> */";
  const auto complete = render_html_document(fragment, page);
  contains(complete, "<!doctype html>\n<html lang=\"en\">\n<head>\n"
                     "<meta charset=\"utf-8\">",
           "complete document is missing its minimal head");
  contains(complete, "<title>Title &lt;&amp;&gt; \"quoted\"</title>",
           "document title was not escaped");
  contains(complete, "<link rel=\"stylesheet\" "
                     "href=\"/css/geist.css?v=1&amp;x=2\">",
           "stylesheet href was not emitted or escaped");
  absent(complete, "</style> */", "inline CSS could close its own element");
  contains(complete, "\\3c /style> */",
           "inline CSS was not neutralised as documented");
  contains(complete, fragment,
           "complete document did not reuse the fragment bytes");
  contains(complete, "</body>\n</html>\n",
           "complete document is not closed");

  // The documented class scheme, checked in both directions against the
  // shipped example stylesheet.
  auto classes = emitted_classes(fragment);
  for (const auto &value : emitted_classes(resolved))
    classes.insert(value);
  // Classes only the topic-level routes can emit, which a synthetic document
  // cannot reach: the book wrapper and the `failed` diagnostic placeholder.
  for (const auto *value :
       {"geist-book", "geist-diagnostic", "geist-diagnostic-message",
        "geist-diagnostic-source", "geist-diagnostic-reason"})
    classes.insert(value);

  const auto css = read_file(std::string(GEIST_DOC_DIR) + "/html-styling.css");
  if (!require(!css.empty(), "the example stylesheet could not be read"))
    return failures == 0 ? 0 : 1;
  const auto styled = stylesheet_classes(css);
  for (const auto &value : classes)
    require(styled.count(value) != 0,
            "the example stylesheet does not style " + value);
  for (const auto &value : styled)
    require(classes.count(value) != 0,
            "the example stylesheet styles a class the renderer never emits: " +
                value);

  // Every block IR alternative has a class, enumerated from the variant
  // itself, and every one of them is styled.
  const auto block_classes = html_block_classes();
  require(block_classes.size() ==
              std::variant_size_v<geist::detail::BlockNodeIR>,
          "the block class list does not cover every block IR alternative");
  for (const auto &value : block_classes)
    require(styled.count(value) != 0,
            "a block class is missing from the example stylesheet: " + value);

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "document HTML renderer synthetic checks passed\n";
  return 0;
}
