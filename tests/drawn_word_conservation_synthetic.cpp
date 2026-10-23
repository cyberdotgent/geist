// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Source-side word conservation (issue #85).
//
// Coverage says which route rendered a topic.  It does not say whether the
// output is complete, and a topic can render, count as fully typed, and still
// drop a word: the flattened decoded string opens a control segment on a word
// that is only *spelled* like a control, and the word leaves the render with an
// anchor put in its place.  The ownership ledger conserves that word -- as the
// phantom control's opcode -- so the ledger alone cannot tell the two apart.
//
// What can tell them apart is the record's own display-line framing.  A payload
// tiles into `<length byte><that many bytes>` display lines; the length byte is
// the row-control slot, always and only, whatever ordinary word the dictionary
// spells for it; and a control's opcode is the first token of its own display
// line.  A word standing anywhere else on a line is display text and nothing
// else, and must reach the render.
//
// Two things are pinned here.  The word cut, synthetically, because both sides
// of the comparison have to agree on where a word ends -- the renderer writes
// emphasis *inside* a word (`CLIST`s), and a dictionary fragment boundary falls
// wherever it falls.  And the whole invariant over `packet.boo`, the one
// redistributable fixture, which drops nothing: the corpus-wide check is
// `tools/drawn_word_conservation.py`, since only `packet.boo` may be opened
// here (issue #59).

#include "geist/detail/container/control_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/lowering/drawn_word_conservation.hpp"
#include "geist/detail/lowering/drawn_word_source.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "geist/detail/render/html_visible_text.hpp"
#include "geist/detail/render/markdown_visible_text.hpp"
#include "geist/detail/render/render_diagnostic_ir.hpp"
#include "geist/document.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string &message) {
  if (condition)
    return;
  std::cerr << "FAIL: " << message << "\n";
  geist_test::record_failure();
}

std::string join(const std::vector<std::string> &words) {
  std::string joined;
  for (const auto &word : words) {
    if (!joined.empty())
      joined += ",";
    joined += word;
  }
  return joined;
}

void check_word_cut() {
  const auto cut = [](const std::string &text) {
    return join(geist::detail::conservation_words(text));
  };
  require(cut("AX.25 Protocol") == "ax,25,protocol",
          "punctuation separates and the cut folds case");
  // The renderer fences the stem and leaves the plural outside it; the source
  // draws one word.  Emphasis, code fencing and the Markdown escape join
  // rather than separate, so the two sides agree.
  require(cut("`CLIST`s") == cut("CLISTs"),
          "code fencing inside a word does not split it");
  require(cut("**bold**word") == "boldword",
          "emphasis inside a word does not split it");
  // The escape goes; the underscore it protects still separates, exactly as
  // the same underscore does in the source.
  require(cut("FRONT\\_2") == cut("FRONT_2") && cut("FRONT_2") == "front,2",
          "an escaped underscore separates on both sides");
  require(cut("   ").empty(), "a blank run yields no words");
}

