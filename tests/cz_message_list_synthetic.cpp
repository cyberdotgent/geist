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

// The last message topic of a list closes a list an earlier topic opened.
// BookServer reads `CZ OFF EMSGL` as the generic `CZ OFF E` closer, which
// prints nothing and changes nothing when no region is open: GC31-8601-00
// `1.1.3` (DT 19970728124306) ends `cz FLOW DEST 3 3`, `   Procedure
// Name:`, `cz OFF EMSGL 0 0`, and hosted serves `<B>Procedure</B>
// <B>Name:</B>` and then nothing but the page footer.
void a_closer_of_an_earlier_topics_list_is_inert() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("cz"), words("FLOW"), words("XPL"), words("3"),
                words("3")});
  builder.line({words("   "), words("Explanation:"), words(" "),
                words("Connection"), words("active.")});
  builder.line({words("SRMSG")});
  builder.line({words("cz"), words("OFF"), words("EMSGL"), words("0"),
                words("0")});
  auto extracted = run(builder.build(104));
  require(extracted.topic.has_value(),
          "a `cz OFF EMSGL` closing an earlier topic's list was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 1 &&
              block_text(blocks[0]) == "Explanation: Connection active.",
          "the closer of an earlier topic's list drew something");
}

// A `cz FLOW` tag that matches none of BookServer's specific `CZ FLOW ..`
// prefixes is the generic `CZ FLOW`: the open `<pre>` closes and the rows
// flow.  SC31-8046-00 `1.1.4` (DT 19950610061004) ends its message with
// `cz FLOW SYSACT 3 3` and `cz FLOW NUMBYTES 3 3` + `   Related Commands:
// LINEMAP`, and hosted serves both blocks as flowing text after `</pre>`.
void a_generic_flow_tag_is_a_paragraph() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("cz"), words("FLOW"), words("SYSACT"), words("3"),
                words("3")});
  builder.line({words("   "), words("System"), words("Action:"), words(" "),
                words("Processing"), words("continues.")});
  builder.line({words("cz"), words("FLOW"), words("NUMBYTES"), words("3"),
                words("3")});
  builder.line({words("   "), words("Related"), words("Commands:"),
                words(" "), words("LINEMAP")});
  auto extracted = run(builder.build(77));
  require(extracted.topic.has_value(),
          "a `cz FLOW NUMBYTES` block was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 2 &&
              blocks[1].kind == ProseBlockKindIR::paragraph &&
              block_text(blocks[1]) == "Related Commands: LINEMAP",
          "the NUMBYTES block is not the paragraph hosted serves");
}

// The code list `cz FLOW CODEL` .. `cz OFF ECODEL` has no string of its own
// in BookServer either: the opener is the generic `CZ FLOW` and the closer
// the generic `CZ OFF E`, which prints nothing.  GC33-1177-01 `2.3.34` (DT
// 19990318184419) ends `cz FLOW MODULE 3 4` `    Module:  DFHCPCOJ`, then
// `cz OFF ECODEL 0 0` (the list an earlier topic opened) and a row-less
// `cz FLOW CODEL 3 3` (the next topic's), and hosted serves nothing after
// `<B>Module:</B>  DFHCPCOJ`.  An opener with rows of its own flows them:
// dfhcag49 `3.4` opens its list with `cz FLOW CODEL 3 3` and the lead-in
// `Whenever a CICS system dump is requested, ...`.
void a_code_list_is_the_generic_flow_and_closer() {
  {
    RecordBuilder builder;
    builder.heading();
    builder.line({words("cz"), words("FLOW"), words("MODULE"), words("3"),
                  words("4")});
    builder.line({words("    "), words("Module:"), words(" "),
                  words("DFHCPCOJ")});
    builder.line({words("cz"), words("OFF"), words("ECODEL"), words("0"),
                  words("0")});
    builder.line({words("cz"), words("FLOW"), words("CODEL"), words("3"),
                  words("3")});
    auto extracted = run(builder.build(6139));
    require(extracted.topic.has_value(),
            "a trailing `cz OFF ECODEL` / `cz FLOW CODEL` pair was rejected: " +
                extracted.error);
    if (extracted.topic)
      require(extracted.topic->blocks.size() == 1 &&
                  block_text(extracted.topic->blocks[0]) == "Module: DFHCPCOJ",
              "the code-list pair drew something");
  }
  {
    RecordBuilder builder;
    builder.heading();
    builder.line({words("cz"), words("BREAK"), words("3")});
    builder.line({words("cz"), words("FLOW"), words("CODEL"), words("3"),
                  words("3")});
    builder.line({words("    "), words("Whenever"), words("a"),
                  words("dump"), words("is"), words("requested.")});
    builder.line({words("cz"), words("FLOW"), words("P"), words("4"),
                  words("4")});
    builder.line({words("    "), words("In"), words("most"), words("cases.")});
    builder.line({words("cz"), words("OFF"), words("ECODEL"), words("0"),
                  words("0")});
    builder.line({words("cz"), words("FLOW"), words("H2"), words("3"),
                  words("3")});
    auto extracted = run(builder.build(14063));
    require(extracted.topic.has_value(),
            "a `cz FLOW CODEL` lead-in was rejected: " + extracted.error);
    if (extracted.topic)
      require(extracted.topic->blocks.size() == 2 &&
                  block_text(extracted.topic->blocks[0]) ==
                      "Whenever a dump is requested." &&
                  extracted.topic->blocks[0].kind ==
                      ProseBlockKindIR::paragraph,
              "the code list's lead-in is not a paragraph");
  }
}

