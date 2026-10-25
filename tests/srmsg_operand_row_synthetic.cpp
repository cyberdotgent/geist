// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// An `SRMSG <id>` row in the body of a flattened (non-CZ) prose topic.
//
// AFM41MST `1.3` (`GPCLWS - Close Workstation`) lists the errors a
// subroutine can return as one `SRMSG <number>` row per message:
//
//   57 8    SRMSG   25                           <- a native row of its own
//   57 9    cfont 5 2 2 13 9 2 ...
//   57 10        25      SPECIFIED WORKSTATION DOES NOT EXIST
//   57 11   SRMSG  147
//   57 12   cfont 4 3 2 13 5 2 ...
//   57 13       147      EVENT QUEUE HAS OVERFLOWED
//   57 14
//   57 15   SRMSG
//   57 16      Related Subroutines: GPCRWS, GPDTR, GPOPWS, GPQOPW
//
// Hosted BookServer (DT 19910927164256) serves
// `<a name="MSG   25">     <B>25</B>      <B>SPECIFIED</B> ...</a>`,
// `<a name="MSG  147">    <B>147</B> ...</a>` and
// `<a name="MSG">   Related Subroutines: ...</a>`: the row names the anchor
// and prints nothing; the next drawn line is the message.  The same holds in
// ICHA6MST, DVHESMST, SC31-6805-03, SC34-4335-00, SC41-3552-00, SH19-4049-00
// and SH24-5221-01.
//
// Everything here is synthetic: the test builds whole topic records by hand
// with `assemble_logical_record_with_sources` and opens no book, because
// `libgeist` may depend on no book but `packet.boo` (issue #59).

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::ProseBlockIR;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "srmsg_operand_row_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;

  // One display line: a length byte covering `content`, whose tokens are two
  // bytes each.  The length byte's dictionary spelling is the row sentinel.
  void line(std::vector<TokenWords> content) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(TokenWords{0x25BA});
    for (auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(std::move(token));
    }
  }

  void heading() {
    line({words("sh1.3")});
    line({words("ctopicn"), words("19")});
    line({words("cparent"), words("1.0")});
    line({words("cforwardlevel"), words("1.4")});
    line({words("cbacklevel"), words("1.2")});
    line({words("csummary"), words("38"), words("0"), words("38")});
    line({words("chdlevel"), words(":H3")});
    line({words("csourcefn"), words("AFM41CTL")});
    line({words("ST"), words("Close"), words("Workstation")});
  }

  DecodedLogicalRecordSource build(std::uint32_t logical_record) {
    record.logical_record = logical_record;
    record.assembled =
        geist::detail::assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = logical_record;
    std::uint32_t byte = 1;
    for (std::size_t token = 0; token < record.tokens.size(); ++token) {
      const auto encoded = record.encoded_tokens[token];
      const auto spacing =
          !record.tokens[token].empty() && record.tokens[token].front() < 4;
      record.ir.tokens.push_back(
          {token, encoded, record.tokens[token],
           {byte, static_cast<std::uint32_t>(byte + encoded.width)}, spacing,
           spacing ? record.tokens[token].front() : std::uint16_t{3}});
      byte += encoded.width;
    }
    record.ir.payload_range = {1, byte};
    geist::detail::assign_display_line_framing(record.ir);
    record.control_segments = geist::detail::decode_control_segments(
        record.logical_record, record.assembled, record.encoded_tokens,
        record.ir.display_lines);
    geist::detail::demote_display_line_owned_controls(record);
    return std::move(record);
  }
};

struct Extracted {
  std::vector<DecodedLogicalRecordSource> records;
  std::optional<geist::detail::ProseTopicIR> topic;
  std::string error;
};

