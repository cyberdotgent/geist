// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A letter or digit the book header declares as a revision character is a
// row-margin revision mark.
//
// The book header (the logical records before the first topic) carries one
// `crevdesc <character> [description]` display line per SCRIPT revision code
// the book compiler resolved (doc/boo-spec/markup.adoc, "Revision characters
// declared in the book header").  Most books declare punctuation (`|`, `#`,
// `+`), which the row-margin reading admits on its own; a few declare letters
// and digits, and draw them in cell 1 of their revised rows exactly where the
// others draw `|`:
//
//  * SC41-4725-00 record 1 lines 64-69 declare `G`, `|`, `|`, `+`, `G`, `@`;
//    24.0 record 129 line 7 is ` G     °   With a time separator, ...`
//    under `cz FLOW LI 7 11`;
//  * SC26-4769-00 declares `x` (and `t`, `e`); 1.6 record 208 line 10 is
//    ` x °   Following a STOP RUN statement, ...`;
//  * SC33-1653-06 declares `1`..`7`; 5.11 record 1258 line 0 is
//    ` 6 °   The generic name coded on a DFHTST TYPE=LOCAL macro ...`.
//
// Hosted BookServer lists those topics on the book's revision page under the
// same character (SC41-4725-00 `REVISIONS`: ` G  24.0 ...`) and serves the
// rows with the character in the margin and the bullet blanked
// (`<li> <br> G         With a time separator, ...`).
//
// An undeclared letter in cell 1 stays row text (` I am ...`).
//
// Everything here is synthetic: the test builds its own records.

#include "geist/detail/container/revision_declarations.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace geist::detail;

