// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The `CZ` message list: `cz FLOW MSGL` .. `cz OFF EMSGL`.
//
// GX27-3999-00 `B.0` (`Appendix B. NDIS MAC Driver Messages`) lists its
// messages inside one message list:
//
//   104 5   cz FLOW MSGL 3 3
//   104 6   cz OFF MSGL
//   104 7   SRMSG LTC0008                      <- a native row of its own
//   104 8      ________________________ ...    <- U+2500 separator row
//   104 9   cfont 3 7 2 11 3 2 ...
//   105 0      LTC0008 The value specified for the parameter xx in ...
//   105 4   cz FLOW PROBD 3 10
//   105 5   cfont 10 12 2
//   105 6             Explanation:  The value provided for the parameter ...
//   105 9   cz FLOW ORESP 3 10
//   105 11            User Action:  Edit the PROTOCOL.INI file ...
//   105 16  SRMSG LTC0009
//   ...
//   120 1   SRMSG
//   120 2   cz OFF EMSGL 0 0
//
// Hosted BookServer (DT 19950730184057) serves each message as
// `<a name="MSG LTC0008"><hr></a>`, the headline, then the two body blocks
// as paragraphs whose `CFONT`-bold labels read `<B>Explanation:</B>` and
// `<B>User</B> <B>Action:</B>`.  Neither `SRMSG` nor `LTC0008` on the anchor
// row is printed.  SC31-6242-02 `APPENDIX1.2.1` (DT 19950803212024) serves
// the `XPL`, `URESP`, `MODULE` and `SEVERITY` body blocks of its list the
// same way.
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
using geist::detail::ProseBlockKindIR;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "cz_message_list_synthetic: " << message << '\n';
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
    line({words("shb.0")});
    line({words("ctopicn"), words("34")});
    line({words("cparent")});
    line({words("cforwardlevel"), words("BACK_1")});
    line({words("cbacklevel"), words("A.0")});
    line({words("csummary"), words("520"), words("0"), words("520")});
    line({words("chdlevel"), words(":H1")});
    line({words("csourcefn"), words("ALY0APB")});
    line({words("ST"), words("Messages")});
  }

  // One message: its `SRMSG <id>` row, the separator rule, the headline and
  // the two body blocks.
  void message(const std::string &id, const std::string &headline) {
    line({words("SRMSG"), words(id)});
    // GX27-3999-00 record 104 tokens 59-62: three blanks, a 63-cell rule
    // run, a bare spacing token and an 11-cell run.
    line({words("   "), TokenWords(63, 0x2500), TokenWords{1},
          TokenWords(11, 0x2500)});
    line({words("cfont"), words("3"), words("7"), words("2")});
    line({words("   "), words(id), words(headline)});
    line({words("cz"), words("FLOW"), words("PROBD"), words("3"), words("10")});
    line({words("cfont"), words("10"), words("12"), words("2")});
    line({words("          "), words("Explanation:"), words(" "),
          words("An"), words("internal"), words("error.")});
    line({words("cz"), words("FLOW"), words("ORESP"), words("3"), words("10")});
    line({words("cfont"), words("10"), words("4"), words("2"), words("15"),
          words("7"), words("2")});
    line({words("          "), words("User"), words("Action:"), words(" "),
          words("Call"), words("service.")});
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
      result.records, layout, *ownership, "Messages", nullptr, &result.error);
  if (result.topic) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(
                result.records, layout, *ownership, "Messages", nullptr,
                *result.topic, &verify_error),
            "the extracted topic failed its own verifier: " + verify_error);
  }
  return result;
}

std::string block_text(const ProseBlockIR &block) {
  std::string text;
  for (const auto &inline_ : block.inlines) text += inline_.text;
  return text;
}

