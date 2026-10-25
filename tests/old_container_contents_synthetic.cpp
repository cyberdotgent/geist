// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The CONTENTS topic of a container older than directory version 1.2
// (doc/boo-spec, toc, "CTOCE In Version 1.0 And 1.1 Containers").  Its
// entries are `CTOCE <style> <id> <title>`, one field fewer than version
// 1.2 writes: EDCGV1R2 CONTENTS record 9 line 18 is `CTOCE 1 ABSTRACT
// Abstract`, line 24 `CTOCE 2 1.1 Who Might Use This Book`, line 27
// `CTOCE 3 1.3.1 IBM C/370 Publications`.  The nesting a version 1.2 entry
// stores is the target topic's `CPARENT` chain length, and that is where an
// older entry's nesting comes from.  Read in the version 1.2 grammar the
// first title word becomes the topic id (`Abstract`, `Who`), which is the
// mistake hosted BookServer makes on these pages (DT 19891031000000), so
// hosted is no evidence for this page and the line is read by its version.
//
// Everything here is synthetic: the test builds whole topic records by hand.

#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/generated_toc_index_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::GeneratedContentsGrammarIR;
using geist::detail::TocEntryGrammar;
using geist::detail::TokenWords;
using Line = std::vector<TokenWords>;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "old_container_contents_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

// One record of display lines, framed as the record decoder frames them:
// each line opens with a length byte whose dictionary spelling is a box
// glyph, then one token per word.
DecodedLogicalRecordSource build(const std::vector<Line> &lines) {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x40;
  for (const auto &content : lines) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(2 * content.size()), 1});
    record.tokens.push_back(TokenWords{0x25BA});
    for (const auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(token);
    }
  }
  record.logical_record = 9;
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  record.ir.logical_record = 9;
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
  return record;
}

Line line(const std::vector<std::string> &cells) {
  Line out;
  for (const auto &cell : cells) out.push_back(words(cell));
  return out;
}

// EDCGV1R2 CONTENTS record 9, shortened: the envelope, the seven CTOCDEF
// lines exactly as stored, and four entries.
std::vector<DecodedLogicalRecordSource> contents_records() {
  std::vector<Line> lines = {
      line({"SHCONTENTS"}),
      line({"CTOPIC#", "5"}),
      line({"CPARENT"}),
      line({"CFORWARDLEVEL", "1.0"}),
      line({"CBACKLEVEL", "VERSION"}),
      line({"CSUMMARY", "CONTENTS", "1#3L52#3L27"}),
      line({"CHDLEVEL", ":TOC"}),
      line({"CSOURCEFN", "EDCGI"}),
      line({"ST", "Table", "of", "Contents"}),
      line({"C.SP", "5", "P"}),
      line({"CTOCDEF=0", "1", "0", "2"}),
      line({"CTOCDEF=1", "1", "0", "2"}),
      line({"CTOCDEF=2", "0", "0"}),
      line({"CTOCDEF=3", "0", "2"}),
      line({"CTOCDEF=4", "0", "4"}),
      line({"CTOCDEF=5", "0", "6"}),
      line({"CTOCDEF=6", "0", "8"}),
      line({"CTOCE", "1", "ABSTRACT", "Abstract"}),
      line({"CTOCE", "1", "1.0", "Chapter", "1.", " Introduction"}),
      line({"CTOCE", "2", "1.1", "Who", "Might", "Use", "This",
            "Book"}),
      line({"CTOCE", "3", "1.3.1", "IBM", "C/370", "Publications"}),
      // EDCGV1R2 CONTENTS record 11 lines 1 and 2, after the last entry:
      // the reader skips every `C.` line, operands included.
      line({"C.CP"}),
      line({"C.CP", "12DV"}),
  };
  std::vector<DecodedLogicalRecordSource> records;
  records.push_back(build(lines));
  return records;
}

GeneratedContentsGrammarIR older_grammar() {
  GeneratedContentsGrammarIR grammar;
  grammar.grammar = TocEntryGrammar::style_only;
  grammar.nesting = {{"abstract", 0}, {"contents", 0}, {"1.0", 0},
                     {"1.1", 1},      {"1.3", 1},      {"1.3.1", 2}};
  return grammar;
}

