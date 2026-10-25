// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Drawn glyphs in typed prose: box words and the `U+2666` bullet word that a
// prose topic used to decline as "placeholder" runs.
//
// Box words and the bullet word are decoded display characters, not
// placeholders: hosted BookServer prints them (single-line box words as
// `|`, `_` or a blank, the bullet as `°`; doc/boo-spec/encoding.adoc).  The
// shapes below are the ones the typed prose model now reads:
//
//  * a closed box whose rows carry a revision mark stored as `U+2502` in the
//    row margin (SH45-5547-00 2.6 record 321);
//  * a closed box in a topic whose only directive is a `cz OFF E<tag>` closer
//    that closes nothing (SC41-3512-05 1.3.3.2 records 106-107);
//  * a row-margin revision mark stored as a two-byte token, or as `+`/`#` in
//    the CZ dialect, in front of a list bullet (GC24-5841-01 PREFACE.5
//    record 32);
//  * the bullet word inside a `cz OFF` verbatim region (SC33-1697-02 1.3
//    record 123);
//  * box words inside a generic `cz OFF <tag>` region (SY27-0327-05 4.5
//    record 691, `cz OFF DANGER`).
//
// Everything here is synthetic: the tests build whole topics'
// `DecodedLogicalRecordSource` values by hand and open no book.

#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace geist::detail;

