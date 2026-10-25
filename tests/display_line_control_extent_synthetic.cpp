// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A control's extent is decided on the carried display-line framing, never on
// the flattened decoded string (issues #62, #64).
//
// A record payload is `<length byte><that many bytes of tokens>`.  The length
// byte is the row-control slot, always and only, but it is a raw byte that a
// token reader resolves through the dictionary like any other token, so it
// routinely spells an ordinary word -- `additional`, `any`, `access`, `ST`.
// Nothing local separates the two roles; only the walk from the record start
// does, which is why the decoder does that walk once and stamps every token
// with its `TokenFramingRole` (book_ir.hpp).
//
// Everything here is synthetic: the tests build `DecodedLogicalRecordSource`
// values by hand and open no book.

#include "geist/detail/container/control_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/render/render_diagnostic_ir.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "geist/detail/layout/layout_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenFramingRole;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
    geist_test::record_failure();
  }
}

void append(DecodedLogicalRecordSource &record, std::uint16_t encoded,
            std::uint8_t width, TokenWords words) {
  record.encoded_tokens.push_back({encoded, width});
  record.tokens.push_back(std::move(words));
}

// Rebuilds the typed IR of a hand-assembled record and lets the decoder decide
// the framing, exactly as `decode_record_payload_ir` does in production.  No
// test re-derives the display-line walk.
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

std::string slice(const DecodedLogicalRecordSource &record,
                  const geist::detail::OutputRangeIR &range) {
  const auto text = geist::detail::token_words_to_ascii(record.assembled.words);
  if (range.end <= range.begin || range.begin >= text.size()) return {};
  return text.substr(range.begin,
                     std::min(range.end, text.size()) - range.begin);
}

// Two display lines: `cz FLOW P 3 3` and then a row of prose.  The flattened
// string runs the directive's segment to the next control, so its trailing
// word list holds the prose too.
void cz_operands_stop_at_the_display_line() {
  DecodedLogicalRecordSource record;
  record.logical_record = 7;
  // Line 0: a length byte covering 13 bytes of tokens, then `cz FLOW P 3 3`.
  // The byte's own dictionary spelling is the ordinary word `zero`, which is
  // the whole difficulty: nothing in the flattened string marks it as
  // structure.
  append(record, 13, 1, {3, 'z', 'e', 'r', 'o'});
  append(record, 0x40, 2, {'c', 'z'});
  append(record, 0x41, 2, {'F', 'L', 'O', 'W'});
  append(record, 0x42, 2, {'P'});
  append(record, 0x43, 2, {'3'});
  append(record, 0x44, 2, {'3'});
  append(record, 0x45, 3, {' '});
  // Line 1: its length byte spells `then`, and then the prose the directive
  // introduces.  A segment runs to the next control, so all of this is in the
  // directive's segment and in the word list the old operand parser read.
  append(record, 4, 1, {3, 't', 'h', 'e', 'n'});
  append(record, 0x50, 2, {'A', 'l', 'p', 'h', 'a'});
  append(record, 0x51, 2, {'b', 'e', 't', 'a'});
  refresh(record);

  require(record.ir.display_lines_parse,
          "the synthetic record's display lines did not parse");
  require(record.ir.display_lines.size() == 2,
          "the synthetic record did not frame into two display lines");
  require(record.ir.tokens[7].framing == TokenFramingRole::line_length,
          "token 7 is the second line's length byte and was not stamped as "
          "one");

  const geist::detail::ControlSegmentIR *directive = nullptr;
  for (const auto &segment : record.control_segments)
    if (segment.kind == geist::detail::BookControlKind::layout_directive)
      directive = &segment;
  require(directive != nullptr, "no cz layout directive was decoded");
  if (directive == nullptr) return;

  require(!directive->malformed,
          "a cz directive whose own display line is exactly `cz FLOW P 3 3` "
          "was read as malformed; its operands were validated against the "
          "body text of the following display line");
  const auto operands = geist::detail::trim_ascii(
      slice(record, directive->operand_range));
  require(operands == "FLOW P 3 3",
          "cz operand range is '" + operands + "', not the directive's own "
          "display line 'FLOW P 3 3'");
  // The prose stays payload: the boundary moved, no word was consumed.
  const auto payload = slice(record, directive->payload_range);
  require(payload.find("Alpha") != std::string::npos &&
              payload.find("beta") != std::string::npos,
          "the following display line's prose left the directive's payload");
}

// The same record with its framing withheld: an unframed record decides
// nothing, so the old whole-segment reading must stand rather than a guess.
void an_unframed_record_decides_nothing() {
  DecodedLogicalRecordSource record;
  record.logical_record = 8;
  append(record, 13, 1, {3, 'z', 'e', 'r', 'o'});
  append(record, 0x40, 2, {'c', 'z'});
  append(record, 0x41, 2, {'F', 'L', 'O', 'W'});
  append(record, 0x42, 2, {'P'});
  append(record, 0x43, 2, {'3'});
  append(record, 0x44, 2, {'3'});
  append(record, 0x45, 3, {' '});
  append(record, 4, 1, {3, 't', 'h', 'e', 'n'});
  append(record, 0x50, 2, {'A', 'l', 'p', 'h', 'a'});
  append(record, 0x51, 2, {'b', 'e', 't', 'a'});
  refresh(record);
  const auto framed = record.control_segments;

  // Same assembled record, no framing handed over.
  const auto unframed = geist::detail::decode_control_segments(
      record.logical_record, record.assembled, record.encoded_tokens);
  const geist::detail::ControlSegmentIR *directive = nullptr;
  for (const auto &segment : unframed)
    if (segment.kind == geist::detail::BookControlKind::layout_directive)
      directive = &segment;
  require(directive != nullptr, "no cz layout directive without framing");
  if (directive == nullptr) return;
  require(directive->malformed,
          "without the framing the operand parser has no boundary and must "
          "fail closed, not guess a prefix");
  require(!framed.empty(), "framed decode produced no segments");
}

