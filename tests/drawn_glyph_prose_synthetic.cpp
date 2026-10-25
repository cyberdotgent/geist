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
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
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
// highlighted, and a wholly highlighted label lowers as plain text because
// the renderer emphasises every term itself (prose lowering,
// `label_is_wholly_highlighted`).
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
                       "\"|\">*YES</span></dt>"),
        "the marked term is not a revised term: " + html);
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

} // namespace

int main() {
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
  generic_region_ended_by_flow();
  unended_generic_region_declines();
  return 0;
}