void check_html_projection() {
  const auto cut = [](const std::string &html) {
    return join(geist::detail::conservation_words(
        geist::detail::html_visible_text(html)));
  };
  require(cut("<p>at<a href=\"132\">132</a></p>") == "at132",
          "linked word fragment preserves visible adjacency");
  require(cut("that<a href=\"target\">144</a> in<a>Table</a>") ==
              "that144,intable",
          "both sides of link boundaries join");
  require(cut("<a href=\"missingword\">kept</a>") == "kept",
          "destinations cannot compensate genuine loss");
  require(cut("<p>lost<a href=\"fragment\"></a></p>") == "lost",
          "an empty link cannot manufacture its missing label");
  require(cut("<p>one</p><p>two</p><table><tr><td>three</td><td>four</td>"
              "</tr></table>five<br>six") == "one,two,three,four,five,six",
          "block, cell and explicit line boundaries separate words");
  require(
      cut("<pre><code>array[132](target) a*b `c` d\\e</code></pre>") ==
          "array,132,target,ab,c,de",
      "literal array punctuation stays literal and code keeps source word cut");
  require(cut("at[132] &lt;a href=\"literal\"&gt;text&lt;/a&gt;") ==
              "at,132,a,href,literal,text,a",
          "escaped literal HTML is text, not a link");
  require(cut("a&#49;&#x32;b &amp; &lt; &gt; x&nbsp;y") == "a12b,x,y",
          "entities decode without artificial inline boundaries");
  require(cut("<pre>at<a href=\"missingword\">132</a>\nnext</pre>") ==
              "at132,next",
          "best-effort verbatim links share inline semantics");
  require(
      cut("pre<img src=\"missingword?alt='wrong'>\" alt=\"A&amp;B\">post") ==
          "pre,a,b,post",
      "only image replacement text supplies image words, as a run of its "
      "own: an image is a replaced element, not text that joins its "
      "neighbours");
  require(cut("one<!-- not visible -->two<span id=\"missingword\"></span>") ==
              "onetwo",
          "anchors and comments do not invent text or spacing");
  require(cut("<ol><li>one</li><li>two</li><li value=\"7\">seven</li>"
              "<li>eight</li></ol>") == "1,one,2,two,7,seven,8,eight",
          "browser-generated ordered markers honor explicit item ordinals");
  require(cut("<ol><li value=\"4\">four<ul><li value=\"99\">bullet</li></ul>"
              "</li><li>five<ol><li>inner</li></ol></li></ol>") ==
              "4,four,bullet,5,five,1,inner",
          "nested ordered and unordered lists keep independent marker state");
  require(cut("<p>plain</p><ul><li value=\"43\">bullet</li></ul>") ==
              "plain,bullet",
          "non-marker attributes never supply numbers");
  require(cut("<p>before</p><hr><p>after</p>") == "before,after",
          "a rendered thematic break is structural and supplies no words");
}

void check_projection_rejects_unsupported_html() {
  for (const auto &html :
       {"<script>lost</script>", "<style>lost</style>",
        "<unknown>lost</unknown>", "<p hidden>lost</p>",
        "<p value=\"42\">lost</p>", "<a title=\"lost\">lost</a>",
        "<p style=\"display:none\">lost</p>", "<a href=\"unterminated>lost</a>",
        "<p>lost &unsupported;</p>", "<!-- unterminated"}) {
    bool rejected = false;
    try {
      (void)geist::detail::html_visible_text(html);
    } catch (const std::logic_error &) {
      rejected = true;
    }
    require(rejected,
            std::string("unsupported emitted HTML must fail: ") + html);
  }
}