// A length byte whose dictionary spelling is an ordinary word is structure.
// `display_text_words` is the checked accessor that refuses to hand it back,
// so a consumer asking for display text cannot be given the byte's spelling.
void a_length_byte_is_never_display_text() {
  DecodedLogicalRecordSource record;
  record.logical_record = 9;
  // Line 0: the length byte is spelled `additional` -- the QSYSINFO shape,
  // where `csourcefn RBAFUP21 additional` read that word as a second operand.
  append(record, 2, 1, {3, 'a', 'd', 'd', 'i', 't', 'i', 'o', 'n', 'a', 'l'});
  append(record, 0x60, 2, {'S', 'T'});
  // Line 1: spelled `any` -- the QSYSNEWG record 232 token 0 shape, where the
  // drawn-box claim read the byte as display text.
  append(record, 2, 1, {3, 'a', 'n', 'y'});
  append(record, 0x61, 2, {'T', 'i', 't', 'l', 'e'});
  refresh(record);

  require(record.ir.display_lines_parse,
          "the length-byte record's display lines did not parse");
  for (const std::size_t token : {std::size_t{0}, std::size_t{2}}) {
    require(geist::detail::is_display_line_length_token(record, token),
            "token " + std::to_string(token) +
                " opens a display line and was not read as its length byte");
    require(geist::detail::display_text_words(record, token) == nullptr,
            "the checked display-text accessor handed back the dictionary "
            "spelling of a length byte; a consumer asking for display text "
            "must never be given structure");
  }
  for (const std::size_t token : {std::size_t{1}, std::size_t{3}}) {
    require(!geist::detail::is_display_line_length_token(record, token),
            "line content was read as a length byte");
    require(geist::detail::display_text_words(record, token) != nullptr,
            "the checked accessor withheld the words of a line-content token");
  }

  std::string error;
  require(geist::detail::verify_display_line_framing(record.ir, &error),
          "the decoder's own framing failed its consistency check: " + error);
}

// SC09-2417-00 3.1.6.1 record 444 begins with a length byte spelling
// CSELECT. It is not a selector and cannot consume the code sample as operands.
void a_length_byte_cannot_select_the_following_code() {
  for (const bool framed : {true, false}) {
    DecodedLogicalRecordSource record;
    record.logical_record = 444;
    append(record, framed ? 4 : 99, 1,
           {3, 'C', 'S', 'E', 'L', 'E', 'C', 'T'});
    append(record, 0x80, 2, {' ', ' ', ' '});
    const std::string code = "#pragma mapinc(\"c:/abc/aaa.h\",\"input\")";
    append(record, 0x81, 2, TokenWords(code.begin(), code.end()));
    append(record, 8, 1, {3, 'z', 'e', 'r', 'o'});
    append(record, 0x82, 2, {'c', 's', 'e', 'l', 'e', 'c', 't'});
    append(record, 0x83, 2, {'3'});
    append(record, 0x84, 2, {'4'});
    append(record, 0x85, 2, {'D', 'E', 'S', 'T'});
    append(record, 4, 1, {3, 'z', 'e', 'r', 'o'});
    append(record, 0x86, 2, {' ', ' ', ' '});
    append(record, 0x87, 2, {'L', 'i', 'n', 'k'});
    refresh(record);
    if (!framed)
      for (const auto& segment : record.control_segments)
        require(!geist::detail::is_display_line_length_opcode(record, segment),
                "undecided framing claimed a false opcode identity");
    geist::detail::demote_display_line_owned_controls(record);
    require(record.ir.display_lines_parse == framed,
            "selector length-byte test framing differs from setup");
    std::size_t selectors = 0;
    for (const auto& segment : record.control_segments)
      if (segment.kind == geist::detail::BookControlKind::select) ++selectors;
    require(selectors == (framed ? 1 : 2),
            "only proven length-byte selectors may be withdrawn");
    if (!framed) continue;
    const auto lines = geist::detail::best_effort_lines({record}, "");
    require(lines.size() == 2 && lines[0] == "   " + code &&
                lines[1] == "   Link",
            "a spurious selector swallowed code, or a real selector leaked");
  }
}

void indented_si_is_display_text() {
  DecodedLogicalRecordSource record;
  record.logical_record = 1038;
  append(record, 6, 1, {3, 'z', 'e', 'r', 'o'});
  append(record, 0x80, 2, {' ', ' ', ' '});
  append(record, 0x81, 2, {'S', 'I'});
  append(record, 0x82, 2, {'a', 'r', 'e', ' ', 'r', 'e', 'm', 'o', 'v', 'e', 'd', '.'});
  append(record, 4, 1, {3, 'z', 'e', 'r', 'o'});
  append(record, 0x81, 2, {'S', 'I'});
  append(record, 0x83, 2, {'i', 'n', 'd', 'e', 'x'});
  refresh(record);
  geist::detail::demote_display_line_owned_controls(record);
  require(record.ir.display_lines_parse, "SI regression requires native framing");
  bool literal_si = false;
  for (const auto& segment : record.control_segments)
    if (segment.display_text &&
        std::find(segment.source_tokens.begin(), segment.source_tokens.end(),
                  std::size_t{2}) != segment.source_tokens.end())
      literal_si = true;
  require(literal_si, "native indentation must demote the false SI segment");
  const auto* native = geist::detail::record_display_lines(record);
  require(native && native->size() == 2 &&
              !geist::detail::is_subject_index_display_line(record, (*native)[0]) &&
              geist::detail::is_subject_index_display_line(record, (*native)[1]),
          "SI identity must distinguish indentation from the native origin");
  const auto lines = geist::detail::best_effort_lines({record}, "");
  require(lines.size() == 1 && lines[0] == "   SI are removed.",
          "indented SI prose was hidden, or genuine SI metadata leaked");
}

