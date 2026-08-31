// Synthetic section-label SRMSG ("trap") catalogue fixtures.
//
// `libgeist` may read no BOO book except `packet.boo` (#59), and packet
// carries no message catalogue at all, so every record here is built by hand
// with `assemble_logical_record_with_sources`. Unlike
// `message_catalog_ir_synthetic.cpp` these records carry real display-line
// framing -- each line is a `<length byte><that many bytes>` run, which
// `assign_display_line_framing` then stamps onto the tokens -- because the
// behaviour under test is decided on that framing and on nothing else.
//
// What is covered:
//   * the minimal catalogue the family admits, and that it conserves the
//     entry's words;
//   * an `SRTBL<id>` ... `SRETBL` envelope inside an entry: the region's own
//     display lines are kept verbatim and none of its words leak into the
//     field the envelope interrupts;
//   * an envelope whose opening control shares its display line with drawn
//     text, which the family declines rather than guess where the drawing
//     begins;
//   * the same envelope drawn in the topic *introduction*, which the prose
//     model must never be offered: the prose either side of it is a separate
//     piece and no word crosses between them;
//   * a display line the flattened splitter cut in two, whose tail LayoutIR
//     leaves unrowed -- the mid-record shape `message_catalog_ir_synthetic.cpp`
//     could not state. Here the framing makes it reproducible, and removing
//     the rule that admits it makes case 4 fail.
//
// What is NOT covered, stated plainly rather than implied: the compact
// marker slot that carries a whole word. Two rules turn on it -- a slot the
// row's own CFONT covers whole is display text, and a slot that opens its
// display line is drawn -- and neither could be reached from a synthetic
// record: LayoutIR never chose a word marker slot for these fixtures, so a
// test written against them would pass with the rules removed and would be
// worse than no test. Both stay covered by the whole-corpus differential and
// by the hosted N2AH1MST audit (`IDC01718I`, `IDC3900I`, DT 19910329000100).
#include "geist/detail/display_lines.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/layout_ir.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/trap_catalog_ir.hpp"
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
};

Token t(const std::string &value, std::uint16_t encoded) {
  return {words(value), encoded};
}

// One display line: a length byte whose encoded value is the byte count of
// the line's content tokens (every token here is one byte wide), followed by
// those tokens. The length byte's own dictionary word draws nothing.
std::vector<Token> line(const std::vector<Token> &content) {
  std::vector<Token> out;
  out.push_back({words(" "), static_cast<std::uint16_t>(content.size())});
  for (const auto &token : content)
    out.push_back(token);
  return out;
}

void append(std::vector<Token> &into, const std::vector<Token> &more) {
  into.insert(into.end(), more.begin(), more.end());
}

DecodedLogicalRecordSource make_source(std::uint32_t logical_record,
                                       const std::vector<Token> &tokens) {
  DecodedLogicalRecordSource source;
  source.logical_record = logical_record;
  source.ir.logical_record = logical_record;
  auto byte = static_cast<std::uint32_t>(logical_record * 1000);
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    const geist::detail::EncodedLogicalToken encoded{tokens[index].encoded_value,
                                                     1};
    source.tokens.push_back(tokens[index].words);
    source.encoded_tokens.push_back(encoded);
    source.ir.tokens.push_back({index,
                                encoded,
                                tokens[index].words,
                                {byte, byte + 1},
                                false,
                                3});
    byte += 1;
  }
  source.ir.payload_range = {
      static_cast<std::uint32_t>(logical_record * 1000), byte};
  geist::detail::assign_display_line_framing(source.ir);
  source.assembled =
      geist::detail::assemble_logical_record_with_sources(source.tokens);
  // The same decode the record reader runs: the splitter sees the framing,
  // and a control-shaped word with drawn text in front of it on its own
  // display line is demoted afterwards.
  source.control_segments = geist::detail::decode_control_segments(
      source.logical_record, source.assembled, source.encoded_tokens,
      source.ir.display_lines);
  geist::detail::demote_display_line_owned_controls(source);
  return source;
}

struct Catalog {
  std::vector<DecodedLogicalRecordSource> sources;
  geist::detail::LayoutIR layout;
  std::optional<geist::detail::VerifiedOwnershipIR> ownership;
  std::optional<geist::detail::TrapCatalogIR> catalog;
  std::string error;
};

Catalog build(std::vector<DecodedLogicalRecordSource> sources,
              const std::string &toc_title) {
  Catalog result;
  result.sources = std::move(sources);
  result.layout = geist::detail::extract_layout_ir(result.sources);
  result.ownership = geist::detail::build_verified_ownership_ir(
      result.sources, result.layout, &result.error);
  if (!result.ownership)
    return result;
  result.catalog = geist::detail::extract_trap_catalog_ir(
      result.sources, result.layout, *result.ownership, toc_title,
      &result.error);
  return result;
}

