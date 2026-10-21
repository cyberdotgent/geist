// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Synthetic message-catalogue fixtures.
//
// `libgeist` may read no BOO book except `packet.boo` (#59), and packet
// carries no message catalogue at all, so every record here is built by hand
// with `assemble_logical_record_with_sources`. The previous `message_*` tests
// were deleted for that reason; this file starts that coverage again.
//
// What is covered: `extract_message_catalog_ir` admits a minimal ordered
// Meaning/Action catalogue, and conserves every word of the entry -- both when
// the whole entry sits in one logical record and when it spans two, so the
// second record's section run is a continuation.
//
// Case 3 adds a record whose payload really does tile into display lines, so
// the record's stored `TokenFramingRole` is decided and the message family
// reads it through `OwnershipIR`'s `SourceFieldRole`. Two of its length bytes
// spell ordinary dictionary words; neither may reach the entry text. Both
// rules were checked to be load-bearing by removing them and watching the
// case fail: the mid-row length byte leaks "occurred", and the length byte
// LayoutIR offers as a body row's marker slot leaks "segment".
//
// Case 4 is the record-prefix row of SC31-711 5.0 message 508: a record whose
// first display line opens on a blank-spelled length byte and a blank origin,
// so the layout leaves it without a row. It must become one row of the
// Application/Action listing, and mutating any of its byte, token, column or
// text provenance must fail verification.
//
// What is honestly NOT covered:
//
//  * the *positioned* half of the same decision -- a word the framing draws
//    at the end of a display line that LayoutIR then offers as the next row's
//    marker slot. Case 3 contains one ("adapter"), and the entry keeps it,
//    but the compact-envelope fallback the framing replaces keeps it too, so
//    removing the rule does not fail the case. Only the hosted SC31-711 5.0
//    audit and the whole-corpus differential distinguish the two answers.
//  * the record-continuation shape behind #66, where LayoutIR opens the
//    continued row at a marker slot and leaves the words before it as an
//    opaque prefix. Reproducing that needs a synthetic record whose framing
//    makes the layout choose a mid-record row start; until this fixture can
//    state that, those paths are covered by the corpus differential and the
//    hosted audit only.
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/layout_ir.hpp"
#include "geist/detail/ir/message_ir.hpp"
#include "geist/detail/ir/message_section_blocks_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "test_failures.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

struct Token {
  TokenWords words;
  std::uint16_t encoded_value = 0;
  std::uint8_t encoded_width = 1;
};

Token text_token(const std::string &value, std::uint16_t encoded) {
  return {words(value), encoded, 1};
}

DecodedLogicalRecordSource make_source(std::uint32_t logical_record,
                                       const std::vector<Token> &tokens,
                                       bool split_on_framing = false) {
  DecodedLogicalRecordSource source;
  source.logical_record = logical_record;
  source.ir.logical_record = logical_record;
  auto byte = static_cast<std::uint32_t>(logical_record * 1000);
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    const geist::detail::EncodedLogicalToken encoded{
        tokens[index].encoded_value, tokens[index].encoded_width};
    source.tokens.push_back(tokens[index].words);
    source.encoded_tokens.push_back(encoded);
    source.ir.tokens.push_back(
        {index,
         encoded,
         tokens[index].words,
         {byte, static_cast<std::uint32_t>(byte + encoded.width)},
         false,
         3});
    byte += encoded.width;
  }
  source.ir.payload_range = {
      static_cast<std::uint32_t>(logical_record * 1000), byte};
  geist::detail::assign_display_line_framing(source.ir);
  source.assembled =
      geist::detail::assemble_logical_record_with_sources(source.tokens);
  // The record reader's own split sees the framing; the earlier cases keep
  // the compatibility split they were written against.
  source.control_segments =
      split_on_framing
          ? geist::detail::decode_control_segments(
                source.logical_record, source.assembled,
                source.encoded_tokens, source.ir.display_lines)
          : geist::detail::decode_control_segments(source.logical_record,
                                                   source.assembled);
  if (split_on_framing)
    geist::detail::demote_display_line_owned_controls(source);
  return source;
}

struct Catalog {
  std::vector<DecodedLogicalRecordSource> sources;
  geist::detail::LayoutIR layout;
  std::optional<geist::detail::VerifiedOwnershipIR> ownership;
  std::optional<geist::detail::MessageCatalogIR> catalog;
  std::string error;
};