// GC28-183's generated Part Contents topics have no body text. Their last
// metadata row is exactly `c.sp 1 p` (topic 1.1, logical record 65, tokens
// 31..33, source bytes [0xef4b,0xef50)); the count and point unit are operands,
// not the visible row `1 p`.
void vertical_space_operands_do_not_leak() {
  DecodedLogicalRecordSource record;
  record.logical_record = 65;
  append(record, 6, 1, {3, 0x2500});
  append(record, 0x80, 2, {'c', '.', 's', 'p'});
  append(record, 0x81, 2, {'1'});
  append(record, 0x82, 2, {'p'});
  refresh(record);

  const auto spacing = std::find_if(
      record.control_segments.begin(), record.control_segments.end(),
      [](const auto &segment) {
        return segment.kind == geist::detail::BookControlKind::spacing;
      });
  require(spacing != record.control_segments.end() && !spacing->malformed,
          "the documented `c.sp 1 p` form was not decoded as spacing");
  require(spacing != record.control_segments.end() &&
              geist::detail::trim_ascii(slice(record, spacing->operand_range)) ==
                  "1 p",
          "the `c.sp 1 p` operands did not retain their exact source range");
  require(geist::detail::best_effort_lines({record}, "").empty(),
          "the invisible `c.sp 1 p` row leaked an operand as body text");
}

// A revision definition is metadata with exactly two operands. SC09-138
// FRONT record 2 carries eleven lines such as `c.rev REL2 |`; IBMMMSTR
// PREFACE.6 record 19 carries `c.rev PREF |`. None is visible body text.
void revision_definitions_have_an_explicit_disposition() {
  DecodedLogicalRecordSource record;
  record.logical_record = 2;
  append(record, 6, 1, {3, 0x2500});
  append(record, 0x80, 2, {'c', '.', 'r', 'e', 'v'});
  append(record, 0x81, 2, {'R', 'E', 'L', '2'});
  append(record, 0x82, 2, {'|'});
  refresh(record);

  const auto revision = std::find_if(
      record.control_segments.begin(), record.control_segments.end(),
      [](const auto &segment) {
        return segment.kind ==
               geist::detail::BookControlKind::revision_definition;
      });
  require(revision != record.control_segments.end() && !revision->malformed,
          "c.rev was not decoded as a revision definition");
  require(revision != record.control_segments.end() &&
              geist::detail::trim_ascii(slice(record, revision->operand_range)) ==
                  "REL2 |",
          "the c.rev operands did not retain their exact source range");
  require(geist::detail::best_effort_lines({record}, "").empty(),
          "an invisible c.rev definition leaked into source-row output");
}

// A topic whose complete body consists of a valid spacing instruction is a
// genuine heading-only topic. Every source token still needs a typed ledger
// disposition; the absence of a prose block is not itself a rejection.
void a_fully_disposed_empty_topic_is_typed() {
  DecodedLogicalRecordSource record;
  record.logical_record = 65;
  const auto control_line = [&](TokenWords opcode,
                                std::vector<TokenWords> operands = {}) {
    std::size_t length = 2;
    for (const auto &operand : operands) length += 2;
    append(record, static_cast<std::uint16_t>(length), 1, {3, 0x2500});
    append(record, 0x8000, 2, std::move(opcode));
    for (auto &operand : operands)
      append(record, 0x8001, 2, std::move(operand));
  };
  control_line({'s', 'h', '1', '.', '1'});
  control_line({'c', 't', 'o', 'p', 'i', 'c', 'n'}, {{'1'}});
  control_line({'c', 'p', 'a', 'r', 'e', 'n', 't'});
  control_line({'c', 'f', 'o', 'r', 'w', 'a', 'r', 'd', 'l', 'e', 'v', 'e', 'l'});
  control_line({'c', 'b', 'a', 'c', 'k', 'l', 'e', 'v', 'e', 'l'});
  control_line({'c', 's', 'u', 'm', 'm', 'a', 'r', 'y'},
               {{'0'}, {'0'}, {'0'}});
  control_line({'c', 'h', 'd', 'l', 'e', 'v', 'e', 'l'},
               {{':', 'H', '3'}});
  control_line({'c', 's', 'o', 'u', 'r', 'c', 'e', 'f', 'n'},
               {{'T', 'E', 'S', 'T'}});
  control_line({'S', 'T'}, {{'E', 'm', 'p', 't', 'y'}});
  control_line({'c', '.', 's', 'p'}, {{'1'}, {'p'}});
  refresh(record);
  geist::detail::demote_display_line_owned_controls(record);

  const std::vector<DecodedLogicalRecordSource> records{record};
  const auto layout = geist::detail::extract_layout_ir(records);
  std::string error;
  const auto ownership =
      geist::detail::build_verified_ownership_ir(records, layout, &error);
  require(ownership.has_value(),
          "empty-topic ownership failed: " + error);
  if (!ownership) return;
  const auto prose = geist::detail::extract_prose_topic_ir(
      records, layout, *ownership, "Empty", nullptr, &error, nullptr);
  require(prose.has_value(),
          "a fully disposed heading-only topic was rejected: " + error);
  if (!prose) return;
  require(prose->blocks.empty() && prose->menu_items.empty() &&
              prose->spans.empty(),
          "the heading-only topic invented a body block");
  require(geist::detail::verify_prose_topic_ir(
              records, layout, *ownership, "Empty", nullptr, *prose, &error,
              nullptr),
          "the heading-only topic failed typed verification: " + error);
}