const auto origin = [] { return t("   ", 20); };

// SH12 / CTOPICN / CHDLEVEL / ST -- the metadata envelope every trap
// catalogue must open with.
std::vector<Token> header(const std::string &title_word_a,
                          const std::string &title_word_b) {
  std::vector<Token> tokens;
  append(tokens, line({t("SH12", 100)}));
  append(tokens, line({t("ctopicn", 101), t("12.", 102)}));
  append(tokens, line({t("chdlevel", 103), t(":H2", 104)}));
  append(tokens,
         line({t("ST", 105), t("  ", 20), t(title_word_a, 106),
               t(title_word_b, 107)}));
  return tokens;
}

// One entry: `SRMSG <id>`, a fully highlighted headline row, and one
// `Description:` field row.
std::vector<Token> entry(const std::string &id, std::uint16_t id_word) {
  std::vector<Token> tokens;
  append(tokens, line({t("SRMSG", 110), t(id, id_word)}));
  append(tokens, line({t("cfont", 111), t("3", 112), t("3", 112), t("2", 113)}));
  append(tokens, line({origin(), t(id, id_word)}));
  append(tokens,
         line({t("cfont", 111), t("3", 112), t("12", 114), t("2", 113)}));
  append(tokens, line({origin(), t("Description:", 115), t("first", 117),
                       t("field", 118)}));
  return tokens;
}

std::string entry_text(const geist::detail::TrapEntryIR &value) {
  std::string out = value.headline.body.text;
  for (const auto &field : value.fields) {
    if (!out.empty())
      out.push_back(' ');
    out += field.line.body.text;
  }
  return out;
}

} // namespace