Extracted run(DecodedLogicalRecordSource record) {
  Extracted result;
  require(record.ir.display_lines_parse,
          "the synthetic topic's display lines did not parse");
  result.records.push_back(std::move(record));
  const auto layout = geist::detail::extract_layout_ir(result.records);
  std::string ownership_error;
  const auto ownership = geist::detail::build_verified_ownership_ir(
      result.records, layout, &ownership_error);
  require(ownership.has_value(),
          "the synthetic topic's ownership is not verifiable: " +
              ownership_error);
  if (!ownership) return result;
  result.topic = geist::detail::extract_prose_topic_ir(
      result.records, layout, *ownership, "Close Workstation", nullptr,
      &result.error);
  if (result.topic) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(
                result.records, layout, *ownership, "Close Workstation",
                nullptr, *result.topic, &verify_error),
            "the extracted topic failed its own verifier: " + verify_error);
  }
  return result;
}

std::string block_text(const ProseBlockIR &block) {
  std::string text;
  for (const auto &inline_ : block.inlines) text += inline_.text;
  return text;
}

// The index of the first block whose text starts with `prefix`, or npos.
std::size_t block_starting(const geist::detail::ProseTopicIR &topic,
                           const std::string &prefix) {
  for (std::size_t index = 0; index < topic.blocks.size(); ++index)
    if (block_text(topic.blocks[index]).rfind(prefix, 0) == 0) return index;
  return static_cast<std::size_t>(-1);
}

std::vector<const geist::detail::ProseAnchorIR *>
anchors_named(const geist::detail::ProseTopicIR &topic, const std::string &id) {
  std::vector<const geist::detail::ProseAnchorIR *> found;
  for (const auto &anchor : topic.anchors)
    if (anchor.id == id) found.push_back(&anchor);
  return found;
}

// Each `SRMSG <id>` row is the `MSG <id>` anchor of the message line after
// it, and none of the row's words is printed.
void an_operand_row_is_the_message_anchor() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("   "), words("Errors")});
  builder.line({});
  builder.line({words("SRMSG"), words("25")});
  builder.line({words("cfont"), words("5"), words("2"), words("2")});
  builder.line({words("     "), words("25"), words("      "),
                words("SPECIFIED"), words("WORKSTATION"), words("DOES"),
                words("NOT"), words("EXIST")});
  // SC41-3552-00 B.6 `SRMSG COMM 16`: an identity of two words.
  builder.line({words("SRMSG"), words("COMM"), words("16")});
  builder.line({words("   "), words("COMM"), words("16")});
  builder.line({});
  builder.line({words("SRMSG")});
  builder.line({words("   "), words("Related"), words("Subroutines:"),
                words("GPCRWS")});
  auto extracted = run(builder.build(57));
  require(extracted.topic.has_value(),
          "a flattened topic with `SRMSG <id>` rows was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &topic = *extracted.topic;
  for (const auto &block : topic.blocks) {
    const auto text = block_text(block);
    require(text.find("SRMSG") == std::string::npos,
            "the SRMSG row is printed in '" + text + "'");
  }
  const auto first = block_starting(topic, "25");
  const auto second = block_starting(topic, "COMM 16");
  const auto related = block_starting(topic, "Related");
  require(first != static_cast<std::size_t>(-1) &&
              second != static_cast<std::size_t>(-1) &&
              related != static_cast<std::size_t>(-1),
          "a message line is missing from the topic");
  const auto msg25 = anchors_named(topic, "MSG 25");
  const auto comm = anchors_named(topic, "MSG COMM 16");
  const auto bare = anchors_named(topic, "MSG");
  require(msg25.size() == 1 && msg25.front()->position == first,
          "`MSG 25` does not precede its message line");
  require(comm.size() == 1 && comm.front()->position == second,
          "`MSG COMM 16` does not precede its message line");
  require(bare.size() == 1 && bare.front()->position == related,
          "the bare `MSG` does not precede the line after it");
}