// A bare SRMSG in the body is a local message destination, not the opening
// of a message-field envelope. IBMMMSTR 3.7.14 record 1631 stores it as
// `01 68`: a one-byte native row whose sole byte is SRMSG. The preceding
// 74-cell U+2500 row is a semantic separator and the following row remains
// prose; admitting the anchor must preserve both neighbors.
void a_bare_message_anchor_preserves_its_native_separator() {
  DecodedLogicalRecordSource record;
  record.logical_record = 1631;
  const auto control_line = [&](TokenWords opcode,
                                std::vector<TokenWords> operands = {}) {
    std::size_t length = 2;
    for (const auto &operand : operands) length += 2;
    append(record, static_cast<std::uint16_t>(length), 1, {3, 0x2534});
    append(record, 0x8000, 2, std::move(opcode));
    for (auto &operand : operands)
      append(record, 0x8001, 2, std::move(operand));
  };
  control_line({'s', 'h', '3', '.', '7', '.', '1', '4'});
  control_line({'c', 't', 'o', 'p', 'i', 'c', 'n'}, {{'1'}});
  control_line({'c', 'p', 'a', 'r', 'e', 'n', 't'});
  control_line({'c', 'f', 'o', 'r', 'w', 'a', 'r', 'd', 'l', 'e', 'v', 'e', 'l'});
  control_line({'c', 'b', 'a', 'c', 'k', 'l', 'e', 'v', 'e', 'l'});
  control_line({'c', 's', 'u', 'm', 'm', 'a', 'r', 'y'},
               {{'0'}, {'0'}, {'0'}});
  control_line({'c', 'h', 'd', 'l', 'e', 'v', 'e', 'l'},
               {{':', 'H', '3'}});
  control_line({'c', 's', 'o', 'u', 'r', 'c', 'e', 'f', 'n'},
               {{'I', 'B', 'M', 'M', 'M', 'S', 'T', 'R'}});
  control_line({'S', 'T'}, {{'M', 'e', 's', 's', 'a', 'g', 'e', 's'}});

  append(record, 4, 1, {3, 0x2534});
  append(record, 0x8100, 2, {' ', ' ', ' '});
  append(record, 0x8101, 2, TokenWords(74, 0x2500));
  append(record, 1, 1, {3, 'a', 'r', 'e'});
  append(record, 0x68, 1, {'S', 'R', 'M', 'S', 'G'});
  append(record, 4, 1, {3, 0x2534});
  append(record, 0x8102, 2, {' ', ' ', ' '});
  append(record, 0x8103, 2, {'F', 'o', 'l', 'l', 'o', 'w', 'i', 'n', 'g', '.'});
  refresh(record);
  geist::detail::demote_display_line_owned_controls(record);

  const std::vector<DecodedLogicalRecordSource> records{record};
  const auto layout = geist::detail::extract_layout_ir(records);
  std::string error;
  const auto ownership =
      geist::detail::build_verified_ownership_ir(records, layout, &error);
  require(ownership.has_value(), "message-anchor ownership failed: " + error);
  if (!ownership) return;
  const auto prose = geist::detail::extract_prose_topic_ir(
      records, layout, *ownership, "Messages", nullptr, &error, nullptr);
  require(prose.has_value(), "bare body SRMSG was rejected: " + error);
  if (!prose) return;
  require(prose->blocks.size() == 2 &&
              prose->blocks[0].kind ==
                  geist::detail::ProseBlockKindIR::thematic_break &&
              prose->blocks[1].kind ==
                  geist::detail::ProseBlockKindIR::paragraph,
          "separator or following paragraph was not preserved as a block");
  require(prose->anchors.size() == 1 && prose->anchors[0].id == "MSG" &&
              prose->anchors[0].position == 1,
          "bare SRMSG did not remain between separator and paragraph");
}

void a_metadata_length_byte_cannot_consume_the_first_word() {
  DecodedLogicalRecordSource record;
  record.logical_record = 151;
  append(record, 4, 1, {3, 'c', 'b', 'a', 'c', 'k', 'l', 'e', 'v', 'e', 'l'});
  append(record, 0x80, 2, {' ', ' ', ' '});
  append(record, 0x81, 2, {'t', 'h', 'e'});
  append(record, 4, 1, {3, 'z', 'e', 'r', 'o'});
  append(record, 0x82, 2, {'c', 'b', 'a', 'c', 'k', 'l', 'e', 'v', 'e', 'l'});
  append(record, 0x83, 2, {'1', '.', '0'});
  refresh(record);
  bool length_opcode = false;
  bool real_opcode = false;
  for (const auto& segment : record.control_segments) {
    if (geist::detail::ascii_lower(segment.opcode) != "cbacklevel") continue;
    if (geist::detail::is_display_line_length_opcode(record, segment))
      length_opcode = true;
    else
      real_opcode = true;
  }
  require(length_opcode && real_opcode,
          "shared opcode identity did not separate metadata from length");
  geist::detail::demote_display_line_owned_controls(record);
  require(record.ir.display_lines_parse, "metadata regression requires framing");
  const auto lines = geist::detail::best_effort_lines({record}, "");
  require(lines.size() == 1 && lines[0] == "   the",
          "length-byte metadata consumed text or genuine metadata leaked");
}

void font_operand_grammar_cannot_override_native_opcode_identity() {
  DecodedLogicalRecordSource record;
  record.logical_record = 79;
  append(record, 6, 1, {3, 'c', 'f', 'o', 'n', 't'});
  append(record, 0x100, 2, {'3'});
  append(record, 0x101, 2, {'4'});
  append(record, 0x102, 2, {'E'});
  append(record, 8, 1, {3, 'z', 'e', 'r', 'o'});
  append(record, 0x103, 2, {'c', 'f', 'o', 'n', 't'});
  append(record, 0x100, 2, {'3'});
  append(record, 0x101, 2, {'4'});
  append(record, 0x102, 2, {'E'});
  refresh(record);
  std::size_t lengths = 0;
  std::size_t instructions = 0;
  for (const auto& segment : record.control_segments) {
    if (segment.kind != geist::detail::BookControlKind::font) continue;
    require(!segment.malformed,
            "false-font regression must have canonical-looking operands");
    if (geist::detail::is_display_line_length_opcode(record, segment))
      ++lengths;
    else
      ++instructions;
  }
  require(lengths == 1 && instructions == 1,
          "operand grammar overrode native false/genuine CFONT identity");
  record.ir.display_lines_parse = false;
  for (const auto& segment : record.control_segments)
    require(!geist::detail::is_display_line_length_opcode(record, segment),
            "undecided framing claimed a font opcode identity");
  for (auto& token : record.ir.tokens)
    token.framing = geist::detail::TokenFramingRole::unframed;
  for (const auto& segment : record.control_segments)
    require(!geist::detail::is_display_line_length_opcode(record, segment),
            "unframed font fixture claimed an opcode identity");
}