DecodedLogicalRecordSource message_list_topic() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("cz"), words("BREAK"), words("3")});
  builder.line({words("cz"), words("FLOW"), words("MSGL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("OFF"), words("MSGL")});
  builder.message("LTC0008", "Bad value.");
  builder.message("LTC0009", "Unrecoverable error.");
  builder.line({words("SRMSG")});
  builder.line({words("cz"), words("OFF"), words("EMSGL"), words("0"),
                words("0")});
  return builder.build(104);
}

// Each message is its `MSG <id>` anchor, a rule, the headline paragraph and
// one paragraph per body block; the closing bare `SRMSG` is the `MSG`
// anchor at the end.
void a_message_list_is_typed() {
  auto extracted = run(message_list_topic());
  require(extracted.topic.has_value(),
          "a CZ message list was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &topic = *extracted.topic;
  const std::vector<ProseBlockKindIR> kinds = {
      ProseBlockKindIR::thematic_break, ProseBlockKindIR::paragraph,
      ProseBlockKindIR::paragraph,      ProseBlockKindIR::paragraph,
      ProseBlockKindIR::thematic_break, ProseBlockKindIR::paragraph,
      ProseBlockKindIR::paragraph,      ProseBlockKindIR::paragraph};
  require(topic.blocks.size() == kinds.size(),
          "the message list has " + std::to_string(topic.blocks.size()) +
              " blocks, not " + std::to_string(kinds.size()));
  if (topic.blocks.size() != kinds.size()) return;
  for (std::size_t index = 0; index < kinds.size(); ++index)
    require(topic.blocks[index].kind == kinds[index],
            "block " + std::to_string(index) + " has the wrong kind");
  require(block_text(topic.blocks[1]) == "LTC0008 Bad value.",
          "the first headline reads '" + block_text(topic.blocks[1]) + "'");
  require(block_text(topic.blocks[2]) == "Explanation: An internal error.",
          "the PROBD block reads '" + block_text(topic.blocks[2]) + "'");
  require(block_text(topic.blocks[3]) == "User Action: Call service.",
          "the ORESP block reads '" + block_text(topic.blocks[3]) + "'");
  require(block_text(topic.blocks[5]) == "LTC0009 Unrecoverable error.",
          "the second headline reads '" + block_text(topic.blocks[5]) + "'");

  const auto anchor = [&](const std::string &id) {
    return std::find_if(topic.anchors.begin(), topic.anchors.end(),
                        [&](const auto &candidate) {
                          return candidate.id == id;
                        });
  };
  const auto first = anchor("MSG LTC0008");
  const auto second = anchor("MSG LTC0009");
  const auto closing = anchor("MSG");
  require(first != topic.anchors.end() && first->position == 0,
          "`MSG LTC0008` does not precede the first message's rule");
  require(second != topic.anchors.end() && second->position == 4,
          "`MSG LTC0009` does not precede the second message's rule");
  require(closing != topic.anchors.end() &&
              closing->position == topic.blocks.size(),
          "the closing bare SRMSG is not the `MSG` anchor at the end");
}

// A message body tag names no structure of its own.  BookServer matches a
// control line by literal prefix, and `CZ FLOW PROBD` (like `XPL`,
// `SYSACT`, `URESP` ...) matches only the generic `CZ FLOW`: the open `<pre>`
// closes and the rows flow as one paragraph, inside a message list or not.
// GC33-1694-01 `1.4.1` (DT 19990719095008) carries no `cz FLOW MSGL` at all
// and serves its `cz FLOW XPL 3 4` rows as
// `</pre>    <B>Explanation:</B>  The CICS subsystem ...`.
void a_body_block_outside_a_message_list_is_a_paragraph() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("cz"), words("BREAK"), words("3")});
  builder.line({words("cz"), words("FLOW"), words("PROBD"), words("3"),
                words("10")});
  builder.line({words("          "), words("Explanation:"), words(" "),
                words("An"), words("internal"), words("error.")});
  builder.line({words("cz"), words("FLOW"), words("SYSACT"), words("3"),
                words("10")});
  builder.line({words("          "), words("System"), words("Action:"),
                words(" "), words("None.")});
  auto extracted = run(builder.build(104));
  require(extracted.topic.has_value(),
          "a `cz FLOW PROBD` outside a message list was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 2 &&
              blocks[0].kind == ProseBlockKindIR::paragraph &&
              blocks[1].kind == ProseBlockKindIR::paragraph,
          "the two body blocks are not two paragraphs");
  if (blocks.size() != 2) return;
  require(block_text(blocks[0]) == "Explanation: An internal error.",
          "the PROBD block reads '" + block_text(blocks[0]) + "'");
  require(block_text(blocks[1]) == "System Action: None.",
          "the SYSACT block reads '" + block_text(blocks[1]) + "'");
}

// The rule under an `SRMSG <id>` row is a block only inside a message list.
// Outside one the anchor would have no row to precede, so the topic declines
// rather than moving the anchor.
void a_message_anchor_outside_a_message_list_declines() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                words("3"), words("   "), words("Messages"),
                words("follow.")});
  builder.line({words("SRMSG"), words("LTC0008")});
  builder.line({words("   "), TokenWords(63, 0x2500), TokenWords{1},
                TokenWords(11, 0x2500)});
  builder.line({words("   "), words("LTC0008"), words("Bad"),
                words("value.")});
  auto extracted = run(builder.build(104));
  require(!extracted.topic.has_value(),
          "an `SRMSG <id>` rule outside a message list was admitted");
}

