// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The word `CZ` inside a row of prose is text, not a layout directive.
//
// BookServer recognises a control by a literal, upper-case prefix of the
// whole display line (doc/boo-spec/markup.adoc, "Reading a CZ body").
// dfhg4a02.boo `1.27`, record 1522, line 14 is the row
// `   In order to format CZ messages correctly in a DBCS environment, use the`
// (token 54 is the two-byte word `CZ`).  The flattened splitter opened a
// segment on that word, the directive reader found the mode word `messages`,
// which is none of FLOW, OFF and BREAK, and read the rest of the line as an
// inert control line: nine words vanished from a typed topic.
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
    std::cerr << "cz_word_in_prose_synthetic: " << message << '\n';
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
    record.logical_record = 1522;
    record.assembled = assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = 1522;
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
        1522, record.assembled, record.encoded_tokens, record.ir.display_lines);
    demote_display_line_owned_controls(record);
    return std::move(record);
  }
};

void a_cz_word_inside_a_row_is_text() {
  Builder b;
  b.line({w("sh1.27")});
  b.line({w("ctopicn"), w("46")});
  b.line({w("cparent"), w("1.0")});
  b.line({w("cforwardlevel"), w("1.28")});
  b.line({w("cbacklevel"), w("1.26")});
  b.line({w("csummary"), w("17"), w("0"), w("17")});
  b.line({w("chdlevel"), w(":H2")});
  b.line({w("csourcefn"), w("DFHG4A00")});
  b.line({w("ST"), w("Messages")});
  b.line({w("cz"), w("FLOW"), w("P"), w("3"), w("3")});
  b.line({w("   "), w("In"), w("order"), w("to"), w("format"), w("CZ"),
          w("messages"), w("correctly"), w("in"), w("a"), w("DBCS"),
          w("environment,"), w("use"), w("the")});
  b.line({w("   "), w("sit"), w("override"), w("MSGCASE=UPPER.")});
  std::string error;
  const auto doc = try_lower_topic_to_document_ir(
      {"1.27", "Messages", ":H2", 46, 1522, 1523}, {b.finish()}, nullptr,
      &error);
  check(doc.has_value(), "the topic declined: " + error);
  if (!doc) return;
  const auto markdown = render_document_markdown(*doc);
  check(markdown.find("format CZ messages correctly in a DBCS environment, "
                      "use the sit override") != std::string::npos,
        "the words after `CZ` were lost: " + markdown);
}
} // namespace

int main() {
  a_cz_word_inside_a_row_is_text();
  geist_test::exit_with_failures();
  std::cout << "cz word in prose checks passed\n";
  return 0;
}