void an_indented_publication_number_is_not_a_topic_start() {
  DecodedLogicalRecordSource record;
  record.logical_record = 520;
  append(record, 2, 1, {0x2534});
  append(record, 0x100, 2, {'S', 'H', '1', '.', '1'});
  refresh(record);
  geist::detail::demote_display_line_owned_controls(record);
  DecodedLogicalRecordSource continuation;
  continuation.logical_record = 521;
  append(continuation, 4, 1, {0x2534});
  append(continuation, 0x101, 2, {' ', ' ', ' ', ' ', ' ', ' ', ' '});
  append(continuation, 0x102, 2,
         {'S', 'H', '2', '6', '-', '7', '0', '0', '0', '.'});
  refresh(continuation);
  geist::detail::demote_display_line_owned_controls(continuation);
  std::size_t starts = 0;
  bool literal = false;
  for (const auto& segment : record.control_segments)
    if (segment.kind == geist::detail::BookControlKind::topic_start) ++starts;
  for (const auto& segment : continuation.control_segments) {
    if (segment.kind == geist::detail::BookControlKind::topic_start) ++starts;
    if (segment.display_text && slice(continuation, segment.payload_range).find(
                                    "SH26-7000.") != std::string::npos)
      literal = true;
  }
  require(
      starts == 1 && literal,
      "indented publication identifier became a topic instruction: starts=" +
          std::to_string(starts) + " literal=" + std::to_string(literal));
}

