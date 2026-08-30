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
// What is NOT yet covered: the exact record-continuation shape behind #66,
// where LayoutIR opens the continued row at a compact marker slot and leaves
// the words before it as an opaque prefix. Reproducing that needs a synthetic
// record whose display-line framing makes the layout choose a mid-record row
// start; until this fixture can state that, those two paths are covered only
// by the whole-corpus differential and the hosted SC31-711 audit.
#include "geist/detail/display_lines.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/layout_ir.hpp"
#include "geist/detail/message_ir.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "test_failures.hpp"

#include <iostream>
#include <optional>
#include <string>
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
                                       const std::vector<Token> &tokens) {
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
  source.control_segments = geist::detail::decode_control_segments(
      source.logical_record, source.assembled);
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

  std::cout << "message catalog IR synthetic tests passed\n";
  return 0;
}