void check(bool ok, const std::string &message) {
  if (!ok) {
    std::cerr << "drawn_glyph_prose_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

bool contains(const std::string &text, const std::string &needle) {
  return text.find(needle) != std::string::npos;
}

// One stored token: its decoded words, its width in bytes and the value the
// record stores (a one-byte token's value is its byte).
struct Tok {
  TokenWords words;
  std::uint8_t width = 2;
  std::uint16_t value = 0;
};

// A two-byte dictionary word.
Tok w(const std::string &text) {
  return {TokenWords(text.begin(), text.end()), 2, 0};
}
// A one-byte token with the given byte.
Tok b(TokenWords words, std::uint16_t value) {
  return {std::move(words), 1, value};
}
// A one-byte blank run of `count` cells (the corpus stores them in one byte).
Tok sp(std::size_t count) {
  return b(TokenWords(count, ' '), static_cast<std::uint16_t>(11 + count));
}
// Box words, stored in one byte as the corpus stores them.
Tok box(TokenWords words) { return b(std::move(words), 0x7A); }
TokenWords run(std::uint16_t first, std::uint16_t middle, std::size_t count,
               std::uint16_t last) {
  TokenWords words{first};
  words.insert(words.end(), count, middle);
  words.push_back(last);
  return words;
}
// The one-byte bullet word (byte 0x03 in the corpus).
Tok bullet() { return b({0x2666}, 3); }
// The bare attach control, as the decoder spells a no-space prefix.
Tok attach() { return b({1}, 0); }

struct Builder {
  DecodedLogicalRecordSource record;
  std::uint16_t encoded = 0x4100;
  std::size_t last_prefix = 0;

  // One display line: a one-byte length byte covering `content`.
  void line(std::vector<Tok> content) {
    std::uint16_t length = 0;
    for (const auto &token : content) length += token.width;
    record.encoded_tokens.push_back({length, 1});
    last_prefix = record.tokens.size();
    record.tokens.push_back(TokenWords{0x25BA});
    for (auto &token : content) {
      record.encoded_tokens.push_back(
          {token.width == 2 ? encoded++ : token.value, token.width});
      record.tokens.push_back(std::move(token.words));
    }
  }
  // The last line's length byte decodes as a blank run, as a corpus length
  // byte whose value is also a blank-run token does (dfhla313 record 791
  // token 190, value 16, decodes as seven blanks).
  void blank_prefix(std::size_t count) {
    record.tokens[last_prefix] = TokenWords(count, ' ');
  }
  void blank() {
    record.encoded_tokens.push_back({0, 1});
    record.tokens.push_back({1});
  }
  explicit Builder(bool cz) {
    line({w("sh1.1")});
    line({w("ctopicn"), w("10")});
    line({w("cparent"), w("1.0")});
    line({w("cforwardlevel")});
    line({w("cbacklevel")});
    line({w("csummary"), w("10"), w("0"), w("10")});
    line({w("chdlevel"), w(":H2")});
    line({w("csourcefn"), w("SAMPLE")});
    line({w("ST"), w("Example")});
    if (cz) line({w("cz"), w("FLOW"), w("P"), w("3"), w("3")});
  }
  DecodedLogicalRecordSource finish() {
    record.logical_record = 20;
    record.assembled = assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = 20;
    std::uint32_t at = 1;
    for (std::size_t t = 0; t < record.tokens.size(); ++t) {
      const auto e = record.encoded_tokens[t];
      const bool prefix = !record.tokens[t].empty() && record.tokens[t][0] < 4;
      record.ir.tokens.push_back(
          {t, e, record.tokens[t], {at, static_cast<std::uint32_t>(at + e.width)},
           prefix, prefix ? record.tokens[t][0] : std::uint16_t{3}});
      at += e.width;
    }
    record.ir.payload_range = {1, at};
    assign_display_line_framing(record.ir);
    record.control_segments = decode_control_segments(
        20, record.assembled, record.encoded_tokens, record.ir.display_lines);
    demote_display_line_owned_controls(record);
    assign_display_line_margin_marks(record);
    return std::move(record);
  }
};

std::optional<DocumentIR> lower(Builder &builder, std::string &error) {
  return try_lower_topic_to_document_ir({"1.1", "Example", ":H2", 10, 20, 21},
                                        {builder.finish()}, nullptr, &error);
}

// The text between the first `<pre` block's tags of a rendered fragment.
std::string pre_body(const std::string &html) {
  const auto open = html.find("<pre");
  if (open == std::string::npos) return {};
  const auto body = html.find('>', open);
  const auto close = html.find("</pre>", body);
  if (body == std::string::npos || close == std::string::npos) return {};
  return html.substr(body + 1, close - body - 1);
}

// `text` without its markup tags.
std::string untagged(const std::string &text) {
  std::string out;
  bool tag = false;
  for (const auto ch : text) {
    if (ch == '<') tag = true;
    if (!tag) out.push_back(ch);
    if (ch == '>') tag = false;
  }
  return out;
}

// A closed box of `width` interior cells at column 3, one text row inside.
void note_box(Builder &builder, const std::string &text, std::size_t width,
              bool marked) {
  const auto margin = [&](std::vector<Tok> row) {
    std::vector<Tok> line;
    if (marked) {
      // The assembler's separator after the mark is the margin's blank cell 2.
      line.push_back(sp(1));
      line.push_back(box({0x2502}));
    } else {
      line.push_back(sp(3));
    }
    line.insert(line.end(), row.begin(), row.end());
    return line;
  };
  builder.line(margin({box(run(0x250C, 0x2500, width, 0x2510))}));
  // A separator stands before a blank run and none after it, so the rail
  // after `text` lands `width + 1` cells after the left rail.
  builder.line(margin({box({0x2502}), w(text),
                       sp(width - text.size() - 2), box({0x2502})}));
  builder.line(margin({box(run(0x2514, 0x2500, width, 0x2518))}));
}

// SH45-5547-00 2.6 record 321 draws its `Note` box behind a revision mark
// stored as `U+2502` in the row margin (line 11 ` |  ___ Note ___`, lines
// 13-14 ` | | If your installation ...  |`); hosted BookServer (DT
// 19950203080455) prints the box verbatim, the marks in the margin.  The
// mark is no left rail: the box's rails stand at column 3.
void marked_box_rows() {
  Builder builder(false);
  // As in the book, the box follows the `ST` line and a `cfont` line with
  // no empty line between them, so only the closed-box geometry can prove
  // it (a blank-delimited run would be read as an open drawing).
  builder.line({w("cfont"), w("8"), w("6"), w("2")});
  note_box(builder, "Inside", 12, true);
  builder.blank();
  builder.line({sp(3), w("After.")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "box behind a U+2502 margin mark declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  const auto pre = untagged(pre_body(html));
  check(contains(pre, " |  ____________ \n | | Inside     |\n | |____________|"),
        "marked box rows were not drawn verbatim: " + html);
}

// SC41-3512-05 1.3.3.2: the closed `Important` box (records 106-107) is
// closed by `cz OFF ELBLBOX 0 0` with no opener and no other directive in the
// topic.  Hosted (DT 19980528085436) prints the box verbatim in the topic's
// own `<pre width="132">` and the prose before it as ordinary rows.
void box_closed_by_unopened_closer(bool trailing_text) {
  Builder builder(false);
  builder.line({sp(3), w("Before.")});
  builder.blank();
  note_box(builder, "Inside", 12, false);
  if (trailing_text) {
    // 1.3.4 record 109 line 28: `cz OFF ELBLBOX 7 7`, then a list item.
    builder.line({w("cz"), w("OFF"), w("ELBLBOX"), w("7"), w("7")});
    builder.line({sp(3), w("After"), w("the"), w("box.")});
  } else {
    builder.line({w("cz"), w("OFF"), w("ELBLBOX"), w("0"), w("0")});
  }
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(),
        "box closed by an unopened closer declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  const auto pre = pre_body(html);
  check(contains(pre, "    ____________ \n   | Inside     |\n   |____________|"),
        "box closed by an unopened closer was not drawn: " + html);
  check(contains(html, "<p class=\"geist-paragraph\">Before.</p>"),
        "prose before the box was not a paragraph: " + html);
  check(!trailing_text ||
            contains(html, "<p class=\"geist-paragraph\">After the box.</p>"),
        "prose after the closer was not a paragraph: " + html);
  check(!contains(html, "ELBLBOX"), "the closer leaked as text: " + html);
}

// A row-margin revision mark is the row's marker whatever width it is
// stored at.  GC24-5841-01 PREFACE.5 record 32 line 18 stores `|` as the
// two-byte token 103 and the bullet as the two-byte token 104 under
// `cz FLOW LI 3 7`; hosted (DT 19990525154647) serves
// `<li> <br> |     Visit our home page at:`.  dfhla313 stores the one-byte
// marks `+` and `#` the same way in front of its CZ list bullets.
void marked_cz_bullet(Tok mark) {
  Builder builder(true);
  builder.line({sp(3), w("Lead"), w("in:")});
  builder.blank();
  builder.line({w("cz"), w("FLOW"), w("UL"), w("3"), w("3")});
  builder.line({w("cz"), w("FLOW"), w("LI"), w("3"), w("7")});
  builder.line({sp(1), mark, b({0x2666}, 3), sp(2), w("Visit"), w("us.")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("EUL"), w("0"), w("0")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "marked CZ bullet declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  const auto mark_text = token_words_to_ascii(mark.words);
  check(contains(html, "data-geist-revision-mark=\"" + mark_text + "\"") &&
            contains(html, "Visit us.</") && !contains(html, "°"),
        "marked CZ bullet is not a revised list item: " + html);
}

// Inside a verbatim region the stored mark is a drawn cell of the row, at
// any width: GC24-5841-01 PREFACE.5 record 32 line 22 stores ` ` + the
// two-byte `|` + a seven-cell run + `http://www.ibm.com/s390/vm` inside
// `cz OFF XMP`, and hosted (DT 19990525154647) serves
// ` |       <samp>http:</samp>...` in its `<pre width="80">`.
void marked_verbatim_row(Tok mark) {
  Builder builder(true);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("XMP")});
  builder.line({sp(1), mark, sp(7), w("http://x")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("EXMP"), w("4"), w("4")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "marked verbatim row declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(pre_body(html) == " |        http://x",
        "the verbatim row lost its stored mark: " + html);
}

// The mark stays a marker when the row is a definition term styled by a
// `cfont` span: SC41-5401-00 2.1.2.21 record 269 line 23 stores ` ` + the
// two-byte `|` (token 161) + `*` + attach + `YES` under `cz FLOW DT 3 13`
// and `cfont 3 4 2`; hosted (DT 19970421115044) serves
// `<dt><br> | <B>*YES</B>     <dd>(Default) The AS/400 ...`.  The mark is
// the entry's revision mark, never term text; the term is then wholly
// highlighted and keeps its `<B>` as hosted does (the Markdown term syntax
// is already bold, so Markdown prints the wholly emphasised term plain).
void marked_styled_term(Tok mark) {
  Builder builder(true);
  builder.line({w("cz"), w("FLOW"), w("DL"), w("3"), w("3")});
  builder.line({w("cz"), w("FLOW"), w("DT"), w("3"), w("13")});
  builder.line({w("cfont"), w("3"), w("4"), w("2")});
  builder.line({sp(1), mark, b({'*'}, 38), attach(), w("YES"), sp(5),
                w("Default"), w("value.")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("EDL"), w("0"), w("0")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "marked styled term declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(contains(html, "<dt class=\"geist-definition-term\"><span "
                       "class=\"geist-revised\" data-geist-revision-mark="
                       "\"|\"><strong class=\"geist-strong\">*YES</strong>"
                       "</span></dt>"),
        "the marked term is not a revised bold term: " + html);
  const auto markdown = render_document_markdown(*doc);
  check(contains(markdown, "- **\\*YES:** ") && !contains(markdown, "****"),
        "the wholly bold term is not a plain bold Markdown label: " +
            markdown);
}

// SC33-1697-02 1.3 record 123: inside `cz OFF LBLBOX` the rows of the
// `Examples in this section` box store `U+2502`, the bullet word, a gap and
// the text; hosted (DT 19990308125631) serves
// `   | °   The examples do not show ...   |` in its `<pre>`.
void bullet_in_verbatim_region() {
  Builder builder(true);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("LBLBOX")});
  builder.line({sp(3), box(run(0x250C, 0x2500, 12, 0x2510))});
  builder.line({sp(3), box({0x2502}), bullet(), sp(2), w("Item"), sp(3),
                box({0x2502})});
  builder.line({sp(3), box(run(0x2514, 0x2500, 12, 0x2518))});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("ELBLBOX"), w("0"), w("0")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "bullet inside a verbatim region declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(contains(pre_body(html), "   | °   Item    |"),
        "the bullet was not drawn in its region row: " + html);
}

// A verbatim pair nested in an open verbatim region leaves the outer region
// open after its closer: acca2000 3.2.1 nests `cz OFF XMP` .. `cz OFF EXMP
// 4 4` inside `cz OFF SCREEN` (record 66 line 16 .. record 67 line 20) and
// draws the screen's last rails and bottom rule after `EXMP`.
void box_rows_after_nested_verbatim_pair() {
  Builder builder(true);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("SCREEN")});
  builder.line({sp(3), box(run(0x250C, 0x2500, 12, 0x2510))});
  builder.line({w("cz"), w("OFF"), w("XMP")});
  builder.line({sp(3), box({0x2502}), w("Inside"), sp(4), box({0x2502})});
  builder.line({w("cz"), w("OFF"), w("EXMP"), w("4"), w("4")});
  builder.line({sp(3), box({0x2502}), sp(12), box({0x2502})});
  builder.line({sp(3), box(run(0x2514, 0x2500, 12, 0x2518))});
  builder.line({w("cz"), w("OFF"), w("ESCREEN"), w("0"), w("0")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(),
        "box rows after a nested verbatim pair declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(contains(html, "   | Inside     |\n   |             |\n   |____________|"),
        "the outer region's rows after the nested pair were not drawn: " +
            html);
}

// SY27-0327-05 4.5 record 691: `cz OFF DANGER` ... `cz OFF EDANGER 0 0`
// around a closed box.  The reader serves the generic `CZ OFF` region as
// `<pre width="80">` with every row at its columns (hosted DT
// 19961016091447).
void box_in_generic_region() {
  Builder builder(true);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("DANGER")});
  builder.line({sp(3), box(run(0x250C, 0x2500, 12, 0x2510))});
  builder.line({sp(3), box({0x2502}), w("Inside"), sp(4), box({0x2502})});
  builder.line({sp(3), box(run(0x2514, 0x2500, 12, 0x2518))});
  builder.line({w("cz"), w("OFF"), w("EDANGER"), w("0"), w("0")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "box in a generic cz OFF region declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(contains(pre_body(html), "   | Inside     |\n   |____________|"),
        "the generic region's box was not drawn: " + html);
}

