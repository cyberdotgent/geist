// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

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
//     the rule that admits it makes case 4 fail;
//   * a highlighted run drawn inside a field body -- neither the entry
//     headline nor a field label -- which the family keeps as a run over the
//     body text;
//   * the same run on a row that draws a glyph in front of the catalogue's
//     own column, so the CFONT mapping slides: the display line disproves it
//     and the family declines rather than record a run over characters the
//     columns do not name.
//
// What is NOT covered, stated plainly rather than implied: the compact
// marker slot that carries a whole word. Two rules turn on it -- a slot the
// row's own CFONT covers whole is display text, and a slot that opens its
// display line is drawn -- and neither could be reached from a synthetic
// record: LayoutIR never chose a word marker slot for these fixtures, so a
// test written against them would pass with the rules removed and would be
// worse than no test. Both stay covered by the whole-corpus differential and
// by the hosted N2AH1MST audit (`IDC01718I`, `IDC3900I`, DT 19910329000100).
// The same holds for the two rules that read a CFONT span onto a cell
// LayoutIR classified as a punctuation slot or a placeholder run (N2AH1MST
// `9.0` `ASB029I`, `24.0` `ICP050D`): neither role could be produced from a
// synthetic record, so both stay covered by the differential and by those
// two hosted pages.
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/layout_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/ir/trap_catalog_ir.hpp"
#include "geist/detail/lowering/trap_catalog_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
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

