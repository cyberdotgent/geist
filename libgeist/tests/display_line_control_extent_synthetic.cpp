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

#include "geist/detail/control_ir.hpp"
#include "geist/detail/display_lines.hpp"
#include "geist/detail/internal.hpp"
#include "test_failures.hpp"

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

} // namespace

int main() {
  cz_operands_stop_at_the_display_line();
  an_unframed_record_decides_nothing();
  a_length_byte_is_never_display_text();
  return 0;
}