void check_markdown_projection() {
  using namespace geist::detail;
  const auto in = [](InlineNodeIR node) {
    InlineIR value;
    value.node = std::move(node);
    value.origin.derivation = DocumentDerivationIR::semantic_lowering;
    return value;
  };
  const auto block = [](BlockNodeIR node) {
    BlockIR value;
    value.node = std::move(node);
    value.origin.derivation = DocumentDerivationIR::semantic_lowering;
    return value;
  };
  DocumentIR document;
  document.topic.id = "PROJECTION";
  document.topic.title = "Serialization projection";
  document.topic.start_logical_record = 1;
  document.topic.end_logical_record = 1;
  document.blocks.push_back(block(ParagraphBlockIR{
      {in(TextInlineIR{"at"}),
       in(CrossReferenceInlineIR{
           {CrossReferenceTargetKindIR::external, "https://missingword.test"},
           "132"}),
       in(TextInlineIR{" array[3] &amp; "}),
       in(CodeInlineIR{"literal[4](codeword) &amp;"}), in(HardBreakInlineIR{}),
       in(TextInlineIR{"next"})}}));
  ListBlockIR list;
  list.ordered = true;
  DocumentNodeOriginIR origin;
  origin.derivation = DocumentDerivationIR::semantic_lowering;
  list.items = {{{in(TextInlineIR{"two"})}, origin, 2},
                {{in(TextInlineIR{"four"})}, origin, 4}};
  document.blocks.push_back(block(list));
  PreformattedBlockIR pre;
  pre.lines = {"raw[7] &amp;"};
  document.blocks.push_back(block(pre));
  PreformattedBlockIR linked_pre;
  linked_pre.lines = {"at132"};
  VerbatimLinkIR link;
  link.begin = 2;
  link.end = 5;
  link.target = "missingword";
  linked_pre.line_links = {{link}};
  document.blocks.push_back(block(linked_pre));
  DocumentRenderTraceIR trace;
  const auto markdown = render_document_markdown(document, {}, &trace);
  const auto projected = markdown_visible_text(markdown, trace);
  require(join(conservation_words(projected)) ==
              "at132,array,3,amp,literal,4,codeword,amp,next,2,two,4,four,raw,"
              "7,amp,at132",
          "actual Markdown bytes project links, escaped text, literal code and "
          "markers");
  require(projected.find("missingword") == std::string::npos,
          "Markdown destination cannot compensate missing visible text");
  auto damaged = markdown;
  const auto at = damaged.find("array");
  damaged[at] = ' ';
  require(
      join(conservation_words(markdown_visible_text(damaged, trace)))
              .find("array") == std::string::npos,
      "projection reads emitted bytes, not the serializer's original IR text");
  auto incomplete = trace;
  incomplete.spans.pop_back();
  bool rejected = false;
  try {
    (void)markdown_visible_text(markdown, incomplete);
  } catch (const std::logic_error &) {
    rejected = true;
  }
  require(rejected, "uncovered output bytes cannot silently pass projection");
  auto unsupported = trace;
  unsupported.spans.front().text_projection =
      static_cast<MarkdownTextProjectionIR>(99);
  rejected = false;
  try {
    (void)markdown_visible_text(markdown, unsupported);
  } catch (const std::logic_error &) {
    rejected = true;
  }
  require(rejected, "unsupported emission classification fails explicitly");
  const std::string diagnostic_prefix = "<!-- geist-render: diagnostic -->\n\n";
  require(verified_markdown_visible_text(diagnostic_prefix + markdown, markdown,
                                         trace, diagnostic_prefix) == projected,
          "only the exact known diagnostic wrapper is excluded");
  rejected = false;
  try {
    (void)verified_markdown_visible_text(diagnostic_prefix + damaged, markdown,
                                         trace, diagnostic_prefix);
  } catch (const std::logic_error &) {
    rejected = true;
  }
  require(rejected,
          "topic-cache mutation cannot bypass serializer conservation");
  rejected = false;
  try {
    (void)verified_markdown_visible_text(markdown.substr(1), markdown, trace,
                                         "");
  } catch (const std::logic_error &) {
    rejected = true;
  }
  require(rejected,
          "topic-cache truncation cannot bypass serializer conservation");
  for (const auto &bytes :
       {"<script>lost</script>", "unescaped*word", "unescaped[link]",
        "bad\\qescape", "trailing\\", "unescaped&copy;"}) {
    DocumentRenderTraceIR malformed;
    DocumentTraceSpanIR span;
    span.output_end = std::string(bytes).size();
    span.text_projection = MarkdownTextProjectionIR::escaped_text;
    malformed.spans.push_back(span);
    rejected = false;
    try {
      (void)markdown_visible_text(bytes, malformed);
    } catch (const std::logic_error &) {
      rejected = true;
    }
    require(rejected,
            std::string("unsupported escaped Markdown must fail: ") + bytes);
    malformed.spans.front().text_projection = MarkdownTextProjectionIR::literal;
    require(markdown_visible_text(bytes, malformed) == bytes,
            "literal code must not be interpreted as Markdown or HTML markup");
  }

  DocumentIR fallback_document;
  fallback_document.topic.id = "FALLBACK";
  fallback_document.topic.title = "title [literal]";
  fallback_document.blocks.push_back(
      block(HeadingBlockIR{1, {in(TextInlineIR{"FALLBACK title [literal]"})}}));
  fallback_document.blocks.push_back(
      block(AnchorBlockIR{"missingword", AnchorRoleIR::cross_reference}));
  VerbatimRowIR row;
  row.text = "at132 literal[5]";
  row.links = {link};
  PreformattedBlockIR fallback_rows;
  fallback_rows.lines = {row.text};
  fallback_rows.line_links = {row.links};
  fallback_document.blocks.push_back(block(std::move(fallback_rows)));
  DocumentRenderTraceIR fallback_trace;
  const auto fallback =
      render_document_markdown(fallback_document, {}, &fallback_trace);
  require(join(conservation_words(
              markdown_visible_text(fallback, fallback_trace))) ==
              "fallback,title,literal,at132,literal,5",
          "source-row DocumentIR projects actual preformatted text and ignores "
          "anchors");
}