void a_parenthetic_word_at_a_native_line_end_stays_text(bool styled = false,
                                                      int linked = 0) {
  DecodedLogicalRecordSource record;
  record.logical_record = 20;
  const auto words = [](const std::string &value) {
    return TokenWords(value.begin(), value.end());
  };
  const auto line = [&](std::vector<TokenWords> content) {
    std::uint16_t length = 0;
    for (const auto &token : content)
      length += token == words("a") ? 1 : 2;
    append(record, length, 1,
           content.empty() ? TokenWords{1} : TokenWords{0x2534});
    for (auto &token : content) {
      const bool compact = token == words("a");
      append(record, compact ? 47 : 0x100, compact ? 1 : 2, std::move(token));
    }
  };
  line({words("sh1.1")});
  line({words("ctopicn"), words("10")});
  line({words("cparent"), words("1.0")});
  line({words("cforwardlevel")});
  line({words("cbacklevel")});
  line({words("csummary"), words("10"), words("0"), words("10")});
  line({words("chdlevel"), words(":H2")});
  line({words("csourcefn"), words("SAMPLE")});
  line({words("ST"), words("Example")});
  line({words("   "), words("Creates"), words("a"), words("string"), words("("),
        TokenWords{1}, words("a")});
  line({words("   "), words("note).")});
  if (styled) {
    line({});
    line({words("cfont"), words("3"), words("4"), words("E"), words("7"),
          words("3"), words("1"), words("14"), words("2"), words("2"),
          words("18"), words("5"), words("E")});
    line({words("   "), words("CALL(1)"), words("   "), words("->"), words(" "),
          words("ONE()"), words("  ")});
    line({words("cfont"), words("3"), words("7"), words("E"), words("14"),
          words("2"), words("2"), words("18"), words("5"), words("E")});
    line({words("   "), words("CALL(2)"), words("   "), words("->"), words(" "),
          words("TWO()")});
    // Differing native gaps and indentation still belong to the same example;
    // they must not split it into independently laid-out tables or prose.
    line({words("cfont"), words("3"), words("7"), words("E"), words("11"),
          words("2"), words("2"), words("15"), words("7"), words("E")});
    line({words("   "), words("CALL(3)"), words("->"), words(" "),
          words("THREE()")});
    line({});
    line({words("cfont"), words("7"), words("7"), words("E"), words("25"),
          words("6"), words("1")});
    line({words("       "), words("ALPHA()"), words("          "),
          words("LEFT()")});
    line({words("cfont"), words("7"), words("6"), words("E"), words("25"),
          words("7"), words("2")});
    line({words("       "), words("BETA()"), words("           "),
          words("RIGHT()")});
    line({words("cfont"), words("7"), words("7"), words("E"), words("25"),
          words("5"), words("C")});
    line({words("       "), words("GAMMA()"), words("          "),
          words("MID()")});
    if (linked) {
      line({words("cfont"), words("3"), words("4"), words("E"), words("8"),
            words("12"), words(linked == 1 ? "E" : "1")});
      line({words("cselect"), words("8"), words("12"), words("HDRLINK")});
      line({words("   "), words("CODE"), words("many"), words("  "),
            words("words")});
    }
    line({});
    line({words("cfont"), words("7"), words("5"), words("E")});
    line({words("   "), words("Use"), words("TOKEN"), words("when"),
          words("needed.")});
    line({words("cfont"), words("7"), words("5"), words("E")});
    line({words("   "), words("and"), words("TOKEN"), words("elsewhere.")});
  }
  refresh(record);
  geist::detail::demote_display_line_owned_controls(record);
  std::string error;
  const auto document = geist::detail::try_lower_topic_to_document_ir(
      {"1.1", "Example", ":H2", 10, 20, 20}, {record}, nullptr, &error);
  require(document.has_value(), "parenthetic native prose declined: " + error);
  if (document) {
    const auto html = geist::detail::render_document_html_fragment(*document);
    require(html.find("(a note).") != std::string::npos,
            "native parenthetic word was consumed as a row marker: " + html);
    if (styled) {
      const geist::detail::PreformattedBlockIR *example = nullptr;
      std::size_t count = 0;
      for (const auto &block : document->blocks)
        if (const auto *value =
                std::get_if<geist::detail::PreformattedBlockIR>(&block.node)) {
          example = value;
          ++count;
        }
      std::vector<std::string> expected{
          "   CALL(1)    ->  ONE()   ",      "   CALL(2)    ->  TWO()",
          "   CALL(3) ->  THREE()",          "",
          "       ALPHA()           LEFT()", "       BETA()            RIGHT()",
          "       GAMMA()           MID()"};
      if (linked == 1)
        expected.push_back("   CODE many   words");
      require(count == 1 && example && example->lines == expected &&
                  example->styled_lines.size() == expected.size(),
              "native styled example spacing/rows were changed: " + html);
      if (linked == 1)
        require(html.find("href=\"#HDRLINK\"><samp class=\"geist-monospace geist-sample\">many   words</samp></a>") !=
                    std::string::npos,
                "native whitespace split one selector into separate anchors: " +
                    html);
      if (linked == 2)
        require(example && example->lines == expected,
                "unrepresentable emphasized link admitted to styled example");
      if (example && example->line_origins.size() == expected.size())
        require(!example->line_origins[3].slices.empty(),
                "native blank example row has no framing provenance");
      require(html.find("<table") == std::string::npos &&
                  html.find("<em class=\"geist-emphasis\">LEFT()</em>") !=
                      std::string::npos &&
                  html.find("Use <samp class=\"geist-monospace geist-sample\">TOKEN</samp> "
                            "when needed. and") != std::string::npos,
              "native styled example or ordinary wrapped code prose "
              "misclassified: " +
                  html);
      const auto layout = geist::detail::extract_layout_ir({record});
      const auto ownership =
          geist::detail::build_verified_ownership_ir({record}, layout, &error);
      if (ownership) {
        auto prose = geist::detail::extract_prose_topic_ir(
            {record}, layout, *ownership, "Example", nullptr, &error);
        require(prose.has_value(),
                "styled example mutation fixture declined: " + error);
        if (prose) {
          const auto canonical = *prose;
          for (auto &block : prose->blocks)
            if (!block.preformatted_line_inline_ends.empty()) {
              --block.preformatted_line_inline_ends.front();
              std::size_t first = 0;
              for (std::size_t row = 0; row < block.preformatted_lines.size();
                   ++row) {
                std::string text;
                for (; first < block.preformatted_line_inline_ends[row];
                     ++first)
                  text += block.inlines[first].text;
                block.preformatted_lines[row] = std::move(text);
              }
              break;
            }
          require(!geist::detail::verify_prose_topic_ir(
                      {record}, layout, *ownership, "Example", nullptr, *prose,
                      &error),
                  "a different but bounded source row partition passed "
                  "verification");
          *prose = canonical;
          for (auto &block : prose->blocks)
            if (!block.preformatted_line_inline_ends.empty()) {
              for (auto &item : block.inlines)
                if (item.text.find("CALL") != std::string::npos) {
                  item.text.replace(item.text.find("CALL"), 4, "FAKE");
                  break;
                }
              block.preformatted_lines.front().replace(
                  block.preformatted_lines.front().find("CALL"), 4, "FAKE");
              break;
            }
          require(!geist::detail::verify_prose_topic_ir(
                      {record}, layout, *ownership, "Example", nullptr, *prose,
                      &error),
                  "matching literal/styled text corruption passed source "
                  "verification");
        }
      }
      auto unframed = record;
      unframed.ir.display_lines_parse = false;
      unframed.ir.display_lines.clear();
      for (auto &token : unframed.ir.tokens)
        token.framing = geist::detail::TokenFramingRole::unframed;
      const auto undecided = geist::detail::try_lower_topic_to_document_ir(
          {"1.1", "Example", ":H2", 10, 20, 20}, {unframed}, nullptr, &error);
      if (undecided)
        for (const auto &block : undecided->blocks)
          if (const auto *value =
                  std::get_if<geist::detail::PreformattedBlockIR>(&block.node))
            require(value->styled_lines.empty(),
                    "undecided native framing inferred a styled example");
    }
  }
}

// The opcode itself ends at the display line too.  The flattened string glues
// the next line's length byte straight onto the opcode word -- SC31-711
// record 14 line 1 is exactly `SRHDRAIXHIGH` and the `-` behind it is line 2's
// length byte -- and the glued spelling `SRHDRAIXHIGH-` then classifies as
// ordinary text, so the anchor is lost.
void an_opcode_stops_at_the_display_line() {
  DecodedLogicalRecordSource record;
  record.logical_record = 11;
  // Line 0: length byte covering the 2-byte anchor token.
  append(record, 2, 1, {3, 'z', 'e', 'r', 'o'});
  // The anchor closes its line with no trailing space, so the next line's
  // length byte lands directly against it in the flattened string.
  append(record, 0x70, 2, {2, 'S', 'R', 'H', 'D', 'R', 'A', 'B', 'C'});
  // Line 1: its length byte is spelled `-`; the flattened string therefore
  // reads `SRHDRABC-` as one word and no mark separates the two.
  append(record, 2, 1, {3, '-'});
  append(record, 0x71, 2, {'B', 'o', 'd', 'y'});
  refresh(record);

  require(record.ir.display_lines_parse,
          "the anchor record's display lines did not parse");
  require(record.ir.tokens[2].framing == TokenFramingRole::line_length,
          "token 2 is the second line's length byte and was not stamped as "
          "one");
  const auto text = geist::detail::token_words_to_ascii(record.assembled.words);
  require(text.find("SRHDRABC-") != std::string::npos,
          "the fixture no longer reproduces the glued spelling this pins; "
          "assembled text is '" + text + "'");

  const geist::detail::ControlSegmentIR *anchor = nullptr;
  for (const auto &segment : record.control_segments)
    if (segment.kind == geist::detail::BookControlKind::structural)
      anchor = &segment;
  require(anchor != nullptr,
          "the anchor was not decoded as a structural control; its opcode "
          "absorbed the next display line's length byte and the glued word "
          "classified as text");
  if (anchor == nullptr) return;
  require(anchor->opcode == "SRHDRABC",
          "opcode is '" + anchor->opcode +
              "', not the word on the anchor's own display line 'SRHDRABC'");
}