// Anchors that follow one another with no drawn line between them all name
// the next drawn line.  SH24-5221-01 4.2 closes each abend code with a bare
// `SRMSG` directly followed by the next code's `SRMSG 002`, and 2.0 opens
// with `SRSPTMSGS` directly followed by `SRMSG 000I`; hosted (DT
// 19930802093837) serves `<a name="SPTMSGS"><a name="MSG 000I"><hr>`, and
// SC41-3552-00 B.6 (DT 19940606215218) `<a name="SPTCOMMMSG"><a
// name="SPTCOMMSG"><a name="MSG COMM 16"><hr>`: every anchor of the run precedes the same row, in source
// order, and none falls to the end of the topic.
void chained_anchors_precede_the_same_line() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("SRSPTMSGS")});
  builder.line({words("SRMSG"), words("001")});
  builder.line({words("   "), words("001"), words("First"), words("code.")});
  builder.line({});
  builder.line({words("SRMSG")});
  builder.line({words("SRMSG"), words("002")});
  builder.line({words("   "), words("002"), words("Second"), words("code.")});
  builder.line({});
  builder.line({words("   "), words("Closing"), words("text.")});
  auto extracted = run(builder.build(46));
  require(extracted.topic.has_value(),
          "chained body anchors were rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &topic = *extracted.topic;
  const auto first = block_starting(topic, "001");
  const auto second = block_starting(topic, "002");
  require(first != static_cast<std::size_t>(-1) &&
              second != static_cast<std::size_t>(-1),
          "a message line is missing from the topic");
  const auto spt = anchors_named(topic, "SPTMSGS");
  const auto msg1 = anchors_named(topic, "MSG 001");
  const auto bare = anchors_named(topic, "MSG");
  const auto msg2 = anchors_named(topic, "MSG 002");
  require(spt.size() == 1 && spt.front()->position == first,
          "`SPTMSGS` does not precede the first message line");
  require(msg1.size() == 1 && msg1.front()->position == first,
          "`MSG 001` does not precede the first message line");
  require(bare.size() == 1 && bare.front()->position == second,
          "the bare `MSG` does not precede the second message line");
  require(msg2.size() == 1 && msg2.front()->position == second,
          "`MSG 002` does not precede the second message line");
  // Source order is kept within the run.
  const auto index_of = [&](const geist::detail::ProseAnchorIR *anchor) {
    return static_cast<std::size_t>(anchor - topic.anchors.data());
  };
  if (spt.size() == 1 && msg1.size() == 1 && bare.size() == 1 &&
      msg2.size() == 1)
    require(index_of(spt.front()) < index_of(msg1.front()) &&
                index_of(bare.front()) < index_of(msg2.front()),
            "chained anchors are out of source order");
}

// The anchor is named after the row exactly as stored: AFM41MST 1.3 record
// 57 line 8 is `SRMSG` + a two-blank word + `25`, the row `SRMSG   25`, and
// hosted (DT 19910927164256) serves `<a name="MSG   25">`; GA27-3832-08 1.4
// `SRMSG 96  to  98` is served as `<a name="MSG 96  to  98">`.
void a_stored_blank_run_stays_in_the_name() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("SRMSG"), words("  "), words("25")});
  builder.line({words("     "), words("25"), words("SPECIFIED")});
  builder.line({words("SRMSG"), words("96"), words(" "), words("to"),
                words(" "), words("98")});
  builder.line({words("     "), words("96"), words("RANGE")});
  auto record = builder.build(58);
  const auto &lines = record.ir.display_lines;
  require(lines.size() > 9 &&
              geist::detail::display_line_text(record, lines[9]) ==
                  "SRMSG   25" &&
              geist::detail::display_line_text(record, lines[11]) ==
                  "SRMSG 96  to  98",
          "the synthetic rows do not spell `SRMSG   25` / `SRMSG 96  to  98`");
  auto extracted = run(std::move(record));
  require(extracted.topic.has_value(),
          "stored blank runs were rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &topic = *extracted.topic;
  require(anchors_named(topic, "MSG   25").size() == 1,
          "`SRMSG   25` is not the anchor `MSG   25`");
  require(anchors_named(topic, "MSG 96  to  98").size() == 1,
          "`SRMSG 96  to  98` is not the anchor `MSG 96  to  98`");
}

}  // namespace

int main() {
  a_stored_blank_run_stays_in_the_name();
  an_operand_row_is_the_message_anchor();
  chained_anchors_precede_the_same_line();
  return 0;
}
