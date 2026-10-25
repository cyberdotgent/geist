// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Prose topics whose SRTBL/SRFIG envelopes do not close the ordinary way
// (issue #90).  Each case builds its own record; no BOO file is read.

#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
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
    std::cerr << "prose_envelope_close_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}
struct Builder {
  DecodedLogicalRecordSource record;
  std::uint16_t encoded = 0x100;
  void line(std::vector<TokenWords> tokens) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(tokens.size() * 2), 1});
    record.tokens.push_back({0x25ba});
    for (auto &words : tokens) {
      record.encoded_tokens.push_back({encoded++, 2});
      record.tokens.push_back(std::move(words));
    }
  }
  Builder() {
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

// GG24-4302-00 FRONT_1 record 23 and SC24-5527-02 3.8.4.1 record 208: the
// last envelope of the topic has no SRETBL, and the SRETBL it lacks stands
// doubled behind the envelope before it (`SRETBL`, `SRETBL`, `SRTBL<id>`).
// Hosted BookServer (DT 19950308184737, 19921218151459) prints the opener as
// the anchor of the next row, every row to the end of the topic verbatim,
// and nothing for either closer.
//   doubled = false: one SRETBL only -- the envelope is not proven to end.
//   stray_text = true: the second SRETBL carries a line of text.
void unclosed_envelope(bool doubled, bool stray_text, bool expect_typed) {
  Builder b;
  b.line({});
  b.line({w("   "), w("Text before.")});
  b.line({w("SRTBLTBLUNIQ1")});
  b.line({w("   "), w("CICS"), w("          "), w("COBOL")});
  b.line({w("SRETBL")});
  if (doubled)
    b.line({w("SRETBL")});
  if (stray_text)
    b.line({w("   "), w("Loose words.")});
  b.line({w("SRTBLTBLUNIQ2")});
  b.line({w("   "), w("Other trademarks.")});
  b.line({w("   "), w("Second row.")});
  std::string error;
  const auto doc = lower(b, error);
  if (!expect_typed) {
    check(!doc, "an unproven unclosed envelope was typed");
    return;
  }
  check(doc.has_value(), "unclosed envelope behind a doubled SRETBL declined: " +
                             error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto anchor = html.find("id=\"TBLTBLUNIQ2\"");
  const auto pre = html.find("<pre", anchor);
  const auto first = html.find("   Other trademarks.", pre);
  const auto second = html.find("   Second row.", first);
  check(anchor != std::string::npos && pre != std::string::npos &&
            first != std::string::npos && second != std::string::npos &&
            html.find("</pre>", first) > second,
        "open envelope rows are not one verbatim region after its anchor: " +
            html);
  check(html.find("ETBL") == std::string::npos &&
            html.find("SRTBL") == std::string::npos &&
            html.find("<table") == std::string::npos,
        "a closer printed or the open envelope became a table: " + html);
  const auto markdown = render_document_markdown(*doc);
  check(markdown.find("   Other trademarks.\n   Second row.") !=
            std::string::npos,
        "Markdown lost the open envelope's rows: " + markdown);
}
// IEAC6MST 7.9 record 512 line 10 (`SRLISone`) and SC24-5527-02 G.3 record
// 815 (`SRLISreceivx`): a list item opened and closed on lines of their own
// inside a drawn SRTBL envelope that renders verbatim.  Hosted BookServer
// (DT 19920124000100, 19921218151459) wraps the row after the opener in
// `<a name="LISone">` inside the same `<pre>` and prints nothing for the
// `SRELIS` closer.  `owner` is the closer's owner; one the topic never opens
// is not proven to be a closer and declines.
void list_item_in_verbatim_envelope(const std::string &owner,
                                    bool expect_typed) {
  Builder b;
  b.line({});
  b.line({w("   "), w("Text before.")});
  b.line({w("SRTBLCLISTS")});
  b.line({w("   | first row  |")});
  b.line({w("SRLISone")});
  b.line({w("   | second row |")});
  b.line({w("SRELIS" + owner)});
  b.line({w("   | third row  |")});
  b.line({w("SRETBL")});
  std::string error;
  const auto doc = lower(b, error);
  if (!expect_typed) {
    check(!doc, "a closer of a list item the topic never opens was typed");
    return;
  }
  check(doc.has_value(), "list item inside a verbatim envelope declined: " +
                             error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  const auto pre = html.find("<pre");
  const auto first = html.find("first row", pre);
  const auto anchor = html.find("id=\"LISone\"", pre);
  const auto second = html.find("second row", pre);
  const auto third = html.find("third row", pre);
  const auto close = html.find("</pre>", pre);
  check(pre != std::string::npos && first < anchor && anchor < second &&
            second < third && third < close &&
            html.find("<pre", pre + 1) == std::string::npos,
        "list anchor is not on the row after SRLIS inside one region: " +
            html);
  check(html.find("ELIS") == std::string::npos &&
            html.find("SRLIS") == std::string::npos,
        "a list control printed: " + html);
  const auto markdown = render_document_markdown(*doc);
  check(markdown.find("id=\"LISone\"") != std::string::npos &&
            markdown.find("third row") != std::string::npos,
        "Markdown lost the list anchor or a row: " + markdown);
}
// SC31-8778-01 2.3.6.1 record 282 line 24: an `SRETBL` alone on its display
// line, after the operand's `statements:`/`dependencies:` rows, in a topic
// that writes no `SRTBL` at all -- the compiler kept the end of a table whose
// start it never wrote, in 714 operand topics of the book.  Hosted BookServer
// (DT 20010606144047) serves the rows on either side as stored and nothing
// for the closer.  `payload` puts display text on the closer's own line,
// which no such topic shows; it stays unproven and declines.
void unopened_table_closer(bool payload, bool expect_typed) {
  Builder b;
  b.line({});
  b.line({w("   "), w("statements:"), w("     "), w("APPL")});
  if (payload)
    b.line({w("SRETBL"), w("   "), w("Loose words.")});
  else
    b.line({w("SRETBL")});
  b.line({});
  b.line({w("   "), w("Specifies"), w("the"), w("minor"), w("node.")});
  std::string error;
  const auto doc = lower(b, error);
  if (!expect_typed) {
    check(!doc, "an unopened SRETBL carrying display text was typed");
    return;
  }
  check(doc.has_value(), "an unopened SRETBL declined the topic: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  check(html.find("statements:") != std::string::npos &&
            html.find("Specifies the minor node.") != std::string::npos &&
            html.find("statements:") < html.find("Specifies"),
        "the rows around the unopened SRETBL were lost: " + html);
  check(html.find("ETBL") == std::string::npos &&
            html.find("<table") == std::string::npos,
        "the unopened SRETBL printed or opened a table: " + html);
}
// SC31-8778-01 2.2.3.4 record 226 lines 20-23: the unopened `SRETBL` is
// followed by `cz OFF ETABLE 0 0` and `cz FLOW P 3 3`, with no `cz OFF TABLE`
// in the topic (315 operand topics of the book).  Hosted (DT 20010606144047)
// prints the diagram and dependency rows, `</pre>` and the paragraph -- the
// closer draws nothing.  `closer_rows` gives the closer a display row of its
// own, which no such topic shows; it stays unproven and declines.
void unopened_cz_table_closer(bool closer_rows, bool expect_typed) {
  Builder b;
  b.line({});
  b.line({w("cz"), w("BREAK"), w("3")});
  b.line({w("   "), w("statement:"), w("      "), w("ADJCP")});
  b.line({w("SRETBL")});
  b.line({});
  b.line({w("cz"), w("OFF"), w("ETABLE"), w("0"), w("0")});
  if (closer_rows)
    b.line({w("   "), w("Loose"), w("row.")});
  b.line({w("cz"), w("FLOW"), w("P"), w("3"), w("3")});
  b.line({w("   "), w("The"), w("network"), w("identifier.")});
  std::string error;
  const auto doc = lower(b, error);
  if (!expect_typed) {
    check(!doc, "an unopened cz OFF ETABLE carrying a row was typed");
    return;
  }
  check(doc.has_value(),
        "an unopened cz OFF ETABLE declined the topic: " + error);
  if (!doc)
    return;
  const auto html = render_document_html_fragment(*doc);
  check(html.find("statement:") != std::string::npos &&
            html.find("The network identifier.") != std::string::npos,
        "the rows around the unopened closers were lost: " + html);
  check(html.find("ETABLE") == std::string::npos &&
            html.find("ETBL") == std::string::npos &&
            html.find("<table") == std::string::npos,
        "an unopened closer printed or opened a table: " + html);
}
} // namespace

int main() {
  unclosed_envelope(true, false, true);
  unclosed_envelope(false, false, false);
  unclosed_envelope(true, true, false);
  list_item_in_verbatim_envelope("one", true);
  list_item_in_verbatim_envelope("two", false);
  unopened_table_closer(false, true);
  unopened_table_closer(true, false);
  unopened_cz_table_closer(false, true);
  unopened_cz_table_closer(true, false);
  geist_test::exit_with_failures();
  std::cout << "prose envelope close checks passed\n";
  return 0;
}