void check_native_message_metadata() {
  using namespace geist::detail;
  DecodedLogicalRecordSource record;
  record.logical_record = 1;
  const auto words = [](const std::string &text) {
    return TokenWords(text.begin(), text.end());
  };
  const auto line = [&](const std::vector<std::string> &text) {
    const auto length = static_cast<std::uint16_t>(text.size() * 2);
    record.encoded_tokens.push_back({length, 1});
    record.tokens.push_back(words("length"));
    for (const auto &token : text) {
      record.encoded_tokens.push_back({0x40, 2});
      record.tokens.push_back(words(token));
    }
  };
  line({"SRMSG", "1", "(symbolicName)"});
  line({"   ", "1", "(symbolicName)"});
  line({"   ", "Keep", "SRMSG", "word"});
  record.assembled = assemble_logical_record_with_sources(record.tokens);
  record.ir.logical_record = record.logical_record;
  std::uint32_t byte = 0;
  for (std::size_t index = 0; index < record.tokens.size(); ++index) {
    const auto encoded = record.encoded_tokens[index];
    record.ir.tokens.push_back(
        {index,
         encoded,
         record.tokens[index],
         {byte, static_cast<std::uint32_t>(byte + encoded.width)},
         false,
         3});
    byte += encoded.width;
  }
  record.ir.payload_range = {0, byte};
  assign_display_line_framing(record.ir);
  record.control_segments =
      decode_control_segments(record.logical_record, record.assembled,
                              record.encoded_tokens, record.ir.display_lines);
  require(record.ir.display_lines_parse,
          "message metadata synthetic source must have decided native framing");
  OwnershipIR ownership;
  std::size_t unframed = 0;
  const auto drawn = project_drawn_topic_text({record}, ownership, &unframed);
  require(unframed == 0 && join(conservation_words(drawn.text)) ==
                               "1,symbolicname,keep,srmsg,word",
          "only the proven SRMSG identity row is metadata; headline and "
          "literal prose remain drawn");
}

