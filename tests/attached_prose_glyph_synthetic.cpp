// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "test_failures.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace geist::detail;
TokenWords w(const std::string &s) { return {s.begin(), s.end()}; }
void check(bool ok, const std::string &message) {
  if (!ok) {
    std::cerr << "attached_prose_glyph_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}
struct Builder {
  DecodedLogicalRecordSource record;
  std::uint16_t encoded = 0x100;
  std::uint8_t glyph_width = 1;
  void line(std::vector<TokenWords> tokens) {
    std::size_t bytes = 0;
    for (const auto &words : tokens)
      bytes += words == TokenWords{0x2666}
                   ? glyph_width
                   : (words == TokenWords{1} || words == TokenWords{2}) ? 1
                                                                         : 2;
    record.encoded_tokens.push_back({static_cast<std::uint16_t>(bytes), 1});
    record.tokens.push_back({0x25ba});
    for (auto &words : tokens) {
      record.encoded_tokens.push_back(
          words == TokenWords{0x2666} && glyph_width == 1
              ? EncodedLogicalToken{3, 1}
              : (words == TokenWords{1} || words == TokenWords{2})
                    ? EncodedLogicalToken{0, 1}
                    : EncodedLogicalToken{encoded++, 2});
      record.tokens.push_back(std::move(words));
    }
  }
  explicit Builder(bool cz = true) {
    line({w("sh1.1")});
    line({w("ctopicn"), w("10")});
    line({w("cparent"), w("1.0")});
    line({w("cforwardlevel")});
    line({w("cbacklevel")});
    line({w("csummary"), w("10"), w("0"), w("10")});
    line({w("chdlevel"), w(":H2")});
    line({w("csourcefn"), w("SAMPLE")});
    line({w("ST"), w("Example")});
    if (cz)
      line({w("cz"), w("FLOW"), w("P"), w("3"), w("3")});
  }
  DecodedLogicalRecordSource finish() {
    record.logical_record = 20;
    record.assembled = assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = 20;
    std::uint32_t byte = 1;
    for (std::size_t t = 0; t < record.tokens.size(); ++t) {
      const auto e = record.encoded_tokens[t];
      const bool prefix = !record.tokens[t].empty() && record.tokens[t][0] < 4;
      record.ir.tokens.push_back(
          {t,
           e,
           record.tokens[t],
           {byte, static_cast<std::uint32_t>(byte + e.width)},
           prefix,
           prefix ? record.tokens[t][0] : std::uint16_t{3}});
      byte += e.width;
    }
    record.ir.payload_range = {1, byte};
    assign_display_line_framing(record.ir);
    record.control_segments = decode_control_segments(
        20, record.assembled, record.encoded_tokens, record.ir.display_lines);
    demote_display_line_owned_controls(record);
    return std::move(record);
  }
};
std::optional<DocumentIR> lower(Builder &b, std::string &error) {
  return try_lower_topic_to_document_ir({"1.1", "Example", ":H2", 10, 20, 21},
                                        {b.finish()}, nullptr, &error);
}
void attached(bool styled, bool selector, bool embedded_prefix,
              bool two_byte_glyph = false) {
  Builder b;
  if (two_byte_glyph)
    b.glyph_width = 2;
  if (styled)
    b.line({w("cfont"), w("3"), w("6"), w("1"), w("10"), w("7"), w("E")});
  if (selector) {
    b.line({w("cselect"), w("3"), w("6"), w("1.2")});
    b.line({w("cselect"), w("10"), w("7"), w("GATEWAY")});
  }
  if (embedded_prefix)
    b.line({w("   "), w("ABC"), {1, 0x2666}, {1, 'd', 'e'}, w("Gateway")});
  else
    b.line({w("   "), w("SNA"), {1}, {0x2666}, {1}, w("ps"), w("Gateway")});
  std::string error;
  auto doc = lower(b, error);
  check(doc.has_value(), "attached mapped glyph rejected: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto expected = embedded_prefix ? "ABC°de" : "SNA°ps";
  check(html.find(expected) != std::string::npos, "attachment lost: " + html);
  check(html.find("<ul") == std::string::npos,
        "inline glyph became list bullet");
  if (styled) {
    check(html.find("<em class=\"geist-emphasis\">SNA°ps</em>") !=
              std::string::npos,
          "CFONT interval split/shifted: " + html);
    check(html.find("<code class=\"geist-monospace\">Gateway</code>") !=
              std::string::npos,
          "CFONT after glyph shifted: " + html);
  }
  if (selector) {
    check(html.find("href=\"#1.2\">SNA°ps</a>") != std::string::npos,
          "selector crossing glyph shifted: " + html);
    check(html.find("href=\"#GATEWAY\">Gateway</a>") != std::string::npos,
          "selector following glyph shifted: " + html);
  }
}
void rejected(std::vector<TokenWords> content) {
  Builder b;
  b.line(std::move(content));
  std::string error;
  check(!lower(b, error), "ambiguous or unmapped glyph was admitted");
}
void single_column_span(bool selector) {
  Builder b;
  b.line(
      selector
          ? std::vector<TokenWords>{w("cselect"), w("6"), w("1"), w("TARGET")}
          : std::vector<TokenWords>{w("cfont"), w("6"), w("1"), w("1")});
  b.line({w("   "), w("ABC"), {1}, {0x2666}, {1}, w("de")});
  std::string error;
  auto doc = lower(b, error);
  check(doc.has_value(), "single-column glyph span rejected: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  check(html.find(selector ? "ABC<a class=\"geist-link geist-link--anchor\" "
                             "href=\"#TARGET\">°</a>de"
                           : "ABC<em class=\"geist-emphasis\">°</em>de") !=
            std::string::npos,
        "single-column glyph span changed text/columns: " + html);
}
void suppress_following_spaces() {
  Builder b;
  b.line({w("   "), {2, 'A', 'B', 'C'}, {2, 0x2666}, w("de")});
  std::string error;
  auto doc = lower(b, error);
  check(doc.has_value(), "prefix-2 attachment rejected: " + error);
  if (doc)
    check(render_document_html_fragment(*doc).find("ABC°de") !=
              std::string::npos,
          "prefix-2 attachment lost");
}
void mixed_word() {
  Builder b;
  b.line({w("   "), {'A', 'B', 'C', 0x2666, 'd', 'e'}});
  std::string error;
  auto doc = lower(b, error);
  check(doc.has_value(), "mixed-token mapped glyph rejected: " + error);
  if (doc)
    check(render_document_html_fragment(*doc).find("ABC°de") !=
              std::string::npos,
          "mixed-token mapped glyph lost");
}
void leading_native_box_glyphs() {
  Builder b(false);
  // The record's display-line framing proves that the box words follow the
  // row's length byte and origin.  They are therefore the first drawn cells
  // of this native row, not a speculative marker for another row.
  b.line({w("cfont"), w("5"), w("6"), w("P")});
  b.line({w("   "), {0x2514, 0x2500},
          {1, 'o', 'p', 't', 'i', 'o', 'n'}, {1, 0x2500, 0x2518}});
  std::string error;
  auto doc = lower(b, error);
  check(doc.has_value(), "leading native box glyphs rejected: " + error);
  if (doc) {
    const auto html = render_document_html_fragment(*doc);
    check(html.find("<pre") != std::string::npos &&
              html.find(">   |_<code") != std::string::npos &&
              html.find(">option</code>_|</pre>") != std::string::npos,
          "leading native box glyphs were not drawn: " + html);
  }

  Builder plain(false);
  plain.line({w("   "), {0x2514, 0x2500},
              {1, 'o', 'p', 't', 'i', 'o', 'n'},
              {1, 0x2500, 0x2518}});
  error.clear();
  doc = lower(plain, error);
  check(doc.has_value(), "unstyled native box row rejected: " + error);
  if (doc) {
    const auto html = render_document_html_fragment(*doc);
    check(html.find("<pre") != std::string::npos &&
              html.find(">   |_option_|</pre>") != std::string::npos,
          "unstyled native box row was reflowed: " + html);
  }
}
void native_drawing_region() {
  Builder b(false);
  // The top rule fixes the rails at columns 3 and 10. Body annotations may
  // extend outside those rails, and the stored empty display row belongs to
  // the drawing because aligned sides and a bottom rule close around it.
  b.line({w("   "), {0x250c, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500,
                      0x2500, 0x2510}});
  b.line({w("cfont"), w("4"), w("5"), w("P")});
  b.line({w("cselect"), w("4"), w("5"), w("TARGET")});
  // One source token supplies five display cells. The arrow is one selector
  // column but three rendered UTF-8 bytes, so its inline claim must use the
  // displayed spelling rather than the decoder's one-byte `?` fallback.
  b.line({{2, '0'}, w("  "), {2, 0x2502},
          {1, 'L', 'A', 0x2192, 'E', 'L'}, {0x2502},
          w("tail")});
  b.line({w("   "), {0x251c, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500,
                      0x2500, 0x2524}});
  b.line({});
  b.line({w("   "), {2, 0x2502}, w("      "), {2, 0x2502}});
  b.line({w("   "), {0x2514, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500,
                      0x2500, 0x2518}});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "native drawing region rejected: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  check(html.find("<pre") != std::string::npos &&
            html.find("0  |") != std::string::npos &&
            html.find("LA→EL") != std::string::npos &&
            html.find("| tail") != std::string::npos &&
            html.find("\n\n   |") != std::string::npos,
        "native drawing rows were not composed exactly: " + html);
  check(html.find("<code class=\"geist-monospace\">") != std::string::npos,
        "native drawing font was lost: " + html);
  check(html.find("href=\"#TARGET\"") != std::string::npos,
        "native drawing selector was lost: " + html);
  const auto markdown = render_document_markdown(*doc);
  check(markdown.find("<pre") != std::string::npos &&
            markdown.find("LA→EL") != std::string::npos &&
            markdown.find("href=\"#TARGET\"") != std::string::npos &&
            markdown.find("geist-monospace") != std::string::npos &&
            markdown.find("\n\n   |") != std::string::npos,
        "native drawing UTF-8 glyph changed in Markdown: " + markdown);
}
// The text between two `<pre>` markers of a rendered fragment.
std::string pre_body(const std::string &html) {
  const auto open = html.find("<pre");
  if (open == std::string::npos) return {};
  const auto body = html.find('>', open);
  const auto close = html.find("</pre>", body);
  if (body == std::string::npos || close == std::string::npos) return {};
  return html.substr(body + 1, close - body - 1);
}
// An unclosed drawing is no closed box region: it has no bottom rule.  It is
// still an open native drawing -- a blank-delimited run of display rows, one
// of them drawn from box words alone -- and hosted BookServer prints such a
// run verbatim (IEAC6MST 2.1 record 83 lines 3..7, DT 19920124000100, an
// ISPF panel excerpt drawn with a top rule and rails and no bottom rule).
// The blank row ends the run: it must not absorb the prose after it.
void unclosed_native_drawing() {
  Builder b(false);
  b.line({});
  b.line({w("   "), {0x250c, 0x2500, 0x2500, 0x2500, 0x2510}});
  b.line({w("   "), {2, 0x2502}, w("   "), {2, 0x2502}});
  b.line({});
  b.line({w("ordinary prose")});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "unclosed native drawing rejected: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto pre = pre_body(html);
  check(pre == "    ___ \n   |   |",
        "unclosed drawing rows were not drawn verbatim: " + html);
  check(html.find("ordinary prose") != std::string::npos &&
            pre.find("ordinary prose") == std::string::npos,
        "an unclosed drawing absorbed its blank row and following prose: " +
            html);
}
// The run must be the drawing's own: a structural control line inside it
// (here the topic's own `ST` title line, with no blank row between) is
// nothing a drawing can hold, so the box-only row keeps its fail-closed
// reading.
void open_drawing_with_structure_declines() {
  Builder b(false);
  b.line({w("   "), {0x250c, 0x2500, 0x2500, 0x2500, 0x2510}});
  b.line({w("   "), {2, 0x2502}, w("   "), {2, 0x2502}});
  b.line({});
  b.line({w("ordinary prose")});
  std::string error;
  const auto doc = lower(b, error);
  check(!doc && error.find("placeholder run") != std::string::npos,
        "a drawing glued to a structural control line was admitted: " +
            (doc ? render_document_html_fragment(*doc) : error));
}
// The IEAC6MST 2.1 record 83 line 3 token shape: origin run, top-left corner,
// an attaching bare token, the rule split over two dictionary runs, and the
// top-right corner.  Nothing on the row is an ordinary word, and it is not a
// rule-only line either, so it is neither the `<hr>` reading nor a marker
// slot: it is drawn, and so is every row of its run, the word-bearing
// `MENU` row included.
void indented_box_only_rows() {
  Builder b(false);
  b.line({});
  b.line({w("You see:")});
  b.line({});
  b.line({w("       "), {0x250c}, {1}, {0x2500, 0x2500, 0x2500, 0x2500},
          {1}, {0x2500, 0x2500}, {1, 0x2510}});
  b.line({w("       "), {0x2502}, w("     "), {0x2502}});
  b.line({w("cfont"), w("8"), w("4"), w("2")});
  b.line({w("       "), {0x2502}, {1, 'M', 'E', 'N', 'U'}, w(" "),
          {0x2502}});
  b.line({w("       "), {0x2502}, w("     "), {0x2502}});
  b.line({});
  b.line({w("Then continue.")});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "indented box-only rows rejected: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto pre = pre_body(html);
  check(pre == "        ______ \n"
               "       |      |\n"
               "       |<strong class=\"geist-strong\">MENU</strong>  |\n"
               "       |      |",
        "indented box-only rows were not drawn verbatim: " + html);
  check(html.find("You see:") < html.find("<pre") &&
            html.find("Then continue.") > html.find("</pre>"),
        "prose around the drawing moved into it: " + html);
  const auto markdown = render_document_markdown(*doc);
  check(markdown.find("       |      |") != std::string::npos,
        "indented box-only row lost in Markdown: " + markdown);
}
// The SC24-546 5.3 record 623 line 20 shape: a bottom rule with junctions,
// below a row whose box words are preceded by a label.  The label row is
// part of the drawing, so its columns survive as well.
void indented_box_bottom_row() {
  Builder b(false);
  b.line({});
  b.line({w("   "), w("FIELDS:"), {1, 0x2502}, {1, 'N', 'A', 'M', 'E'},
          w(" "), {0x2502}});
  b.line({w("          "), {0x2514}, {1}, {0x2500, 0x2500, 0x2500, 0x2500},
          {1, 0x2518}});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "indented box bottom row rejected: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  check(pre_body(html) == "   FIELDS:|NAME  |\n"
                          "          |____|",
        "field template was not drawn verbatim: " + html);
}
// A row of nothing but `U+2500` stays the reader's horizontal rule, however
// far it is indented: the open-drawing reading must not take it.
void indented_rule_line_stays_rule() {
  Builder b(false);
  b.line({});
  b.line({w("Above.")});
  b.line({w("   "), {0x2500, 0x2500, 0x2500, 0x2500}});
  b.line({w("Below.")});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "indented rule line rejected: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  check(html.find("<hr") != std::string::npos &&
            html.find("____") == std::string::npos,
        "indented rule line was drawn instead of ruled: " + html);
}
// A box word whose hosted glyph is unproven (`U+255A`, FA1PLMM0 C.0 record
// 781 line 16) would print the decoder's `?` inside the drawing.
void unproven_box_glyph_declines() {
  Builder b(false);
  b.line({});
  b.line({w("   "), {0x250c, 0x2500, 0x2510}});
  b.line({w("   "), {0x255a, 0x255a, 0x255a}});
  b.line({});
  b.line({w("ordinary prose")});
  std::string error;
  const auto doc = lower(b, error);
  check(!doc, "a drawing with an unproven box glyph was admitted: " +
                  (doc ? render_document_html_fragment(*doc) : error));
}
// A cross-book `LNK` selector line inside a closed box draws nothing: its
// `<...>` alternatives are the selector's destination (SC24-5527-02 1.0
// record 59 line 29, `cselect 47 30 LNK <BOOK> <> <> <SC24-5444> <ANY>
// <HCPA3>` inside the `Note` box, DT 19921218151459).
void lnk_selector_inside_closed_box() {
  Builder b(false);
  b.line({});
  b.line({w("   "), {0x250c, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500,
                      0x2510}});
  b.line({w("   "), {2, 0x2502}, w("      "), {2, 0x2502}});
  b.line({w("cselect"), w("4"), w("5"), w("LNK"), w("<BOOK>"), w("<>"),
          w("<>"), w("<SC24-5444>"), w("<ANY>"), w("<HCPA3>")});
  b.line({w("   "), {2, 0x2502}, {1, 'G', 'U', 'I', 'D', 'E'}, w(" "),
          {0x2502}});
  b.line({w("   "), {0x2514, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500,
                      0x2518}});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "closed box around a LNK selector rejected: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto pre = pre_body(html);
  check(pre.find("SC24-5444") == std::string::npos &&
            pre.find("cselect") == std::string::npos,
        "LNK selector line was drawn inside the box: " + html);
  check(pre.find(">GUIDE</a>") != std::string::npos &&
            pre.find("|______|") != std::string::npos,
        "LNK selector did not link the next box row: " + html);
}
void bullet() {
  Builder b;
  b.line({w("cz"), w("BREAK"), w("3")});
  b.line({w("cz"), w("FLOW"), w("UL"), w("3"), w("3")});
  b.line({w("cz"), w("FLOW"), w("LI"), w("3"), w("6")});
  b.line({w("   "), {0x2666}, w("  "), w("First item")});
  b.line({w("cz"), w("OFF"), w("EUL"), w("3"), w("3")});
  std::string error;
  auto doc = lower(b, error);
  check(doc.has_value(), "real list bullet rejected: " + error);
  if (doc) {
    const auto html = render_document_html_fragment(*doc);
    check(html.find("<li") != std::string::npos,
          "real bullet no longer produces a list: " + html);
  }
}
} // namespace
int main() {
  attached(false, false, false);
  attached(true, false, false);
  attached(false, true, false);
  attached(false, false, true);
  attached(false, false, false, true);
  single_column_span(false);
  single_column_span(true);
  suppress_following_spaces();
  mixed_word();
  leading_native_box_glyphs();
  native_drawing_region();
  unclosed_native_drawing();
  open_drawing_with_structure_declines();
  indented_box_only_rows();
  indented_box_bottom_row();
  indented_rule_line_stays_rule();
  unproven_box_glyph_declines();
  lnk_selector_inside_closed_box();
  bullet();
  rejected({w("   "), w("SNA"), {1}, {0xffff}, {1}, w("ps")});
  rejected({w("   "), w("SNA"), {0x2666}, w("ps")});
  rejected({w("   "), w("SNA"), {1}, {0x2666}, w("ps")});
  rejected({w("   "), w("SNA "), {1}, {0x2666}, {1}, w("ps")});
  rejected({w("   "), {'A', 0xffff, 'B'}});
  return 0;
}
