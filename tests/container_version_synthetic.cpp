// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Containers older than directory version 1.2 (doc/boo-spec, container,
// "Containers Older Than Version 1.2").  Five real books carry them:
// version 1.0 EDCGV1R2, EDCIV1R2, EHMGMST and EHMPMST, version 1.1 EDCUSER.
// Their layout is the version 1.2 layout, but three things are read
// differently:
//
//   * dictionary literal bytes go through text selector 351, not 500:
//     version 1.0 always, version 1.1 when its directory word 0x004c is 0.
//     EDCGV1R2 VERSION draws its revision bars with byte 0x4F, which hosted
//     BookServer serves as `|` (DT 19891031000000); CP500 reads it as `!`.
//   * the topic-number control is spelled `CTOPIC#`, not `CTOPICN`:
//     EDCGV1R2 record 2 opens `SHABSTRACT`, `CTOPIC# 1.`, `CPARENT`.
//   * a `CTOCE` line is `CTOCE <style> <id> <title>`, with no nesting:
//     EDCGV1R2 CONTENTS `CTOCE 3 3.3.1 VSAM Data Sets`.  The nesting is the
//     target topic's `CPARENT` chain, as the stored field is in version 1.2.
//
// Everything here is synthetic: the test builds directory pages, a
// dictionary page and topic records by hand and opens no book.

#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/topic_header_title.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TocEntryGrammar;
using geist::detail::TokenWords;
using Line = std::vector<TokenWords>;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "container_version_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

// A three-page container: header, directory, and one dictionary page holding
// a single token whose only letter is the literal byte `literal`.
std::vector<std::uint8_t> book(std::uint8_t version, std::uint16_t parameter,
                               std::uint8_t literal) {
  std::vector<std::uint8_t> bytes(3 * geist::boo_page_size);
  bytes[1] = 1;
  const std::size_t directory = geist::boo_page_size;
  bytes[directory + 0x10] = 0x40;
  bytes[directory + 0x11] = 0xf1;
  bytes[directory + 0x12] = 0x4b;
  bytes[directory + 0x13] = version;
  bytes[directory + 0x14] = 0xdc;
  bytes[directory + 0x15] = 0xf0;
  bytes[directory + 0x17] = 2;  // last logical page
  bytes[directory + 0x29] = 2;  // dictionary run starts at logical page 2
  bytes[directory + 0x2f] = 1;  // and is one page long
  bytes[directory + 0x4c] = static_cast<std::uint8_t>(parameter >> 8);
  bytes[directory + 0x4d] = static_cast<std::uint8_t>(parameter);
  const std::size_t page = 2 * geist::boo_page_size;
  const std::vector<std::uint8_t> dictionary = {
      0x01, 0x00, 0x00, 0x0d,  // page class 0x0100, used to offset 13
      0x08, 0x00, 0x00, 0x00,  // one block: 8 bytes, no key prefix
      0x04, 0xdc, 0x00,        // one token record: key 0xdc00
      0x01, literal};          // base record of one literal byte
  std::copy(dictionary.begin(), dictionary.end(), bytes.begin() + page);
  return bytes;
}

std::optional<std::uint16_t> dictionary_word(std::uint8_t version,
                                             std::uint16_t parameter,
                                             std::uint8_t literal) {
  const auto bytes = book(version, parameter, literal);
  const auto prologue =
      geist::detail::read_container_prologue(bytes, "synthetic.boo");
  try {
    const auto tokens =
        geist::detail::decode_experimental_dictionary(bytes, prologue.directory);
    const auto found = tokens.find(0xdc00);
    if (found == tokens.end() || found->second.size() != 1) return 0xfffe;
    return found->second.front();
  } catch (const std::runtime_error &) {
    return std::nullopt;
  }
}

void verify_dictionary_text_selector() {
  // 0x4F is the vertical bar under selector 351, `!` under selector 500.
  require(dictionary_word(0xf0, 0, 0x4f) == std::optional<std::uint16_t>('|'),
          "version 1.0 did not read its literals through selector 351");
  require(dictionary_word(0xf0, 500, 0x4f) == std::optional<std::uint16_t>('|'),
          "version 1.0 must ignore the directory text parameter");
  require(dictionary_word(0xf1, 0, 0x4f) == std::optional<std::uint16_t>('|'),
          "version 1.1 with parameter 0 did not fall back to selector 351");
  require(dictionary_word(0xf1, 500, 0x4f) == std::optional<std::uint16_t>('!'),
          "version 1.1 with parameter 500 did not use selector 500");
  require(dictionary_word(0xf2, 500, 0x4f) == std::optional<std::uint16_t>('!'),
          "version 1.2 changed: selector 500 reads 0x4F as '!'");
  // The brackets move too: 0xAD/0xBD under 351, 0x4A/0x5A under 500.
  require(dictionary_word(0xf0, 0, 0xad) == std::optional<std::uint16_t>('['),
          "selector 351 did not read 0xAD as '['");
  require(dictionary_word(0xf0, 0, 0x4a) == std::optional<std::uint16_t>(0xa2),
          "selector 351 did not read 0x4A as the cent sign");
  require(dictionary_word(0xf2, 500, 0x4a) == std::optional<std::uint16_t>('['),
          "version 1.2 changed: selector 500 reads 0x4A as '['");
  // No book has shown these: refuse rather than read the wrong table.
  require(!dictionary_word(0xf2, 0, 0x4f),
          "version 1.2 without a text parameter was decoded");
  require(!dictionary_word(0xf3, 1, 0x4f),
          "version 1.3 dictionary text was decoded through a guessed table");
}

