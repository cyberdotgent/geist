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

  // One display line: a one-byte length byte covering `content`.
  void line(std::vector<Tok> content) {
    std::uint16_t length = 0;
    for (const auto &token : content) length += token.width;
    record.encoded_tokens.push_back({length, 1});
    record.tokens.push_back(TokenWords{0x25BA});
    for (auto &token : content) {
      record.encoded_tokens.push_back(
          {token.width == 2 ? encoded++ : token.value, token.width});
      record.tokens.push_back(std::move(token.words));
    }
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

// The CZ builder's block for a generic region keeps no inline model, so a
// region whose rows carry a cross-reference keeps its old reading and the
// topic still declines: SY27-0327-05 2.1.8 draws a step diagram with linked
// `Yes` branches inside `cz OFF MAP`, and hosted links them.
void linked_generic_region_declines() {
  Builder builder(true);
  builder.line({sp(3), w("Lead.")});
  builder.blank();
  builder.line({w("cz"), w("OFF"), w("MAP")});
  builder.line({sp(3), box(run(0x250C, 0x2500, 12, 0x2510))});
  builder.line({w("cselect"), w("5"), w("6"), w("TARGET")});
  builder.line({sp(3), box({0x2502}), w("Inside"), sp(4), box({0x2502})});
  builder.line({sp(3), box(run(0x2514, 0x2500, 12, 0x2518))});
  builder.line({w("cz"), w("OFF"), w("EMAP"), w("0"), w("0")});
  std::string error;
  const auto doc = lower(builder, error);
  check(!doc.has_value(),
        "a generic region with a cross-reference was typed without it");
}

} // namespace

int main() {
  marked_box_rows();
  box_closed_by_unopened_closer(false);
  box_closed_by_unopened_closer(true);
  marked_cz_bullet(w("|"));
  marked_cz_bullet(b({'+'}, 0x2B));
  marked_cz_bullet(b({'#'}, 0x23));
  marked_styled_term(w("|"));
  marked_styled_term(b({'|'}, 0x7C));
  bullet_in_verbatim_region();
  box_in_generic_region();
  linked_generic_region_declines();
  return 0;
}