// A generic region is a verbatim region like `XMP`, and it keeps the
// `CFONT` highlighting and the `CSELECT` links of its rows: hosted serves
// SC41-4153-00 FRONT_1.1.1 (DT 19950706142729) `cz OFF DANGER` rows as
// `   | <strong>An</strong> <strong>electrical</strong> ...` inside
// `<pre width="80">`, and SY27-0327-05 2.1.8 (DT 19961016091447) its
// `cz OFF MAP` step diagram with the `Yes` branches linked and its
// `STPSTPUNIQ...` anchors on the rows they open.  The block is no degraded
// fallback.
void styled_generic_region(bool link) {
  Builder builder(true);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w(link ? "MAP" : "DANGER")});
  builder.line({sp(3), box(run(0x250C, 0x2500, 12, 0x2510))});
  if (link)
    builder.line({w("cselect"), w("5"), w("6"), w("TARGET")});
  else
    builder.line({w("cfont"), w("5"), w("6"), w("2")});
  builder.line({sp(3), box({0x2502}), w("Inside"), sp(4), box({0x2502})});
  builder.line({sp(3), box(run(0x2514, 0x2500, 12, 0x2518))});
  builder.line({w("cz"), w("OFF"), w(link ? "EMAP" : "EDANGER"), w("0"),
                w("0")});
  builder.line({w("SRTARGET")});
  builder.line({sp(3), w("Target.")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "a styled generic region declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  const auto pre = pre_body(html);
  // The closer prints nothing and leaves the region open: the row it owns
  // is a row of the region, and the anchor before that row opens it.
  check(untagged(pre) ==
            "    ____________ \n   | Inside     |\n   |____________|\n   Target.",
        "the styled generic region's rows changed: " + html);
  check(contains(pre, "id=\"TARGET\"></span>   Target."),
        "the anchor does not open its region row: " + html);
  check(link ? contains(pre, "<a ") && contains(pre, "Inside</a>")
             : contains(pre, "<strong") && contains(pre, "Inside</strong>"),
        std::string("the generic region lost its ") +
            (link ? "link" : "highlighting") + ": " + html);
  check(!contains(html, "data-geist-degraded"),
        "the generic region is still a degraded block: " + html);
}

// A one-byte revision mark in the margin of a generic region's row is one of
// the region's drawn cells: GC28-1215-01 4.11.1 record 1068 line 42 stores a
// blank, `|` (token 254) and `ASFP033E ...` inside `cz OFF DIRECT`, and
// hosted (DT 19950727111944) serves ` | ASFP033E          2           12`.
void marked_generic_region_row() {
  Builder builder(true);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("DIRECT")});
  builder.line({sp(3), w("ASFP016I"), sp(10), w("2")});
  builder.line({sp(1), b({'|'}, 0x7C), w("ASFP033E"), sp(10), w("2")});
  builder.line({w("cz"), w("OFF"), w("EDIRECT"), w("0"), w("0")});
  builder.line({w("cz"), w("FLOW"), w("P"), w("3"), w("3")});
  builder.line({sp(3), w("After.")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "a marked generic region row declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(contains(untagged(pre_body(html)),
                 "   ASFP016I           2\n | ASFP033E           2"),
        "the generic region row lost its stored mark: " + html);
}