void check_styled_preformatted_rows() {
  using namespace geist::detail;
  const auto in = [](InlineNodeIR node) {
    InlineIR result;
    result.node = std::move(node);
    result.origin.derivation = DocumentDerivationIR::semantic_lowering;
    return result;
  };
  PreformattedBlockIR pre;
  pre.lines = {"  A<&> 123  tail", "", "    END"};
  pre.line_origins.resize(pre.lines.size());
  for (auto &origin : pre.line_origins)
    origin.derivation = DocumentDerivationIR::semantic_lowering;
  pre.styled_lines = {
      {in(TextInlineIR{"  "}),
       in(EmphasisInlineIR{"A<&>", EmphasisKindIR::strong}),
       in(CodeInlineIR{" 123 "}),
       in(CrossReferenceInlineIR{{CrossReferenceTargetKindIR::anchor, "target"},
                                 " tail"})},
      {},
      {in(CodeInlineIR{"    END"})}};
  BlockIR block;
  block.node = pre;
  block.origin.derivation = DocumentDerivationIR::semantic_lowering;
  DocumentIR document;
  document.topic.id = "STYLED";
  document.topic.title = "Styled positioned rows";
  document.blocks = {block};
  std::string error;
  require(verify_document_ir(document, &error),
          "valid styled rows declined: " + error);
  const auto html = render_document_html_blocks(document, {});
  require(html.find("<strong class=\"geist-strong\">A&lt;&amp;&gt;</strong>") !=
                  std::string::npos &&
              html.find("href=\"#target\"") != std::string::npos &&
              html.find("<code class=\"geist-monospace\">    END</code>") !=
                  std::string::npos,
          "HTML preformatted rows lost font/link/space semantics");
  const auto expected =
      std::string("\n") + pre.lines[0] + "\n\n" + pre.lines[2] + "\n";
  const auto visible = html_visible_text(html);
  require(visible == expected,
          "HTML styled rows changed exact text or row boundaries");
  DocumentRenderTraceIR trace;
  const auto markdown = render_document_markdown(document, {}, &trace);
  require(markdown.find("<pre class=\"geist-preformatted\">\n") !=
                  std::string::npos &&
              markdown.find("<strong class=\"geist-strong\">") !=
                  std::string::npos &&
              markdown.find("href=\"#target\"") != std::string::npos,
          "styled Markdown rows are not faithful shared HTML in a preformatted "
          "block");
  const auto projected = markdown_visible_text(markdown, trace);
  require(projected.find(pre.lines[0] + "\n\n" + pre.lines[2]) !=
              std::string::npos,
          "Markdown styled-row trace lost source spaces, links or empty rows");
  DocumentMarkdownRendererOptions options;
  options.resolve_cross_reference =
      [](const CrossReferenceTargetIR &) -> std::optional<std::string> {
    return "topics/a?x=\"1\"&y=2";
  };
  const auto resolved = render_document_markdown(document, options);
  require(resolved.find("href=\"topics/a?x=&quot;1&quot;&amp;y=2\"") !=
              std::string::npos,
          "styled Markdown ignored or failed to escape the exporter's link "
          "resolver");
  auto edge_rows = pre;
  edge_rows.lines = {"", "\t  END  ", ""};
  edge_rows.styled_lines = {{}, {in(CodeInlineIR{"\t  END  "})}, {}};
  auto edge_document = document;
  edge_document.blocks.front().node = edge_rows;
  const auto edge_html = render_document_html_blocks(edge_document, {});
  require(edge_html.find("<pre class=\"geist-preformatted\"><span></span>\n") ==
              0,
          "HTML stripped the first empty positioned row");
  require(html_visible_text(edge_html) == "\n\n\t  END  \n\n",
          "styled HTML lost first/last empty rows, indentation, tabs or "
          "trailing spaces");
  DocumentRenderTraceIR edge_trace;
  const auto edge_markdown =
      render_document_markdown(edge_document, {}, &edge_trace);
  require(
      markdown_visible_text(edge_markdown, edge_trace).find("\n\t  END  \n") !=
          std::string::npos,
      "styled Markdown lost blank rows, tabs or trailing spaces");
  const auto rejects = [&](PreformattedBlockIR changed,
                           const std::string &reason) {
    auto mutation = document;
    mutation.blocks.front().node = std::move(changed);
    error.clear();
    require(!verify_document_ir(mutation, &error), reason);
  };
  auto changed = pre;
  std::get<TextInlineIR>(changed.styled_lines[0][0].node).text = " ";
  rejects(changed, "styled row may not delete a source space");
  changed = pre;
  changed.styled_lines.pop_back();
  rejects(changed, "styled rows must match the source line count");
  changed = pre;
  changed.line_origins.clear();
  rejects(changed, "styled rows must retain per-line origins");
  changed = pre;
  changed.styled_lines[0].push_back(in(HardBreakInlineIR{}));
  rejects(changed, "a styled inline may not invent another source line");
  changed = pre;
  changed.styled_lines[0][0].origin = {};
  rejects(changed, "styled inline provenance must verify");
  changed = pre;
  std::get<EmphasisInlineIR>(changed.styled_lines[0][1].node).kind =
      static_cast<EmphasisKindIR>(99);
  rejects(changed, "styled inline font kind must verify");
  changed = pre;
  changed.line_links.resize(pre.lines.size());
  changed.line_links[0].push_back({0, 1, VerbatimLinkKindIR::in_book, "other"});
  rejects(changed, "styled rows may not have a second link owner");
  for (const auto &inline_node :
       {InlineNodeIR(TextInlineIR{"\n"}), InlineNodeIR(CodeInlineIR{"\n"}),
        InlineNodeIR(CrossReferenceInlineIR{
            {CrossReferenceTargetKindIR::anchor, "target"}, "\n"}),
        InlineNodeIR(ImageInlineIR{"resource:1", "image"})}) {
    changed = pre;
    changed.styled_lines[1] = {in(inline_node)};
    changed.lines[1] = "\n";
    rejects(changed,
            "styled rows may not contain embedded line breaks or images");
  }
  changed = pre;
  DocumentSourceSliceIR source;
  source.logical_record = 1;
  source.segment_index = 0;
  source.token_begin = 3;
  source.token_end = 4;
  source.byte_begin = 10;
  source.byte_end = 11;
  changed.line_origins[0].slices = {source};
  changed.styled_lines[0][0].origin.slices = {source};
  changed.styled_lines[0][0].origin.slices[0].token_begin = 5;
  changed.styled_lines[0][0].origin.slices[0].token_end = 6;
  rejects(changed, "styled row origin must contain inline source claims");
  auto before_provenance = document;
  std::get<PreformattedBlockIR>(before_provenance.blocks.front().node)
      .styled_lines[0][0]
      .origin.slices = {source};
  auto after_provenance = before_provenance;
  std::get<PreformattedBlockIR>(after_provenance.blocks.front().node)
      .styled_lines[0][0]
      .origin.slices[0]
      .character_begin = 1;
  std::get<PreformattedBlockIR>(after_provenance.blocks.front().node)
      .styled_lines[0][0]
      .origin.slices[0]
      .character_end = 2;
  require(
      format_document_ir(before_provenance) !=
          format_document_ir(after_provenance),
      "canonical formatting ignores styled partial-word provenance mutations");
}