// A compiled menu item's label is bounded by the item's own display line, in
// both directions (issue #77).
//
// The label must not run past the line end and pick up the next line's length
// byte: SC26-457 record 543 display line 18 ends at `Example 8`, and the byte
// behind it is line 19's length, spelled `----------` by that book's
// dictionary.  Hosted serves the label without the rule, and the menu pass
// rejects the topic because the label no longer equals the catalogue title.
const geist::detail::ControlSegmentIR *only_menu_item(
    const DecodedLogicalRecordSource &record) {
  const geist::detail::ControlSegmentIR *item = nullptr;
  for (const auto &segment : record.control_segments)
    if (segment.kind == geist::detail::BookControlKind::menu_item) {
      if (item != nullptr) return nullptr;
      item = &segment;
    }
  return item;
}

std::string label_of(const DecodedLogicalRecordSource &record,
                     const geist::detail::ControlSegmentIR &item) {
  return geist::detail::trim_ascii(slice(record, item.payload_range));
}

void a_menu_label_stops_at_the_display_line() {
  DecodedLogicalRecordSource record;
  record.logical_record = 543;
  // Line 0: eight bytes of tokens spelling `cmitem 1.1 Alpha Beta`.
  append(record, 8, 1, {3, 'l', 'e', 'a', 'd'});
  append(record, 0x80, 2, {'c', 'm', 'i', 't', 'e', 'm'});
  append(record, 0x81, 2, {'1', '.', '1'});
  append(record, 0x82, 2, {'A', 'l', 'p', 'h', 'a'});
  append(record, 0x83, 2, {'B', 'e', 't', 'a'});
  // Line 1: its length byte is spelled `----------`, the shape of a drawn
  // rule, and the row it opens is the next item's text.
  append(record, 4, 1, {3, '-', '-', '-', '-', '-', '-', '-', '-', '-', '-'});
  append(record, 0x84, 2, {'G', 'a', 'm', 'm', 'a'});
  append(record, 0x85, 2, {'D', 'e', 'l', 't', 'a'});
  refresh(record);
  geist::detail::demote_display_line_owned_controls(record);

  require(record.ir.display_lines_parse,
          "the menu record's display lines did not parse");
  require(record.ir.tokens[5].framing == TokenFramingRole::line_length,
          "token 5 is the second line's length byte and was not stamped as "
          "one");
  const auto text = geist::detail::token_words_to_ascii(record.assembled.words);
  require(text.find("Beta ----------") != std::string::npos,
          "the fixture no longer reproduces the glued spelling this pins; "
          "assembled text is '" + text + "'");

  const auto *item = only_menu_item(record);
  require(item != nullptr, "no single cmitem control was decoded");
  if (item == nullptr) return;
  require(label_of(record, *item) == "Alpha Beta",
          "the label is '" + label_of(record, *item) +
              "', not the text on the item's own display line 'Alpha Beta'");
}

// The other direction: a word inside the label spelled like a control opens a
// segment of its own, and the label stops short.  SC24-5527-02 record 592
// display line 2 is `cmitem 6.3.7 Create an APPLY List from Two SRVAPPS
// Tables` and arrives as two segments, because `SRVAPPS` reads as an SR
// anchor.  It is not one: a genuine control opens its own display line, and
// this word stands inside one.
void a_menu_label_reaches_the_end_of_its_display_line() {
  DecodedLogicalRecordSource record;
  record.logical_record = 592;
  // Line 0: `cmitem 1.1 Alpha SRVAPPS Beta`, ten bytes of tokens.
  append(record, 10, 1, {3, 'l', 'e', 'a', 'd'});
  append(record, 0x80, 2, {'c', 'm', 'i', 't', 'e', 'm'});
  append(record, 0x81, 2, {'1', '.', '1'});
  append(record, 0x82, 2, {'A', 'l', 'p', 'h', 'a'});
  append(record, 0x83, 2, {'S', 'R', 'V', 'A', 'P', 'P', 'S'});
  append(record, 0x84, 2, {'B', 'e', 't', 'a'});
  // Line 1: an ordinary row, so the record frames into two lines.
  append(record, 2, 1, {3, 'n', 'e', 'x', 't'});
  append(record, 0x85, 2, {'G', 'a', 'm', 'm', 'a'});
  refresh(record);
  require(record.control_segments.size() > 1,
          "the fixture no longer reproduces the split this pins: the "
          "flattened splitter did not open a segment on `SRVAPPS`");
  geist::detail::demote_display_line_owned_controls(record);

  const auto *item = only_menu_item(record);
  require(item != nullptr, "no single cmitem control was decoded");
  if (item == nullptr) return;
  require(label_of(record, *item) == "Alpha SRVAPPS Beta",
          "the label is '" + label_of(record, *item) +
              "', not the whole text of the item's own display line "
              "'Alpha SRVAPPS Beta'");
}