// A drawn box keeps the `CFONT` highlighting and the `CSELECT` links of its
// rows, with or without an `SRTBL` envelope around it: ICHA6MST 5.0 record
// 818 opens `SRTBLTBLUNIQ66` (line 8) above a closed box whose first row
// carries `cfont 43 8 2 52 8 2 61 2 2 64 5 2` and whose second row
// `cselect 62 3 SPTVMSP1I` (line 12); hosted (DT 19910801172557) serves
// `<B>Password</B> ...` and `<a href="...#SPTVMSP1I">5.0</a>` in its `<pre>`.
void styled_box_rows(bool envelope) {
  Builder builder(false);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  if (envelope) builder.line({w("SRTBLTBLUNIQ66")});
  builder.line({sp(3), box(run(0x250C, 0x2500, 16, 0x2510))});
  builder.line({w("cfont"), w("5"), w("4"), w("2")});
  builder.line({sp(3), box({0x2502}), w("Bold"), w("text"), sp(6),
                box({0x2502})});
  builder.line({w("cselect"), w("5"), w("4"), w("SPTTARGET")});
  builder.line({sp(3), box({0x2502}), w("Link"), w("text"), sp(6),
                box({0x2502})});
  builder.line({sp(3), box(run(0x2514, 0x2500, 16, 0x2518))});
  if (envelope) builder.line({w("SRETBL")});
  builder.blank();
  builder.line({w("SRSPTTARGET")});
  builder.line({sp(3), w("Target.")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "a styled box declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(contains(html, ">Bold</strong>") || contains(html, ">Bold</b>"),
        std::string("the box row lost its highlighting") +
            (envelope ? " (envelope)" : "") + ": " + html);
  check(contains(html, "href=\"#SPTTARGET\">Link</a>"),
        std::string("the box row lost its link") +
            (envelope ? " (envelope)" : "") + ": " + html);
}

// A generic region with no closer of its own ends at the next `CZ FLOW`:
// the reader's generic `CZ OFF` opens `<pre width="80">` and only a
// `CZ FLOW` closes it.  SC33-0824-02 2.1.1.1 record 117 opens
// `cz OFF CGRAPHIC` (line 17), draws the function-shipping diagram (lines
// 18-31) and goes on with `cz FLOW P 3 3` (line 33); hosted (DT
// 19950807110137) prints the diagram rows in `<pre width="80">` and the
// paragraph after `</pre>`.
void generic_region_ended_by_flow() {
  Builder builder(true);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("CGRAPHIC")});
  builder.line({sp(3), box(run(0x250C, 0x2500, 12, 0x2510))});
  builder.line({sp(3), box({0x2502}), w("Inside"), sp(4), box({0x2502})});
  builder.line({sp(3), box(run(0x2514, 0x2500, 12, 0x2518))});
  builder.blank();
  builder.line({w("cz"), w("FLOW"), w("P"), w("3"), w("3")});
  builder.line({sp(3), w("After"), w("the"), w("diagram.")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(),
        "a generic region ended by cz FLOW declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(untagged(pre_body(html)) ==
            "    ____________ \n   | Inside     |\n   |____________|",
        "the flow-ended region's rows were not drawn: " + html);
  check(contains(html, "<p class=\"geist-paragraph\">After the diagram.</p>"),
        "the paragraph after the region is not typed: " + html);
  check(!contains(html, "data-geist-degraded"),
        "the flow-ended region is a degraded block: " + html);
}

// A generic region that neither its own closer nor a `CZ FLOW` ends still
// declines: nothing proves where its rows stop.
void unended_generic_region_declines() {
  Builder builder(true);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("CGRAPHIC")});
  builder.line({sp(3), box(run(0x250C, 0x2500, 12, 0x2510))});
  builder.line({sp(3), box({0x2502}), w("Inside"), sp(4), box({0x2502})});
  builder.line({sp(3), box(run(0x2514, 0x2500, 12, 0x2518))});
  builder.line({w("cz"), w("OFF"), w("EGIZMO"), w("0"), w("0")});
  std::string error;
  const auto doc = lower(builder, error);
  check(!doc.has_value(),
        "a generic region closed by another tag's closer was typed");
}

