// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The topic's metadata envelope and its title control, decided on the carried
// display-line framing (issue #67, the envelope and topic-metadata classes).
//
// A record payload is `<length byte><that many bytes of tokens>`.  The length
// byte is row geometry and nothing else, but a token reader resolves it
// through the dictionary like any other byte, so it routinely spells an
// ordinary word -- `.`, `ST`, `cfont`.  Nothing local separates the two roles;
// only the walk from the record start does, which the decoder performs once
// and stamps on every token (`TokenFramingRole`, book_ir.hpp).
//
// Everything here is synthetic: the tests build `DecodedLogicalRecordSource`
// values by hand and open no book (issue #59).

#include "geist/detail/container/control_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/layout_ir.hpp"
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

using geist::detail::BookControlKind;
using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "topic_envelope_ir_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

void append(DecodedLogicalRecordSource &record, std::uint16_t encoded,
            std::uint8_t width, const std::string &value) {
  record.encoded_tokens.push_back({encoded, width});
  record.tokens.push_back(words(value));
}

// One display line: its length byte -- whose dictionary spelling is `slot`,
// an ordinary word -- and then one two-byte token per cell.
void line_words(DecodedLogicalRecordSource &record, const TokenWords &slot,
                const std::vector<TokenWords> &cells) {
  record.encoded_tokens.push_back(
      {static_cast<std::uint16_t>(2 * cells.size()), 1});
  record.tokens.push_back(slot);
  for (const auto &cell : cells) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(0x100 + record.tokens.size()), 2});
    record.tokens.push_back(cell);
  }
}

void line(DecodedLogicalRecordSource &record, const std::string &slot,
          const std::vector<std::string> &cells) {
  std::vector<TokenWords> cell_words;
  cell_words.reserve(cells.size());
  for (const auto &cell : cells) cell_words.push_back(words(cell));
  line_words(record, words(slot), cell_words);
}

// Rebuilds the typed IR of a hand-assembled record and lets the decoder decide
// the framing, exactly as `decode_record_payload_ir` does in production.  No
// test re-derives the display-line walk.
void refresh(DecodedLogicalRecordSource &record, std::uint32_t base) {
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  record.ir.logical_record = record.logical_record;
  record.ir.tokens.clear();
  auto byte = base;
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
  record.ir.payload_range = {base, byte};
  geist::detail::assign_display_line_framing(record.ir);
  record.control_segments = geist::detail::decode_control_segments(
      record.logical_record, record.assembled, record.encoded_tokens,
      record.ir.display_lines);
  geist::detail::demote_display_line_owned_controls(record);
}

std::size_t title_controls(const DecodedLogicalRecordSource &record) {
  std::size_t count = 0;
  for (const auto &segment : record.control_segments)
    if (segment.kind == BookControlKind::title) ++count;
  return count;
}

// ---------------------------------------------------------------------------
// The `ST` title control
// ---------------------------------------------------------------------------

// GC28-183 2.5.9 record 216 display line 32 is the JCL example
// `     //JOBC  JOB     ,'V. ST PIERRE',MSGLEVEL=(1,1)`.  The flattened string
// splits a segment on the word `ST`, and the topic reads as carrying a second
// title control; the framing shows the word standing inside a row.
void an_st_inside_a_row_is_not_a_title() {
  DecodedLogicalRecordSource record;
  record.logical_record = 216;
  line(record, "slot", {"ST", "Alpha", "Beta"});
  line(record, "next", {"JOB", "V.", "ST", "PIERRE"});
  refresh(record, 0);

  require(record.ir.display_lines_parse,
          "the synthetic record's display lines did not parse");
  require(title_controls(record) == 1,
          "an `ST` word with a displayed word in front of it on its own "
          "display line is that row's text, not a second title control; " +
              std::to_string(title_controls(record)) +
              " title controls were decoded");
}

// SH12-565 3.1.4.1 record 184 display line 23 is the definition term `   ST`
// of a two-column list, styled by the `cfont 3 2 P` on the line above it.  It
// opens no display line: the row's three-cell indent stands in front of it,
// and every genuine control of a record begins at the first cell of its line.
void an_indented_st_is_not_a_title() {
  DecodedLogicalRecordSource record;
  record.logical_record = 184;
  line(record, "slot", {"ST", "Alpha", "Beta"});
  line(record, "next", {"   ", "ST"});
  refresh(record, 0);

  require(record.ir.display_lines_parse,
          "the synthetic record's display lines did not parse");
  require(title_controls(record) == 1,
          "an `ST` word that does not stand at the first cell of its display "
          "line opens no control; " +
              std::to_string(title_controls(record)) +
              " title controls were decoded");
}

