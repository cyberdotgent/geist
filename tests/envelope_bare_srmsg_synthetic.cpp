// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A bare `SRMSG` in the topic envelope whose decoded segment runs on into the
// next display line's length byte.
//
// A message-section heading topic carries one bare `SRMSG` among its metadata
// controls.  dfhg4a02.boo `1.6`, record 67: line 6 is `SRMSG` alone (length
// byte `01` at 0x225C9, opcode `4B` at 0x225CA), and line 7 opens with the
// length byte `04` at 0x225CB, which the book's dictionary spells as a long
// run of box-drawing glyphs, before `chdlevel :H2`.  No boundary byte stands
// between them, so the flattened segment of `SRMSG` carries that glyph run as
// its payload.  A length byte is the row-control slot and never display text
// (doc/boo-spec/logical-controls.adoc), so the `SRMSG` still carries nothing;
// 146 topics of the book were rejected as "bare SRMSG inside the envelope
// carries payload".
//
// Everything here is synthetic: the test builds whole topic records by hand.

#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "test_failures.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace geist::detail;
TokenWords w(const std::string &s) { return {s.begin(), s.end()}; }
void check(bool ok, const std::string &message) {
  if (!ok) {
    std::cerr << "envelope_bare_srmsg_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}
struct Builder {
  DecodedLogicalRecordSource record;
  std::uint16_t encoded = 0x100;
  // A display line whose length byte the dictionary spells `spelling`.
  void line(std::vector<TokenWords> tokens,
            TokenWords spelling = TokenWords{0x25ba}) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(tokens.size() * 2), 1});
    record.tokens.push_back(std::move(spelling));
    for (auto &words : tokens) {
      record.encoded_tokens.push_back({encoded++, 2});
      record.tokens.push_back(std::move(words));
    }
  }
  DecodedLogicalRecordSource finish() {
    record.logical_record = 67;
    record.assembled = assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = 67;
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
        67, record.assembled, record.encoded_tokens, record.ir.display_lines);
    demote_display_line_owned_controls(record);
    return std::move(record);
  }
};

// `glyph_run` spells the length byte after `SRMSG` as a box-glyph run (the
// book's shape); `payload` puts a text row between `SRMSG` and `chdlevel`,
// which is display text in the envelope and still declines.
void bare_srmsg(bool payload, bool expect_typed) {
  Builder b;
  b.line({w("sh1.6")});
  b.line({w("ctopicn"), w("25")});
  b.line({w("cparent"), w("1.0")});
  b.line({w("cforwardlevel"), w("1.7")});
  b.line({w("cbacklevel"), w("1.5")});
  b.line({w("csummary"), w("17"), w("0"), w("17")});
  b.line({w("SRMSG")});
  if (payload)
    b.line({w("   "), w("Loose"), w("words.")}, TokenWords(52, 0x2500));
  b.line({w("chdlevel"), w(":H2")}, TokenWords(52, 0x2500));
  b.line({w("csourcefn"), w("DFHG4A00")});
  b.line({w("ST"), w("Messages")});
  b.line({w("   "), w("The"), w("messages"), w("follow.")});
  std::string error;
  const auto doc = try_lower_topic_to_document_ir(
      {"1.6", "Messages", ":H2", 25, 67, 68}, {b.finish()}, nullptr, &error);
  if (!expect_typed) {
    check(!doc, "a bare SRMSG carrying a word in the envelope was typed");
    return;
  }
  check(doc.has_value(), "the bare SRMSG declined the topic: " + error);
  if (!doc) return;
  const auto markdown = render_document_markdown(*doc);
  check(markdown.find("The messages follow") != std::string::npos &&
            markdown.find("chdlevel") == std::string::npos,
        "the topic body is wrong: " + markdown);
}
} // namespace

int main() {
  bare_srmsg(false, true);
  bare_srmsg(true, false);
  geist_test::exit_with_failures();
  std::cout << "envelope bare SRMSG checks passed\n";
  return 0;
}
