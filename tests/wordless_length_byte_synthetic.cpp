// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A display line whose length byte spells no word at all.
//
// The length byte that opens a display line is the row's control slot,
// whatever the dictionary spells for it (doc/boo-spec/logical-controls.adoc).
// dfhs4c00.boo gives most of its length bytes an empty dictionary entry: no
// word and no attach control.  Record 41 line 27 is
// `cfont 7 8 E 16 1 E ...` and line 28 opens with token 216 (value 18, the
// byte `12` at 0x34C91, no words) in front of `       FUNCTION = ...`, the
// next row of a `cz OFF XMP` control-block listing.  Read as a bare spacing
// token, that byte glued the row to the one above it.
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
    std::cerr << "wordless_length_byte_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}
struct Builder {
  DecodedLogicalRecordSource record;
  std::uint16_t encoded = 0x100;
  // A display line; `wordless` gives its length byte an empty spelling.
  void line(std::vector<TokenWords> tokens, bool wordless = false) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(tokens.size() * 2), 1});
    record.tokens.push_back(wordless ? TokenWords{} : TokenWords{0x25ba});
    for (auto &words : tokens) {
      record.encoded_tokens.push_back({encoded++, 2});
      record.tokens.push_back(std::move(words));
    }
  }
  Builder() {
    line({w("sh1.2")});
    line({w("ctopicn"), w("8")});
    line({w("cparent"), w("1.0")});
    line({w("cforwardlevel"), w("1.3")});
    line({w("cbacklevel"), w("1.1")});
    line({w("csummary"), w("10"), w("0"), w("10")});
    line({w("chdlevel"), w(":H2")});
    line({w("csourcefn"), w("DFHS4C00")});
    line({w("ST"), w("Statistics")});
  }
  DecodedLogicalRecordSource finish() {
    record.logical_record = 41;
    record.assembled = assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = 41;
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
        41, record.assembled, record.encoded_tokens, record.ir.display_lines);
    demote_display_line_owned_controls(record);
    return std::move(record);
  }
};

// The listing rows of a `cz OFF XMP` region, each styled by the `cfont` line
// above it, with wordless length bytes on the text rows.
void wordless_rows_stay_apart() {
  Builder b;
  b.line({w("cz"), w("BREAK"), w("3")});
  b.line({});
  b.line({w("cz"), w("OFF"), w("XMP")});
  b.line({w("cfont"), w("11"), w("6"), w("E")}, true);
  b.line({w("          "), w("@BANNER"), w("END")}, true);
  b.line({w("cfont"), w("8"), w("8"), w("E")}, true);
  b.line({w("       "), w("FUNCTION"), w("="), w("statistics.")}, true);
  b.line({w("cz"), w("OFF"), w("EXMP"), w("0"), w("0")});
  std::string error;
  const auto doc = try_lower_topic_to_document_ir(
      {"1.2", "Statistics", ":H2", 8, 41, 42}, {b.finish()}, nullptr, &error);
  check(doc.has_value(), "the listing topic declined: " + error);
  if (!doc) return;
  const auto markdown = render_document_markdown(*doc);
  check(markdown.find("END\n") != std::string::npos &&
            markdown.find("FUNCTION") != std::string::npos &&
            markdown.find("END       FUNCTION") == std::string::npos &&
            markdown.find("END FUNCTION") == std::string::npos,
        "the row after a wordless length byte was glued to the row above: " +
            markdown);
}
} // namespace

int main() {
  wordless_rows_stay_apart();
  geist_test::exit_with_failures();
  std::cout << "wordless length byte checks passed\n";
  return 0;
}