// The opener is always followed by its bare `cz OFF MSGL`; without it the
// list boundary is not the one the source writes.
void a_message_list_opener_without_its_off_declines() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("cz"), words("BREAK"), words("3")});
  builder.line({words("cz"), words("FLOW"), words("MSGL"), words("3"),
                words("3")});
  builder.message("LTC0008", "Bad value.");
  builder.line({words("cz"), words("OFF"), words("EMSGL"), words("0"),
                words("0")});
  auto extracted = run(builder.build(104));
  require(!extracted.topic.has_value(),
          "a `cz FLOW MSGL` not followed by `cz OFF MSGL` was admitted");
}

// SC26-7132-01 2.3.2 closes each message list with a bare `SRMSG` and opens
// the next list at once (record 256 segment 24 .. record 257 segment 3).
// Hosted (DT 19980422095213) serves `<a name="MSG"></a>` directly before
// `<a name="MSG ILU901A"><hr>`: both anchors precede the next message's rule.
void back_to_back_lists_keep_the_closing_anchor_in_place() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("cz"), words("BREAK"), words("3")});
  for (const auto *id : {"ILU900A", "ILU901A"}) {
    builder.line({words("cz"), words("FLOW"), words("MSGL"), words("3"),
                  words("3")});
    builder.line({words("cz"), words("OFF"), words("MSGL")});
    builder.message(id, "Code overlaid.");
    builder.line({words("SRMSG")});
    builder.line({words("cz"), words("OFF"), words("EMSGL"), words("0"),
                  words("0")});
  }
  auto extracted = run(builder.build(256));
  require(extracted.topic.has_value(),
          "back-to-back message lists were rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &anchors = extracted.topic->anchors;
  require(anchors.size() == 4, "the topic has " +
                                   std::to_string(anchors.size()) +
                                   " anchors, not 4");
  if (anchors.size() != 4) return;
  require(anchors[0].id == "MSG ILU900A" && anchors[0].position == 0,
          "`MSG ILU900A` does not precede the first rule");
  require(anchors[1].id == "MSG" && anchors[1].position == 4,
          "the first list's closing `MSG` does not precede the second "
          "message's rule; it stands at block " +
              std::to_string(anchors[1].position));
  require(anchors[2].id == "MSG ILU901A" && anchors[2].position == 4,
          "`MSG ILU901A` does not precede the second rule");
  require(anchors[3].id == "MSG" &&
              anchors[3].position == extracted.topic->blocks.size(),
          "the last closing `MSG` is not at the end");
}

// A list announced at the end of a menu topic (GC35-0271-02 6.0) is closed
// by a later topic and draws nothing here.  Anything after the announcement
// would belong to a list this topic never closes.
void a_trailing_announcement_draws_nothing() {
  const auto topic = [](bool trailing_row) {
    RecordBuilder builder;
    builder.heading();
    builder.line({words("cz"), words("FLOW"), words("P"), words("3"),
                  words("3"), words("   "), words("Messages"),
                  words("follow.")});
    builder.line({words("cz"), words("BREAK"), words("3")});
    builder.line({words("cz"), words("FLOW"), words("MSGL"), words("3"),
                  words("3")});
    builder.line({words("cz"), words("OFF"), words("MSGL")});
    if (trailing_row) builder.line({words("   "), words("Stray.")});
    return builder.build(628);
  };
  auto announced = run(topic(false));
  require(announced.topic.has_value(),
          "a trailing message-list announcement was rejected: " +
              announced.error);
  if (announced.topic)
    require(announced.topic->blocks.size() == 1,
            "the trailing announcement drew a block of its own");
  auto stray = run(topic(true));
  require(!stray.topic.has_value(),
          "a row after an unclosed message-list announcement was admitted");
}

}  // namespace

int main() {
  a_message_list_is_typed();
  a_body_block_outside_a_message_list_is_a_paragraph();
  a_message_anchor_outside_a_message_list_declines();
  a_message_list_opener_without_its_off_declines();
  back_to_back_lists_keep_the_closing_anchor_in_place();
  a_trailing_announcement_draws_nothing();
  return 0;
}
