// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// An implicit CFONT grid inside a prose topic (doc/boo-spec/markup.adoc,
// "Implicit CFONT grids"): a plural `cfont` header row over repeated
// one-byte row controls, with no drawn box.  SC31-711 1.1, 1.2 and 1.3 have
// this shape, and hosted BookServer (DT 19941010174546) serves every row at
// its native columns -- key at the first header group's column, value and
// its continuation lines at the second's -- with an empty line between two
// entries.  The prose model keeps the rows verbatim instead of running each
// key into its value, and still declines a grid whose rows it cannot prove.

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

void check(bool ok, const std::string &message) {
  if (!ok) {
    std::cerr << "implicit_grid_prose_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

// One source token: its words and how the record encodes it.
struct Token {
  TokenWords words;
  EncodedLogicalToken encoded;
};

struct Builder {
  DecodedLogicalRecordSource record;
  std::uint16_t next = 0x100;

  // A dictionary word stored as a two-byte token.
  Token word(const std::string &text) {
    return {TokenWords(text.begin(), text.end()), {next++, 2}};
  }
  // A space run stored as the one-byte token `value`.
  static Token run(std::size_t cells, std::uint16_t value) {
    return {TokenWords(cells, ' '), {value, 1}};
  }
  // The compact `.` that ends a sentence: one-byte value 0x01.
  static Token dot() { return {{1, '.'}, {1, 1}}; }

  // A display line: its length byte (the line's byte count, spelled by the
  // dictionary as some word that displays nothing), then its tokens.
  void line(std::vector<Token> tokens) {
    std::uint16_t bytes = 0;
    for (const auto &token : tokens) bytes += token.encoded.width;
    record.encoded_tokens.push_back({bytes, 1});
    record.tokens.push_back({0x25ba});
    for (auto &token : tokens) {
      record.encoded_tokens.push_back(token.encoded);
      record.tokens.push_back(std::move(token.words));
    }
  }
  void words_line(const std::vector<std::string> &words) {
    std::vector<Token> tokens;
    for (const auto &text : words) tokens.push_back(word(text));
    line(std::move(tokens));
  }
  // An empty display line: the length byte `00`.
  void blank() {
    record.encoded_tokens.push_back({0, 1});
    record.tokens.push_back({1});
  }

  Builder() {
    words_line({"sh1.3"});
    words_line({"ctopicn", "10"});
    words_line({"cparent", "1.0"});
    words_line({"cforwardlevel"});
    words_line({"cbacklevel"});
    words_line({"csummary", "10", "0", "10"});
    words_line({"chdlevel", ":H2"});
    words_line({"csourcefn", "SAMPLE"});
    words_line({"ST", "Daemons"});
    blank();
    std::vector<Token> intro{run(3, 9)};
    for (const auto *text : {"The", "following", "list", "describes", "them:"})
      intro.push_back(word(text));
    line(std::move(intro));
    blank();
    // SC31-711 record 22 line 29 and 30: the header's three HP2 spans form
    // two column groups, key at 3 and value at 18.
    words_line({"cfont", "3", "7", "2", "11", "4", "2", "18", "11", "2"});
    line({run(3, 9), word("Process"), word("Name"), run(2, 8),
          word("Description")});
  }

  // One entry: `blank`, then the row whose length byte, three-cell origin
  // (one-byte 0x09) and the `.` ending the row before it form the repeated
  // `01 00 nn 09` row control.
  void entry(const std::string &key, const std::vector<std::string> &value,
             const std::vector<std::string> &continuation = {}) {
    blank();
    std::vector<Token> row{run(3, 9), word(key),
                           run(18 - 3 - key.size() - 1, 14)};
    for (const auto &text : value) row.push_back(word(text));
    if (continuation.empty()) row.push_back(dot());
    line(std::move(row));
    if (continuation.empty()) return;
    std::vector<Token> next{run(18, 18)};
    for (const auto &text : continuation) next.push_back(word(text));
    next.push_back(dot());
    line(std::move(next));
  }

  DecodedLogicalRecordSource finish() {
    record.logical_record = 22;
    record.assembled = assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = 22;
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
        22, record.assembled, record.encoded_tokens, record.ir.display_lines);
    demote_display_line_owned_controls(record);
    return std::move(record);
  }
};

void entries(Builder &b) {
  b.entry("lnmd", {"Manages", "the", "monitoring", "applications"},
          {"for", "the", "command", "line", "interface"});
  b.entry("lnmstart", {"Starts", "the", "daemon", "processes"});
  b.entry("lnmstop", {"Stops", "the", "daemon", "processes"});
  b.entry("lnmstatus", {"Displays", "the", "daemon", "status"});
  b.entry("lnmbrmon", {"Runs", "the", "bridge", "monitor"});
  b.entry("lnmbrmgr", {"Manages", "the", "bridge", "application"});
  b.entry("lnmtrmon", {"Runs", "the", "ring", "monitor"});
}

std::optional<DocumentIR> lower(Builder &b, std::string &error) {
  return try_lower_topic_to_document_ir({"1.3", "Daemons", ":H2", 10, 22, 23},
                                        {b.finish()}, nullptr, &error);
}

void grid_rows_stay_in_their_columns() {
  Builder b;
  entries(b);
  std::string error;
  const auto doc = lower(b, error);
  check(doc.has_value(), "implicit grid declined: " + error);
  if (!doc) return;
  const auto html = render_document_html_fragment(*doc);
  check(html.find("data-geist-severity=\"typed\"") != std::string::npos,
        "implicit grid topic is not typed: " + html);
  // The introduction is still reflowed prose.
  check(html.find("<p class=\"geist-paragraph\">The following list describes "
                  "them:</p>") != std::string::npos,
        "introduction lost its paragraph: " + html);
  // Every row keeps its native columns: key at 3, value at 18, and the
  // continuation line under the value.
  const auto pre = html.find("<pre");
  check(pre != std::string::npos && pre > html.find("describes"),
        "grid rows are not one preformatted block: " + html);
  for (const auto *row :
       {"   lnmd           Manages the monitoring applications\n"
        "                  for the command line interface.",
        "   lnmstart       Starts the daemon processes.",
        "   lnmtrmon       Runs the ring monitor."})
    check(html.find(row) != std::string::npos,
          std::string("grid row lost its columns: ") + row + "\n" + html);
  // The header row is styled by its `cfont` spans.
  check(html.find("<strong class=\"geist-strong\">Process Name") !=
            std::string::npos,
        "grid header lost its CFONT style: " + html);
  // Entries stay separated by their empty display lines, as hosted prints.
  check(html.find("interface.\n\n   lnmstart") != std::string::npos,
        "empty line between two entries was dropped: " + html);
  // No key runs into its value.
  check(html.find("lnmd Manages") == std::string::npos,
        "a key ran into its value: " + html);
  const auto markdown = render_document_markdown(*doc);
  check(markdown.find("   lnmstart       Starts the daemon processes.") !=
            std::string::npos,
        "Markdown lost the grid columns: " + markdown);
}

void control_inside_grid_declines() {
  Builder b;
  entries(b);
  // A structural control among the rows is nothing the grid can print.
  b.words_line({"SRANCHOR"});
  b.entry("lnmtrmgr", {"Manages", "the", "ring", "application"});
  std::string error;
  const auto doc = lower(b, error);
  check(!doc.has_value(), "grid holding a control line was admitted");
  check(error.find("implicit two-column grid") != std::string::npos,
        "control inside the grid declined for another reason: " + error);
}

} // namespace

int main() {
  grid_rows_stay_in_their_columns();
  control_inside_grid_declines();
  return 0;
}
