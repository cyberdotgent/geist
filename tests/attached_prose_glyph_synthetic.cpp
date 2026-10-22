// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/prose/prose_topic_internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "test_failures.hpp"

#include <algorithm>
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
  // `length_word` is the word the book's dictionary spells for the line's
  // length byte; it is a length and displays nothing.
  void line(std::vector<TokenWords> tokens,
            TokenWords length_word = {0x25ba}) {
    std::size_t bytes = 0;
    for (const auto &words : tokens)
      bytes += words == TokenWords{0x2666}
                   ? glyph_width
                   : (words == TokenWords{1} || words == TokenWords{2}) ? 1
                                                                         : 2;
    record.encoded_tokens.push_back({static_cast<std::uint16_t>(bytes), 1});
    record.tokens.push_back(std::move(length_word));
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
  // An empty display line spelled exactly as a book stores it: the length
  // byte `00`, which decodes to the bare attach control.
  void blank() {
    record.encoded_tokens.push_back({0, 1});
    record.tokens.push_back({1});
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
// The rows of a closed note box, with an `SRTBL<id>` ... `SRETBL` envelope
// standing on lines of its own between them (SC24-5527-02 7.4.5 records
// 675-676, `TBLTBLUNIQ492`).  `inner` draws a box rail inside the envelope's
// rows, the ITPPIBOK 4.3.5.4 shape of a genuine table inside a frame.
void boxed_envelope(Builder &b, bool inner, const std::string &opener) {
  const TokenWords top{0x250c, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500,
                       0x2510};
  const TokenWords bottom{0x2514, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500,
                          0x2500, 0x2518};
  b.line({});
  b.line({w("   "), top});
  b.line({w("   "), {2, 0x2502}, {1, 'N', 'O', 'T', 'E'}, w(" "),
          {0x2502}});
  b.line({w(opener)});
  b.line({w("   "), {2, 0x2502}, w("      "), {2, 0x2502}});
  if (inner)
    b.line({w("   "), {2, 0x2502}, w("  "), {2, 0x2502}, w("   "),
            {2, 0x2502}});
  else
    b.line({w("   "), {2, 0x2502}, {1, 'C', 'M', 'D'}, w("  "), {0x2502}});
  b.line({w("SRETBL")});
  b.line({w("   "), {2, 0x2502}, w("      "), {2, 0x2502}});
  b.line({w("   "), bottom});
}
// Hosted SC24-5527-02 7.4.5 (DT 19921218151459) prints the note box verbatim
// with no `<table>`: the opener is `<a name="TBLTBLUNIQ492">` on the first box
// row after it, and the closer prints nothing.
void envelope_inside_closed_box() {
  Builder b(false);
  boxed_envelope(b, false, "SRTBLTBLUNIQ7");
  b.line({});
  b.line({w("ordinary prose")});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "envelope inside a closed box rejected: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto pre = pre_body(html);
  const auto anchor = pre.find("id=\"TBLTBLUNIQ7\"");
  const auto note = pre.find("NOTE");
  const auto cmd = pre.find("CMD");
  check(html.find("<table") == std::string::npos &&
            html.find("ETBL") == std::string::npos &&
            pre.find("SRTBL") == std::string::npos,
        "box envelope became a table, a control row or an ETBL anchor: " +
            html);
  check(anchor != std::string::npos && note < anchor && anchor < cmd &&
            pre.find("|______|") != std::string::npos,
        "box envelope anchor is not on the row after its opener: " + html);
}
// Each shape the rule does not prove keeps the envelope with the table pass.
void envelope_box_markup_is_narrow() {
  const auto markup = [](bool inner, const std::string &opener,
                         bool second_opener) {
    Builder b(false);
    boxed_envelope(b, inner, opener);
    if (second_opener) {
      b.line({});
      b.line({w(opener)});
      b.line({w("   "), w("x")});
      b.line({w("SRETBL")});
    }
    return prose_internal::box_markup_envelopes({b.finish()});
  };
  check(markup(false, "SRTBLTBLUNIQ7", false).size() == 2,
        "a box's own envelope was not read as box markup");
  check(markup(true, "SRTBLTBLUNIQ7", false).empty(),
        "an envelope that draws an inner rail was read as box markup");
  check(markup(false, "SRTBLTBLUNIQ7", true).empty(),
        "an envelope id the topic repeats was read as box markup");
  // An envelope beside an open drawing has no rails that bound it.
  Builder open(false);
  open.line({});
  open.line({w("   "), {0x250c, 0x2500, 0x2500, 0x2500, 0x2510}});
  open.line({w("SRTBLTBLUNIQ8")});
  open.line({w("   "), {2, 0x2502}, w("   "), {2, 0x2502}});
  open.line({w("SRETBL")});
  check(prose_internal::box_markup_envelopes({open.finish()}).empty(),
        "an envelope inside an open drawing was read as box markup");
}
// An `SRELIS<owner>` closer directly under an open drawing ends the run
// (OFCUSEOV 1.8 record 119 line 7 `SRELISse` under the open `Daily Calendar`
// panel, DT 19900805103816): the panel is drawn and the closer prints
// nothing.
void list_closer_ends_open_drawing() {
  Builder b(false);
  b.line({});
  b.line({w("SRLISab")});
  b.line({w("   "), w("Step")});
  b.line({});
  b.line({w("   "), {0x250c, 0x2500, 0x2500, 0x2500, 0x2510}});
  b.line({w("   "), {2, 0x2502}, w("   "), {2, 0x2502}});
  b.line({w("SRELISab")});
  b.line({});
  b.line({w("   "), w("Next")});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "open drawing ended by a list closer rejected: " +
                             error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  check(pre_body(html) == "    ___ \n   |   |" &&
            html.find("ELIS") == std::string::npos &&
            html.find("Next") != std::string::npos,
        "list closer did not end the open drawing cleanly: " + html);
}
// A typed topic never loses drawn content.  Each shape below draws box-only
// rows that neither a closed box nor an open drawing claims: the run holds a
// structural control line (an envelope opener, a closer with no list owner)
// that no drawing can hold.  The lowering either draws every box cell or
// declines; it must not succeed with the drawn rows missing.
std::size_t drawn_cells(const std::string &text) {
  std::size_t cells = 0;
  for (const auto ch : text)
    if (ch == '|' || ch == '_') ++cells;
  return cells;
}
void unclaimed_rows_never_vanish(DecodedLogicalRecordSource record,
                                 std::size_t drawn, const std::string &shape,
                                 const std::string &reason = "drawn row") {
  std::string error;
  const auto doc = try_lower_topic_to_document_ir(
      {"1.1", "Example", ":H2", 10, 20, 21}, {std::move(record)}, nullptr,
      &error);
  if (!doc) {
    check(error.find(reason) != std::string::npos,
          shape + ": declined for an unexpected reason: " + error);
    return;
  }
  const auto html = render_document_html_fragment(*doc);
  check(drawn_cells(html) >= drawn,
        shape + ": typed topic dropped drawn box rows: " + html);
  const auto markdown = render_document_markdown(*doc);
  check(drawn_cells(markdown) >= drawn,
        shape + ": typed Markdown dropped drawn box rows: " + markdown);
}
void unclaimed_rows_never_vanish(Builder &b, std::size_t drawn,
                                 const std::string &shape,
                                 const std::string &reason = "drawn row") {
  unclaimed_rows_never_vanish(b.finish(), drawn, shape, reason);
}
// The field-template bottom rule is the topic's last display line, and the
// run above it is glued to a list opener: nothing follows the rule.
void unclaimed_box_rows_at_topic_end() {
  Builder b(false);
  b.line({});
  b.line({w("SRLISab")});
  b.line({w("   "), w("FIELDS:"), {1, 0x2502}, {1, 'N', 'A', 'M', 'E'},
          w(" "), {0x2502}});
  b.line({w("          "), {0x2514}, {1}, {0x2500, 0x2500, 0x2500, 0x2500},
          {1, 0x2518}});
  unclaimed_rows_never_vanish(b, 2 + 6, "topic end");
}
// An open drawing (top rule and rails, no bottom) glued directly to a
// structural closer that is no list-item delimiter, with prose after it.
void unclaimed_box_rows_before_closer() {
  Builder b(false);
  b.line({});
  b.line({w("   "), w("Step")});
  b.line({});
  b.line({w("   "), {0x250c, 0x2500, 0x2500, 0x2500, 0x2510}});
  b.line({w("   "), {2, 0x2502}, w("   "), {2, 0x2502}});
  b.line({w("SRELIS")});
  b.line({});
  b.line({w("   "), w("Next")});
  unclaimed_rows_never_vanish(b, 3 + 2, "glued closer");
}
// The same run opened by a structural opener rather than closed by one.  The
// top rule here stands directly in front of the next row's text, which the
// placeholder-run rule already declines.
void unclaimed_box_rows_after_opener() {
  Builder b(false);
  b.line({});
  b.line({w("   "), w("Step")});
  b.line({});
  b.line({w("SRNOTEab")});
  b.line({w("   "), {0x250c, 0x2500, 0x2500, 0x2500, 0x2510}});
  b.line({w("   "), {2, 0x2502}, w("   "), {2, 0x2502}});
  b.line({});
  b.line({w("   "), w("Next")});
  unclaimed_rows_never_vanish(b, 3 + 2, "glued opener", "placeholder run");
}
// Inside a `cz OFF` verbatim region every display row is served as stored
// (SC41-485 1.2, DT 19951003131222: the labelled box, its anchors on the box
// rows).  An `SR<id>` anchor alone on its line whose flattened segment runs
// on into the next rail row must leave that row to the region: SC41-485 1.2
// record 8 lines 29/30 (`SRFIGTBLUNIQ1`, then `   |` .. `|`).
void verbatim_anchor_keeps_rail_row() {
  Builder b;
  b.line({w("cz"), w("OFF"), w("LBLBOX")});
  b.line({w("   "), {0x250c, 0x2500, 0x2500, 0x2500, 0x2510}});
  b.line({w("   "), {2, 0x2502}, {1, 'A', 'B', 'C'}, {2, 0x2502}});
  b.line({w("SRFIGX1")});
  b.line({w("   "), {0x2502}, w("   "), {0x2502}});
  b.line({w("   "), {0x2514, 0x2500, 0x2500, 0x2500, 0x2518}});
  b.line({w("cz"), w("OFF"), w("ELBLBOX")});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "verbatim region with an anchor rejected: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto pre = pre_body(html);
  // Top rule, the `ABC` row, the rail row after the anchor, bottom rule.
  check(std::count(pre.begin(), pre.end(), '\n') == 3 &&
            pre.find("|ABC") != std::string::npos &&
            pre.find("\n   |    |\n") != std::string::npos &&
            pre.find("|___|") != std::string::npos,
        "verbatim region lost the rail row after its anchor: " + html);
}
// A picture-less `SRFIG<id>` frame drawn around a genuine table: ITPPIBOK
// 4.3.5.4 records 158-160 (`SRFIGSCHED`, `SRTBLTBLUNIQ34`).  The frame draws
// its own top rule and rails before the `SRTBL` line and its rails and
// bottom rule after `SRETBL`; the table's rows carry the frame's rails beside
// its own grid.  Hosted BookServer (DT 19910628074854) prints every one of
// those rows in one `<pre>`, with `<a name="TBLTBLUNIQ34">` on the first row
// after the `SRTBL` line and no `<table>`.  The frame's rows are the table
// span's: they must be drawn around the table rows, in source order, and the
// caption after them stays prose text after the region.
TokenWords repeat(TokenWords head, std::uint16_t fill, std::size_t count,
                  std::uint16_t tail) {
  head.insert(head.end(), count, fill);
  head.push_back(tail);
  return head;
}
void framed_table(Builder &b, bool blank_inside_frame,
                  bool prose_inside_frame = false,
                  TokenWords after_length = w("<BOOK>")) {
  const auto rail = [&]() {
    b.line({w("   "), {0x2502}, w(std::string(20, ' ')), {0x2502}});
  };
  b.line({});
  b.line({w("   "), w("Text before.")});
  b.line({});
  b.line({w("SRFIGFR1")});
  b.line({w("   "), repeat({0x250c}, 0x2500, 20, 0x2510)});
  rail();
  if (prose_inside_frame)
    b.line({w("   "), w("Loose words.")});
  b.line({w("SRTBLTBLUNIQ9")});
  rail();
  b.line({w("   "), {0x2502}, w(" "),
          repeat(repeat({0x250c}, 0x2500, 8, 0x252c), 0x2500, 7, 0x2510),
          w(" "), {0x2502}});
  b.line({w("   "), {0x2502}, w(" "), {0x2502}, w(" "), w("A"),
          w("      "), {0x2502}, w(" "), w("B"), w("     "), {0x2502},
          w(" "), {0x2502}});
  b.line({w("   "), {0x2502}, w(" "),
          repeat(repeat({0x2514}, 0x2500, 8, 0x2534), 0x2500, 7, 0x2518),
          w(" "), {0x2502}});
  rail();
  b.line({w("SRETBL")});
  rail();
  if (blank_inside_frame)
    b.line({});
  rail();
  b.line({w("   "), repeat({0x2514}, 0x2500, 20, 0x2518)});
  b.line({w("   "), w("Figure 1. Caption")});
  b.line({w("SREFIG")});
  // An empty display line as the record stores it: the length byte `00`,
  // which a token reader spells as the bare attach control, then the next
  // line, whose length byte the dictionary spells as some word: ITPPIBOK
  // record 160 tokens 41 and 42 after `SREFIG` (`<BOOK>`), DREICMST record
  // 524 tokens 266 and 267 (`U+251C`, a decoder placeholder).
  b.blank();
  b.line({w("   "), w("Text after.")}, std::move(after_length));
}
void frame_rows_around_table(bool blank_inside_frame,
                             TokenWords after_length = w("<BOOK>")) {
  Builder b(false);
  framed_table(b, blank_inside_frame, false, std::move(after_length));
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "framed table declined: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto pre = pre_body(html);
  std::vector<std::string> rows;
  for (std::size_t at = 0; at <= pre.size();) {
    const auto end = std::min(pre.find('\n', at), pre.size());
    rows.push_back(pre.substr(at, end - at));
    at = end + 1;
  }
  // Top rule, rail, [anchor] rail, grid top, row, grid bottom, rail, rail,
  // (blank), rail, bottom rule.
  const std::size_t expected = blank_inside_frame ? 11 : 10;
  check(rows.size() == expected, "framed table does not draw the frame's " +
                                     std::to_string(expected) +
                                     " rows in one block: " + html);
  if (rows.size() != expected)
    return;
  const auto anchor = pre.find("id=\"TBLTBLUNIQ9\"");
  check(rows.front().find("____________________") != std::string::npos &&
            rows.back().find("|____________________|") != std::string::npos,
        "framed table lost the frame's top or bottom rule: " + html);
  check(anchor != std::string::npos && anchor > pre.find(rows[1]) &&
            anchor < pre.find(" A "),
        "table anchor is not on the first row after SRTBL: " + html);
  check(html.find("<table") == std::string::npos &&
            html.find("SRTBL") == std::string::npos &&
            html.find("SREFIG") == std::string::npos,
        "framed table printed a control or became a table: " + html);
  const auto figure = html.find("id=\"FIGFR1\"");
  const auto caption = html.find("Figure 1. Caption");
  const auto after = html.find("Text after.");
  check(figure != std::string::npos && figure < html.find("<pre") &&
            caption > html.find("</pre>") && caption != std::string::npos &&
            after != std::string::npos && after > caption,
        "frame anchor, caption or following prose misplaced: " + html);
  // The empty display line after `SREFIG` is a paragraph break, exactly as
  // anywhere else in the body: hosted ITPPIBOK 4.3.5.4 serves `Figure 2.
  // ...` then `<p>` then `Note that ...` (record 160 lines 5-8).
  check(html.find(">Figure 1. Caption</p>") != std::string::npos &&
            html.find("Caption Text after.") == std::string::npos,
        "caption ran on into the paragraph after SREFIG: " + html);
  const auto markdown = render_document_markdown(*doc);
  check(drawn_cells(markdown) >= drawn_cells(pre),
        "framed table Markdown dropped frame rows: " + markdown);
}
// A line of prose between the frame's rows and its table would be printed
// by the stream on the wrong side of the rows; that shape is not proven and
// the topic declines rather than reorder or drop anything.
// An empty display line followed by a subject-index line and then the next
// paragraph: ITPPIBOK 1.1 record 34 lines 17-21 (`... system testing.`,
// empty, two `SI` lines, `    TPNS Version 3 ...`).  The
// `SI` line displays nothing, so the empty line before it is still the
// paragraph break hosted DT 19910628074854 serves as `<p>`; reading the
// index term must not swallow it.
void index_line_keeps_paragraph_break() {
  Builder b(false);
  b.line({w("   "), w("First paragraph.")});
  b.blank();
  b.line({w("SI"), w("term")});
  b.line({w("   "), w("Second paragraph.")}, w("<BOOK>"));
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "index line between paragraphs declined: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  check(html.find(">First paragraph.</p>") != std::string::npos &&
            html.find(">Second paragraph.</p>") != std::string::npos,
        "index line swallowed the paragraph break before it: " + html);
}
// A paragraph whose first line stands directly below a subject-index line
// and is set one column deeper than the line that continues it: ITPPIBOK
// 4.3.5.4 record 158 lines 10-12 (`SI schedule, example`, `    The final
// section ... to follow`, `   during the test. ...`).  No empty display line
// stands between the two rows, and hosted DT 19910628074854 serves them in
// one paragraph; the one-column step is not an outdent.
void index_line_extra_blank_keeps_paragraph() {
  Builder b(false);
  b.line({w("SI"), w("term")});
  b.line({w("    "), w("First row")}, w("<BOOK>"));
  b.line({w("   "), w("continues here.")}, w("<BOOK>"));
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "index line before a paragraph declined: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  check(html.find(">First row continues here.</p>") != std::string::npos,
        "paragraph split after its index line's extra blank: " + html);
}
void frame_rows_around_prose_decline() {
  Builder b(false);
  framed_table(b, false, true);
  std::string error;
  const auto doc = lower(b, error);
  check(!doc && error.find("frame rows around prose") != std::string::npos,
        "frame rows around prose were not declined: " + error);
}
// A bare `SR<id>` anchor alone on its line directly above the top rule of a
// closed box: SC24-546 4.3.21 record 375 line 12 is `SRSPTFDATE`, lines
// 13-20 the closed syntax-diagram box, and the anchor's flattened segment
// runs on into lines 13-14.  Hosted BookServer (DT 19940323131240) wraps the
// top rule in `<a name="SPTFDATE">` and prints every box row verbatim.
// `gap` puts an empty display line between the anchor and the box; `open`
// leaves the box without its bottom rule.  Neither shape is proven.
void anchored_box(Builder &b, const std::string &opener, bool gap = false,
                  bool open = false) {
  b.line({});
  b.line({w("   "), w("Before.")});
  b.line({});
  b.line({w(opener)});
  if (gap)
    b.line({});
  b.line({w("   "), repeat({0x250c}, 0x2500, 12, 0x2510)});
  b.line({w("   "), {0x2502}, w(std::string(11, ' ')), {0x2502}});
  b.line({w("cfont"), w("8"), w("3"), w("P")});
  b.line({w("   "), {0x2502}, w(">>_CMD__><"), {0x2502}});
  if (!open)
    b.line({w("   "), repeat({0x2514}, 0x2500, 12, 0x2518)});
  b.line({});
  b.line({w("   "), w("After.")});
}
// The real record's flattened `SRSPTFDATE` segment (record 375 segment 11)
// runs on past its own line through the top rule and the blank rail row, up
// to the `cfont` line that opens the next segment; its payload is box words
// alone.  The synthetic splitter ends the segment at its line, so the run-on
// is restored here: every token from the opener up to the `cfont` line joins
// the opener's segment.
DecodedLogicalRecordSource run_on_anchor(Builder &b,
                                         const std::string &opener) {
  auto record = b.finish();
  for (auto &segment : record.control_segments) {
    if (segment.opcode != opener || segment.source_tokens.empty())
      continue;
    for (auto token = segment.source_tokens.back() + 1;
         token < record.tokens.size(); ++token) {
      if (record.tokens[token] == w("cfont"))
        break;
      segment.source_tokens.push_back(token);
    }
    // The cfont line's length byte stays with the cfont line.
    segment.source_tokens.pop_back();
  }
  return record;
}
std::optional<DocumentIR> lower_record(DecodedLogicalRecordSource record,
                                       std::string &error) {
  return try_lower_topic_to_document_ir({"1.1", "Example", ":H2", 10, 20, 21},
                                        {std::move(record)}, nullptr, &error);
}
void anchor_above_closed_box() {
  Builder b(false);
  anchored_box(b, "SRSPTX1");
  std::string error;
  const auto doc = lower_record(run_on_anchor(b, "SRSPTX1"), error);
  check(doc.has_value(), "anchor above a closed box declined: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto pre = pre_body(html);
  // The anchor wraps the region's first row, which the lowering places in
  // front of the block (as it does a figure frame's top rule).
  const auto anchor = html.find("id=\"SPTX1\"");
  check(std::count(pre.begin(), pre.end(), '\n') == 3 &&
            anchor != std::string::npos && anchor > html.find("Before.") &&
            anchor < html.find("<pre") &&
            pre.find("CMD") != std::string::npos &&
            pre.find("|____________|") != std::string::npos,
        "anchored box is not drawn with its anchor on the top rule: " + html);
  check(html.find("SRSPT") == std::string::npos &&
            html.find("Before.") < html.find("<pre") &&
            html.find("After.") > html.find("</pre>"),
        "anchored box printed its control or misplaced its prose: " + html);
  const auto markdown = render_document_markdown(*doc);
  check(drawn_cells(markdown) >= drawn_cells(pre),
        "anchored box Markdown dropped drawn rows: " + markdown);
}
// The shapes the rule does not prove keep their former handling.
void anchor_above_box_is_narrow() {
  {
    Builder b(false);
    anchored_box(b, "SRSPTX1", true);
    unclaimed_rows_never_vanish(run_on_anchor(b, "SRSPTX1"), 12 + 2 + 5 + 14,
                                "anchor, gap, box");
  }
  {
    Builder b(false);
    anchored_box(b, "SRSPTX1", false, true);
    unclaimed_rows_never_vanish(run_on_anchor(b, "SRSPTX1"), 12 + 2 + 5,
                                "anchor above an open box");
  }
  {
    Builder b(false);
    anchored_box(b, "SRSPTX1");
    check(prose_internal::box_leading_anchors({run_on_anchor(b, "SRSPTX1")})
                  .size() == 1,
          "an anchor above a closed box was not read as its leader");
  }
  {
    Builder b(false);
    anchored_box(b, "SRSPTX1", true);
    check(prose_internal::box_leading_anchors({run_on_anchor(b, "SRSPTX1")})
              .empty(),
          "an anchor a blank line above a box was read as its leader");
  }
  {
    Builder b(false);
    anchored_box(b, "SRSPTX1", false, true);
    check(prose_internal::box_leading_anchors({run_on_anchor(b, "SRSPTX1")})
              .empty(),
          "an anchor above an open drawing was read as its leader");
  }
  {
    // A reserved envelope opener is never a bare anchor.
    Builder b(false);
    anchored_box(b, "SRFIGX1");
    std::string error;
    check(!lower_record(run_on_anchor(b, "SRFIGX1"), error),
          "a reserved envelope opener above a box was read as a bare anchor");
  }
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
  envelope_inside_closed_box();
  envelope_box_markup_is_narrow();
  list_closer_ends_open_drawing();
  unclaimed_box_rows_at_topic_end();
  unclaimed_box_rows_before_closer();
  unclaimed_box_rows_after_opener();
  verbatim_anchor_keeps_rail_row();
  anchor_above_closed_box();
  anchor_above_box_is_narrow();
  frame_rows_around_table(false);
  frame_rows_around_table(true);
  frame_rows_around_table(false, {0x251c});
  frame_rows_around_prose_decline();
  index_line_keeps_paragraph_break();
  index_line_extra_blank_keeps_paragraph();
  bullet();
  rejected({w("   "), w("SNA"), {1}, {0xffff}, {1}, w("ps")});
  rejected({w("   "), w("SNA"), {0x2666}, w("ps")});
  rejected({w("   "), w("SNA"), {1}, {0x2666}, w("ps")});
  rejected({w("   "), w("SNA "), {1}, {0x2666}, {1}, w("ps")});
  rejected({w("   "), {'A', 0xffff, 'B'}});
  return 0;
}