void verify_older_entries_are_read_by_their_grammar() {
  const auto records = contents_records();
  require(records.front().ir.display_lines_parse,
          "the synthetic CONTENTS record's display lines did not parse");
  const auto grammar = older_grammar();
  std::string error;
  const auto topic = geist::detail::extract_generated_toc_index_topic_ir(
      records, nullptr, &error, &grammar);
  require(topic.has_value(),
          "a version 1.0 CONTENTS topic was declined: " + error);
  if (!topic) return;
  require(topic->entries.size() == 4, "the four CTOCE entries were not read");
  if (topic->entries.size() != 4) return;
  const auto &abstract = topic->entries[0];
  require(abstract.style == 1 && abstract.topic_id == "ABSTRACT" &&
              abstract.title == "Abstract" && abstract.depth == 0,
          "`CTOCE 1 ABSTRACT Abstract` is not style 1, id ABSTRACT, title "
          "Abstract at nesting 0");
  const auto &chapter = topic->entries[1];
  require(chapter.style == 1 && chapter.topic_id == "1.0" &&
              chapter.title == "Chapter 1.  Introduction" && chapter.depth == 0,
          "a chapter entry lost its id or its title's inner spacing");
  const auto &who = topic->entries[2];
  require(who.style == 2 && who.topic_id == "1.1" &&
              who.title == "Who Might Use This Book" && who.depth == 1,
          "`CTOCE 2 1.1 Who Might Use This Book` did not nest under 1.0");
  const auto &publications = topic->entries[3];
  require(publications.style == 3 && publications.topic_id == "1.3.1" &&
              publications.depth == 2,
          "the nesting is not the CPARENT chain length");
  require(geist::detail::verify_generated_toc_index_topic_ir(
              records, nullptr, *topic, &error, &grammar),
          "the extracted topic failed its own verifier: " + error);

  // The verifier re-reads with the grammar it is given: a nesting that
  // differs from the one the model was built with is a different topic.
  auto other = grammar;
  other.nesting["1.3.1"] = 1;
  require(!geist::detail::verify_generated_toc_index_topic_ir(
              records, nullptr, *topic, &error, &other),
          "the verifier accepted a model built from another nesting");
}

void verify_version_1_2_grammar_still_declines() {
  // Read as `<nesting> <style> <id> <title>`, `CTOCE 1 ABSTRACT Abstract`
  // has a non-decimal style and `CTOCE 2 1.1 Who ...` an id `Who`: the
  // version 1.2 reading of these lines is wrong, and stays declined.
  const auto records = contents_records();
  std::string error;
  require(!geist::detail::extract_generated_toc_index_topic_ir(
              records, nullptr, &error),
          "a version 1.0 CONTENTS topic was read in the version 1.2 grammar");
}

void verify_unresolved_nesting_declines() {
  // An entry whose topic has no CPARENT chain to the top is not given a
  // guessed nesting: the topic declines.
  const auto records = contents_records();
  auto grammar = older_grammar();
  grammar.nesting.erase("1.1");
  std::string error;
  require(!geist::detail::extract_generated_toc_index_topic_ir(
              records, nullptr, &error, &grammar),
          "an entry without a CPARENT chain was given a nesting");
  require(error.find("1.1") != std::string::npos,
          "the decline does not name the entry: " + error);
}

void verify_lowering_routes_the_older_contents() {
  const auto records = contents_records();
  const auto grammar = older_grammar();
  std::string rejection;
  geist::detail::TypedLoweringTraceIR trace;
  const auto document = geist::detail::try_lower_topic_to_document_ir(
      {"CONTENTS", "Table of Contents", ":TOC", 5, 9, 10}, records, nullptr,
      &rejection, &trace, nullptr, &grammar);
  require(document.has_value(),
          "the version 1.0 CONTENTS topic was not lowered: " + rejection);
  require(trace.family == "generated navigation",
          "the version 1.0 CONTENTS topic went to family '" + trace.family +
              "'");
}

} // namespace

int main() {
  verify_older_entries_are_read_by_their_grammar();
  verify_version_1_2_grammar_still_declines();
  verify_unresolved_nesting_declines();
  verify_lowering_routes_the_older_contents();
}
