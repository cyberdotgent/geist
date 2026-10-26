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
#include <set>
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
                   : (words == TokenWords{1} || words == TokenWords{2} ||
                      words == TokenWords{1, ','})
                         ? 1
                         : 2;
    record.encoded_tokens.push_back({static_cast<std::uint16_t>(bytes), 1});
    record.tokens.push_back(std::move(length_word));
    for (auto &words : tokens) {
      record.encoded_tokens.push_back(
          words == TokenWords{0x2666} && glyph_width == 1
              ? EncodedLogicalToken{3, 1}
              : (words == TokenWords{1} || words == TokenWords{2})
                    ? EncodedLogicalToken{0, 1}
                    : words == TokenWords{1, ','}
                          ? EncodedLogicalToken{2, 1}
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
    // `E` (XMP) is hosted `<samp>`.
    check(html.find("<samp class=\"geist-monospace geist-sample\">Gateway"
                    "</samp>") !=
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
// An unmapped dictionary word (0xFFFF) inside a display line is printed by
// the reader as its substitution byte 0xFF, attached or not: EDCGV1R2 4.1
// (DT 19891031000000) `<samp>\xff</samp>`, S85G-8678-00 6.1
// (DT 19950223205812) `\xff[Transfer]`.
void unmapped_displayed(std::vector<TokenWords> content,
                        const std::string &expected) {
  Builder b;
  b.line(std::move(content));
  std::string error;
  auto doc = lower(b, error);
  check(doc.has_value(), "unmapped word rejected: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  check(html.find(expected) != std::string::npos,
        "unmapped word not displayed as the substitution byte: " + html);
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
              html.find(">   |_<kbd") != std::string::npos &&
              html.find(">option</kbd>_|</pre>") != std::string::npos,
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
  // `P` (PK) is hosted `<kbd>`.
  check(html.find("<kbd class=\"geist-monospace geist-keyboard\">") !=
            std::string::npos,
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
// A box word whose hosted glyph is not established (`U+255D`) would print
// the decoder's `?` inside the drawing.
void unproven_box_glyph_declines() {
  Builder b(false);
  b.line({});
  b.line({w("   "), {0x250c, 0x2500, 0x2510}});
  b.line({w("   "), {0x255d, 0x255d, 0x255d}});
  b.line({});
  b.line({w("ordinary prose")});
  std::string error;
  const auto doc = lower(b, error);
  check(!doc, "a drawing with an unproven box glyph was admitted: " +
                  (doc ? render_document_html_fragment(*doc) : error));
}
// FA1PLMM0 C.0 record 781 lines 13-28: column heads underlined with runs of
// the word `U+255A`, inside an open drawing between two blank lines.  Hosted
// BookServer (DT 19910927114801) prints one display byte 0xC8 per cell, as
// SG24-2047-00 6.2.2 (DT 19971218054640) does for the same word in its
// table rules; like the arrows, the word's own character is emitted.
void u255a_run_draws_hosted_glyph() {
  Builder b(false);
  b.line({});
  b.line({w("   "), w("Available macros are:")});
  b.line({});
  b.line({w("         "), w("SUPVR"), w("     "), w("FOPT")});
  b.line({w("         "), {0x255a, 0x255a, 0x255a, 0x255a, 0x255a},
          w("     "), {0x255a, 0x255a, 0x255a, 0x255a}});
  b.line({w("          "), w("ID"), w("        "), w("DASDSHR")});
  b.line({});
  b.line({w("ordinary prose")});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "a U+255A underline row declined: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto pre = pre_body(html);
  check(pre.find("\xE2\x95\x9A\xE2\x95\x9A\xE2\x95\x9A\xE2\x95\x9A"
                 "\xE2\x95\x9A") !=
                std::string::npos &&
            pre.find("SUPVR") != std::string::npos &&
            pre.find("DASDSHR") != std::string::npos &&
            pre.find('?') == std::string::npos,
        "a U+255A underline row is not drawn: " + html);
}
// OFCUSEOV 2.1.6 record 416 line 17 draws an input field marker inside a
// closed screen box: `:` then the word `U+25A0` (`D7 47` at 0x2923C), in
// `|  Start typing your note on the next line.:` + `U+25A0` + spaces + `|`.
// Hosted BookServer (DT 19900805103816) prints the display byte 0xFE (`þ`),
// the CP437 code of `U+25A0`; the word's own character is emitted, never the
// decoder's `?`.
void u25a0_draws_its_character() {
  const auto rule = [](std::uint16_t left, std::uint16_t right) {
    TokenWords words{left};
    words.insert(words.end(), 12, 0x2500);
    words.push_back(right);
    return words;
  };
  Builder b(false);
  b.line({});
  b.line({w("   "), rule(0x250c, 0x2510)});
  b.line({w("   "), {0x2502},
          {' ', 'N', 'o', 't', 'e', ':', 0x25a0, ' ', ' ', ' ', ' '},
          {0x2502}});
  b.line({w("   "), rule(0x2514, 0x2518)});
  b.line({});
  b.line({w("ordinary prose")});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "a box with a U+25A0 cell declined: " + error);
  if (doc) {
    const auto pre = pre_body(render_document_html_fragment(*doc));
    check(pre.find("Note:\xE2\x96\xA0") != std::string::npos &&
              pre.find('?') == std::string::npos,
          "a U+25A0 cell is not drawn as its character: " + pre);
  }
  // In an open drawing the word was an unproven glyph that declined the
  // run; it is drawn the same way.
  Builder open(false);
  open.line({});
  open.line({w("   "), {0x250c, 0x2500, 0x2510}});
  open.line({w("   "), {0x2502, 0x25a0, 0x2502}});
  open.line({});
  open.line({w("ordinary prose")});
  const auto drawing = lower(open, error);
  check(drawing.has_value() &&
            pre_body(render_document_html_fragment(*drawing)) ==
                "    _ \n   |\xE2\x96\xA0|",
        "an open drawing with a U+25A0 cell is not drawn: " +
            (drawing ? render_document_html_fragment(*drawing) : error));
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
// rows: a genuine table when the box is a figure's frame (ITPPIBOK
// 4.3.5.4), the box's markup otherwise (SG24-2599-00 6.5.1.1).
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
                         bool second_opener, bool frame = false) {
    Builder b(false);
    if (frame) b.line({w("SRFIGFRAME")});
    boxed_envelope(b, inner, opener);
    if (frame) b.line({w("SREFIG")});
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
  // An envelope whose rows draw an inner rail is box markup in a box that
  // no figure envelope encloses (hosted SG24-2599-00 6.5.1.1, DT
  // 19960424163436, prints the grid inside its box verbatim), and a genuine
  // table inside a figure's frame (ITPPIBOK 4.3.5.4).
  check(markup(true, "SRTBLTBLUNIQ7", false).size() == 2,
        "an envelope that draws an inner rail in a plain box was not read as "
        "box markup");
  check(markup(true, "SRTBLTBLUNIQ7", false, true).empty(),
        "an envelope that draws an inner rail in a figure frame was read as "
        "box markup");
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
  // Top rule, the `ABC` row, the rail row after the anchor (which may now
  // open that row inside the block), bottom rule.
  check(std::count(pre.begin(), pre.end(), '\n') == 3 &&
            pre.find("|ABC") != std::string::npos &&
            pre.find("   |    |\n") != std::string::npos &&
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
                  TokenWords after_length = w("<BOOK>"),
                  bool styled_row = false, bool caption_comma = false) {
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
  // `cfont 9 1 2 20 1 2` styles `A` (column 9 of `   |  |  A       |  B`)
  // and `B` (column 20) of the row below in HP2, as ITPPIBOK record 158 line
  // 27 styles `TASK`, `START` and `DATE` on line 28.
  if (styled_row)
    b.line({w("cfont"), w("9"), w("1"), w("2"), w("20"), w("1"), w("2")});
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
  if (caption_comma)
    b.line({w("   "), w("Figure 1. Caption"), {1, ','}, w("continued")});
  else
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
// A prose line inside the frame keeps its glued punctuation.  SC24-5527-02
// COMMENTS frames `SRTBLTBLUNIQ549` in `SRFIGFIGUNIQ548`; record 978 line 5
// stores `comments` (token 17), the attach-prefixed comma `?,` (token 18,
// bytes `0x62092` = `02`) and `you` (token 19), and hosted BookServer (DT
// 19921218151459) serves `If you wish to make comments, you may print out
// this topic, fill in your`.  The comma sits on a line the table owns no
// token of, so it is the stream's to lower, not the frame's padding.
void frame_prose_line_keeps_comma() {
  Builder b(false);
  framed_table(b, false, false, w("<BOOK>"), false, true);
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "framed table with a captioned comma declined: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  check(html.find(">Figure 1. Caption, continued</p>") != std::string::npos,
        "frame prose line lost its glued comma: " + html);
}
// The end marker of a table span is followed the same way.  GG24-395 3.3.15
// record 635 closes `SRTBLTBLUNIQ21` with `SRETBL` alone on line 28, line 29
// is empty and line 30 opens the list ` | °   Need to reduce the cost ...`
// with a length byte the dictionary spells as five spaces; hosted DT
// 19941215160749 serves `<p>` and then all three items at the same columns.
// Dropping that length byte as spacing glued the first item to the column-0
// row of the end marker, and the topic declined as a misaligned list.
void list_after_table_end_marker(TokenWords first_length = {0x25ba}) {
  Builder b(false);
  b.line({w("   "), w("Text before.")});
  b.blank();
  b.line({w("SRTBLTBLUNIQ9")});
  b.line({w("   "), repeat(repeat({0x250c}, 0x2500, 8, 0x252c), 0x2500, 7,
                           0x2510)});
  b.line({w("   "), {0x2502}, w(" "), w("A"), w("      "), {0x2502}, w(" "),
          w("B"), w("     "), {0x2502}});
  b.line({w("   "), repeat(repeat({0x2514}, 0x2500, 8, 0x2534), 0x2500, 7,
                           0x2518)});
  b.line({w("SRETBL")});
  b.blank();
  b.line({w("   "), {0x2666}, w("  "), w("Need"), w("to"), w("reduce")},
         std::move(first_length));
  b.line({w("   "), {0x2666}, w("  "), w("Require"), w("improved")});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "list after a table end marker declined: " + error);
  if (!doc)
    return;
  const auto markdown = render_document_markdown(*doc);
  check(markdown.find("- Need to reduce\n- Require improved") !=
            std::string::npos,
        "the items after SRETBL are not one list: " + markdown);
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
// The `CFONT` line inside a verbatim table region styles the row directly
// below it, in that row's own display columns.  Hosted ITPPIBOK 4.3.5.4 (DT
// 19910628074854) serves `| <B>TASK</B> ... |      <B>START</B> <B>DATE</B>`
// from record 158 lines 27-28 (`cfont 7 4 2 58 5 2 64 4 2`).  The style is
// admitted only where the row's cells prove it: a span that does not open
// and close on a drawn word leaves the row plain.
void styled_table_row(bool framed, bool aligned) {
  Builder b(false);
  if (framed) {
    framed_table(b, false, false, w("<BOOK>"), true);
  } else {
    b.line({w("   "), w("Text before.")});
    b.blank();
    b.line({w("SRTBLTBLUNIQ9")});
    b.line({w("   "),
            repeat(repeat({0x250c}, 0x2500, 8, 0x252c), 0x2500, 7, 0x2510)});
    // `   |  A       |  B      |`: `A` is column 6 and `B` column 17.  The
    // unproven span `cfont 5 2 2` starts on a blank.
    if (aligned)
      b.line({w("cfont"), w("6"), w("1"), w("2"), w("17"), w("1"), w("E")});
    else
      b.line({w("cfont"), w("5"), w("2"), w("2")});
    b.line({w("   "), {0x2502}, w(" "), w("A"), w("      "), {0x2502},
            w(" "), w("B"), w("     "), {0x2502}});
    b.line({w("   "),
            repeat(repeat({0x2514}, 0x2500, 8, 0x2534), 0x2500, 7, 0x2518)});
    b.line({w("SRETBL")});
    b.blank();
    b.line({w("   "), w("Text after.")}, w("<BOOK>"));
  }
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "styled table row declined: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto strong_a =
      html.find("<strong class=\"geist-strong\">A</strong>") !=
      std::string::npos;
  if (framed) {
    check(strong_a &&
              html.find("<strong class=\"geist-strong\">B</strong>") !=
                  std::string::npos &&
              html.find("|____________________|") != std::string::npos,
          "framed table row lost its CFONT styling or its frame: " + html);
  } else if (aligned) {
    check(strong_a && html.find("<samp") != std::string::npos &&
              html.find(">B</samp>") != std::string::npos,
          "table row lost its CFONT styling: " + html);
  } else {
    check(html.find("geist-strong") == std::string::npos &&
              html.find("   |  A       |  B      |") != std::string::npos,
          "unproven CFONT span styled a table row: " + html);
  }
  const auto markdown = render_document_markdown(*doc);
  check(markdown.find("Text before") != std::string::npos &&
            (framed || markdown.find("Text after") != std::string::npos),
        "styled table lost the prose around it: " + markdown);
}
void frame_rows_around_prose_decline() {
  Builder b(false);
  framed_table(b, false, true);
  std::string error;
  const auto doc = lower(b, error);
  check(!doc && error.find("frame rows around prose") != std::string::npos,
        "frame rows around prose were not declined: " + error);
}
// A picture-less `SRFIG<id>` form that draws a closed `Note` box between two
// of its paragraphs, well above the table it encloses: SC24-5527-02
// COMMENTS records 976-978 (`SRFIGFIGUNIQ548`, the box on record 977 line 4
// .. record 978 line 0, `SRTBLTBLUNIQ549`).  Hosted BookServer (DT
// 19921218151459) prints the paragraphs, the box between them and the table
// in source order, styles the `cfont` lines' words in bold, and prints no
// `cfont`.  The box is the form's own, not the frame's drawing.
void prose_box_inside_frame() {
  Builder b(false);
  const auto rail = [&](const std::string &text) {
    b.line({w("   "), {0x2502}, w(text), {0x2502}});
  };
  b.line({});
  b.line({w("SRFIGFR2")});
  b.line({w("cfont"), w("3"), w("5"), w("2")});
  b.line({w("   "), w("First words of the form.")});
  b.line({});
  b.line({w("   "), repeat({0x250c}, 0x2500, 20, 0x2510)});
  b.line({w("   "), {0x2502}, w(std::string(19, ' ')), {0x2502}});
  rail("Do not use this.   ");
  b.line({w("   "), repeat({0x2514}, 0x2500, 20, 0x2518)});
  b.line({});
  b.line({w("   "), w("Words after the box.")});
  b.line({});
  b.line({w("SRTBLTBLUNIQ10")});
  b.line({w("   "), w("Format"), w("     "), w("Address")});
  b.line({w("   "), w("INTERNET"), w("   "), w("pub@host")});
  b.line({w("SRETBL")});
  b.line({w("   "), w("Last words.")});
  b.line({w("SREFIG")});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "prose box inside a frame declined: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto first = html.find("First");
  const auto box = html.find("Do not use this.");
  const auto after = html.find("Words after the box.");
  const auto table = html.find("id=\"TBLTBLUNIQ10\"");
  const auto last = html.find("Last words.");
  check(html.find("id=\"FIGFR2\"") < first && first < box && box < after &&
            after < table && table < last && last != std::string::npos &&
            html.find("|____________________|") != std::string::npos,
        "prose box inside a frame is not drawn between its paragraphs: " +
            html);
  check(html.find("cfont") == std::string::npos &&
            html.find("<strong class=\"geist-strong\">First</strong>") !=
                std::string::npos,
        "a frame's cfont line printed or did not style its row: " + html);
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
    // A figure opener that does open its figure is never the box's bare
    // anchor: here the `SREFIG` after the box closes the figure, which the
    // figure family reads.
    Builder b(false);
    anchored_box(b, "SRFIGX1");
    b.line({w("SREFIG")});
    const auto record = run_on_anchor(b, "SRFIGX1");
    check(prose_internal::box_leading_anchors({record}).size() == 1 &&
              prose_internal::box_leading_figure_openers({record}).empty(),
          "a terminated figure opener was read as its box's anchor");
  }
  {
    Builder b(false);
    anchored_box(b, "SRTBLX1");
    std::string error;
    check(!lower_record(run_on_anchor(b, "SRTBLX1"), error),
          "a table opener above a box was read as a bare anchor");
  }
  {
    Builder b(false);
    // An `SRFIG<id>` that opens no figure is a bare anchor wherever it
    // stands; a blank line above a box it names the next row, the box's top
    // rule (its segment's run-on payload is the box's rows, SG24-4575-00
    // 2.1.1).  It is not the box's leader, so `box_leading_anchors` still
    // ignores it (above).
    anchored_box(b, "SRFIGX1", true);
    std::string error;
    check(lower_record(run_on_anchor(b, "SRFIGX1"), error).has_value(),
          "a figure opener a blank line above a box declined: " + error);
  }
}
// An `SRFIG<id>` that opens no figure -- no picture selector, and no
// `SREFIG` anywhere after it in the topic -- alone on the line directly
// above a closed box: SC09-138 3.1.9 record 376 line 18 `SRFIGFIGUNIQ30`
// above the closed RENAME syntax-diagram box (lines 19-26).  Hosted
// BookServer (DT 19910321130500) wraps the top rule in
// `<a name="FIGFIGUNIQ30">`, prints the box verbatim and continues with the
// prose after it.
void unterminated_figure_opener_above_closed_box() {
  Builder b(false);
  anchored_box(b, "SRFIGX1");
  std::string error;
  const auto doc = lower_record(run_on_anchor(b, "SRFIGX1"), error);
  check(doc.has_value(),
        "unterminated figure opener above a closed box declined: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto pre = pre_body(html);
  const auto anchor = html.find("id=\"FIGX1\"");
  check(std::count(pre.begin(), pre.end(), '\n') == 3 &&
            anchor != std::string::npos && anchor > html.find("Before.") &&
            anchor < html.find("<pre") &&
            pre.find("CMD") != std::string::npos &&
            pre.find("|____________|") != std::string::npos,
        "figure-anchored box is not drawn with its anchor on the top rule: " +
            html);
  check(html.find("SRFIG") == std::string::npos &&
            html.find("After.") > html.find("</pre>"),
        "figure-anchored box printed its control or lost its prose: " + html);
  const auto markdown = render_document_markdown(*doc);
  check(drawn_cells(markdown) >= drawn_cells(pre),
        "figure-anchored box Markdown dropped drawn rows: " + markdown);
}
// Two closed boxes drawn one after the other are two regions.  When empty
// display lines or a picture figure stand between them, hosted BookServer
// prints those rows between the boxes (QSYSNEWG 3.1.2.3, DT 19910524085706:
// the `F1=Help` box, three empty lines, `SRFIGFIGUNIQ43` with its picture,
// four empty lines, the `F3=Exit` box; IEAC6MST NOTICES, DT 19920124000100:
// the `Note!` box, one empty line, the `Production of This Book` box).  The
// boxes must not run together into one block, and the picture must stay
// between them rather than move behind the last box.
void closed_box(Builder &b, const std::string &label) {
  b.line({w("   "), repeat({0x250c}, 0x2500, 12, 0x2510)});
  b.line({w("   "), {0x2502}, w(" " + label + std::string(11 - 1 - label.size(), ' ')),
          {0x2502}});
  b.line({w("   "), repeat({0x2514}, 0x2500, 12, 0x2518)});
}
std::size_t occurrences(const std::string &text, const std::string &needle) {
  std::size_t count = 0;
  for (auto at = text.find(needle); at != std::string::npos;
       at = text.find(needle, at + 1))
    ++count;
  return count;
}
void consecutive_boxes_stay_apart() {
  {
    Builder b(false);
    b.line({});
    closed_box(b, "ONE");
    // IEAC6MST NOTICES record 4 line 17: the length byte `00`.
    b.blank();
    closed_box(b, "TWO");
    b.line({});
    b.line({w("   "), w("After.")});
    std::string error;
    const auto doc = lower(b, error);
    check(doc.has_value(), "boxes split by an empty line declined: " + error);
    if (doc) {
      const auto html = render_document_html_fragment(*doc);
      check(occurrences(html, "<pre") == 2 &&
                html.find("ONE") < html.find("</pre>") &&
                html.find("TWO") > html.find("</pre>"),
            "boxes split by an empty line ran into one block: " + html);
    }
  }
  {
    Builder b(false);
    b.line({});
    closed_box(b, "ONE");
    closed_box(b, "TWO");
    std::string error;
    const auto doc = lower(b, error);
    check(doc.has_value(), "adjacent boxes declined: " + error);
    if (doc) {
      const auto html = render_document_html_fragment(*doc);
      check(occurrences(html, "<pre") == 1,
            "boxes with no row between them were split: " + html);
    }
  }
  {
    Builder b(false);
    b.line({});
    closed_box(b, "ONE");
    b.line({});
    b.line({w("SRFIGFIGX1")});
    b.line({w("cselect"), w("3"), w("9"), w("PIC1")});
    b.line({w("   "), w("PICTURE"), w("1")});
    b.line({w("SREFIG")});
    b.line({});
    // As in the book, a `cfont` line styles the next box's label; it also
    // ends the `SREFIG` segment before the box.
    b.line({w("cfont"), w("7"), w("3"), w("P")});
    closed_box(b, "TWO");
    b.line({});
    b.line({w("   "), w("After.")});
    const std::set<std::string> resources{"1"};
    std::string error;
    const auto doc = try_lower_topic_to_document_ir(
        {"1.1", "Example", ":H2", 10, 20, 21}, {b.finish()}, nullptr, &error,
        nullptr, &resources);
    check(doc.has_value(), "picture between boxes declined: " + error);
    if (doc) {
      const auto html = render_document_html_fragment(*doc);
      const auto picture = html.find("<img");
      check(occurrences(html, "<pre") == 2 && picture != std::string::npos &&
                html.find("ONE") < picture && html.find("TWO") > picture,
            "picture between boxes moved behind them: " + html);
    }
  }
}
// A labelled rule standing alone between empty display lines: the
// `End of ...` and `Change for ...` delimiters BookMaster draws around
// marked passages.  SC33-033 4.132 record 813 line 23 is `   ` +
// `U+2514` + attach + `---------------` + `End of General-Use Programming
// Interface` + `---------------` + attach + `U+2518`, with empty lines above
// and below and no `cfont` line; hosted (DT 19930422134757) prints
// `   |--------------- End of General-Use Programming Interface
// ---------------|` in its `<pre>`.  GC23-046 CHANGES.1 record 29 line 10
// is the opening delimiter behind a change bar: ` |  ` + `U+250C` + rule
// text + `U+2510`, served as ` |  ----- Change for Root Cause SPE -----`
// (DT 19910711093248).  Read as prose, the leading corner was taken for a
// marker slot and dropped, or the whole row was reflowed into a paragraph.
// `run_on` puts a picture figure and two empty lines above the rule and lets
// the `SREFIG` segment run on through the rule's corner and the attach after
// it.  FA1PLMM0 15.4 record 647 does this (`SREFIG` on line 3, empty lines
// 4-6, the rule on line 7), and so does the `SRETBL` segment above SC33-033
// 4.132 record 813 line 23 (tokens 254-260; the rule text starts a segment of
// its own at token 261).  The span owns the closer's run-on tokens, so the
// rule's first tokens are no item of the prose stream; the rule is drawn all
// the same.
std::string labelled_rule(bool top, bool change_bar, bool styled = false,
                          bool run_on = false) {
  Builder b(false);
  b.line({});
  b.line({w("   "), w("Before.")});
  b.line({});
  if (run_on) {
    b.line({w("SRFIGFIGX1")});
    b.line({w("cselect"), w("3"), w("9"), w("PIC1")});
    b.line({w("   "), w("PICTURE"), w("1")});
    b.line({w("SREFIG")});
    b.line({});
    b.line({});
  }
  if (styled)
    b.line({w("cfont"), w("8"), w("3"), w("P")});
  std::vector<TokenWords> row;
  if (change_bar)
    row = {w(" "), w("|"), w("  ")};
  else
    row = {w("   ")};
  row.push_back({static_cast<std::uint16_t>(top ? 0x250c : 0x2514)});
  row.push_back({1});
  row.push_back(w("---"));
  row.push_back(w("End"));
  row.push_back(w("of"));
  row.push_back(w("X"));
  row.push_back(w("---"));
  row.push_back({1, static_cast<std::uint16_t>(top ? 0x2510 : 0x2518)});
  b.line(row);
  b.line({});
  b.line({w("   "), w("After.")});
  auto record = b.finish();
  if (run_on) {
    std::size_t corner = 0;
    while (corner < record.tokens.size() &&
           record.tokens[corner] != TokenWords{0x2514} &&
           record.tokens[corner] != TokenWords{0x250c})
      ++corner;
    for (auto &segment : record.control_segments) {
      if (segment.opcode != "SREFIG" || segment.source_tokens.empty())
        continue;
      for (auto token = segment.source_tokens.back() + 1; token <= corner + 1;
           ++token)
        segment.source_tokens.push_back(token);
    }
  }
  const std::set<std::string> resources{"1"};
  std::string error;
  const auto doc = try_lower_topic_to_document_ir(
      {"1.1", "Example", ":H2", 10, 20, 21}, {std::move(record)}, nullptr,
      &error, nullptr, &resources);
  if (!doc)
    return "declined: " + error;
  return render_document_html_fragment(*doc);
}
void labelled_rule_rows() {
  {
    const auto html = labelled_rule(false, false);
    check(pre_body(html) == "   |--- End of X ---|" &&
              html.find("Before.") < html.find("<pre") &&
              html.find("After.") > html.find("</pre>"),
          "standalone bottom labelled rule was not drawn: " + html);
  }
  {
    const auto html = labelled_rule(true, true);
    // The change bar stays in the margin and both top corners print blank.
    check(pre_body(html) == " |    --- End of X --- " &&
              html.find("After.") > html.find("</pre>"),
          "top labelled rule behind a change bar was not drawn: " + html);
  }
  {
    const auto html = labelled_rule(false, false, false, true);
    check(pre_body(html) == "   |--- End of X ---|" &&
              html.find("<img") < html.find("<pre"),
          "labelled rule under a run-on figure closer was not drawn: " +
              html);
  }
  {
    const auto html = labelled_rule(false, false, true);
    check(pre_body(html).find("   |") == 0 &&
              pre_body(html).find("X ---|") != std::string::npos,
          "styled labelled rule was not drawn: " + html);
  }
}
// An inline railroad syntax diagram (SC24-546 3.14 record 238 lines 22-25):
// a text row carrying the main path, whose `U+252C` junctions (hosted prints
// blanks) stand exactly above the `U+2514`/`U+2518` ends of the optional
// item's row below it, with `cfont` lines between.  No row is drawn from box
// words alone.  How the rows below the main path are spelled is the test's
// variable.
//
// `control_length` is the aligned diagram whose main path's length byte the
// book's dictionary spells as a control name: GG24-4302-00 5.3.2.2 record
// 446 line 17 opens with `3C` (60) at 0x5DF16, spelled `cbacklevel`, and the
// flattened splitter read the main path after it as that control's
// operands.  The byte is the row-control slot by its position alone.
enum class Stack {
  aligned,
  shifted,
  gap,
  two_deep,
  open_end,
  change_bar,
  control_length
};
void diagram(Builder &b, Stack stack) {
  b.line({});
  b.line({w("Code it as:")});
  b.line({});
  b.line({w("cfont"), w("7"), w("5"), w("P")});
  // `>>__PARSE__` `U+252C` 6 x `U+2500` `U+252C` `__><`: columns 3..24,
  // junctions at 14 and 21.
  b.line({w("   "), w(">>"), {1}, {0x2500, 0x2500}, {1}, w("PARSE"), {1},
          {0x2500, 0x2500}, {1}, {0x252C}, {1},
          {0x2500, 0x2500, 0x2500, 0x2500, 0x2500, 0x2500}, {1}, {0x252C},
          {1}, {0x2500, 0x2500}, {1}, w("><")},
         stack == Stack::control_length ? w("cbacklevel")
                                        : TokenWords{0x25ba});
  if (stack == Stack::gap) b.line({});
  b.line({w("cfont"), w("16"), w("4"), w("V")});
  const auto indent = stack == Stack::shifted ? w("               ")
                                              : w("              ");
  switch (stack) {
  case Stack::aligned:
  case Stack::control_length:
  case Stack::shifted:
  case Stack::gap:
    b.line({indent, {0x2514, 0x2500}, {1, 'i', 't', 'e', 'm'},
            {1, 0x2500, 0x2518}});
    break;
  case Stack::two_deep:
    b.line({indent, {0x251C, 0x2500}, {1, 'i', 't', 'e', 'm'},
            {1, 0x2500, 0x2524}});
    b.line({indent, {0x2514, 0x2500}, {1, 'n', 'e', 'x', 't'},
            {1, 0x2500, 0x2518}});
    break;
  case Stack::open_end:
    // The stack leaves strokes open downward, but a blank row follows.
    b.line({indent, {0x251C, 0x2500}, {1, 'i', 't', 'e', 'm'},
            {1, 0x2500, 0x2524}});
    b.line({});
    break;
  case Stack::change_bar:
    // A revised prose row: an ASCII change bar in the margin, no stroke.
    b.line({w("|"), w("             "), w("revised prose row")});
    break;
  }
  b.line({w("Then continue.")});
}
void inline_syntax_diagram() {
  for (const auto stack :
       {Stack::aligned, Stack::two_deep, Stack::control_length}) {
    Builder b(false);
    diagram(b, stack);
    std::string error;
    const auto doc = lower(b, error);
    check(doc.has_value(), "inline syntax diagram rejected: " + error);
    if (!doc) continue;
    const auto html = render_document_html_fragment(*doc);
    const std::string main =
        "   &gt;&gt;__<kbd class=\"geist-monospace geist-keyboard\">PARSE"
        "</kbd>__ ______ "
        "__&gt;&lt;\n";
    const std::string item =
        "              |_<var class=\"geist-emphasis geist-variable\">"
        "item</var>_|";
    const std::string expected =
        stack != Stack::two_deep ? main + item
                                 : main + item + "\n              |_next_|";
    check(pre_body(html) == expected,
          "inline syntax diagram was not drawn as one verbatim block: " + html);
    check(html.find("Code it as:") < html.find("<pre") &&
              html.find("Then continue.") > html.find("</pre>"),
          "prose around the syntax diagram moved into it: " + html);
    const auto markdown = render_document_markdown(*doc);
    check(markdown.find("\\>\\>") == std::string::npos &&
              markdown.find("   &gt;&gt;__") != std::string::npos,
          "inline syntax diagram main path reflowed in Markdown: " + markdown);
  }
}
// A vertical arrow drawn into prose: an arrowhead whose shaft is a stroke
// in the same column of the adjacent drawn row.  DREICMST 1.5.5.4 record 162
// lines 29..37 draw a collating-order listing whose rows carry `U+2502` in
// one column, and the last row, `      GEN     `, ends with the two-byte
// dictionary word `U+2193` (token 310, `CB 34` at 0x1B68D) in that column.
// The flattened splitter trims the arrow (its ASCII projection is `?`) from
// the segment it closes, so no segment owns it; hosted DT 19911219125856
// prints every row at its columns, the arrow as the display byte 0x19.
// How the arrow and its shaft are spelled is the test's variable; an arrow
// row ends the topic or stands before a `cfont` line, as in the books.
enum class Shaft { rail_above, ascii_below, shifted, none };
void arrow_rows(Builder &b, Shaft shaft) {
  b.line({w("   "), w("Sorted"), w("as"), w("follows:")});
  b.line({});
  switch (shaft) {
  case Shaft::rail_above:
  case Shaft::shifted:
    // A word after a space run starts one cell further on: `|` at 15.
    b.line({w("      "), w("ABCDE"), w("   "), {0x2502}, w("   "),
            w("Order")});
    b.line({w("      "), w("AB*"), w("     "), {0x2502}});
    b.line({w("      "), w("GEN"),
            shaft == Shaft::shifted ? w("      ") : w("     "), {0x2193}});
    break;
  case Shaft::ascii_below:
    // SC28-1881-05 1.29 record 701 lines 15..21: `TMARGIN value`, `U+2191`
    // under it, then ASCII `|` rows, each after a `cfont 41 1 E` line.
    b.line({w("        "), w("TMARGIN"), w("value")});
    b.line({w("cfont"), w("8"), w("1"), w("E")});
    b.line({w("        "), {0x2191}});
    b.line({w("cfont"), w("8"), w("1"), w("E")});
    b.line({w("        "), w("|")});
    b.line({});
    b.line({w("Then continue.")});
    break;
  case Shaft::none:
    b.line({w("      "), w("GEN"), w("     "), {0x2193}});
    break;
  }
}
void arrow_drawing() {
  for (const auto shaft : {Shaft::rail_above, Shaft::ascii_below}) {
    Builder b(false);
    arrow_rows(b, shaft);
    std::string error;
    const auto doc = lower(b, error);
    check(doc.has_value(), "vertical arrow drawing rejected: " + error);
    if (!doc) continue;
    const auto html = render_document_html_fragment(*doc);
    const std::string expected =
        shaft == Shaft::rail_above
            ? "      ABCDE    |    Order\n      AB*      |\n      GEN      ↓"
            : "        TMARGIN value\n"
              "        <samp class=\"geist-monospace geist-sample\">↑</samp>\n"
              "        <samp class=\"geist-monospace geist-sample\">|</samp>";
    check(pre_body(html) == expected,
          "vertical arrow drawing was not drawn verbatim: " + html);
    check(html.find("Sorted as follows:") < html.find("<pre") &&
              (shaft == Shaft::rail_above ||
               html.find("Then continue.") > html.find("</pre>")),
          "prose around the arrow drawing moved into it: " + html);
  }
}
// An arrow whose shaft is missing proves no drawing: read as prose it would
// run the rows together, so the topic declines, and the reason names the
// decoded word by its code point rather than the ASCII projection's `?`.
void unstroked_arrow_declines_by_name() {
  for (const auto shaft : {Shaft::shifted, Shaft::none}) {
    Builder b(false);
    arrow_rows(b, shaft);
    std::string error;
    const auto doc = lower(b, error);
    check(!doc, "an arrow without a shaft was admitted");
    check(error.find("unclaimed visible token 'U+2193'") != std::string::npos,
          "unclaimed arrow is not named by its code point: " + error);
  }
}
// Two diagrams separated by an empty display line (SC24-546 6.2.7 record
// 707 line 7) stay two blocks: the empty row between them is not lost.
void consecutive_syntax_diagrams() {
  Builder b(false);
  b.line({});
  b.line({w("Numbers:")});
  b.line({});
  for (int part = 0; part < 2; ++part) {
    if (part == 1) b.blank();
    b.line({w("   "), w(part == 0 ? ">>" : ">"), {1}, {0x2500, 0x2500}, {1},
            {0x252C}, {1}, {0x2500, 0x2500, 0x2500}, {1}, {0x252C}, {1},
            {0x2500, 0x2500}, {1}, w(part == 0 ? ">" : "><")});
    // Junctions at columns 7/11 (`>>`) or 6/10 (`>`), corners below them.
    b.line({w(part == 0 ? "       " : "      "), {0x2514}, {1, 'a', 'b', 'c'},
            {1, 0x2518}});
  }
  b.line({w("Then continue.")});
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "consecutive syntax diagrams rejected: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  const auto second = html.find("<pre", html.find("<pre") + 1);
  check(second != std::string::npos &&
            pre_body(html) == "   &gt;&gt;__ ___ __&gt;\n       |abc|" &&
            pre_body(html.substr(second)) ==
                "   &gt;__ ___ __&gt;&lt;\n      |abc|",
        "consecutive syntax diagrams lost the empty row between them: " +
            html);
}
// Strokes that do not meet prove nothing: the main path stays the row
// model's, whatever it makes of it, and is never drawn together with the
// row below.
void inline_syntax_diagram_is_narrow() {
  for (const auto stack : {Stack::shifted, Stack::gap, Stack::open_end,
                           Stack::change_bar}) {
    Builder b(false);
    diagram(b, stack);
    std::string error;
    const auto doc = lower(b, error);
    if (!doc) continue;
    const auto html = render_document_html_fragment(*doc);
    const auto pre = pre_body(html);
    check(pre.find("&gt;&gt;__") == std::string::npos,
          "a main path whose strokes meet nothing was drawn: " + html);
  }
  // An underscore run in prose is no main path: no junction.
  Builder b(false);
  b.line({});
  b.line({w("Fill"), w("in"), w("the"), {0x2500, 0x2500, 0x2500},
          w("field.")});
  b.line({w("              "), {0x2514, 0x2500}, {1, 'i', 't', 'e', 'm'},
          {1, 0x2500, 0x2518}});
  std::string error;
  const auto doc = lower(b, error);
  if (doc) {
    const auto html = render_document_html_fragment(*doc);
    check(pre_body(html).find("Fill") == std::string::npos,
          "prose with a rule run was drawn as a diagram: " + html);
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
  u255a_run_draws_hosted_glyph();
  u25a0_draws_its_character();
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
  unterminated_figure_opener_above_closed_box();
  frame_rows_around_table(false);
  frame_rows_around_table(true);
  frame_rows_around_table(false, {0x251c});
  frame_prose_line_keeps_comma();
  frame_rows_around_prose_decline();
  list_after_table_end_marker();
  list_after_table_end_marker(w("     "));
  arrow_drawing();
  unstroked_arrow_declines_by_name();
  index_line_keeps_paragraph_break();
  index_line_extra_blank_keeps_paragraph();
  styled_table_row(false, true);
  styled_table_row(false, false);
  styled_table_row(true, true);
  prose_box_inside_frame();
  consecutive_boxes_stay_apart();
  labelled_rule_rows();
  inline_syntax_diagram();
  inline_syntax_diagram_is_narrow();
  consecutive_syntax_diagrams();
  bullet();
  unmapped_displayed({w("   "), w("SNA"), {1}, {0xffff}, {1}, w("ps")},
                     "SNA\xC3\xBFps");
  rejected({w("   "), w("SNA"), {0x2666}, w("ps")});
  rejected({w("   "), w("SNA"), {1}, {0x2666}, w("ps")});
  rejected({w("   "), w("SNA "), {1}, {0x2666}, {1}, w("ps")});
  unmapped_displayed({w("   "), {'A', 0xffff, 'B'}}, "A\xC3\xBF" "B");
  return 0;
}