Catalog build(std::vector<DecodedLogicalRecordSource> sources) {
  Catalog result;
  result.sources = std::move(sources);
  result.layout = geist::detail::extract_layout_ir(result.sources);
  result.ownership = geist::detail::build_verified_ownership_ir(
      result.sources, result.layout, &result.error);
  if (!result.ownership)
    return result;
  result.catalog = geist::detail::extract_message_catalog_ir(
      result.sources, result.layout, *result.ownership, &result.error);
  return result;
}

std::string entry_text(const geist::detail::MessageEntryIR &entry) {
  std::string out = entry.headline.text;
  for (const auto &section : entry.sections)
    for (const auto &paragraph : section.paragraphs) {
      if (!out.empty())
        out.push_back(' ');
      out += paragraph.text;
    }
  return out;
}

} // namespace

int main() {
  const auto t = [](const std::string &value, std::uint16_t encoded) {
    return text_token(value, encoded);
  };
  // A row origin: the padding run that opens a display row.
  const auto origin = [&] { return t(std::string(13, ' '), 15); };

  // Case 1 -- the ordinary shape, entirely inside one logical record. Both
  // section labels have a row of their own.
  {
    std::vector<Token> tokens;
    const auto push = [&](std::initializer_list<Token> more) {
      for (const auto &token : more)
        tokens.push_back(token);
    };
    push({t("SRMSG", 200), t("601", 201)});
    push({t("cfont", 202), t("3", 203), t("3", 203), t("2", 204)});
    push({origin(), t("601", 201), t("Duplicate", 205), t("adapter", 205)});
    push({t("cfont", 202), t("13", 206), t("8", 207), t("2", 204)});
    push({origin(), t("Meaning", 150), t(":", 25), t("An", 208), t("address", 209),
          t("was", 210), t("found", 211)});
    push({t("cfont", 202), t("13", 206), t("7", 212), t("2", 204)});
    push({origin(), t("Action", 151), t(":", 25), t("Ignore", 213),
          t("the", 113), t("warning", 214)});
    const auto built = build({make_source(10, tokens)});
    require(built.ownership.has_value(),
            "synthetic message ownership was rejected: " + built.error);
    require(built.catalog.has_value(),
            "synthetic message catalog declined: " + built.error);
    if (built.catalog) {
      require(built.catalog->entries.size() == 1,
              "synthetic message catalog did not produce one entry");
      const auto text = entry_text(built.catalog->entries.front());
      require(text == "601 Duplicate adapter An address was found Ignore the "
                      "warning",
              "single-record message entry text is wrong: [" + text + "]");
    }
  }

  // Case 2 -- the section label is carried by a record continuation, so the
  // row the typed boundary points at is body text whose spelling does not
  // repeat the label. Before #66 the whole row was discarded.
  {
    std::vector<Token> first;
    const auto push_first = [&](std::initializer_list<Token> more) {
      for (const auto &token : more)
        first.push_back(token);
    };
    push_first({t("SRMSG", 200), t("602", 201)});
    push_first({t("cfont", 202), t("3", 203), t("3", 203), t("2", 204)});
    push_first({origin(), t("602", 201), t("Starting", 205), t("monitor", 206)});
    push_first({t("cfont", 202), t("13", 206), t("8", 207), t("2", 204)});

    std::vector<Token> second;
    const auto push_second = [&](std::initializer_list<Token> more) {
      for (const auto &token : more)
        second.push_back(token);
    };
    push_second({origin(), t("Meaning", 150), t(":", 25), t("This", 164),
                 t("message", 82), t("is", 76), t("logged", 215)});
    push_second({t("cfont", 202), t("13", 206), t("7", 212), t("2", 204)});
    push_second({origin(), t("Action", 151), t(":", 25), t("None", 216)});

    const auto built =
        build({make_source(20, first), make_source(21, second)});
    require(built.ownership.has_value(),
            "continuation ownership was rejected: " + built.error);
    require(built.catalog.has_value(),
            "continuation catalog declined: " + built.error);
    if (built.catalog && built.catalog->entries.size() == 1) {
      const auto text = entry_text(built.catalog->entries.front());
      for (const char *word : {"message", "is", "logged"})
        require(text.find(word) != std::string::npos,
                std::string("continuation label row lost the word '") + word +
                    "': [" + text + "]");
    }
  }

  // Case 3 -- a record whose payload really does tile into display lines, so
  // the record's own framing (`TokenFramingRole`) decides which width-1 slots
  // are structure. A display line's length byte is below the book's token
  // threshold, so the dictionary spells an ordinary word for it; here two of
  // them spell "occurred" and "segment". Neither is display text. The words
  // "adapter" and "address", which the same framing places at the end of a
  // line and LayoutIR then offers as the next row's marker slot, are display
  // text and must survive.
  {
    // A display line is `<length byte> <that many one-byte tokens>`, so each
    // length token's encoded value is the number of tokens in its line.
    const auto len = [&](std::uint16_t count, const std::string &spelling) {
      return text_token(spelling, count);
    };
    const auto pad = [&] { return t(std::string(13, ' '), 15); };
    std::vector<Token> tokens;
    const auto push = [&](std::initializer_list<Token> more) {
      for (const auto &token : more)
        tokens.push_back(token);
    };
    push({len(2, " "), t("SRMSG", 200), t("603", 201)});
    push({len(4, " "), t("cfont", 202), t("3", 203), t("3", 203), t("2", 204)});
    push({len(4, " "), pad(), t("603", 201), t("Duplicate", 205),
          t("adapter", 30)});
    // "adapter" above ends its display line and this line's length byte is a
    // padding run, so LayoutIR offers "adapter" as the next row's marker slot
    // with a native origin of 13 -- squarely inside the compact envelope the
    // framing replaces.
    push({len(4, std::string(13, ' ')), t("cfont", 202), t("13", 206),
          t("8", 207), t("2", 204)});
    push({len(5, " "), pad(), t("Meaning", 150), t(":", 25), t("An", 33),
          t("address", 31)});
    // The next line's length byte spells "occurred", and the line does not
    // open on a padding run, so no row boundary falls there: the length byte
    // lands in the middle of the row that is already open.
    push({len(3, "occurred"), t("was", 217), t("found", 211), t("here", 218)});
    // The next line's length byte spells "segment" and the line does open on
    // a padding run, so LayoutIR offers the length byte itself as that body
    // row's marker slot.
    push({len(3, "segment"), pad(), t("on", 92), t("startup", 219)});
    push({len(4, " "), t("cfont", 202), t("13", 206), t("7", 212), t("2", 204)});
    push({len(4, " "), pad(), t("Action", 151), t(":", 25), t("Ignore", 213)});

    const auto source = make_source(30, tokens);
    require(source.ir.display_lines_parse,
            "case 3 fixture does not tile into display lines, so the framing "
            "under test is never decided");
    require(geist::detail::is_display_line_length_token(source, 24) &&
                geist::detail::is_display_line_length_token(source, 28),
            "case 3 fixture does not put a word-spelling length byte where the "
            "rule is exercised");
    const auto built = build({source});
    require(built.ownership.has_value(),
            "framed-record ownership was rejected: " + built.error);
    require(built.catalog.has_value(),
            "framed-record catalog declined: " + built.error);
    if (built.catalog && built.catalog->entries.size() == 1) {
      const auto text = entry_text(built.catalog->entries.front());
      // Structure, never text -- whatever the dictionary spells for it.
      for (const char *word : {"occurred", "segment"})
        require(text.find(word) == std::string::npos,
                std::string("a display line's length byte reached the entry "
                            "text as '") +
                    word + "': [" + text + "]");
      // Line content the layout merely opened a row on.
      for (const char *word : {"adapter", "address"})
        require(text.find(word) != std::string::npos,
                std::string("display text at a row marker slot was dropped: '") +
                    word + "': [" + text + "]");
    }
  }


  // Case 4 -- a record-prefix row (SC31-711 5.0 message 508, records
  // 230/231).  The second record's first display line opens on a length byte
  // that spells blanks, then a blank origin run, so the leading text
  // segment's range starts on the line's first drawn word and the layout
  // finds no marker/origin pair for it.  The record's framing still proves
  // the row; it must become one row of the Application/Action listing, in
  // source order before the next row, with exact provenance.
  {
    // Drawn words are two-byte dictionary tokens, as `Trap` (60234) is in
    // record 231: a one-byte word followed by a blank run would read as a
    // row-control slot.  Each line's length byte counts its bytes.
    std::uint16_t next_word = 0x6000;
    const auto w = [&](const std::string &value) {
      return Token{words(value), next_word++, 2};
    };
    const auto pad = [&](std::size_t width) {
      return t(std::string(width, ' '), static_cast<std::uint16_t>(width + 2));
    };
    const auto line = [&](std::vector<Token> &into,
                          const std::string &length_spelling,
                          std::initializer_list<Token> content) {
      std::uint16_t bytes = 0;
      for (const auto &token : content)
        bytes = static_cast<std::uint16_t>(bytes + token.encoded_width);
      into.push_back(text_token(length_spelling, bytes));
      for (const auto &token : content)
        into.push_back(token);
    };
    std::vector<Token> first;
    line(first, " ", {t("SRMSG", 200), t("508", 201)});
    line(first, " ", {t("cfont", 202), t("3", 203), t("3", 203), t("2", 204)});
    line(first, " ", {pad(13), t("508", 201), w("Socket"), w("failed")});
    line(first, " ", {t("cfont", 202), t("13", 206), t("8", 207), t("2", 204)});
    line(first, " ", {pad(13), t("Meaning", 150), t(":", 25), w("A"),
                      w("failure.")});
    line(first, " ", {t("cfont", 202), t("13", 206), t("7", 212), t("2", 204)});
    line(first, " ", {pad(13), t("Action", 151), t(":", 25)});
    // As in records 230/231, each key row follows its own `cfont` line and
    // opens on a length byte the dictionary spells as thirteen blanks.
    line(first, " ", {t("cfont", 202), t("13", 206), t("11", 239),
                      t("2", 204)});
    line(first, std::string(13, ' '),
         {pad(13), w("Application"), pad(1), w("Action")});
    line(first, " ", {t("cfont", 202), t("13", 206), t("2", 204),
                      t("2", 204)});
    line(first, std::string(13, ' '),
         {pad(13), w("CP"), pad(10), w("Consult"), w("the"), w("log.")});
    line(first, "   ", {pad(26), w("Then"), w("restart"), w("LNM.")});
    line(first, " ", {t("cfont", 202), t("13", 206), t("4", 228), t("2", 204),
                      t("18", 229), t("4", 228), t("2", 204)});

    std::vector<Token> second;
    // Line 0: the length byte spells eighteen blanks, the origin thirteen.
    line(second, std::string(18, ' '),
         {pad(13), w("SNMP"), w("Trap"), pad(3), w("Verify"), w("that"),
          w("AIX"), w("runs.")});
    // Line 1: its own row, whose length byte also spells blanks.
    line(second, "   ", {pad(26), w("Then"), w("restart"), w("it.")});
    line(second, " ", {t("cfont", 202), t("13", 206), t("10", 235),
                       t("2", 204)});
    line(second, std::string(13, ' '),
         {pad(13), w("lnmfddimgr"), pad(2), w("Restart"),
                       w("the"), w("manager.")});

    auto built = build({make_source(40, first, true),
                        make_source(41, second, true)});
    require(built.ownership.has_value(),
            "record-prefix ownership was rejected: " + built.error);
    require(built.catalog.has_value(),
            "record-prefix catalog declined: " + built.error);
    if (built.catalog && built.catalog->entries.size() == 1) {
      const auto &catalog = *built.catalog;
      require(catalog.record_prefix_rows.size() == 1,
              "expected exactly one record-prefix row, found " +
                  std::to_string(catalog.record_prefix_rows.size()));
      if (catalog.record_prefix_rows.size() == 1) {
        const auto &row = catalog.record_prefix_rows.front();
        require(row.logical_record == 41 && row.segment_index == 0 &&
                    row.prefix_token == 0 && row.origin_token == 1 &&
                    row.origin_column == 13 && row.content.token_begin == 2 &&
                    row.content.token_end == 9 &&
                    row.prefix_bytes.begin == 41000 &&
                    row.content.byte_begin == 41002 &&
                    row.content.byte_end == 41015,
                "record-prefix row framing or provenance is wrong: tokens [" +
                    std::to_string(row.content.token_begin) + "," +
                    std::to_string(row.content.token_end) + ") column " +
                    std::to_string(row.origin_column));
        require(row.visible_text.find("SNMP Trap") == 0 &&
                    row.visible_text.find("runs.") != std::string::npos &&
                    row.visible_text.find("Then") == std::string::npos,
                "record-prefix row text is wrong: [" + row.visible_text + "]");
        require(!row.cells.empty() && row.cells.front().token_index == 2 &&
                    row.cells.front().display_column == 13,
                "record-prefix row's first cell is not SNMP at column 13");
      }
      // Both rows of the second record survive, distinct and in order.
      const auto &action = catalog.entries.front().sections[1];
      std::vector<std::string> texts;
      for (const auto &paragraph : action.paragraphs)
        texts.push_back(paragraph.text);
      const auto at = [&](const std::string &needle) {
        for (std::size_t index = 0; index < texts.size(); ++index)
          if (texts[index].find(needle) != std::string::npos)
            return index;
        return texts.size();
      };
      require(at("SNMP Trap Verify that AIX runs.") < at("Then restart it.") &&
                  at("Then restart it.") < at("lnmfddimgr") &&
                  at("lnmfddimgr") < texts.size(),
              "record-prefix row is missing, merged or out of order");
      for (const auto &paragraph : action.paragraphs)
        require(paragraph.text.find("SNMP") == std::string::npos ||
                    paragraph.text.find("Then") == std::string::npos,
                "record-prefix row merged with the row after it: [" +
                    paragraph.text + "]");

      // The listing is proven: a table whose SNMP Trap row is the prefix row.
      const auto blocks = geist::detail::extract_message_section_blocks_ir(
          built.layout, *built.ownership, catalog);
      const geist::detail::MessageStructuredTableBlockIR *table = nullptr;
      for (const auto &block : blocks.blocks)
        if (const auto *candidate =
                std::get_if<geist::detail::MessageStructuredTableBlockIR>(
                    &block.node))
          table = candidate;
      require(table != nullptr,
              "the Application/Action listing was not recovered as a table");
      if (table != nullptr) {
        bool snmp = false;
        for (const auto &item : table->rows)
          if (item.cells.size() == 2 && item.cells[0].text == "SNMP Trap" &&
              item.cells[1].text == "Verify that AIX runs. Then restart it.")
            snmp = true;
        require(snmp, "the SNMP Trap row lost its key, action or continuation");
        std::string error;
        require(geist::detail::verify_message_section_blocks_ir(
                    built.layout, *built.ownership, catalog, blocks, &error),
                "section blocks failed their verifier: " + error);
      }

      // Every piece of prefix-row provenance is load-bearing.
      std::string error;
      require(geist::detail::verify_message_catalog_ir(
                  built.sources, built.layout, *built.ownership, catalog,
                  &error),
              "record-prefix catalog failed its verifier: " + error);
      const auto mutated = [&](auto mutate, const char *what) {
        auto changed = catalog;
        mutate(changed.record_prefix_rows.front());
        require(!geist::detail::verify_message_catalog_ir(
                    built.sources, built.layout, *built.ownership, changed,
                    &error),
                std::string("mutated record-prefix ") + what +
                    " passed verification");
      };
      if (!catalog.record_prefix_rows.empty()) {
        mutated([](auto &row) { ++row.prefix_bytes.begin; }, "prefix byte");
        mutated([](auto &row) { ++row.content.byte_end; }, "content byte");
        mutated([](auto &row) { --row.content.token_end; }, "token range");
        mutated([](auto &row) { ++row.origin_token; }, "origin token");
        mutated([](auto &row) { ++row.origin_column; }, "origin column");
        mutated([](auto &row) { ++row.cells.front().display_column; },
                "cell column");
        mutated([](auto &row) { row.visible_text += "x"; }, "text");
      }
    }

    // A non-blank origin is not a record-prefix row: the row's placement is
    // no longer proven, and nothing may be invented for it.
    auto unproven = second;
    unproven[1].words = words("    x        ");
    const auto declined = build({make_source(40, first, true),
                                 make_source(41, unproven, true)});
    if (declined.catalog)
      require(declined.catalog->record_prefix_rows.empty(),
              "a record-prefix row was admitted without a blank origin run");
  }

  std::cout << "message catalog IR synthetic tests passed\n";
  return 0;
}
