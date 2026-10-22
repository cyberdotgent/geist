// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// `SRELIS<owner>` closes the list item `SRLIS<owner>` opened; it is no anchor.
//
// SC24-5520-00 `1.1.27` (DT 19911011135123) opens one list-item anchor and
// closes it five times:
//
//   218 line 24  SRLISvmcparm
//   225 line 4   SRELISvmcparm          <- `02` length byte, one two-byte word
//   233 line 18  SRELISvmcparm          <- the next item's text follows it
//   ...
//
// Hosted BookServer serves `<a name="LISvmcparm">` once and no `ELISvmcparm`
// anchor anywhere, so a closer emitting one wrote the same destination id
// five times.  The closer claims its own token as a control, contributes no
// anchor and no text, and any payload the flattened segment carries after it
// is ordinary prose.  The closer may stand before its opener.  A closer
// whose owner the topic never names is not proven to be one and fails
// closed.
//
// Everything here is synthetic: the test builds whole topic records by hand.

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::ProseBlockKindIR;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "list_closer_anchor_synthetic: " << message << '\n';
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

  // The eight metadata controls and the `ST` title every prose topic opens
  // with.
  void heading(const std::string &id, const std::string &title_word) {
    line({words("sh" + id)});
    line({words("ctopicn"), words("7")});
    line({words("cparent"), words("1.0")});
    line({words("cforwardlevel"), words("1.2")});
    line({words("cbacklevel"), words("1.0")});
    line({words("csummary"), words("9"), words("0"), words("9")});
    line({words("chdlevel"), words(":H2")});
    line({words("csourcefn"), words("ALY0FRO")});
    line({words("ST"), words(title_word)});
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

Extracted run(DecodedLogicalRecordSource record, const std::string &title) {
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
      result.records, layout, *ownership, title, nullptr, &result.error);
  if (result.topic) {
    std::string verify_error;
    require(geist::detail::verify_prose_topic_ir(
                result.records, layout, *ownership, title, nullptr,
                *result.topic, &verify_error),
            "the extracted topic failed its own verifier: " + verify_error);
  }
  return result;
}

DecodedLogicalRecordSource list_topic(const std::string &opener,
                                      const std::string &closer,
                                      bool closer_payload) {
  RecordBuilder builder;
  builder.heading("1.1.27", "Diagnose");
  builder.line({words("   "), words("Some"), words("introduction.")});
  builder.line({words(opener)});
  builder.line({words("   "), words("2."), words("The"), words("first"),
                words("item.")});
  if (closer_payload)
    builder.line({words(closer), words("   "), words("5."), words("Closing"),
                  words("payload.")});
  else
    builder.line({words(closer)});
  builder.line({words("   "), words("3."), words("The"), words("second"),
                words("item.")});
  builder.line({words(closer)});
  return builder.build(218);
}

std::size_t anchors_named(const geist::detail::ProseTopicIR &topic,
                          const std::string &id) {
  std::size_t count = 0;
  for (const auto &anchor : topic.anchors)
    if (anchor.id == id) ++count;
  return count;
}

std::string topic_text(const geist::detail::ProseTopicIR &topic) {
  std::string text;
  for (const auto &block : topic.blocks)
    for (const auto &item : block.inlines) text += item.text + " ";
  return text;
}