void verify_topic_number_spelling() {
  require(geist::detail::is_topic_header_record(
              ", SHABSTRACT, CTOPIC# 1. CPARENT ?library CFORWARDLEVEL TITLE"),
          "a version 1.0 topic header (CTOPIC#) was not recognised");
  require(geist::detail::is_topic_header_record(
              ", shcover, ctopicn 1. cparent ? cforwardlevel TITLE"),
          "a version 1.2 topic header (ctopicn) is no longer recognised");
}

void verify_toc_entry_grammar() {
  geist::BooDirectory directory;
  directory.version_text = " 1.0";
  require(geist::detail::toc_entry_grammar(directory) ==
              TocEntryGrammar::style_only,
          "version 1.0 CTOCE lines must be read without a nesting field");
  directory.version_text = " 1.1";
  require(geist::detail::toc_entry_grammar(directory) ==
              TocEntryGrammar::style_only,
          "version 1.1 CTOCE lines must be read without a nesting field");
  directory.version_text = " 1.2";
  require(geist::detail::toc_entry_grammar(directory) ==
              TocEntryGrammar::nesting_and_style,
          "version 1.2 CTOCE lines lost their nesting field");

  const std::string record =
      "CTOCE 3 3.3.1 VSAM Data Sets ? CTOCE 1 INDEX Index";
  const auto entries = geist::detail::extract_toc_entries(
      record, {}, TocEntryGrammar::style_only);
  require(entries.size() == 2 && entries[0].id == "3.3.1" &&
              entries[0].style == 3 && entries[0].title == "VSAM Data Sets" &&
              entries[1].id == "INDEX" && entries[1].style == 1 &&
              entries[1].title == "Index",
          "a style-only CTOCE line was not read as <style> <id> <title>");
}

geist::detail::TopicData topic(const std::string &id,
                               std::optional<std::string> parent) {
  geist::detail::TopicData data;
  data.id = id;
  data.parent = std::move(parent);
  return data;
}

void verify_nesting_from_parent_chain() {
  const std::vector<std::string> records = {
      ", SHCONTENTS, CTOPIC# 5. CHDLEVEL :TOC ? CTOCDEF=0 1 0 2 ? "
      "CTOCE 1 COVER Book Cover ? CTOCE 1 3.0 Chapter 3. Features ? "
      "CTOCE 2 3.3 Input and Output ? CTOCE 3 3.3.1 VSAM Data Sets"};
  const std::vector<geist::detail::TopicData> topics = {
      topic("COVER", ""), topic("CONTENTS", ""), topic("3.0", ""),
      topic("3.3", "3.0"), topic("3.3.1", "3.3")};
  const auto toc = geist::detail::build_table_of_contents(
      records, topics, false, nullptr, nullptr, TocEntryGrammar::style_only);
  require(toc.size() == 4 && toc[0].level == 0 && toc[1].level == 0 &&
              toc[2].level == 1 && toc[3].level == 2 && toc[3].style == 3,
          "style-only CTOCE nesting is not the CPARENT chain length");

  // A parent that is not a topic of the book: no nesting is guessed.
  auto broken = topics;
  broken[4].parent = "9.9";
  require(geist::detail::build_table_of_contents(records, broken, false,
                                                 nullptr, nullptr,
                                                 TocEntryGrammar::style_only)
              .empty(),
          "an unresolvable CPARENT chain still produced a contents list");
  // A parent that was never read: likewise.
  broken = topics;
  broken[3].parent.reset();
  require(geist::detail::build_table_of_contents(records, broken, false,
                                                 nullptr, nullptr,
                                                 TocEntryGrammar::style_only)
              .empty(),
          "an unread CPARENT still produced a contents list");
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
  record.logical_record = 2;
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  record.ir.logical_record = 2;
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

void verify_parent_display_line() {
  const auto nested = build({{words("SH3.3.1")},
                             {words("CTOPIC#"), words("31")},
                             {words("CPARENT"), words("3.3")},
                             {words("CHDLEVEL"), words(":H3")},
                             {words("ST"), words("VSAM"), words(" Data"),
                              words(" Sets")}});
  require(geist::detail::topic_header_parent_of_record(nested) ==
              std::optional<std::string>("3.3"),
          "the CPARENT display line did not yield the parent topic id");
  const auto top = build({{words("SHCOVER")},
                          {words("CTOPIC#"), words("3")},
                          {words("CPARENT")},
                          {words("ST"), words("Book"), words(" Cover")}});
  require(geist::detail::topic_header_parent_of_record(top) ==
              std::optional<std::string>(""),
          "an empty CPARENT line did not read as a top-level topic");
  const auto none = build({{words("SHCOVER")},
                           {words("ST"), words("Book"), words(" Cover")}});
  require(!geist::detail::topic_header_parent_of_record(none),
          "a header without CPARENT invented a parent");
}

} // namespace

int main() {
  verify_dictionary_text_selector();
  verify_topic_number_spelling();
  verify_toc_entry_grammar();
  verify_nesting_from_parent_chain();
  verify_parent_display_line();
}