// An anchor whose next display line is a body control line (`c.cp 6`) that
// draws nothing precedes the next block that draws text.  dfhla313 1.2.3.1.3
// record 791 stores `SRHDRDFHA314` (line 19), two empty lines, `c.cp 6`
// (line 22), the index lines and then `   CICS task manager parameters: ...`
// (line 28).
void anchor_before_body_control_line(bool efig_closer) {
  Builder builder(false);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({sp(1), b({'|'}, 0x7C), sp(1), w("2."), sp(2), w("Before"),
                w("the")});
  builder.line({sp(1), b({'|'}, 0x7C), sp(5), w("anchor.")});
  builder.line({w("SRHDRANCHOR")});
  builder.blank();
  builder.line({w("c.cp"), w("6")});
  builder.blank();
  builder.line({w("SI"), w("task"), w("manager"), w("parameters")});
  builder.blank_prefix(7);
  builder.line({sp(3), w("Target"), w("text.")});
  builder.blank();
  builder.line({sp(3), w("Last.")});
  if (efig_closer)
    builder.line({w("cz"), w("OFF"), w("EFIG"), w("0"), w("0")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "anchor before a body control line declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  const auto anchor = html.find("id=\"HDRANCHOR\"");
  const auto target = html.find("Target text.");
  const auto before = html.find("anchor.");
  check(anchor != std::string::npos && target != std::string::npos &&
            before < anchor && anchor < target,
        std::string("anchor did not precede the next text block") +
            (efig_closer ? " (EFIG closer)" : "") + ": " + html);
}

// SC41-3512-05 2.2.2: a topic whose only directives are unopened
// `cz OFF EFIG` closers is read like one closed by `cz OFF ELBLBOX`: its box
// is drawn verbatim (hosted DT 19980528085436 prints every row in the topic's
// own `<pre width="132">`).
void box_closed_by_unopened_figure_closer() {
  Builder builder(false);
  builder.line({sp(3), w("Before.")});
  builder.blank();
  note_box(builder, "Inside", 12, false);
  builder.line({w("cz"), w("OFF"), w("EFIG"), w("0"), w("0")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(),
        "box closed by an unopened figure closer declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(contains(pre_body(html),
                 "    ____________ \n   | Inside     |\n   |____________|"),
        "box closed by an unopened figure closer was not drawn: " + html);
  check(!contains(html, "EFIG"), "the figure closer leaked as text: " + html);
}

// An `SRFIG`/`SRTBL` envelope whose openers stand directly above a closed
// box's top rule and whose closers stand inside the box, before its bottom
// rule, is the box's own markup.  SH09-8098-02 3.3.3.4.1 record 191 stores
// `SRSPTOPEN`, `SRFIGTBLUNIQ17` and `SRTBLTBLUNIQ17` alone on lines 14-16,
// the top rule on line 17, the rows on lines 19-22, `SRETBL` and `SREFIG`
// alone on lines 23-24 and the bottom rule on line 25; hosted (DT
// 19930430114900) wraps the top rule in the three anchors, prints the five
// box rows verbatim and nothing for the closers.
void envelope_straddling_box(bool figure) {
  Builder builder(false);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({w("SRSPTOPEN")});
  if (figure) builder.line({w("SRFIGTBLUNIQ17")});
  builder.line({w("SRTBLTBLUNIQ17")});
  builder.line({sp(3), box(run(0x250C, 0x2500, 12, 0x2510))});
  builder.line({sp(3), box({0x2502}), w("Format:"), sp(3), box({0x2502})});
  // The envelope's rows may draw rules of their own (the syntax diagram's
  // main path in the book).
  builder.line({sp(3), box(run(0x2502, 0x2500, 12, 0x2502))});
  builder.line({w("SRETBL")});
  if (figure) builder.line({w("SREFIG")});
  builder.line({sp(3), box(run(0x2514, 0x2500, 12, 0x2518))});
  builder.blank();
  builder.line({sp(3), w("After.")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), std::string("envelope straddling a box declined") +
                             (figure ? " (figure)" : "") + ": " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(untagged(pre_body(html)) ==
            "    ____________ \n   | Format:    |\n   |____________|"
            "\n   |____________|",
        "the straddled box was not drawn: " + html);
  check(contains(html, "id=\"TBLTBLUNIQ17\"") &&
            contains(html, "id=\"SPTOPEN\"") &&
            (!figure || contains(html, "id=\"FIGTBLUNIQ17\"")),
        "the box lost its envelope anchors: " + html);
  check(!contains(html, "<table") && !contains(html, "ETBL") &&
            !contains(html, "EFIG"),
        "the envelope was read as a table or its closers leaked: " + html);
  check(contains(html, "<p class=\"geist-paragraph\">After.</p>"),
        "prose after the box is not a paragraph: " + html);
}

// An `SRTBL` envelope drawn wholly inside a closed box is the box's markup
// even when its rows draw a grid of their own, as long as the box is no
// figure frame.  SG24-2599-00 6.5.1.1 records 686-688 draw the `Where Did My
// PIDTs Go?` box: `SRTBLPIDTTBL` alone on record 687 line 4, the `Table 3`
// grid on lines 7-23 and record 688 lines 0-18, `SRETBL` alone on record 688
// line 19, then three more side rows and the bottom rule.  Hosted (DT
// 19960424163436) prints every row in its `<pre>`, the grid included, with
// `<a name="TBLPIDTTBL">` on the row after the opener and no `<table>`.
void grid_inside_box() {
  Builder builder(false);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({sp(3), box(run(0x250C, 0x2500, 12, 0x2510))});
  builder.line({sp(3), box({0x2502}), w("Inside"), sp(4), box({0x2502})});
  builder.line({w("SRTBLGRID")});
  builder.line({sp(3), box({0x2502}), sp(11), box({0x2502})});
  // The grid: an inner box drawn between the outer rails.
  const auto grid_row = [](std::uint16_t left, std::uint16_t right) {
    TokenWords words{0x2502, left};
    words.insert(words.end(), 10, 0x2500);
    words.insert(words.end(), {right, 0x2502});
    return box(std::move(words));
  };
  builder.line({sp(3), grid_row(0x250C, 0x2510)});
  builder.line({sp(3), grid_row(0x2514, 0x2518)});
  builder.line({w("SRETBL")});
  builder.line({sp(3), box({0x2502}), sp(11), box({0x2502})});
  builder.line({sp(3), box(run(0x2514, 0x2500, 12, 0x2518))});
  builder.blank();
  builder.line({sp(3), w("After.")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "grid inside a box declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(contains(untagged(pre_body(html)),
                 "   | Inside     |\n   |            |\n"),
        "the box around the grid was not drawn: " + html);
  check(contains(html, "id=\"TBLGRID\"") && !contains(html, "<table") &&
            !contains(html, "ETBL"),
        "the inner grid was read as a table: " + html);
}

// In a CZ topic the reader is inside a `<pre>` before the first directive
// (`<pre width="132">` after the heading) and after a named region closer
// (`cz OFF ETABLE`, `EFIG`, `ELBLBOX`, `EDL` print `</pre>` and open
// `<pre width="132">`) until the next `cz FLOW`, so a closed box drawn there
// is printed verbatim.  dfhla313 1.1.3.2 record 154 draws its `APAR
// PQ12071` box on lines 11-15, before its first directive (`cz OFF ELBLBOX
// 0 0`, line 18); dfhla313 1.1.3.1 record 153 draws its `Important` box on
// lines 10-15, right after `cz OFF ETABLE 0 0` (line 8).
void box_in_cz_pre_zone(bool after_closer) {
  Builder builder(false);
  if (after_closer) {
    builder.line({w("cz"), w("FLOW"), w("DL"), w("3"), w("3")});
    builder.line({w("cz"), w("FLOW"), w("DT"), w("3"), w("13")});
    builder.line({sp(3), w("Term"), sp(6), w("Its"), w("definition.")});
    builder.blank();
    builder.line({w("cz"), w("OFF"), w("EDL"), w("0"), w("0")});
  }
  builder.line({w("cfont"), w("8"), w("6"), w("2")});
  note_box(builder, "Inside", 12, false);
  builder.blank();
  if (!after_closer)
    builder.line({w("cz"), w("OFF"), w("ELBLBOX"), w("0"), w("0")});
  builder.line({w("cz"), w("FLOW"), w("P"), w("3"), w("3")});
  builder.line({sp(3), w("After.")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), std::string("box in a CZ pre zone declined") +
                             (after_closer ? " (after EDL)" : " (leading)") +
                             ": " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(untagged(pre_body(html)) ==
            "    ____________ \n   | Inside     |\n   |____________|",
        "the box in a CZ pre zone was not drawn: " + html);
  check(contains(html, "<p class=\"geist-paragraph\">After.</p>"),
        "the paragraph after the box is not typed: " + html);
  check(!after_closer || contains(html, "Its definition."),
        "the definition list before the closer was lost: " + html);
}

// A closed box drawn in flowed CZ text -- after `cz FLOW P`, where the reader
// has closed its `<pre>` -- has no proven reading and still declines.
void box_in_cz_flow_declines() {
  Builder builder(true);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  note_box(builder, "Inside", 12, false);
  builder.blank();
  builder.line({sp(3), w("After.")});
  std::string error;
  check(!lower(builder, error).has_value(),
        "a box in flowed CZ text was typed");
}

// The broken bar U+00A6 is a row-margin revision mark like `|`: SC34-5559-00
// 4.1.3 record 990 line 18 stores a blank, `¦` (the two-byte token at
// 0x61C82, U+00A6), the bullet word (`03` at 0x61C84), a gap and `CL
// commands: COMMIT and ROLLBACK` under `cz FLOW LI 3 7`; hosted (DT
// 20000125132847) serves `<li> <br> ¦     CL commands: COMMIT and ROLLBACK`.
// A paragraph row keeps it out of its text as well (SC34-5390-01 CHANGES,
// hosted DT 20000119152808 `<p><br> ¦ This section describes ...`).
void broken_bar_margin_mark() {
  const Tok mark{TokenWords{0x00A6}, 2, 0};
  Builder builder(true);
  builder.line({sp(1), mark, w("Lead"), w("in:")});
  builder.blank();
  builder.line({w("cz"), w("FLOW"), w("UL"), w("3"), w("3")});
  builder.line({w("cz"), w("FLOW"), w("LI"), w("3"), w("7")});
  builder.line({sp(1), mark, b({0x2666}, 3), sp(2), w("Visit"), w("us.")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("EUL"), w("0"), w("0")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "rows behind a broken-bar mark declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(contains(html, "data-geist-revision-mark=\"\xC2\xA6\"") &&
            contains(html, "Visit us.</") && !contains(html, "°") &&
            !contains(html, "\xC2\xA6 Lead"),
        "the broken bar was not read as the rows' revision mark: " + html);
}

} // namespace

// A bare `SRGLS` makes the reader look ahead for a glossary letter box and
// print it as a letter divider, not as box art: GA32-0100-05 GLOSSARY
// record 1227 lines 20-24 are `SRGLS`, `       __________ `,
// `cfont 8 8 2`, `      | Numerics |`, `      |__________|`, and hosted
// (DT 19960213104337) serves `<!-- glossary --><hr><br><center><b><font
// size="+4">Numerics</font></b></center><hr ...>` and no box row; 20 of 20
// sampled glossary books do the same.  The divider is the letter heading
// the glossary catalog family already writes (`## C`).
void glossary_letter_box(bool cz) {
  Builder builder(cz);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({w("SRGLS")});
  // As GA32-0100-05 stores it: corner, attach, rule run, attach, corner.
  builder.line({sp(6), box({0x250C}), attach(), box(TokenWords(3, 0x2500)),
                attach(), box({0x2510})});
  builder.line({w("cfont"), w("8"), w("1"), w("2")});
  builder.line({sp(6), box({0x2502}), w("A"), box({0x2502})});
  builder.line({sp(6), box({0x2514}), attach(), box(TokenWords(3, 0x2500)),
                attach(), box({0x2518})});
  builder.blank();
  builder.line({w("SRGLS"), w("access")});
  builder.line({sp(3), w("access."), sp(2), w("Entry.")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "glossary letter box declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(contains(html, "<h2") && contains(html, ">A</strong></h2>") &&
            !contains(html, "<pre") && !contains(html, "___"),
        "the letter box is not a letter heading: " + html);
  check(contains(html, "id=\"GLS access\""),
        "the entry anchor after the letter box was lost: " + html);
}

// A full-width rule -- more than 71 `U+2500` cells from column 3 -- is not
// drawn in a verbatim region: the reader empties the line and the page shows
// `<hr>` there.  SC09-2123-00 2.2.1.2 (hosted DT 19951101111056) frames its
// `cz OFF SYNTAX` railroad diagram with two such rows and serves
// `<pre width="80"><hr>` ... `<hr>`, the diagram rows between them.  Only a
// `cz OFF TABLE` region keeps the row.
void full_width_rule_in_verbatim_region() {
  Builder builder(true);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("SYNTAX")});
  builder.line({sp(3), box(TokenWords(74, 0x2500))});
  builder.line({sp(3), w(">>__name__><")});
  builder.line({sp(3), box(TokenWords(74, 0x2500))});
  builder.line({w("cz"), w("OFF"), w("ESYNTAX"), w("0"), w("0")});
  builder.line({w("cz"), w("FLOW"), w("P"), w("3"), w("3")});
  builder.line({sp(3), w("After.")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "rule-framed syntax region declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(!contains(html, "__________") && contains(html, "&gt;&gt;__name__&gt;&lt;") &&
            contains(html, "<hr"),
        "the full-width rules are drawn instead of breaking: " + html);
}

// The rows after a named CZ closer -- `cz OFF ETABLE`, `EFIG`, `ELBLBOX` or
// `EDL` -- are drawn as stored: the reader prints `</pre>` and opens
// `<pre width="132">` there, which the next `cz FLOW` closes.
// SC41-3512-05 APPENDIX1.7.1 (DT 19980528085436) follows an unopened
// `cz OFF ELBLBOX 0 0` with prose and `__ 1.` rows, all served verbatim in
// `<pre width="132"><!-- * -->`; 145 sampled books serve the rows after a
// named closer inside that `<pre>`.
void rows_after_named_closer(bool list) {
  Builder builder(true);
  builder.line({sp(3), w("Lead.")});
  if (list) {
    builder.line({w("cz"), w("FLOW"), w("DL"), w("3"), w("3")});
    builder.line({w("cz"), w("FLOW"), w("DT"), w("3"), w("13")});
    builder.line({sp(3), w("Term"), sp(6), w("Definition.")});
    builder.line({w("cz"), w("OFF"), w("EDL"), w("0"), w("0")});
  } else {
    builder.line({w("cz"), w("OFF"), w("ELBLBOX"), w("0"), w("0")});
  }
  builder.line({sp(3), w("Rows"), w("after"), w("the"), w("closer:")});
  builder.line({sp(3), bullet(), sp(3), w("first"), w("row")});
  builder.line({w("cz"), w("FLOW"), w("P"), w("3"), w("3")});
  builder.line({sp(3), w("Flowed"), w("again.")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "rows after a named closer declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(contains(html, "<pre class=\"geist-preformatted\">   Rows after the "
                       "closer:\n   \u00b0    first row</pre>") &&
            contains(html, "<p class=\"geist-paragraph\">Flowed again.</p>"),
        "the rows after the named closer are not drawn as stored: " + html);
}

// A `frame=rule` drawn figure: its top and bottom frame rows are full-width
// rules, which the reader empties and the page shows as `<hr>`.
// SC26-4817-04 1.3.12.2 (DT 19950325182347) opens `SRFIGFIGUNIQ22` with a
// 92-cell rule from column 3 and serves `<a name="FIGFIGUNIQ22"><hr>`, the
// numbered steps, then `<hr>` before the following prose; 51 of 52 sampled
// books serve one `<hr>` per such rule.  A shorter rule stays spacing.
void figure_frame_rules(bool full) {
  Builder builder(false);
  builder.line({sp(3), w("Lead.")});
  builder.line({w("SRFIGRULED")});
  const std::size_t cells = full ? 74 : 60;
  builder.line({sp(3), box(TokenWords(cells, 0x2500))});
  builder.blank();
  builder.line({sp(3), w("first"), w("drawn"), w("row")});
  builder.line({sp(3), w("second"), w("drawn"), w("row")});
  builder.blank();
  builder.line({sp(3), box(TokenWords(cells, 0x2500))});
  builder.line({sp(3), w("Figure"), w("1."), w("Ruled")});
  builder.line({w("SREFIG")});
  builder.line({sp(3), w("After.")});
  std::string error;
  const auto doc = lower(builder, error);
  check(doc.has_value(), "rule-framed figure declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  const auto anchor = html.find("id=\"FIGRULED\"");
  const auto top = html.find("<hr", anchor);
  const auto body = html.find("first drawn row");
  const auto bottom = html.find("<hr", body);
  const auto caption = html.find("Figure 1. Ruled");
  if (full)
    check(anchor != std::string::npos && top != std::string::npos &&
              body != std::string::npos && bottom != std::string::npos &&
              caption != std::string::npos && anchor < top && top < body &&
              body < bottom && bottom < caption &&
              !contains(html, "_____"),
          "the full-width frame rules are not the page's rules: " + html);
  else {
    // A shorter rule is an ordinary drawn row: the reader empties only rows
    // of more than 71 `U+2500` from column 3, and hosted prints the short
    // ones in the figure's `<pre>` (GC31-7554-01 6.6.2, DT 19930625133538:
    // `<a name="FIGC06F6">   ____...</a>` with 45 `_`, then the inner rules
    // and the closing one).
    const std::string rule = "   " + std::string(cells, '_');
    const auto pre = pre_body(html);
    // The caption under it is the drawing's next row (hosted ARIP1VM
    // 6.1.6), not a paragraph.
    check(!contains(html, "<hr") && body != std::string::npos &&
              pre.rfind(rule + "\n", 0) == 0 &&
              contains(pre, "\n" + rule + "\n   Figure 1. Ruled") &&
              !contains(html, "<em"),
          "a short frame rule is not a drawn row of the figure: " + html);
  }
}

// SG24-4478-00 12.1.4 record 1203: a foil's closed box holds nothing but a
// bare `cselect 35 10 PIC13` (no SRFIG, no SRTBL) over `PICTURE 13` between
// its rails.  Hosted (DT 19951114165715) serves the box verbatim in its
// `<pre>`, the `<img>` at the selector column and the selected columns
// blank, exactly as a framed figure.  The anchorless picture used to claim
// the placement row out of the box and the topic fell to best-effort.
void bare_picture_in_closed_box() {
  Builder builder(false);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({sp(3), box(run(0x250C, 0x2500, 14, 0x2510))});
  builder.line({sp(3), box({0x2502}), sp(13), box({0x2502})});
  builder.line({w("cselect"), w("5"), w("9"), w("PIC1")});
  builder.line({sp(3), box({0x2502}), w("PICTURE"), w("1"), sp(3),
                box({0x2502})});
  builder.line({sp(3), box(run(0x2514, 0x2500, 14, 0x2518))});
  builder.blank();
  builder.line({sp(3), w("After.")});
  std::string error;
  const std::set<std::string> resources{"1"};
  const auto doc = try_lower_topic_to_document_ir(
      {"1.1", "Example", ":H2", 10, 20, 21}, {builder.finish()}, nullptr,
      &error, nullptr, &resources);
  check(doc.has_value(), "bare picture in a closed box declined: " + error);
  if (!doc) return;
  std::string verify_error;
  check(verify_document_ir(*doc, &verify_error),
        "bare picture box document invalid: " + verify_error);
  const auto html = render_document_html_fragment(*doc);
  check(!contains(html, "data-geist-degraded"),
        "bare picture box is degraded: " + html);
  const auto body = pre_body(html);
  check(contains(body, "   |              |\n   | <img") &&
            contains(body,
                     "alt=\"PICTURE 1\">             |\n   |______________|"),
        "the box was not drawn around its picture: " + html);
  check(!contains(html, "<figure"),
        "bare picture in a box still served as a figure: " + html);
}

// A revised figure caption keeps its stored mark.  CNMYB0MS 1.1.4 stores
// ` | Figure 1. Primary Components ...` under a picture (hosted DT
// 19911015181502 prints ` | Figure 1. ...`), ARIP1VM 6.1.6 ` | Figure 41.
// Predicate Hierarchy` under a drawn figure (DT 19911002123224); 419 such
// caption rows in 38 of 94 sampled books.  The caption lost the mark.
void marked_figure_caption(bool picture) {
  Builder builder(false);
  builder.line({sp(3), w("Lead.")});
  builder.line({w("SRFIGMARKED")});
  if (picture) {
    builder.line({w("cselect"), w("3"), w("9"), w("PIC1")});
    builder.line({sp(3), w("PICTURE"), w("1")});
  } else {
    builder.line({sp(3), box(run(0x250C, 0x2500, 12, 0x2510))});
    builder.line({sp(3), box({0x2502}), w("Inside"), sp(4), box({0x2502})});
    builder.line({sp(3), box(run(0x2514, 0x2500, 12, 0x2518))});
  }
  builder.blank();
  builder.line({sp(1), w("|"), w("Figure"), w("1."), w("Marked")});
  builder.line({w("SREFIG")});
  builder.line({sp(3), w("After.")});
  std::string error;
  const std::set<std::string> resources{"1"};
  const auto doc = try_lower_topic_to_document_ir(
      {"1.1", "Example", ":H2", 10, 20, 21}, {builder.finish()}, nullptr,
      &error, nullptr, &resources);
  check(doc.has_value(), "figure with a marked caption declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  if (!picture) {
    // A drawn figure's caption is a row of its drawing, printed as stored,
    // mark included (hosted ARIP1VM 6.1.6).
    check(contains(pre_body(html), "   |____________|\n\n | Figure 1. Marked"),
          "drawn figure caption is not its marked row: " + html);
    return;
  }
  const auto caption = html.find("Figure 1. Marked");
  const auto revised = html.rfind("data-geist-revision-mark=\"|\"", caption);
  check(caption != std::string::npos && revised != std::string::npos &&
            html.find('>', revised) < caption &&
            html.find("</span>", revised) > caption,
        "picture figure caption lost its revision mark: " + html);
}

int main() {
  marked_figure_caption(true);
  marked_figure_caption(false);
  bare_picture_in_closed_box();
  figure_frame_rules(true);
  figure_frame_rules(false);
  rows_after_named_closer(false);
  rows_after_named_closer(true);
  full_width_rule_in_verbatim_region();
  glossary_letter_box(false);
  broken_bar_margin_mark();
  box_in_cz_pre_zone(false);
  box_in_cz_pre_zone(true);
  box_in_cz_flow_declines();
  grid_inside_box();
  envelope_straddling_box(false);
  envelope_straddling_box(true);
  anchor_before_body_control_line(false);
  anchor_before_body_control_line(true);
  box_closed_by_unopened_figure_closer();
  marked_box_rows();
  box_closed_by_unopened_closer(false);
  box_closed_by_unopened_closer(true);
  marked_cz_bullet(w("|"));
  marked_cz_bullet(b({'+'}, 0x2B));
  marked_cz_bullet(b({'#'}, 0x23));
  marked_verbatim_row(w("|"));
  marked_verbatim_row(b({'|'}, 0x7C));
  marked_styled_term(w("|"));
  marked_styled_term(b({'|'}, 0x7C));
  bullet_in_verbatim_region();
  box_rows_after_nested_verbatim_pair();
  box_in_generic_region();
  styled_generic_region(false);
  styled_generic_region(true);
  marked_generic_region_row();
  styled_box_rows(false);
  styled_box_rows(true);
  generic_region_ended_by_flow();
  unended_generic_region_declines();
  return 0;
}