// Fail-closed in the other direction: the title control itself must survive.
void a_title_that_opens_its_line_stays_a_title() {
  DecodedLogicalRecordSource record;
  record.logical_record = 5;
  line(record, "slot", {"ST", "Alpha", "Beta"});
  refresh(record, 0);

  require(title_controls(record) == 1,
          "an `ST` standing at the first cell of its display line is the "
          "title control and must keep that reading");
}

// Fail-closed: a record whose payload does not tile into display lines has no
// decided framing, so nothing is demoted.
void an_unframed_st_is_not_demoted() {
  DecodedLogicalRecordSource record;
  record.logical_record = 6;
  append(record, 99, 1, "lead");
  append(record, 0x40, 2, "JOB");
  append(record, 0x41, 2, "ST");
  append(record, 0x42, 2, "PIERRE");
  refresh(record, 0);

  require(!record.ir.display_lines_parse,
          "the unframed fixture unexpectedly framed into display lines");
  require(title_controls(record) == 1,
          "an unframed record decides nothing and must keep its previous "
          "reading of the `ST` word");
}

// ---------------------------------------------------------------------------
// The metadata envelope
// ---------------------------------------------------------------------------

std::string envelope_error(std::vector<DecodedLogicalRecordSource> records,
                           const std::string &title) {
  const auto layout = geist::detail::extract_layout_ir(records);
  std::string error;
  const auto ownership =
      geist::detail::build_verified_ownership_ir(records, layout, &error);
  if (!ownership) return "ownership: " + error;
  geist::detail::extract_prose_topic_ir(records, layout, *ownership, title,
                                        nullptr, &error);
  return error;
}

// SC26-457 FRONT_2.1.1, FRONT_2.1.2 and FRONT_3.2: the `ST` title control is
// glued into the `csourcefn` segment, so the whole topic spends exactly the
// eight metadata segments.  An arity precondition that also counted the title
// rejected the topic before the walk could look at it.
void a_glued_title_is_not_a_short_envelope() {
  DecodedLogicalRecordSource record;
  record.logical_record = 30;
  // Every length byte here spells a two-cell space run, so none of them opens
  // a segment of its own and the topic really does spend exactly the eight
  // metadata segments -- the shape the arity guard has to admit.
  line_words(record, TokenWords{3}, {words("sh1.1")});
  line(record, "  ", {"ctopicn", "3"});
  line(record, "  ", {"cparent", "1"});
  line(record, "  ", {"cforwardlevel", "1.2"});
  line(record, "  ", {"cbacklevel", "1.0"});
  line(record, "  ", {"csummary", "5", "0", "5"});
  line(record, "  ", {"chdlevel", ":H2"});
  // `csourcefn SRCFILE ST? Alpha Body text.`: the record writes its attach
  // byte directly behind the `ST`, so the flattened splitter -- which needs a
  // space, `=`, `,` or `.` after an opcode -- never splits there and the title
  // arrives glued into the `csourcefn` segment, exactly as it does in
  // SC26-457 record 30.
  line_words(record, words("  "),
             {words("csourcefn"), words("FILE01"), words("ST"),
              TokenWords{1, 0xFFFF}, words("Alpha"), words("Body"),
              words("text.")});
  refresh(record, 0);

  require(record.control_segments.size() == 8,
          "the fixture must spend exactly eight segments; it spent " +
              std::to_string(record.control_segments.size()));
  const auto error = envelope_error({std::move(record)}, "Alpha");
  require(error.find("first record lacks the topic metadata envelope") ==
              std::string::npos,
          "a topic whose `ST` title is glued into its `csourcefn` segment "
          "spends exactly eight segments and must not be rejected for lacking "
          "an envelope; the error was '" + error + "'");
}