std::filesystem::path book(const std::string &name) {
  return std::filesystem::path(GEIST_FIXTURE_DIR) / name;
}

void check_packet_conserves_every_drawn_word() {
  const auto document = geist::BooDocument::open(book("packet.boo"));
  const auto report = document.drawn_word_conservation();

  require(report.topics_checked > 0, "packet.boo offered no topic to check");
  for (const auto &indexed : document.topics()) {
    const auto found = std::find_if(
        report.topics.begin(), report.topics.end(),
        [&](const auto &candidate) { return candidate.id == indexed.id; });
    require(found != report.topics.end(),
            "packet.boo indexed topic is absent from conservation: " +
                indexed.id);
    if (found != report.topics.end()) {
      require(found->source_kind == "topic-index" &&
                  found->source_start_logical_record ==
                      indexed.start_logical_record &&
                  found->source_end_logical_record ==
                      indexed.end_logical_record,
              "packet.boo conservation lost source identity for " + indexed.id);
    }
  }
  require(report.topics_with_unframed_records == 0,
          "packet.boo holds a topic whose records do not tile into display "
          "lines, so its drawn text is undecided");

  for (const auto &topic : report.topics) {
    for (const auto &deficit : topic.deficits) {
      if (deficit.unaccounted == 0)
        continue;
      std::cerr << "FAIL: packet.boo " << topic.id << " [" << topic.route
                << "] draws `" << deficit.word << "` " << deficit.drawn_inline
                << " time(s) inside a display line and emits it "
                << deficit.emitted << " time(s): " << deficit.evidence << "\n";
      geist_test::record_failure();
    }
  }
  require(report.unaccounted_words == 0,
          "packet.boo drops display text its source draws");
  require(report.unaccounted_markdown_words == 0,
          "packet.boo drops a drawn word from Markdown");
  require(report.unaccounted_html_words == 0,
          "packet.boo drops a drawn word from HTML");
}

} // namespace

int main() {
  check_word_cut();
  check_html_projection();
  check_projection_rejects_unsupported_html();
  check_markdown_projection();
  check_native_message_metadata();
  check_styled_preformatted_rows();
  check_packet_conserves_every_drawn_word();
  return 0;
}