// A message inside a code list.  The full-width rule under its `SRMSG <id>`
// row is the page's `<hr>` wherever it stands, not only in a message list:
// SC31-6222-04 `2.1` (DT 19961011013500) opens `cz FLOW CODEL 3 3`, a
// `cz FLOW XPL` row, then `SRMSG 0801 0000` and the rule, served
// `<a name="MSG 0801 0000"><hr>` and the headline.
void a_message_rule_in_a_code_list_is_a_rule() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("cz"), words("BREAK"), words("3")});
  builder.line({words("cz"), words("FLOW"), words("CODEL"), words("3"),
                words("3")});
  builder.message("IST0801", "Request reject.");
  builder.line({words("cz"), words("OFF"), words("ECODEL"), words("0"),
                words("0")});
  auto extracted = run(builder.build(1430));
  require(extracted.topic.has_value(),
          "a message rule inside a code list was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &topic = *extracted.topic;
  require(!topic.blocks.empty() &&
              topic.blocks[0].kind == ProseBlockKindIR::thematic_break,
          "the message rule is not the topic's first block");
  const auto anchor = std::find_if(
      topic.anchors.begin(), topic.anchors.end(),
      [](const auto &candidate) { return candidate.id == "MSG IST0801"; });
  require(anchor != topic.anchors.end() && anchor->position == 0,
          "`MSG IST0801` does not precede the rule");
}

// The same rule among the rows before the first directive: GC33-1694-01
// `1.4.1` (DT 19990719095008) serves `<hr>` under its message heading,
// ahead of the `DFH0100` headline and the `cz FLOW XPL` block.
void a_leading_full_width_rule_is_a_rule() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("   "), TokenWords(63, 0x2500), TokenWords{1},
                TokenWords(11, 0x2500)});
  builder.line({words("   "), words("DFH0100"), words("Initialized.")});
  builder.line({words("cz"), words("FLOW"), words("XPL"), words("3"),
                words("4")});
  builder.line({words("    "), words("Explanation:"), words("Done.")});
  auto extracted = run(builder.build(85));
  require(extracted.topic.has_value(),
          "a leading full-width rule was rejected: " + extracted.error);
  if (!extracted.topic) return;
  const auto &blocks = extracted.topic->blocks;
  require(blocks.size() == 3 &&
              blocks[0].kind == ProseBlockKindIR::thematic_break &&
              block_text(blocks[1]) == "DFH0100 Initialized.",
          "the leading rule is not a rule block before the headline");
}