// One observed source-title form stores the title opcode, an attach-control
// word, and a native `|` marker as three consecutive tokens of the same
// display line. The flattened control splitter consequently reports `ST|`,
// but the marker is framing rather than part of the opcode.
std::vector<Token> marker_title_header(const std::string &title_word_a,
                                       const std::string &title_word_b) {
  std::vector<Token> tokens;
  append(tokens, line({t("SH12", 100)}));
  append(tokens, line({t("ctopicn", 101), t("12.", 102)}));
  append(tokens, line({t("chdlevel", 103), t(":H2", 104)}));
  append(tokens,
         line({t("ST", 105), {TokenWords{1}, 0}, t("|", 108),
               t(title_word_a, 106), t(title_word_b, 107)}));
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

  // SC34-425 stores some source titles as `ST`, attach byte `0x01`, marker
  // `|`, then title text on one native display line. The source title can
  // also differ from the contents spelling only in ASCII case. Both forms
  // retain the source row and its spelling; the marker is not title text.
  {
    auto tokens = marker_title_header("Sample", "messages");
    append(tokens, entry("620", 231));
    const auto built = build({make_source(61, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "marker-attached source title declined: " + built.error);
    if (built.catalog) {
      require(built.catalog->title == "Sample messages",
              "marker-attached title did not retain source spelling: [" +
                  built.catalog->title + "]");
      require(built.catalog->title.find('|') == std::string::npos,
              "native title marker leaked into title text");
      require(built.catalog->title_source.logical_record == 61 &&
                  built.catalog->title_source.token_begin == 12 &&
                  built.catalog->title_source.token_end == 14 &&
                  built.catalog->title_source.byte_begin == 61012 &&
                  built.catalog->title_source.byte_end == 61014,
              "marker-attached title did not retain its exact visible source "
              "slice");
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(),
              "marker-attached title lowering declined: " + error);
      if (document) {
        const auto markdown =
            geist::detail::render_document_markdown(*document);
        const auto html =
            geist::detail::render_document_html_blocks(*document, {});
        require(markdown.find("Sample messages") != std::string::npos &&
                    html.find("Sample messages") != std::string::npos,
                "marker-attached source title did not reach both renderers");
      }
    }

    // A literal dictionary word `ST|` has no attach-control/source-token
    // framing and is not the title control described above.
    auto unframed = header("Sample", "Messages");
    unframed.resize(8);
    append(unframed,
           line({t("ST|", 109), t("Sample", 106), t("Messages", 107)}));
    append(unframed, entry("620", 231));
    const auto rejected =
        build({make_source(63, unframed)}, "Sample Messages");
    require(!rejected.catalog.has_value(),
            "an unframed literal ST| word was admitted as a title control");
  }

  // A field label can intentionally carry an empty value. Its CFONT run and
  // source row still establish a real field; absence of following text must
  // not erase the label or turn the catalogue opaque.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({t("SRMSG", 110), t("621", 232)}));
    append(tokens,
           line({t("cfont", 111), t("3", 112), t("3", 112), t("2", 113)}));
    append(tokens, line({origin(), t("621", 232)}));
    append(tokens,
           line({t("cfont", 111), t("3", 112), t("6", 181), t("2", 113)}));
    append(tokens, line({origin(), t("Value:", 233)}));
    const auto built = build({make_source(62, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "empty message field declined: " + built.error);
    if (built.catalog) {
      require(built.catalog->entries.front().fields.size() == 1 &&
                  built.catalog->entries.front().fields.front().label_text ==
                      "Value:",
              "empty message field lost its typed label");
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(),
              "empty message field lowering declined: " + error);
      if (document) {
        const auto markdown =
            geist::detail::render_document_markdown(*document);
        const auto html =
            geist::detail::render_document_html_blocks(*document, {});
        require(markdown.find("Value:") != std::string::npos,
                "empty message field label was not rendered in Markdown");
        require(html.find("Value:") != std::string::npos,
                "empty message field label was not rendered in HTML");
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

  // Case 6 -- a highlighted run drawn inside a field body. It opens a
  // continuation display line at the same column the labels use and does not
  // end in `:`, so it is neither the entry headline nor a field label
  // (SC09-138 `F.1` record 2048 line 1, SC24-546 `A.0` record 996 line 0).
  // The family keeps the run as a run over the body text and loses no word.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("606", 125));
    append(tokens,
           line({t("cfont", 111), t("3", 112), t("6", 144), t("C", 145)}));
    append(tokens, line({origin(), t("Sample", 146), t("book", 147)}));
    const auto built = build({make_source(15, tokens)}, "Sample Messages");
    require(built.ownership.has_value(),
            "body-highlight ownership was rejected: " + built.error);
    require(built.catalog.has_value(),
            "body-highlight catalog declined: " + built.error);
    if (built.catalog && built.catalog->entries.size() == 1) {
      const auto &only = built.catalog->entries.front();
      const auto text = entry_text(only);
      for (const char *word : {"Sample", "book"})
        require(text.find(word) != std::string::npos,
                std::string("the body highlight lost the word '") + word +
                    "': [" + text + "]");
      require(only.fields.size() == 1,
              "the highlighted run was taken for a field: " +
                  std::to_string(only.fields.size()) + " fields");
      if (only.fields.size() == 1) {
        const auto &body = only.fields.front().line.body;
        require(body.highlights.size() == 1,
                "the field body recorded " +
                    std::to_string(body.highlights.size()) +
                    " highlighted runs, not 1");
        if (body.highlights.size() == 1) {
          const auto &highlight = body.highlights.front();
          require(highlight.style == geist::detail::FontStyleIR::citation,
                  "the body highlight lost the style its CFONT states");
          require(highlight.begin < highlight.end &&
                      highlight.end <= body.text.size(),
                  "the body highlight is outside the text it names");
          if (highlight.begin < highlight.end &&
              highlight.end <= body.text.size()) {
            const auto covered = body.text.substr(
                highlight.begin, highlight.end - highlight.begin);
            require(covered == "Sample",
                    "the body highlight covers [" + covered +
                        "], not the run its columns name");
          }
        }
      }
    }
  }

  // Case 7 -- the same shape, but the row draws a glyph in front of the
  // catalogue's own column, so the projected text no longer starts where the
  // CFONT counts from and the column mapping slides (SC34-425
  // `APPENDIX1.5.3` record 2540, whose `|` change bar makes `User` map as
  // `| Us`). A CFONT column is an index into its display line's cells, so
  // the line disproves the mapping and the family declines.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("607", 126));
    append(tokens,
           line({t("cfont", 111), t("3", 112), t("6", 144), t("C", 145)}));
    append(tokens, line({t(" | ", 148), t("Sample", 146), t("book", 147)}));
    const auto built = build({make_source(16, tokens)}, "Sample Messages");
    require(built.ownership.has_value(),
            "slid-column ownership was rejected: " + built.error);
    require(!built.catalog.has_value(),
            "a highlighted run whose columns the line disproves was admitted");
    require(built.error.find("does not cover the columns it names") !=
                std::string::npos,
            "slid-column run declined for the wrong reason: " + built.error);
  }

  // SC24-546 A.0: a framed continuation precedes the next record's
  // first LayoutIR row, which belongs to a different display run.
  {
    auto first = header("Sample", "Messages");
    append(first, line({t("SI", 150), t("hidden", 151), t("index", 152)}));
    append(first, line({origin(), t("Opening", 153), t("sentence", 154)}));
    std::vector<Token> second;
    append(second, line({origin(), t("continues", 155), t("across", 156),
                         t("records.", 157)}));
    append(second, line({t("SI", 150), t("secret", 158), t("operand", 159)}));
    append(second, line({t("cfont", 111), t("3", 112), t("4", 160), t("2", 113)}));
    append(second, line({origin(), t("Next", 161), t("paragraph.", 162)}));
    append(second, entry("608", 163));
    const auto sources = std::vector<DecodedLogicalRecordSource>{
        make_source(20, first), make_source(21, second)};
    const auto built = build(sources, "Sample Messages");
    require(built.catalog.has_value(), "framed record-prefix catalog declined: " + built.error);
    if (built.catalog) {
      std::string introduction;
      for (const auto &paragraph : built.catalog->introduction)
        introduction += paragraph.text + "|";
      require(introduction == "Opening sentence continues across records.|Next paragraph.|",
              "record-prefix introduction changed: " + introduction);
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(), "record-prefix lowering declined: " + error);
      if (document)
        for (const auto &output : {geist::detail::render_document_html_blocks(*document, {}),
                                   geist::detail::render_document_markdown(*document)}) {
          const auto opening = output.find("Opening sentence continues across records");
          const auto next = output.find("Next");
          require(opening != std::string::npos && next != std::string::npos && opening < next &&
                      output.find("Opening sentence", opening + 1) == std::string::npos &&
                      output.find("hidden") == std::string::npos &&
                      output.find("secret") == std::string::npos &&
                      output.find("MSG 608") != std::string::npos,
                  "record-prefix rendering changed: " + output);
        }
      if (document) {
        require(geist::detail::render_document_html_blocks(*document, {}).find(
                    "<strong class=\"geist-strong\">Next</strong> paragraph.") !=
                    std::string::npos &&
                    geist::detail::render_document_markdown(*document).find(
                        "**Next** paragraph\\.") != std::string::npos,
                "record-prefix recovery lost the following font span");
      }
      auto changed = *built.catalog;
      bool found_prefix_source = false;
      for (auto& slice : changed.introduction.front().source_slices)
        if (slice.logical_record == 21) {
          found_prefix_source = true;
          require(slice.token_begin >= 1 && slice.token_end <= 5 &&
                      slice.byte_begin == 21000 + slice.token_begin &&
                      slice.byte_end == 21000 + slice.token_end,
                  "continuation provenance includes framing or hidden controls");
          ++slice.byte_begin;
          break;
        }
      std::string verify_error;
      require(found_prefix_source &&
                  !geist::detail::verify_trap_catalog_ir(
                      built.sources, built.layout, *built.ownership, changed,
                      &verify_error),
              "continuation byte-provenance mutation passed verification");
    }

    require(!build({make_source(20, first), make_source(22, second)},
                   "Sample Messages").catalog,
            "missing record was accepted as a paragraph continuation");
    auto undecided = sources;
    undecided[1].ir.display_lines_parse = false;
    for (auto& token : undecided[1].ir.tokens)
      token.framing = geist::detail::TokenFramingRole::unframed;
    require(!build(std::move(undecided), "Sample Messages").catalog,
            "undecided cross-run prefix was guessed as continuation text");

    auto closed = first;
    append(closed, line({}));
    const auto after_closed = build(
        {make_source(20, closed), make_source(21, second)}, "Sample Messages");
    require(after_closed.catalog && after_closed.catalog->introduction.size() == 3 &&
                after_closed.catalog->introduction[0].text == "Opening sentence" &&
                after_closed.catalog->introduction[1].text == "continues across records.",
            "native trailing blank row did not separate paragraphs");
    auto leading_blank = line({});
    append(leading_blank, second);
    const auto after_leading_blank = build(
        {make_source(20, first), make_source(21, leading_blank)}, "Sample Messages");
    require(after_leading_blank.catalog &&
                after_leading_blank.catalog->introduction.size() == 3 &&
                after_leading_blank.catalog->introduction[0].text == "Opening sentence" &&
                after_leading_blank.catalog->introduction[1].text == "continues across records.",
            "native leading blank row did not separate paragraphs");

    auto spelled_prefix = second;
    spelled_prefix.front().words = words("framing");
    const auto spelled = build(
        {make_source(20, first), make_source(21, spelled_prefix)},
        "Sample Messages");
    require(spelled.catalog && spelled.catalog->introduction.front().text ==
                "Opening sentence continues across records.",
            "native length byte's dictionary word entered the continuation");

    auto extra_line = line({origin(), t("and", 164), t("further", 165)});
    append(extra_line, second);
    const auto multiple = build(
        {make_source(20, first), make_source(21, extra_line)},
        "Sample Messages");
    require(multiple.catalog && multiple.catalog->introduction.front().text ==
                "Opening sentence and further continues across records.",
            "multiple native prefix rows were not recovered in source order");

    auto split_prefix = line({origin(), t("additional", 164), t("text.", 165)});
    append(split_prefix, line({}));
    append(split_prefix, second);
    const auto split = build(
        {make_source(20, first), make_source(21, split_prefix)},
        "Sample Messages");
    require(split.catalog && split.catalog->introduction.size() == 3 &&
                split.catalog->introduction[0].text == "Opening sentence additional text." &&
                split.catalog->introduction[1].text == "continues across records.",
            "blank row within a native prefix did not separate paragraphs");

    auto attached_prefix = line({origin(), t("has", 164), t("ended", 165),
                                 {TokenWords{1}, 1}, t(".", 166)});
    append(attached_prefix, second);
    const auto attached = build(
        {make_source(20, first), make_source(21, attached_prefix)},
        "Sample Messages");
    require(attached.catalog && attached.catalog->introduction.front().text ==
                "Opening sentence has ended. continues across records.",
            "native prefix spacing control was rejected or printed");

    const auto rowless = line({origin(), t("through", 164), t("another", 165),
                              t("record", 166)});
    const auto three_records = build(
        {make_source(20, first), make_source(21, rowless), make_source(22, second)},
        "Sample Messages");
    require(three_records.catalog && three_records.catalog->introduction.front().text ==
                "Opening sentence through another record continues across records.",
            "rowless intervening native record lost its text");
  }

  // A blank row ends the paragraph, not the compatibility display run.
  // The old run's CFONT must not be reapplied to later unstyled text.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({t("cfont", 111), t("3", 112), t("4", 160), t("2", 113)}));
    append(tokens, line({origin(), t("Next", 161), t("paragraph.", 162)}));
    append(tokens, line({}));
    append(tokens, line({origin(), t("Following", 164), t("text.", 165)}));
    append(tokens, entry("610", 170));
    const auto built = build({make_source(30, tokens)}, "Sample Messages");
    require(built.catalog && built.catalog->introduction.size() == 2,
            "native blank row within a font run did not split the paragraphs");
    if (built.catalog) {
      require(built.catalog->introduction[0].leading_spans.size() == 1 &&
                  built.catalog->introduction[1].leading_spans.empty(),
              "native paragraph boundary reapplied an earlier font span");
    }
  }

  // Introduction destinations survive promotion out of the fallback route.
  // Metadata operands extend the name; body-anchor payloads do not.
  {
    auto tokens = header("Sample", "Messages");
    const auto metadata_anchor = line({t("SRLEN", 176), t("Example", 177)});
    tokens.insert(tokens.begin() + 5, metadata_anchor.begin(), metadata_anchor.end());
    append(tokens, line({origin(), t("Opening", 153), t("sentence", 154)}));
    append(tokens, line({t("SRSPTSECTION", 178)}));
    append(tokens, entry("611", 179));
    const auto built = build({make_source(30, tokens)}, "Sample Messages");
    require(built.catalog.has_value(), "anchor fixture declined: " + built.error);
    if (built.catalog) {
      bool metadata = false, body = false;
      for (const auto& anchor : built.catalog->anchors) {
        metadata = metadata || anchor.id == "LEN Example";
        body = body || anchor.id == "SPTSECTION";
      }
      require(metadata && body, "typed trap catalogue lost source destinations");
    }
  }

  // Word values are Unicode, not single-byte characters. UTF-8 byte lengths
  // must not shift the source columns or the highlighted byte ranges.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("609", 170));
    append(tokens, line({t("cfont", 111), t("3", 112), t("1", 171),
                         t("2", 113), t("5", 172), t("1", 171), t("1", 171)}));
    append(tokens, line({origin(), {TokenWords{0x00a2}, 173},
                         {TokenWords{0x00e9}, 174}, t("tail", 175)}));
    const auto built = build({make_source(30, tokens)}, "Sample Messages");
    require(built.catalog.has_value(), "Unicode trap payload declined: " + built.error);
    if (built.catalog) {
      const auto& field = built.catalog->entries.front().fields.front().line.body;
      require(field.text == u8"Description: first field ¢ é tail",
              "trap payload is not UTF-8: " + field.text);
      if (!field.flow.blocks.empty()) {
        const auto &paragraph =
            std::get<geist::detail::TrapFlowParagraphIR>(
                field.flow.blocks.front().value);
        bool cent = false, acute = false;
        for (const auto &projection : paragraph.projection) {
          const auto glyph = paragraph.text.substr(
              projection.begin, projection.end - projection.begin);
          if (glyph == u8"¢")
            cent = projection.end - projection.begin == 2 &&
                   projection.cell_end - projection.cell_begin == 1;
          if (glyph == u8"é")
            acute = projection.end - projection.begin == 2 &&
                    projection.cell_end - projection.cell_begin == 1;
        }
        require(cent && acute && paragraph.highlights.size() == 2,
                "paragraph-local UTF-8 projection or highlights shifted");
      }
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(), "Unicode trap lowering declined: " + error);
      if (document) {
        const auto html = geist::detail::render_document_html_blocks(*document, {});
        const auto markdown = geist::detail::render_document_markdown(*document);
        require(html.find(u8"<strong class=\"geist-strong\">¢</strong> <em class=\"geist-emphasis\">é</em> tail") != std::string::npos &&
                    markdown.find(u8"**¢** *é* tail") != std::string::npos,
                "UTF-8 bytes shifted trap font spans: " + html);
      }
    }
  }
  // A terminal font instruction styles the first native row of the next
  // consecutive record, even when compatibility layout has no row for it.
  {
    auto first = header("Sample", "Messages");
    append(first, line({origin(), t("Introduction.", 180)}));
    append(first, line({}));
    append(first, line({t("cfont", 111), t("3", 112), t("6", 181),
                        t("K", 182), t("10", 183), t("5", 184), t("K", 182)}));
    auto second = line({origin(), t("Return", 185), t("Codes", 186)});
    append(second, line({}));
    append(second, line({origin(), t("Unstyled", 187), t("afterwards.", 188)}));
    auto offset_entry = entry("612", 189);
    // The catalogue's entry origin is independent of the introduction's.
    offset_entry[5].words = words("8");
    offset_entry[13].words = words("8");
    offset_entry[9].words = words("        ");
    offset_entry[17].words = words("        ");
    append(second, offset_entry);
    auto sources = std::vector<DecodedLogicalRecordSource>{
        make_source(40, first), make_source(41, second)};
    const auto built = build(sources, "Sample Messages");
    require(built.catalog.has_value(), "deferred introduction font declined: " + built.error);
    if (built.catalog) {
      require(built.catalog->introduction.size() == 3 &&
                  built.catalog->introduction[1].text == "Return Codes" &&
                  built.catalog->introduction[1].leading_spans.size() == 2 &&
                  built.catalog->introduction[2].leading_spans.empty(),
              "deferred font lost its row or leaked across a blank row");
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(), "deferred font lowering declined: " + error);
      if (document) {
        const auto html = geist::detail::render_document_html_blocks(*document, {});
        require(html.find(">Return</strong> <strong class=\"geist-strong\">Codes</strong>") !=
                    std::string::npos && html.find("Unstyled afterwards.") != std::string::npos,
                "deferred font rendering changed: " + html);
      }
      auto changed = *built.catalog;
      auto& paragraph = changed.introduction[1];
      require(paragraph.source_slices.size() >= 2 &&
                  paragraph.source_slices.back().logical_record == 40,
              "deferred font instruction has no provenance");
      if (!paragraph.source_slices.empty()) ++paragraph.source_slices.back().byte_begin;
      require(!geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership, changed, &error),
              "mutated deferred font provenance was accepted");
    }
    require(!build({make_source(40, first), make_source(42, second)},
                   "Sample Messages").catalog,
            "deferred font crossed a missing record");
    auto bad_font = first;
    bad_font[bad_font.size() - 2].words = words("50");
    require(!build({make_source(40, bad_font), make_source(41, second)},
                   "Sample Messages").catalog,
            "deferred font exceeded its target row");
    auto unknown_font = first;
    unknown_font.back().words = words("Z");
    require(!build({make_source(40, unknown_font), make_source(41, second)},
                   "Sample Messages").catalog,
            "deferred font silently dropped an unsupported style");
    auto changed_origin = second;
    changed_origin[1].words = words(" | ");
    require(!build({make_source(40, first), make_source(41, changed_origin)},
                   "Sample Messages").catalog,
            "deferred font slid over a leading change bar");
    auto blank_target = line({});
    append(blank_target, second);
    require(!build({make_source(40, first), make_source(41, blank_target)},
                   "Sample Messages").catalog,
            "deferred font was silently moved past its empty target row");
  }
  // Selector coordinates bind the second identical phrase to source glyphs,
  // not the first textual occurrence in the flattened field.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("613", 190));
    append(tokens, line({t("cselect", 191), t("19", 192), t("6", 181),
                         t("HDRTARGET", 193)}));
    append(tokens, line({origin(), t("See", 194), t("Target", 195),
                         t("then", 196), t("Target", 195)}));
    const auto built = build({make_source(50, tokens)}, "Sample Messages");
    require(built.catalog.has_value(), "selector catalogue declined: " + built.error);
    if (built.catalog) {
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(), "selector lowering declined: " + error);
      if (document) {
        const auto html = geist::detail::render_document_html_blocks(*document, {});
        require(html.find("See Target then <a") != std::string::npos &&
                    html.find("href=\"#HDRTARGET\">Target</a>") != std::string::npos,
                "typed trap selector was lost or attached to the wrong occurrence: " + html);
        const auto markdown = geist::detail::render_document_markdown(*document);
        require(markdown.find("See Target then [Target](<#HDRTARGET>)") != std::string::npos,
                "Markdown lost the source-bound selector: " + markdown);
      }
      auto changed = *built.catalog;
      auto& links = changed.entries.front().fields.front().line.body.links;
      require(links.size() == 1, "selector has no typed source claim");
      if (!links.empty()) ++links.front().begin;
      require(!geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership, changed, &error),
              "mutated selector projection was accepted");
      auto overlapping = *built.catalog;
      auto& body = overlapping.entries.front().fields.front().line.body;
      if (!body.links.empty()) {
        body.highlights.push_back({body.links.front().begin, body.links.front().end,
                                    geist::detail::FontStyleIR::highlight_2});
        require(!geist::detail::lower_trap_catalog_to_document_ir(
                    {"12", "Sample Messages"}, overlapping, &error),
                "unrepresentable overlapping link and emphasis was flattened");
      }
    }
    auto overlong = tokens;
    for (auto& token : overlong)
      if (token.encoded_value == 181) token.words = words("999");
    require(!build({make_source(50, overlong)}, "Sample Messages").catalog,
            "selector exceeding its native row was accepted");
    auto unicode = tokens;
    for (auto& token : unicode)
      if (token.encoded_value == 195) token.words = TokenWords{'T', 'a', 'r', 'g', 0xe9, 't'};
    const auto nonascii = build({make_source(50, unicode)}, "Sample Messages");
    require(nonascii.catalog.has_value(), "Unicode selector declined: " + nonascii.error);
    if (nonascii.catalog) {
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *nonascii.catalog, &error);
      require(document && geist::detail::render_document_html_blocks(*document, {}).find(
                  u8"href=\"#HDRTARGET\">Targét</a>") != std::string::npos,
              "Unicode bytes shifted selector coordinates");
    }
  }
  // A selector proves that its closing quote is drawn, even at a row end.
  // Compatibility terminal-marker suppression must not erase part of a link.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("614", 197));
    append(tokens, line({t("cselect", 191), t("3", 112), t("8", 198),
                         t("HDRQUOTE", 199)}));
    append(tokens, line({origin(), t("\"", 43), {TokenWords{1}, 1},
                         t("Target", 195), {TokenWords{1}, 1}, t("\"", 43)}));
    const auto built = build({make_source(51, tokens)}, "Sample Messages");
    require(built.catalog.has_value(), "quoted selector declined: " + built.error);
    if (built.catalog) {
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document && geist::detail::render_document_html_blocks(*document, {}).find(
                  "href=\"#HDRQUOTE\">\"Target\"</a>") != std::string::npos,
              "selector's terminal quote was dropped");
    }
  }

  // Native blank rows are source objects, not a boolean annotation. Preserve
  // every row, and give each paragraph its own cells and UTF-8 projection.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("615", 200));
    append(tokens, line({}));
    append(tokens, line({}));
    append(tokens, line({origin(), t("Second", 201), t("paragraph.", 202)}));
    const auto built = build({make_source(52, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "source-flow paragraphs declined: " + built.error);
    if (built.catalog) {
      const auto &body =
          built.catalog->entries.front().fields.front().line.body;
      require(body.flow.blocks.size() == 2,
              "two source paragraphs were flattened");
      if (body.flow.blocks.size() == 2) {
        const auto &first = std::get<geist::detail::TrapFlowParagraphIR>(
            body.flow.blocks[0].value);
        const auto &second = std::get<geist::detail::TrapFlowParagraphIR>(
            body.flow.blocks[1].value);
        require(second.break_before.size() == 2,
                "repeated native blank rows were coalesced");
        require(first.cell_end <= second.break_before[0].cell_begin &&
                    second.break_before[0].cell_end <=
                        second.break_before[1].cell_begin &&
                    second.break_before[1].cell_end <= second.cell_begin,
                "paragraph cells and boundary cells overlap or reorder");
        const auto exact_projection = [](const auto &paragraph) {
          std::size_t at = 0;
          for (const auto &span : paragraph.projection) {
            if (span.begin != at || span.begin >= span.end ||
                span.end > paragraph.text.size())
              return false;
            at = span.end;
          }
          return at == paragraph.text.size();
        };
        require(exact_projection(first) && exact_projection(second),
                "paragraph projection does not cover its UTF-8 text exactly");
        require(first.cell_end <= second.cell_begin &&
                    first.source_slices != second.source_slices,
                "paragraphs duplicate broad source provenance");
        auto changed = *built.catalog;
        auto &boundary = std::get<geist::detail::TrapFlowParagraphIR>(
            changed.entries.front().fields.front().line.body.flow.blocks[1]
                .value)
                             .break_before.front();
        ++boundary.source.byte_begin;
        std::string error;
        require(!geist::detail::verify_trap_catalog_ir(
                    built.sources, built.layout, *built.ownership, changed,
                    &error),
                "mutated paragraph-boundary provenance was accepted");
        changed = *built.catalog;
        auto &projection = std::get<geist::detail::TrapFlowParagraphIR>(
                               changed.entries.front()
                                   .fields.front()
                                   .line.body.flow.blocks.front()
                                   .value)
                               .projection.front();
        ++projection.cell_end;
        error.clear();
        require(!geist::detail::verify_trap_catalog_ir(
                    built.sources, built.layout, *built.ownership, changed,
                    &error),
                "mutated paragraph text projection was accepted");
      }
    }
  }

  // Paragraph boundaries survive both append paths: a highlighted CFONT
  // continuation and a selector continuation retain local claims.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("616", 203));
    append(tokens, line({}));
    append(tokens,
           line({t("cfont", 111), t("3", 112), t("6", 181), t("C", 145)}));
    append(tokens, line({origin(), t("Styled", 204), t("text", 205)}));
    append(tokens, line({}));
    append(tokens, line({t("cselect", 191), t("3", 112), t("6", 181),
                         t("HDRLOCAL", 206)}));
    append(tokens, line({origin(), t("Linked", 207), t("text", 205)}));
    const auto built = build({make_source(53, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "claimed continuation flow declined: " + built.error);
    if (built.catalog) {
      const auto &flow = built.catalog->entries.front()
                             .fields.front()
                             .line.body.flow.blocks;
      require(flow.size() == 3,
              "highlight/select paragraph continuations were flattened");
      if (flow.size() == 3) {
        const auto &styled =
            std::get<geist::detail::TrapFlowParagraphIR>(flow[1].value);
        const auto &linked =
            std::get<geist::detail::TrapFlowParagraphIR>(flow[2].value);
        require(styled.break_before.size() == 1 &&
                    styled.highlights.size() == 1 &&
                    styled.text.substr(styled.highlights[0].begin,
                                       styled.highlights[0].end -
                                           styled.highlights[0].begin) ==
                        "Styled",
                "highlighted continuation lost its boundary or local range");
        require(linked.break_before.size() == 1 && linked.links.size() == 1 &&
                    linked.text.substr(linked.links[0].begin,
                                       linked.links[0].end -
                                           linked.links[0].begin) == "Linked",
                "selector continuation lost its boundary or local range");
      }
    }
  }

  // A drawn envelope can interrupt and resume the same field. Its canonical
  // source position is inside that field's flow, not merely after field N.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("617", 208));
    append(tokens, line({t("SRTBLT4", 209)}));
    append(tokens, line({origin(), t("Drawn", 210), t("row", 211)}));
    append(tokens, line({t("SRETBL", 135), origin(), t("resumed", 212),
                         t("text", 205)}));
    const auto built = build({make_source(54, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "interrupted field flow declined: " + built.error);
    if (built.catalog) {
      const auto &blocks = built.catalog->entries.front()
                               .fields.front()
                               .line.body.flow.blocks;
      require(blocks.size() == 3 &&
                  std::holds_alternative<geist::detail::TrapFlowParagraphIR>(
                      blocks[0].value) &&
                  std::holds_alternative<geist::detail::TrapEmbeddedRegionIR>(
                      blocks[1].value) &&
                  std::holds_alternative<geist::detail::TrapFlowParagraphIR>(
                      blocks[2].value),
              "embedded region was moved outside its interrupted field");
      if (blocks.size() == 3) {
        const auto &resumed =
            std::get<geist::detail::TrapFlowParagraphIR>(blocks[2].value);
        require(resumed.text == "resumed text" &&
                    std::get<geist::detail::TrapEmbeddedRegionIR>(
                        blocks[1].value)
                            .cell_end <= resumed.cell_begin,
                "field did not resume after the embedded region");
      }
    }
  }

  // A drawn envelope can interrupt the headline itself. Once compound flow
  // is selected, lowering must keep the region inside the list item rather
  // than rendering only the headline's flat compatibility projection.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({t("SRMSG", 110), t("619", 220)}));
    append(tokens,
           line({t("cfont", 111), t("3", 112), t("3", 112), t("2", 113)}));
    append(tokens, line({origin(), t("619", 220)}));
    append(tokens, line({t("SRTBLT5", 221)}));
    append(tokens, line({origin(), t("Headline", 222), t("drawing", 223)}));
    append(tokens, line({t("SRETBL", 135)}));
    append(tokens,
           line({t("cfont", 111), t("3", 112), t("12", 114), t("2", 113)}));
    append(tokens, line({origin(), t("Description:", 115), t("body", 224)}));
    const auto built = build({make_source(56, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "compound headline extraction declined: " + built.error);
    if (built.catalog) {
      const auto &headline = built.catalog->entries.front().headline.body.flow;
      require(headline.blocks.size() == 2 &&
                  std::holds_alternative<geist::detail::TrapEmbeddedRegionIR>(
                      headline.blocks.back().value),
              "headline drawing was not retained in source flow");
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(),
              "compound headline lowering declined: " + error);
      if (document) {
        const auto markdown =
            geist::detail::render_document_markdown(*document);
        const auto headline_at = markdown.find("**619**");
        const auto drawing_at = markdown.find("Headline drawing");
        const auto field_at = markdown.find("**Description:** body");
        require(headline_at != std::string::npos && drawing_at > headline_at &&
                    field_at > drawing_at,
                "compound headline flow was flattened or reordered: " +
                    markdown);
      }
    }
  }

  // A blank row inside a native list starts a second paragraph in the same
  // item when the following row remains indented past the marker column.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("620", 225));
    append(tokens, line({origin(), {TokenWords{0x2666}, 214}, t("  ", 20),
                         t("First", 215), t("item", 216)}));
    append(tokens, line({}));
    append(tokens,
           line({t("      ", 226), t("Continuation", 227), t("paragraph", 228)}));
    append(tokens, line({origin(), {TokenWords{0x2666}, 214}, t("  ", 20),
                         t("Second", 219), t("item", 216)}));
    const auto built = build({make_source(57, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "list continuation extraction declined: " + built.error);
    if (built.catalog) {
      const auto &blocks = built.catalog->entries.front()
                               .fields.front()
                               .line.body.flow.blocks;
      require(blocks.size() == 2 &&
                  std::holds_alternative<geist::detail::TrapFlowListIR>(
                      blocks.back().value),
              "blank row moved a list continuation outside its list");
      if (blocks.size() == 2 &&
          std::holds_alternative<geist::detail::TrapFlowListIR>(
              blocks.back().value)) {
        const auto &list =
            std::get<geist::detail::TrapFlowListIR>(blocks.back().value);
        require(list.items.size() == 2 &&
                    list.items.front().blocks.size() == 2,
                "list continuation did not remain a second item paragraph");
      }
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(),
              "list continuation lowering declined: " + error);
      if (document) {
        const auto markdown =
            geist::detail::render_document_markdown(*document);
        const auto html =
            geist::detail::render_document_html_blocks(*document, {});
        const auto first = markdown.find("First item");
        const auto continuation = markdown.find("Continuation paragraph");
        const auto second = markdown.find("Second item");
        require(first != std::string::npos && continuation > first &&
                    second > continuation,
                "Markdown reordered the list continuation: " + markdown);
        require(html.find(
                    "<p class=\"geist-paragraph\">First item</p>"
                    "<p class=\"geist-paragraph\">Continuation paragraph"
                    "</p></li><li class=\"geist-list-item\">Second item") !=
                    std::string::npos,
                "HTML moved the continuation outside its item: " + html);
      }
    }
  }

  // Native list ownership comes from the row's exact origin + U+2666 +
  // two-cell-gap shape. Differing source columns form a recursive list, and
  // both full renderers must consume that same compound DocumentIR.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("618", 213));
    append(tokens, line({origin(), {TokenWords{0x2666}, 214}, t("  ", 20),
                         t("First", 215), t("item", 216)}));
    append(tokens,
           line({t("          ", 217), {TokenWords{0x2666}, 214},
                 t("  ", 20), t("Nested", 218), t("item", 216)}));
    append(tokens, line({origin(), {TokenWords{0x2666}, 214}, t("  ", 20),
                         t("Second", 219), t("item", 216)}));
    const auto built = build({make_source(55, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "native list extraction declined: " + built.error);
    if (built.catalog) {
      const auto &flow = built.catalog->entries.front()
                             .fields.front()
                             .line.body.flow.blocks;
      require(flow.size() == 2 &&
                  std::holds_alternative<geist::detail::TrapFlowListIR>(
                      flow.back().value),
              "native list was flattened into a trap paragraph");
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(),
              "native list DocumentIR lowering declined: " + error);
      if (document) {
        const auto markdown =
            geist::detail::render_document_markdown(*document);
        const auto html =
            geist::detail::render_document_html_blocks(*document, {});
        const auto first = markdown.find("First item");
        const auto nested = markdown.find("Nested item");
        const auto second = markdown.find("Second item");
        require(first != std::string::npos && nested > first &&
                    second > nested &&
                    markdown.find("**Description:** first field") !=
                        std::string::npos &&
                    markdown.find("\n            -     Nested item") !=
                        std::string::npos &&
                    markdown.find("\n    - Second item") !=
                        std::string::npos,
                "Markdown flattened recursive trap list content: " +
                    markdown);
        require(html.find(
                    "<strong class=\"geist-strong\">Description:</strong> "
                    "first field") != std::string::npos &&
                    html.find(
                    "<ul class=\"geist-list\"><li class=\"geist-list-item\">"
                    "<p class=\"geist-paragraph\">First item</p>"
                    "<ul class=\"geist-list\"><li class=\"geist-list-item\">"
                    "Nested item</li></ul>"
                    "</li><li class=\"geist-list-item\">"
                    "Second item</li></ul>") !=
                    std::string::npos,
                "HTML flattened recursive trap list content: " + html);
      }
    }
  }
  std::cout << "trap catalog IR synthetic tests passed\n";
  return 0;
}
