// Cross references a verbatim topic resolves (issue #72).
//
// `cselect <column> <length> <target>` opens its own display line and marks
// that column range of the row it precedes.  A topic no typed family claims
// still names those references, and hosted BookServer serves them as `<a
// href>` inside the topic's `<pre>`, so the verbatim rendering has to spell
// them into the row -- without moving a single byte of the row itself.
//
// Everything here is synthetic: the tests build `DecodedLogicalRecordSource`
// values by hand and open no book (issue #59).

#include "geist/detail/render_diagnostic_ir.hpp"
#include "geist/detail/verbatim_cross_references.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    geist_test::record_failure();
  }
}

void append(DecodedLogicalRecordSource &record, std::uint16_t encoded,
            std::uint8_t width, TokenWords words) {
  record.encoded_tokens.push_back({encoded, width});
  record.tokens.push_back(std::move(words));
}

// A display line: its length byte, then the words of the line.  The byte's
// value is the width the line's tokens occupy, which is what the record
// decoder walks.
void line(DecodedLogicalRecordSource &record,
          const std::vector<std::string> &words) {
  std::uint16_t width = 0;
  for (const auto &word : words)
    width = static_cast<std::uint16_t>(width + 2);
  append(record, width, 1, {3, 'r', 'o', 'w'});
  for (const auto &word : words) {
    TokenWords token;
    for (const auto ch : word)
      token.push_back(static_cast<std::uint16_t>(
          static_cast<unsigned char>(ch)));
    append(record, 0x40, 2, std::move(token));
  }
}

// Rebuilds the typed IR of a hand-assembled record and lets the decoder decide
// the framing, exactly as `decode_record_payload_ir` does in production.
void refresh(DecodedLogicalRecordSource &record) {
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  record.ir.logical_record = record.logical_record;
  record.ir.tokens.clear();
  std::uint32_t byte = 0;
  for (std::size_t token = 0; token < record.tokens.size(); ++token) {
    const auto encoded = record.encoded_tokens[token];
    const auto spacing = !record.tokens[token].empty() &&
                         record.tokens[token].front() < 4;
    record.ir.tokens.push_back(
        {token, encoded, record.tokens[token],
         {byte, static_cast<std::uint32_t>(byte + encoded.width)}, spacing,
         spacing ? record.tokens[token].front() : std::uint16_t{3}});
    byte += encoded.width;
  }
  record.ir.payload_range = {0, byte};
  geist::detail::assign_display_line_framing(record.ir);
  record.control_segments = geist::detail::decode_control_segments(
      record.logical_record, record.assembled, record.encoded_tokens,
      record.ir.display_lines);
}

// The row with every `[label](<#target>)` collapsed back to `label`.  A
// verbatim row may gain link syntax and nothing else, so this must reproduce
// the row the renderer would have emitted without this pass, byte for byte.
std::string without_links(std::string row) {
  for (auto at = row.find("](<#"); at != std::string::npos;
       at = row.find("](<#")) {
    const auto close = row.find(">)", at);
    if (close == std::string::npos) break;
    row.erase(at, (close + 2) - at);
    const auto label = row.rfind('[', at);
    if (label == std::string::npos) break;
    row.erase(label, 1);
  }
  return row;
}

std::vector<std::string> linked(
    const std::vector<DecodedLogicalRecordSource> &sources,
    const std::vector<std::string> &printed_footnotes = {}) {
  return geist::detail::link_verbatim_cross_references(
             sources, geist::detail::best_effort_display_lines(sources, {}),
             printed_footnotes)
      .lines;
}

// The row `Alpha Beta Gamma` preceded by `cselect 6 4 HDRTGT`: hosted wraps
// exactly columns 6..9, which is the word `Beta`.
DecodedLogicalRecordSource marked_row(const std::string &operands) {
  DecodedLogicalRecordSource record;
  record.logical_record = 1;
  std::vector<std::string> control{"cselect"};
  std::size_t begin = 0;
  while (begin < operands.size()) {
    auto end = operands.find(' ', begin);
    if (end == std::string::npos) end = operands.size();
    control.push_back(operands.substr(begin, end - begin));
    begin = end + 1;
  }
  line(record, control);
  line(record, {"Alpha", "Beta", "Gamma"});
  refresh(record);
  return record;
}