int main() {
  // Case 1 -- the minimal catalogue: metadata, one entry, one field.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("601", 120));
    const auto built = build({make_source(10, tokens)}, "Sample Messages");
    require(built.ownership.has_value(),
            "synthetic trap ownership was rejected: " + built.error);
    require(built.catalog.has_value(),
            "synthetic trap catalog declined: " + built.error);
    if (built.catalog) {
      require(built.catalog->title == "Sample Messages",
              "trap catalog title is wrong: [" + built.catalog->title + "]");
      require(built.catalog->entries.size() == 1,
              "trap catalog did not produce one entry");
      if (built.catalog->entries.size() == 1) {
        const auto &only = built.catalog->entries.front();
        require(only.id == "601", "trap entry id is wrong: [" + only.id + "]");
        require(only.embedded_regions.empty(),
                "plain trap entry reported an embedded region");
        const auto text = entry_text(only);
        require(text == "601 Description: first field",
                "trap entry text is wrong: [" + text + "]");
      }
    }
  }

  // Case 2 -- an `SRTBL` ... `SRETBL` envelope inside the entry. Its two
  // drawn lines belong to the region; none of their words joins the field.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("602", 121));
    append(tokens, line({t("SRTBLT1", 130)}));
    append(tokens, line({origin(), t("Return", 131), t("Code", 132)}));
    append(tokens, line({origin(), t("4", 133), t("Everything", 134)}));
    append(tokens, line({t("SRETBL", 135)}));
    const auto built = build({make_source(11, tokens)}, "Sample Messages");
    require(built.ownership.has_value(),
            "embedded-envelope ownership was rejected: " + built.error);
    require(built.catalog.has_value(),
            "embedded-envelope catalog declined: " + built.error);
    if (built.catalog && built.catalog->entries.size() == 1) {
      const auto &only = built.catalog->entries.front();
      require(only.embedded_regions.size() == 1,
              "the entry did not report its embedded envelope");
      if (only.embedded_regions.size() == 1) {
        const auto &region = only.embedded_regions.front();
        require(region.identifier == "T1",
                "embedded envelope identifier is wrong: [" + region.identifier +
                    "]");
        require(region.lines.size() == 2,
                "embedded envelope drew " +
                    std::to_string(region.lines.size()) + " lines, not 2");
        require(region.lines.size() == region.line_sources.size(),
                "embedded envelope lines have no line provenance");
        for (const auto &drawn : region.lines)
          require(drawn.find("   ") == 0,
                  "embedded envelope line lost its left margin: [" + drawn +
                      "]");
        std::string joined;
        for (const auto &drawn : region.lines)
          joined += drawn;
        for (const char *word : {"Return", "Code", "Everything"})
          require(joined.find(word) != std::string::npos,
                  std::string("embedded envelope lost the word '") + word +
                      "': [" + joined + "]");
      }
      const auto text = entry_text(only);
      for (const char *word : {"Return", "Code", "Everything"})
        require(text.find(word) == std::string::npos,
                std::string("envelope word '") + word +
                    "' leaked into the entry text: [" + text + "]");
    }
  }

  // Case 3 -- the opening control shares its display line with drawn text.
  // Where the envelope's drawing begins is then not decidable from the
  // framing, so the family declines instead of guessing.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("603", 122));
    append(tokens, line({t("SRTBLT2", 136), t("Return", 131)}));
    append(tokens, line({origin(), t("4", 133), t("Everything", 134)}));
    append(tokens, line({t("SRETBL", 135)}));
    const auto built = build({make_source(12, tokens)}, "Sample Messages");
    require(built.ownership.has_value(),
            "straddling-envelope ownership was rejected: " + built.error);
    require(!built.catalog.has_value(),
            "a straddling embedded envelope was admitted");
    require(built.error.find("boundary falls inside a display line") !=
                std::string::npos,
            "straddling envelope declined for the wrong reason: " +
                built.error);
  }

  // Case 4 -- the flattened splitter cuts a segment boundary inside one
  // display line, so LayoutIR ends the row there and leaves the line's tail
  // unrowed. The line is drawn whole and its words must survive.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("604", 123));
    append(tokens, line({origin(), t("before", 140), t("SRXY", 142),
                         t("after", 141)}));
    const auto built = build({make_source(13, tokens)}, "Sample Messages");
    require(built.ownership.has_value(),
            "split-line ownership was rejected: " + built.error);
    require(built.catalog.has_value(),
            "split-line catalog declined: " + built.error);
    if (built.catalog && built.catalog->entries.size() == 1) {
      const auto text = entry_text(built.catalog->entries.front());
      for (const char *word : {"before", "after"})
        require(text.find(word) != std::string::npos,
                std::string("split display line lost the word '") + word +
                    "': [" + text + "]");
    }
  }

  // Case 5 -- the same `SRTBL` ... `SRETBL` envelope drawn in the topic
  // introduction rather than inside an entry (SC09-138 `F.1`/`H.0`,
  // SC24-546 `A.0`, SC34-425 `2.4.32`/`2.4.33`). The introduction is
  // modelled as prose paragraphs, so the drawing must not be offered to the
  // prose model at all: the prose either side of the envelope is a separate
  // piece, and the region keeps its own display lines.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({origin(), t("Intro", 140), t("before", 141)}));
    append(tokens, line({t("SRTBLT3", 137)}));
    append(tokens, line({origin(), t("Return", 131), t("Code", 132)}));
    append(tokens, line({origin(), t("4", 133), t("Everything", 134)}));
    append(tokens, line({t("SRETBL", 135)}));
    append(tokens, line({origin(), t("Intro", 140), t("after", 143)}));
    append(tokens, entry("605", 124));
    const auto built = build({make_source(14, tokens)}, "Sample Messages");
    require(built.ownership.has_value(),
            "introduction-envelope ownership was rejected: " + built.error);
    require(built.catalog.has_value(),
            "introduction-envelope catalog declined: " + built.error);
    if (built.catalog) {
      const auto &catalog = *built.catalog;
      require(catalog.introduction_regions.size() == 1,
              "the introduction did not report its drawn envelope");
      require(catalog.introduction_prose_envelopes.size() ==
                  catalog.introduction_regions.size() + 1,
              "the introduction pieces do not bracket its drawn envelopes");
      std::string introduction;
      for (const auto &paragraph : catalog.introduction) {
        introduction.push_back(' ');
        introduction += paragraph.text;
      }
      for (const char *word : {"before", "after"})
        require(introduction.find(word) != std::string::npos,
                std::string("the introduction lost the word '") + word +
                    "': [" + introduction + "]");
      for (const char *word : {"Return", "Code", "Everything"})
        require(introduction.find(word) == std::string::npos,
                std::string("envelope word '") + word +
                    "' leaked into the introduction: [" + introduction + "]");
      if (catalog.introduction_regions.size() == 1) {
        const auto &region = catalog.introduction_regions.front();
        require(region.identifier == "T3",
                "introduction envelope identifier is wrong: [" +
                    region.identifier + "]");
        require(region.lines.size() == 2 &&
                    region.lines.size() == region.line_sources.size(),
                "introduction envelope drew " +
                    std::to_string(region.lines.size()) +
                    " lines with provenance for " +
                    std::to_string(region.line_sources.size()));
        std::string joined;
        for (const auto &drawn : region.lines)
          joined += drawn;
        for (const char *word : {"Return", "Code", "Everything"})
          require(joined.find(word) != std::string::npos,
                  std::string("introduction envelope lost the word '") + word +
                      "': [" + joined + "]");
        require(region.after_field == 1,
                "the introduction envelope was not placed after the paragraph "
                "it interrupts: " +
                    std::to_string(region.after_field));
      }
    }
  }

  std::cout << "trap catalog IR synthetic tests passed\n";
  return 0;
}