void closers_emit_no_anchor(bool closer_payload) {
  const auto label =
      std::string(closer_payload ? "payload" : "bare") + " closer: ";
  auto extracted =
      run(list_topic("SRLISvmcparm", "SRELISvmcparm", closer_payload),
          "Diagnose");
  require(extracted.topic.has_value(),
          label + "a topic closing its SRLIS list was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &topic = *extracted.topic;
  require(anchors_named(topic, "LISvmcparm") == 1,
          label + "the SRLIS opener lost its anchor or its exact spelling");
  require(anchors_named(topic, "ELISvmcparm") == 0,
          label + "an SRELIS closer became an anchor");
  require(topic.anchors.size() == 1,
          label + "the topic has " + std::to_string(topic.anchors.size()) +
              " anchors, not the opener alone");
  const auto text = topic_text(topic);
  require(text.find("ELIS") == std::string::npos,
          label + "the closer leaked into the text: '" + text + "'");
  require(text.find("second") != std::string::npos,
          label + "the item after the closer was lost: '" + text + "'");
  if (closer_payload)
    require(text.find("Closing") != std::string::npos &&
                text.find("payload.") != std::string::npos,
            label + "the closer's payload did not stay prose: '" + text + "'");
  // The closer's own word is claimed as a control, never as text.
  std::size_t closer_controls = 0;
  for (const auto &disposition : topic.ledger) {
    const auto &record = extracted.records.front();
    if (disposition.token.logical_record != record.logical_record) continue;
    const auto &word = record.tokens[disposition.token.token_index];
    if (std::string(word.begin(), word.end()) != "SRELISvmcparm") continue;
    require(disposition.role == geist::detail::ProseTokenRoleIR::control,
            label + "the closer token was not claimed as a control");
    ++closer_controls;
  }
  require(closer_controls == 2,
          label + "expected two claimed closer tokens, found " +
              std::to_string(closer_controls));
}

// The owner must match: closing a list the topic never opened is not proven.
// GX27-3999-00 2.2 (DT 19950730184057) stores `SRELISamy` three rows before
// `SRLISamy`; hosted serves `LISamy` alone, so the marker still names the item.
void a_closer_before_its_opener_is_no_anchor() {
  RecordBuilder builder;
  builder.heading("2.2", "Diagnose");
  builder.line({words("   "), words("Begin"), words("with"), words("step.")});
  builder.line({words("SRELISamy")});
  builder.line({words("SRLISamy")});
  builder.line({words("   "), words("1."), words("If"), words("your"),
                words("computer.")});
  auto extracted = run(builder.build(27), "Diagnose");
  require(extracted.topic.has_value(),
          "a closer standing before its opener was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  require(anchors_named(*extracted.topic, "LISamy") == 1 &&
              extracted.topic->anchors.size() == 1,
          "a closer before its opener emitted an anchor or lost the opener");
}

void an_unmatched_closer_declines() {
  auto extracted =
      run(list_topic("SRLISvmcparm", "SRELISother", false), "Diagnose");
  require(!extracted.topic.has_value(),
          "an SRELIS closer whose owner opened no list was admitted");
}

// An owner may begin with `t`, so the opener spells `SRLIST<rest>` and the
// closer `SRELIST<rest>`.  Those prefixes are reserved structural opcodes,
// but no `SRLIST`/`SRELIST` control has ever been observed; the pair of an
// opener and a closer naming the same owner is what proves the list item.
// FA1PLMM0 D.0 record 817 (`SRLIStemppar` line .. `SRELIStemppar`),
// SC24-5520-00 1.1.7 records 61/62 (`SRLIStimzon` .. `SRELIStimzon`) and
// OFCUSEOV 3.1.1 record 516 (`SRLIStres` .. `SRELIStres`); hosted BookServer
// serves `<a name="LIStemppar">` (DT 19910927114801), `LIStimzon`
// (DT 19911011135123) and `LIStres` (DT 19900805103816), each wrapping the
// item's first row, and no `ELIS*` anchor.
void an_owner_spelled_with_t_is_a_list_item(bool closer_payload) {
  const auto label =
      std::string(closer_payload ? "payload" : "bare") + " t-owner closer: ";
  auto extracted =
      run(list_topic("SRLIStimzon", "SRELIStimzon", closer_payload),
          "Diagnose");
  require(extracted.topic.has_value(),
          label + "a topic opening SRLIStimzon was rejected: " +
              extracted.error);
  if (!extracted.topic) return;
  const auto &topic = *extracted.topic;
  require(anchors_named(topic, "LIStimzon") == 1 && topic.anchors.size() == 1,
          label + "the SRLIStimzon opener is not the topic's one anchor");
  const auto text = topic_text(topic);
  require(text.find("LIS") == std::string::npos,
          label + "a list control leaked into the text: '" + text + "'");
  require(text.find("second") != std::string::npos,
          label + "the item after the closer was lost: '" + text + "'");
}

// Without its partner the reserved spelling is not proven to be a list item:
// an `SRLIST<rest>` no `SRELIST<rest>` closes, and an `SRELIST<rest>` no
// `SRLIST<rest>` opens, still decline.
void an_unpaired_t_owner_declines() {
  {
    RecordBuilder builder;
    builder.heading("3.1.1", "Diagnose");
    builder.line({words("   "), words("Begin"), words("with"), words("step.")});
    builder.line({words("SRLIStres")});
    builder.line({words("   "), words("3."), words("On"), words("the"),
                  words("menu.")});
    auto extracted = run(builder.build(516), "Diagnose");
    require(!extracted.topic.has_value(),
            "an SRLIST<rest> opener with no closer was admitted");
  }
  {
    auto extracted =
        run(list_topic("SRLISvmcparm", "SRELIStres", false), "Diagnose");
    require(!extracted.topic.has_value(),
            "an SRELIST<rest> closer with no opener was admitted");
  }
}

// A lone `SRLIST<rest>` opener whose anchor an in-book selector of the same
// topic targets is a list item too: the selector is independent proof that
// `LIST<rest>` names a destination of this topic.  SC21-8295-03 `E.0` record
// 840 segment 13 is `SRLISTWOB`, with no `SRELISTWOB` anywhere, and records
// 837 and 840 carry `cselect 69 2 LISTWOB` and `cselect 52 2 LISTWOB` over
// `2b` ("go to Step 2b").  Hosted BookServer (`SC21-829`, DT 19970409135933)
// serves `<a name="LISTWOB">` around item b's first row and both selectors as
// `href="E.0?...#LISTWOB"`.
DecodedLogicalRecordSource targeted_t_owner_topic(const std::string &target) {
  RecordBuilder builder;
  builder.heading("E.0", "Install");
  builder.line({words("cselect"), words("14"), words("2"), words(target)});
  builder.line({words("   "), words("go"), words("to"), words("Step"),
                words("2b.")});
  builder.line({words("SRLISTWOB")});
  builder.line({words("   "), words("b."), words("Determine"), words("the"),
                words("space.")});
  return builder.build(840);
}

void a_targeted_t_owner_opener_is_a_list_item() {
  auto extracted = run(targeted_t_owner_topic("LISTWOB"), "Install");
  require(extracted.topic.has_value(),
          "a targeted SRLISTWOB opener was rejected: " + extracted.error);
  if (!extracted.topic) return;
  require(anchors_named(*extracted.topic, "LISTWOB") == 1,
          "the targeted SRLISTWOB opener is not an anchor");
  const auto text = topic_text(*extracted.topic);
  require(text.find("SRLIS") == std::string::npos &&
              text.find("Determine") != std::string::npos,
          "the list item text is wrong: '" + text + "'");
}

// A selector naming some other destination proves nothing about the opener.
void an_untargeted_t_owner_opener_still_declines() {
  auto extracted = run(targeted_t_owner_topic("LISTWOC"), "Install");
  require(!extracted.topic.has_value(),
          "an SRLISTWOB opener no selector targets was admitted");
}

}  // namespace

int main() {
  an_owner_spelled_with_t_is_a_list_item(false);
  an_owner_spelled_with_t_is_a_list_item(true);
  an_unpaired_t_owner_declines();
  a_targeted_t_owner_opener_is_a_list_item();
  an_untargeted_t_owner_opener_still_declines();
  closers_emit_no_anchor(false);
  closers_emit_no_anchor(true);
  a_closer_before_its_opener_is_no_anchor();
  an_unmatched_closer_declines();
  return 0;
}