void check(bool ok, const std::string &message) {
  if (!ok) {
    std::cerr << "declared_revision_mark_synthetic: " << message << '\n';
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

Tok w(const std::string &text) {
  return {TokenWords(text.begin(), text.end()), 2, 0};
}
Tok b(TokenWords words, std::uint16_t value) {
  return {std::move(words), 1, value};
}
Tok sp(std::size_t count) {
  return b(TokenWords(count, ' '), static_cast<std::uint16_t>(11 + count));
}
Tok bullet() { return b({0x2666}, 3); }

// A record built by hand, display line by display line.
struct Builder {
  DecodedLogicalRecordSource record;
  std::uint16_t encoded = 0x4100;
  std::size_t last_prefix = 0;

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
  // The last line's length byte decodes as a blank run, as SC41-4725-00
  // record 129 line 7's does (token 84, value 27, ten blanks).
  void blank_prefix(std::size_t count) {
    record.tokens[last_prefix] = TokenWords(count, ' ');
  }
  void blank() {
    record.encoded_tokens.push_back({0, 1});
    record.tokens.push_back({1});
  }
  void header() {
    line({w("sh1.1")});
    line({w("ctopicn"), w("10")});
    line({w("cparent"), w("1.0")});
    line({w("cforwardlevel")});
    line({w("cbacklevel")});
    line({w("csummary"), w("10"), w("0"), w("10")});
    line({w("chdlevel"), w(":H2")});
    line({w("csourcefn"), w("SAMPLE")});
    line({w("ST"), w("Example")});
  }
  DecodedLogicalRecordSource
  finish(std::uint32_t logical_record,
         const DeclaredRevisionCharacters &declared) {
    record.logical_record = logical_record;
    record.assembled = assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = logical_record;
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
    record.control_segments =
        decode_control_segments(logical_record, record.assembled,
                                record.encoded_tokens, record.ir.display_lines);
    demote_display_line_owned_controls(record);
    assign_display_line_margin_marks(record, declared);
    return std::move(record);
  }
};

// The header's `crevdesc` lines name the book's revision characters: the
// first operand, one character, whatever description follows it.
// SC33-0807-00 record 1 stores `crevdesc 1 Changes since the First Draft of
// this book`; a first operand longer than one character declares nothing.
void header_declarations() {
  Builder builder;
  builder.line({w("cmargin=2")});
  builder.line({w("crevdesc"), w("G")});
  builder.line({w("crevdesc"), w("|")});
  builder.line({w("crevdesc"), w("1"), w("Changes"), w("since"), w("draft")});
  builder.line({w("crevdesc"), w("G")});
  builder.line({w("crevdesc"), w("ver2rel2")});
  builder.line({w("c.rev"), w("I80"), w("x")});
  builder.line({w("ctopics=488")});
  const auto record = builder.finish(1, {});
  check(record.ir.display_lines_parse,
        "the synthetic header record's display lines do not parse");
  const auto declared = read_declared_revision_characters({record.ir});
  check(declared == DeclaredRevisionCharacters{U'1', U'G', U'|'},
        "the header's crevdesc lines do not declare exactly 1, G and |");
}

// A declared letter in cell 1 is the row's margin mark; an undeclared one is
// row text.
void declared_letter_is_a_margin_mark() {
  const auto build = [](const DeclaredRevisionCharacters &declared) {
    Builder builder;
    builder.line({sp(1), w("G"), sp(5), bullet(), sp(3), w("With"), w("a")});
    builder.line({sp(1), w("G"), sp(9), w("the"), w("time")});
    builder.line({sp(1), w("I"), w("am")});
    return builder.finish(129, declared);
  };
  const auto marked = build({U'G', U'|'});
  const auto &lines = marked.ir.display_lines;
  check(lines.size() == 3, "the synthetic record does not hold 3 lines");
  if (lines.size() != 3) return;
  check(lines[0].margin_mark == U'G' &&
            lines[0].margin_mark_place == MarginMarkPlaceIR::row_margin &&
            lines[0].margin_mark_token == 2,
        "the declared `G` in front of a bullet is not the row's mark");
  check(lines[1].margin_mark == U'G',
        "the declared `G` in front of a continuation row is not its mark");
  check(!lines[2].margin_mark, "an undeclared `I` was read as a mark");
  std::string error;
  check(verify_display_line_margin_marks(marked, &error, {U'G', U'|'}),
        "the declared marks do not verify: " + error);

  const auto plain = build({});
  check(!plain.ir.display_lines[0].margin_mark &&
            !plain.ir.display_lines[1].margin_mark,
        "an undeclared `G` was read as a mark");
}

// SC41-4725-00 24.0: the declared `G` in front of a CZ list bullet is the
// item's revision mark, as `|` is; the topic no longer declines with a
// bullet "inside prose text".
void declared_mark_before_cz_bullet() {
  const auto lower = [](const DeclaredRevisionCharacters &declared,
                        std::string &error) {
    Builder builder;
    builder.header();
    builder.line({w("cz"), w("FLOW"), w("PARML"), w("3"), w("3")});
    builder.line({w("cz"), w("FLOW"), w("PT"), w("3"), w("7")});
    builder.line({sp(3), w("CRTDATE")});
    builder.line({w("cz"), w("BREAK"), w("7")});
    builder.line({w("cz"), w("FLOW"), w("P"), w("7"), w("7")});
    builder.line({sp(7), w("Lead"), w("in:")});
    builder.blank();
    builder.line({w("cz"), w("FLOW"), w("UL"), w("7"), w("7")});
    builder.line({w("cz"), w("FLOW"), w("LI"), w("7"), w("11")});
    builder.line({sp(1), w("G"), sp(4), bullet(), sp(2), w("With"), w("a"),
                  w("separator,")});
    builder.blank_prefix(10);
    builder.line({sp(1), w("G"), sp(8), w("the"), w("hours.")});
    builder.blank_prefix(4);
    builder.line({sp(11), w("Then"), w("it"), w("fails.")});
    builder.blank();
    builder.line({w("cz"), w("FLOW"), w("LI"), w("7"), w("11")});
    builder.line({sp(7), bullet(), sp(3), w("Without"), w("one.")});
    builder.blank();
    builder.line({w("cz"), w("OFF"), w("EUL"), w("0"), w("0")});
    builder.line({w("cz"), w("OFF"), w("EPARML"), w("0"), w("0")});
    return try_lower_topic_to_document_ir(
        {"24.0", "Example", ":H2", 10, 20, 21},
        {builder.finish(20, declared)}, nullptr, &error);
  };
  std::string error;
  const auto doc = lower({U'G', U'|'}, error);
  check(doc.has_value(), "a declared mark before a CZ bullet declined: " + error);
  if (doc) {
    const auto html = render_document_html_fragment(*doc);
    check(contains(html, "data-geist-revision-mark=\"G\"") &&
              contains(html, "With a separator, the hours.") &&
              contains(html, "Without one.") && !contains(html, "°") &&
              !contains(html, "G With") && !contains(html, "G the"),
          "the declared mark is not the item's revision mark: " + html);
    const auto markdown = render_document_markdown(*doc);
    check(contains(markdown, "With a separator, the hours.") &&
              !contains(markdown, "G the"),
          "the Markdown item kept the mark as text: " + markdown);
  }
}

// SC26-4769-00 1.6 (no CZ directive; hosted DT 19920801115802 serves the
// topic in its `<pre>`): record 208 line 10 is ` x °   Following a STOP RUN
// statement, ...` between `   °   ` rows of one list, `x` declared by the
// header.  The declared mark is the item's revision mark; undeclared, the
// letter is row text and the bullet behind it is no list bullet.
void declared_mark_before_flattened_bullet() {
  const auto lower = [](const DeclaredRevisionCharacters &declared,
                        std::string &error) {
    Builder builder;
    builder.header();
    builder.line({sp(3), w("Lead:")});
    builder.blank();
    builder.line({sp(3), bullet(), sp(3), w("First.")});
    builder.blank();
    builder.line({sp(1), w("x"), bullet(), sp(3), w("Following"), w("a"),
                  w("STOP"), w("RUN.")});
    builder.blank();
    builder.line({sp(3), bullet(), sp(3), w("Third.")});
    builder.blank();
    builder.line({sp(3), w("After.")});
    return try_lower_topic_to_document_ir(
        {"1.6", "Example", ":H2", 10, 20, 21},
        {builder.finish(20, declared)}, nullptr, &error);
  };
  std::string error;
  const auto doc = lower({U'e', U't', U'x', U'|'}, error);
  check(doc.has_value(),
        "a declared mark before a flattened bullet declined: " + error);
  if (doc) {
    const auto html = render_document_html_fragment(*doc);
    check(contains(html, "data-geist-revision-mark=\"x\"") &&
              contains(html, "Following a STOP RUN.") &&
              html.find("<ul") == html.rfind("<ul") &&
              contains(html, "Third.</") && !contains(html, "x Following"),
          "the declared mark is not the list item's revision mark: " + html);
  }
  std::string undeclared_error;
  check(!lower({}, undeclared_error).has_value(),
        "an undeclared letter before a bullet was typed");
}

// The book's declarations reach every topic decode: they are read from the
// records before the first topic and kept on the decode context.
struct Book {
  LogicalDecodeContext context;
  std::map<std::uint16_t, TokenWords> dictionary;
  std::uint16_t next = 0x8000;

  Book() {
    context.directory.token_threshold = 0x80;
    context.directory.version_text = " 1.2";
    context.directory.dictionary_text_parameter = 500;
    context.bytes.resize(512);
    for (std::size_t byte = 0; byte < 128; ++byte) {
      const auto key = static_cast<std::uint16_t>(0x100 + byte);
      context.bytes[byte * 2] = key >> 8;
      context.bytes[byte * 2 + 1] = key & 0xff;
      dictionary[key] = {'?'};
    }
  }

  void record(const std::vector<std::vector<std::string>> &lines) {
    const auto begin = context.bytes.size();
    for (const auto &words : lines) {
      const auto length = static_cast<std::uint8_t>(2 * words.size());
      dictionary[0x100 + length] = TokenWords{'?'};
      context.bytes.push_back(length);
      for (const auto &word : words) {
        dictionary[next] = TokenWords(word.begin(), word.end());
        context.bytes.push_back(next >> 8);
        context.bytes.push_back(next & 0xff);
        ++next;
      }
    }
    context.record_payload_ranges.push_back(
        {static_cast<std::uint32_t>(begin),
         static_cast<std::uint32_t>(context.bytes.size())});
    context.decoded_records.emplace_back();
  }

  void finish() {
    context.source_dictionary =
        std::make_shared<const std::map<std::uint16_t, TokenWords>>(dictionary);
  }
};

void context_reads_header_declarations() {
  Book book;
  book.record({{"cdocnum=SC41-4725-00"}, {"crevdesc", "G"}, {"crevdesc", "|"}});
  book.record({{"sh24.0"}, {"ctopicn", "24"}, {"ST", "Example"}});
  book.record({{" ", "G", " ", "Revised", "text."}, {" ", "I", " ", "am."}});
  book.finish();
  book.context.declared_revision_characters =
      read_book_header_revision_characters(book.context, 2);
  check(book.context.declared_revision_characters ==
            DeclaredRevisionCharacters{U'G', U'|'},
        "the context's header does not declare G and |");
  const auto sources = decode_logical_record_sources(book.context, 3, 4);
  check(sources.size() == 1, "the synthetic topic record did not decode");
  if (sources.size() != 1) return;
  const auto &lines = sources.front().ir.display_lines;
  check(lines.size() == 2 && lines[0].margin_mark == U'G' &&
            !lines[1].margin_mark,
        "the header's declared `G` did not reach the topic's display lines");

  // No header, no declaration: the same row's `G` is text.
  Book bare;
  bare.record({{" ", "G", " ", "Revised", "text."}});
  bare.finish();
  const auto plain = decode_logical_record_sources(bare.context, 1, 2);
  check(plain.size() == 1 && !plain.front().ir.display_lines.empty() &&
            !plain.front().ir.display_lines[0].margin_mark,
        "a book with no header declared a revision character");
}

} // namespace

int main() {
  header_declarations();
  declared_letter_is_a_margin_mark();
  declared_mark_before_cz_bullet();
  declared_mark_before_flattened_bullet();
  context_reads_header_declarations();
  return 0;
}
