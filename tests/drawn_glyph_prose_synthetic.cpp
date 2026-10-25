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
//    row margin (SH45-5547-00 2.6 record 321).
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

} // namespace

int main() {
  marked_box_rows();
  return 0;
}