// Fail-closed: a record whose payload does not tile into display lines has no
// decided boundary, so nothing is bounded and nothing is absorbed.
void an_unframed_menu_label_is_not_bounded() {
  DecodedLogicalRecordSource record;
  record.logical_record = 4;
  append(record, 99, 1, {3, 'l', 'e', 'a', 'd'});
  append(record, 0x80, 2, {'c', 'm', 'i', 't', 'e', 'm'});
  append(record, 0x81, 2, {'1', '.', '1'});
  append(record, 0x82, 2, {'A', 'l', 'p', 'h', 'a'});
  append(record, 4, 1, {3, '-', '-', '-', '-', '-', '-', '-', '-', '-', '-'});
  append(record, 0x83, 2, {'G', 'a', 'm', 'm', 'a'});
  refresh(record);
  geist::detail::demote_display_line_owned_controls(record);
  require(!record.ir.display_lines_parse,
          "the unframed fixture unexpectedly framed into display lines");
  const auto *item = only_menu_item(record);
  require(item != nullptr, "no single cmitem control was decoded");
  if (item == nullptr) return;
  require(label_of(record, *item) == "Alpha ---------- Gamma",
          "an unframed record has no decided line boundary and must keep its "
          "previous reading; the label is '" + label_of(record, *item) + "'");
}

// The record encoder's control separator (encoded value 2, words
// {0x0001, ','}) is drawn text where the framing places it inside a display
// line, and only there.  SC31-7111-00 5.0 record 388 line 25 ends `could not
// be done,` with that token (0x27EFA) inside the line, before line 26
// `cfont 13 7 2 ...`; hosted (DT 19941010174546) serves `done,`.  The same
// token as the next line's length byte (FA1PLMM0 record 353 token 72, before
// `c.cp`) draws nothing.
void a_framed_separator_inside_a_line_is_text(bool on_length_byte) {
  DecodedLogicalRecordSource record;
  record.logical_record = 388;
  const TokenWords separator{1, ','};
  // Line 0: blanks, `done`, and (inside the line) the separator.
  append(record, on_length_byte ? 2 : 3, 1, {' '});
  append(record, 0x40, 1, {' ', ' ', ' '});
  append(record, 0x41, 1, {'d', 'o', 'n', 'e'});
  if (!on_length_byte) append(record, 2, 1, separator);
  // Line 1: `cfont 13 7 2`; its length byte is the separator when asked.
  if (on_length_byte)
    append(record, 4, 1, separator);
  else
    append(record, 4, 1, {' '});
  append(record, 0x42, 1, {'c', 'f', 'o', 'n', 't'});
  append(record, 0x43, 1, {'1', '3'});
  append(record, 0x44, 1, {'7'});
  append(record, 0x45, 1, {'2'});
  // Line 2: the row the font styles.
  append(record, 2, 1, {' '});
  append(record, 0x46, 1, {' ', ' ', ' '});
  append(record, 0x47, 1, {'n', 'e', 'x', 't'});
  refresh(record);
  require(record.ir.display_lines_parse,
          "separator record: synthetic framing did not parse");
  std::string payloads;
  for (const auto &segment : record.control_segments)
    payloads += slice(record, segment.payload_range) + "|";
  const auto kept = payloads.find("done,") != std::string::npos;
  if (on_length_byte)
    require(!kept, "a separator standing on a length byte became text: " +
                       payloads);
  else
    require(kept, "a separator drawn inside its display line was left in no "
                  "segment: " + payloads);
}

// The reader skips every display line whose control name starts with `C.`,
// operands included.  Version 1.0/1.1 books spell these controls in capitals
// and give them more than one operand: EDCUSER 9.0 record 1220 lines 9-11
// are `C.SP 5 P`, `C.CP` and `C.CP 12DV`, and hosted (DT 19900523000000)
// serves that topic as its heading alone.  The verbatim route drew `P`,
// `C.CP` and `C.CP 12DV`.  A row of text that has `C.` after its origin run
// is still drawn.
void capital_script_control_lines_draw_nothing() {
  DecodedLogicalRecordSource record;
  record.logical_record = 1220;
  const auto line = [&](std::vector<TokenWords> tokens) {
    append(record, static_cast<std::uint16_t>(2 * tokens.size()), 1,
           {3, 0x2500});
    for (auto &token : tokens) append(record, 0x8000, 2, std::move(token));
  };
  line({{'C', '.', 'S', 'P'}, {'5'}, {'P'}});
  line({{'C', '.', 'C', 'P'}});
  line({{'C', '.', 'C', 'P'}, {'1', '2', 'D', 'V'}});
  line({{'C', '.', 'S', 'K'}, {'1'}});
  line({{' ', ' ', ' '}, {'C', '.'}, {'i', 's'}, {'t', 'e', 'x', 't'}});
  refresh(record);
  const auto lines = geist::detail::best_effort_lines({record}, "");
  require(lines.size() == 1 && lines.front().find("C. is text") !=
                                   std::string::npos,
          "capital C. control lines were drawn, or the text row was lost: " +
              std::to_string(lines.size()) + " line(s), first '" +
              (lines.empty() ? std::string() : lines.front()) + "'");
}

} // namespace

int main() {
  capital_script_control_lines_draw_nothing();
  an_opcode_stops_at_the_display_line();
  cz_operands_stop_at_the_display_line();
  an_unframed_record_decides_nothing();
  a_length_byte_is_never_display_text();
  a_length_byte_cannot_select_the_following_code();
  indented_si_is_display_text();
  vertical_space_operands_do_not_leak();
  revision_definitions_have_an_explicit_disposition();
  a_fully_disposed_empty_topic_is_typed();
  a_bare_message_anchor_preserves_its_native_separator();
  a_metadata_length_byte_cannot_consume_the_first_word();
  font_operand_grammar_cannot_override_native_opcode_identity();
  an_indented_publication_number_is_not_a_topic_start();
  a_parenthetic_word_at_a_native_line_end_stays_text();
  a_parenthetic_word_at_a_native_line_end_stays_text(true);
  a_parenthetic_word_at_a_native_line_end_stays_text(true, 1);
  a_parenthetic_word_at_a_native_line_end_stays_text(true, 2);
  a_menu_label_stops_at_the_display_line();
  a_menu_label_reaches_the_end_of_its_display_line();
  an_unframed_menu_label_is_not_bounded();
  a_framed_separator_inside_a_line_is_text(false);
  a_framed_separator_inside_a_line_is_text(true);
  return 0;
}