// SC09-138 8.5.6.6 and DREICMST 1.5.4.1: the metadata run continues into the
// next record, and that record opens with a display line like every other, so
// its first byte is a length byte.  Here the byte's dictionary spelling is
// `.` -- the terminator of the previous control's operand -- and the flattened
// splitter opens a segment on it in the middle of the run.
void a_length_byte_between_records_is_not_a_metadata_control() {
  DecodedLogicalRecordSource first;
  first.logical_record = 1669;
  line(first, "slot", {"sh1.1"});
  line(first, "slot", {"ctopicn", "3"});
  line(first, "slot", {"cparent", "1"});
  refresh(first, 0);

  DecodedLogicalRecordSource second;
  second.logical_record = 1670;
  line(second, ".", {"cforwardlevel", "1.2"});
  line(second, "slot", {"cbacklevel", "1.0"});
  line(second, "slot", {"csummary", "5", "0", "5"});
  line(second, "slot", {"chdlevel", ":H2"});
  line(second, "slot", {"csourcefn", "SRCFILE"});
  line(second, "slot", {"ST", "Alpha"});
  line(second, "slot", {"Body", "text", "here."});
  refresh(second, 4096);

  const auto error =
      envelope_error({std::move(first), std::move(second)}, "Alpha");
  require(error.find("topic metadata controls are incomplete") ==
              std::string::npos,
          "the length byte that opens the record the metadata run continues "
          "into is row geometry, not a metadata control out of order; the "
          "error was '" + error + "'");
}

// OFCUSEOV PREFACE.2 and QSYSNEWG B.3: a non-numeric topic id standing alone
// in the topic's first record, with the `CTOPICN` that corroborates it in the
// next.  The segment classifier promotes an `SH` word only on a digit or on a
// `CTOPICN` in the same record, so the corroboration has to be read across the
// break the metadata run legitimately crosses.
void a_corroborated_topic_start_crosses_the_record_break() {
  DecodedLogicalRecordSource first;
  first.logical_record = 16;
  line(first, "slot", {"SHPREFACE.2"});
  refresh(first, 0);

  DecodedLogicalRecordSource second;
  second.logical_record = 17;
  line(second, "slot", {"ctopicn", "7"});
  line(second, "slot", {"cparent", "PREFACE"});
  line(second, "slot", {"cforwardlevel", "PREFACE.3"});
  line(second, "slot", {"cbacklevel", "PREFACE.1"});
  line(second, "slot", {"csummary", "5", "0", "5"});
  line(second, "slot", {"chdlevel", ":H2"});
  line(second, "slot", {"csourcefn", "SRCFILE"});
  line(second, "slot", {"ST", "Alpha"});
  line(second, "slot", {"Body", "text", "here."});
  refresh(second, 4096);

  const auto error =
      envelope_error({std::move(first), std::move(second)}, "Alpha");
  require(error.find("topic metadata controls are incomplete or out of "
                     "order") == std::string::npos,
          "a record-leading `SH` topic id whose `CTOPICN` follows in the next "
          "record is the topic-start control; the error was '" + error + "'");
}

// Fail-closed: without the `CTOPICN` corroboration the leading `SH` word stays
// prose, exactly as `promote_corroborated_topic_start` leaves it inside one
// record.
void an_uncorroborated_topic_start_stays_prose() {
  DecodedLogicalRecordSource first;
  first.logical_record = 16;
  line(first, "slot", {"SHPREFACE.2"});
  refresh(first, 0);

  DecodedLogicalRecordSource second;
  second.logical_record = 17;
  line(second, "slot", {"cparent", "PREFACE"});
  line(second, "slot", {"cforwardlevel", "PREFACE.3"});
  line(second, "slot", {"cbacklevel", "PREFACE.1"});
  line(second, "slot", {"csummary", "5", "0", "5"});
  line(second, "slot", {"chdlevel", ":H2"});
  line(second, "slot", {"csourcefn", "SRCFILE"});
  line(second, "slot", {"ST", "Alpha"});
  line(second, "slot", {"Body", "text", "here."});
  refresh(second, 4096);

  const auto error =
      envelope_error({std::move(first), std::move(second)}, "Alpha");
  require(!error.empty(),
          "an `SH` word with no `CTOPICN` behind it is not corroborated and "
          "the topic must still decline");
}

} // namespace

int main() {
  an_st_inside_a_row_is_not_a_title();
  an_indented_st_is_not_a_title();
  a_title_that_opens_its_line_stays_a_title();
  an_unframed_st_is_not_demoted();
  a_glued_title_is_not_a_short_envelope();
  a_length_byte_between_records_is_not_a_metadata_control();
  a_corroborated_topic_start_crosses_the_record_break();
  an_uncorroborated_topic_start_stays_prose();
  return 0;
}
