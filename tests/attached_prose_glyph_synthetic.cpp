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
  bullet();
  rejected({w("   "), w("SNA"), {1}, {0xffff}, {1}, w("ps")});
  rejected({w("   "), w("SNA"), {0x2666}, w("ps")});
  rejected({w("   "), w("SNA"), {1}, {0x2666}, w("ps")});
  rejected({w("   "), w("SNA "), {1}, {0x2666}, {1}, w("ps")});
  rejected({w("   "), {'A', 0xffff, 'B'}});
  return 0;
}