// The selector's own display line draws nothing, so the row it marks is the
// next row the rendering emits.  The link covers exactly the marked columns.
void a_selector_marks_the_row_it_precedes() {
  const std::vector<DecodedLogicalRecordSource> sources{
      marked_row("6 4 HDRTGT")};
  const auto plain = geist::detail::best_effort_lines(sources, {});
  const auto rows = linked(sources);
  require(plain.size() == 1 && rows.size() == 1,
          "the synthetic topic did not emit exactly one verbatim row");
  if (plain.size() != 1 || rows.size() != 1) return;
  require(plain[0] == "Alpha Beta Gamma",
          "the synthetic row reads '" + plain[0] + "'");
  require(rows[0] == "Alpha [Beta](<#HDRTGT>) Gamma",
          "the marked columns were not wrapped; row is '" + rows[0] + "'");
  // The hard constraint: the row's own bytes did not move.
  require(without_links(rows[0]) == plain[0],
          "the verbatim row changed beyond the link syntax: '" +
              without_links(rows[0]) + "'");
}

// A column range reaching past the row it marks proves nothing about which
// text the reference covers, so the row stays plain.
void a_span_outside_the_row_is_declined() {
  const std::vector<DecodedLogicalRecordSource> sources{
      marked_row("6 40 HDRTGT")};
  const auto rows = linked(sources);
  require(rows.size() == 1 && rows[0] == "Alpha Beta Gamma",
          "a span past the end of the row was linked anyway: '" +
              (rows.empty() ? std::string() : rows[0]) + "'");
}

// `cselect <c> <l> LNK <kind> ...` carries more operands than the three the
// column/length/target form has; it names an external destination and is not
// admitted here.
void a_non_canonical_selector_is_declined() {
  const std::vector<DecodedLogicalRecordSource> sources{
      marked_row("6 4 LNK OTHER")};
  const auto rows = linked(sources);
  require(rows == geist::detail::best_effort_lines(sources, {}),
          "a selector with non-canonical operands was linked");
}

// A picture selector places a stored object.  The verbatim route draws no
// picture, and must not publish a text link to one.
void a_picture_selector_is_declined() {
  const std::vector<DecodedLogicalRecordSource> sources{
      marked_row("6 4 PIC29")};
  const auto rows = linked(sources);
  require(rows.size() == 1 && rows[0] == "Alpha Beta Gamma",
          "a picture selector was linked as a cross reference: '" +
              (rows.empty() ? std::string() : rows[0]) + "'");
}

// A footnote is reachable only from the page that prints it.  One this topic
// does not print is declined; one it prints is admitted and reported back, so
// the renderer can emit the local anchor the link needs.
void a_footnote_target_must_be_printed_here() {
  const std::vector<DecodedLogicalRecordSource> sources{
      marked_row("6 4 FTNUNIQ1")};
  const auto absent = linked(sources);
  require(absent.size() == 1 && absent[0] == "Alpha Beta Gamma",
          "a footnote this topic does not print was linked: '" +
              (absent.empty() ? std::string() : absent[0]) + "'");

  const auto present = geist::detail::link_verbatim_cross_references(
      sources, geist::detail::best_effort_display_lines(sources, {}),
      {"FTNUNIQ1"});
  require(present.lines.size() == 1 &&
              present.lines[0] == "Alpha [Beta](<#FTNUNIQ1>) Gamma",
          "a footnote this topic prints was not linked: '" +
              (present.lines.empty() ? std::string() : present.lines[0]) + "'");
  require(present.footnote_anchors.size() == 1 &&
              present.footnote_anchors[0] == "FTNUNIQ1",
          "the footnote anchor the link needs was not reported");
}

// A topic with no selector at all is untouched: the pass may not disturb the
// 219 declining topics that name nothing.
void a_topic_without_selectors_is_untouched() {
  DecodedLogicalRecordSource record;
  record.logical_record = 1;
  line(record, {"Alpha", "Beta", "Gamma"});
  refresh(record);
  const std::vector<DecodedLogicalRecordSource> sources{record};
  require(linked(sources) == geist::detail::best_effort_lines(sources, {}),
          "a topic carrying no selector was rewritten");
}

} // namespace

int main() {
  a_selector_marks_the_row_it_precedes();
  a_span_outside_the_row_is_declined();
  a_non_canonical_selector_is_declined();
  a_picture_selector_is_declined();
  a_footnote_target_must_be_printed_here();
  a_topic_without_selectors_is_untouched();
  return 0;
}