// A closer whose list this topic did open, but closed already, still
// declines: the source then closes a list twice.
void a_second_closer_of_a_closed_list_declines() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("cz"), words("BREAK"), words("3")});
  builder.line({words("cz"), words("FLOW"), words("MSGL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("OFF"), words("MSGL")});
  builder.message("LTC0008", "Bad value.");
  builder.line({words("cz"), words("OFF"), words("EMSGL"), words("0"),
                words("0")});
  builder.line({words("cz"), words("OFF"), words("EMSGL"), words("0"),
                words("0")});
  auto extracted = run(builder.build(104));
  require(!extracted.topic.has_value(),
          "a second `cz OFF EMSGL` of a closed list was admitted");
}

// The rule under an `SRMSG <id>` row is a full-width rule, the page's
// `<hr>` wherever it stands (outside `cz OFF TABLE`), so it is a block
// outside a message list too and the anchor precedes it.  Hosted
// GC33-1694-01 and ICHA6MST `1.2` serve such rules as
// `<a name="MSG ..."><hr></a>` with no message list around them.
void a_message_anchor_outside_a_message_list_precedes_its_rule() {
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
  require(extracted.topic.has_value(),
          "an `SRMSG <id>` rule outside a message list was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &topic = *extracted.topic;
  require(topic.blocks.size() == 3 &&
              topic.blocks[1].kind == ProseBlockKindIR::thematic_break,
          "the rule outside a message list is not a rule block");
  const auto anchor = std::find_if(
      topic.anchors.begin(), topic.anchors.end(),
      [](const auto &candidate) { return candidate.id == "MSG LTC0008"; });
  require(anchor != topic.anchors.end() && anchor->position == 1,
          "`MSG LTC0008` does not precede the rule");
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
  // GC33-1177-01 `1.81.54` ends its message with `cz OFF EMSGL 0 0` (the
  // list an earlier topic opened), then `cz FLOW MSGL 3 3`, `cz OFF MSGL`
  // and an empty `cz FLOW P 4 4`: the announcement of the next topic's
  // list followed by a paragraph opener that owns no row.  Hosted (DT
  // 19990318184419) serves `<p><p></pre><pre width="80"><!-- * -->`,
  // `</pre>` and nothing visible after the last message row.
  const auto empty_tail = [] {
    RecordBuilder builder;
    builder.heading();
    builder.line({words("cz"), words("FLOW"), words("XPL"), words("3"),
                  words("4"), words("   "), words("Explanation:"),
                  words("Failed.")});
    builder.line({words("cz"), words("OFF"), words("EMSGL"), words("0"),
                  words("0")});
    builder.line({words("cz"), words("FLOW"), words("MSGL"), words("3"),
                  words("3")});
    builder.line({words("cz"), words("OFF"), words("MSGL")});
    builder.line({words("cz"), words("FLOW"), words("P"), words("4"),
                  words("4")});
    return builder.build(1081);
  };
  auto tail = run(empty_tail());
  require(tail.topic.has_value(),
          "an announcement followed by a row-less directive was rejected: " +
              tail.error);
  if (tail.topic)
    require(tail.topic->blocks.size() == 1,
            "the announcement's row-less tail drew a block");
  auto stray = run(topic(true));
  require(!stray.topic.has_value(),
          "a row after an unclosed message-list announcement was admitted");
}

}  // namespace

// Messages listed by headline alone: eyua1b01 `3.43` opens a message list
// and writes `SRMSG <id>`, `cz OFF MSG` and the headline row for each of its
// EYUVE messages, one after the other, then `SRMSG` and
// `cz OFF EMSGL 0 0`.  Each `CZ OFF MSG` after the first only deepens the
// reader's region, and the list closer, the generic `CZ OFF E`, ends it.
void headline_only_messages_end_at_the_list_closer() {
  RecordBuilder builder;
  builder.heading();
  builder.line({words("cz"), words("FLOW"), words("MSGL"), words("3"),
                words("3")});
  builder.line({words("cz"), words("OFF"), words("MSGL")});
  builder.line({words("SRMSG"), words("EYUVE1001E")});
  builder.line({words("cz"), words("OFF"), words("MSG")});
  builder.line({words("   "), words("EYUVE1001E"), words("  "),
                words("Editor"), words("error.")});
  builder.line({words("SRMSG"), words("EYUVE1002E")});
  builder.line({words("cz"), words("OFF"), words("MSG")});
  builder.line({words("   "), words("EYUVE1002E"), words("  "),
                words("Invalid"), words("request.")});
  builder.line({words("SRMSG")});
  builder.line({words("cz"), words("OFF"), words("EMSGL"), words("0"),
                words("0")});
  auto extracted = run(builder.build(1800));
  require(extracted.topic.has_value(),
          "headline-only messages closed by EMSGL were rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  std::string text;
  for (const auto &block : extracted.topic->blocks)
    text += block_text(block) + "\n";
  require(text.find("Editor") != std::string::npos &&
              text.find("request.") != std::string::npos,
          "a headline lost its words:\n" + text);
}

int main() {
  headline_only_messages_end_at_the_list_closer();
  a_message_list_is_typed();
  a_body_block_outside_a_message_list_is_a_paragraph();
  a_closer_of_an_earlier_topics_list_is_inert();
  a_generic_flow_tag_is_a_paragraph();
  a_code_list_is_the_generic_flow_and_closer();
  a_message_rule_in_a_code_list_is_a_rule();
  a_leading_full_width_rule_is_a_rule();
  a_second_closer_of_a_closed_list_declines();
  a_message_anchor_outside_a_message_list_precedes_its_rule();
  a_message_list_opener_without_its_off_declines();
  back_to_back_lists_keep_the_closing_anchor_in_place();
  a_trailing_announcement_draws_nothing();
  return 0;
}
