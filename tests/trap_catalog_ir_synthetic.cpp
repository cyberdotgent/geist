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
//     columns do not name;
//   * the headline and each field lowered as separate paragraphs, with no
//     synthesized separator;
//   * `SRFIG`/`SREFIG` and `SRFTN`/`SREFTN` envelopes placed where the entry
//     draws them, with closers that name nothing, and a stray closer that
//     declines;
//   * selectors queued on consecutive control lines, each keeping its own
//     claim on the shared row;
//   * a bare `SRMSG` that stays where the entry names it;
//   * introduction bullets read as a native list, including a record that
//     opens on an unrowed bullet row, a deeper paragraph that continues the
//     item, and run-together bullets that decline;
//   * introduction `cselect` rows placed as links on the cells they mark --
//     queued, behind a destination line, and across a record boundary --
//     and a selector past its row that declines;
//   * an `SRFIG` envelope drawn in the introduction, and a stray closer;
//   * an `SI` subject-index line inside an entry kept as a silent index
//     term, and one whose segment runs on into drawn text, which declines.
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

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
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
    // `T` (TP) has no verified presentation.
    unknown_font.back().words = words("T");
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
                    markdown.find("\n    - First item\n\n        - Nested item") !=
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
  // The headline and every field are separate paragraphs. Hosted BookServer
  // serves `<B>0</B>`, `<p>`, `<B>Description:</B>  coldStart`, `<p>`, ...
  // (SC31-711 4.1.1, DT 19941010174546); no separator character exists in
  // the source or in the served page.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("629", 234));
    const auto built = build({make_source(58, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "field paragraph catalogue declined: " + built.error);
    if (built.catalog) {
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(), "field paragraph lowering declined: " + error);
      if (document) {
        const auto markdown =
            geist::detail::render_document_markdown(*document);
        const auto html =
            geist::detail::render_document_html_blocks(*document, {});
        require(markdown.find(u8"—") == std::string::npos &&
                    html.find(u8"—") == std::string::npos,
                "trap lowering synthesized a separator: " + markdown);
        require(html.find("<p class=\"geist-paragraph\"><strong "
                          "class=\"geist-strong\">Description:</strong> first "
                          "field</p>") != std::string::npos,
                "trap field did not open its own paragraph: " + html);
      }
    }
  }

  // `SRFIG<id>` ... `SREFIG` inside an entry is a drawn figure envelope.
  // Hosted BookServer serves N2AH1MST 10.0 `ATB101I` (DT 19910329000100) as
  // the figure's own display lines with `<a name="FIGFIGUNIQ20">` on the
  // first, in place; the closer names nothing.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("630", 235));
    append(tokens, line({t("SRFIGFIGUNIQ5", 236)}));
    append(tokens,
           line({t("cfont", 111), t("3", 112), t("5", 237), t("2", 113)}));
    append(tokens, line({origin(), t("Drawn", 238), t("figure", 239)}));
    append(tokens, line({origin(), t("second", 240), t("row", 211)}));
    append(tokens, line({t("SREFIG", 241)}));
    append(tokens, line({origin(), t("after", 141), t("figure.", 242)}));
    const auto built = build({make_source(59, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "figure envelope catalogue declined: " + built.error);
    if (built.catalog) {
      require(built.catalog->entry_named_destinations.empty(),
              "figure envelope controls were hoisted to topic destinations");
      const auto &blocks = built.catalog->entries.front()
                               .fields.front()
                               .line.body.flow.blocks;
      const auto region = std::find_if(
          blocks.begin(), blocks.end(), [](const auto &block) {
            return std::holds_alternative<geist::detail::TrapEmbeddedRegionIR>(
                block.value);
          });
      require(region != blocks.end(),
              "figure envelope was not kept as a positioned region");
      if (region != blocks.end()) {
        const auto &drawn =
            std::get<geist::detail::TrapEmbeddedRegionIR>(region->value);
        require(drawn.start.kind ==
                        geist::detail::TrapEmbeddedControlKindIR::figure_start &&
                    drawn.end.kind ==
                        geist::detail::TrapEmbeddedControlKindIR::figure_end &&
                    drawn.lines.size() == 2 &&
                    drawn.start.source.token_begin != drawn.end.source.token_begin,
                "figure region lost its kind, lines or source bounds");
      }
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(), "figure envelope lowering declined: " + error);
      if (document) {
        require(document->named_destinations.empty(),
                "figure envelope ids became topic destinations");
        const auto html =
            geist::detail::render_document_html_blocks(*document, {});
        const auto markdown =
            geist::detail::render_document_markdown(*document);
        const auto anchor = html.find("id=\"FIGFIGUNIQ5\"");
        require(anchor != std::string::npos &&
                    html.find("id=\"FIGFIGUNIQ5\"", anchor + 1) ==
                        std::string::npos &&
                    html.find("EFIG") == std::string::npos,
                "figure anchor is missing, duplicated, or its closer named one: " +
                    html);
        const auto field = markdown.find("first field");
        const auto figure = markdown.find(
            "<strong class=\"geist-strong\">Drawn</strong> figure");
        const auto after = markdown.find("after figure.");
        require(field != std::string::npos && figure > field && after > figure,
                "figure envelope was not drawn in place: " + markdown);
      }
    }
  }

  // `SRFTN<id>` ... `SREFTN` is a footnote placed where the source puts it:
  // hosted N2AH1MST 10.0 serves `<a name="FTNFTNUNIQ19"><hr><h5> ... </h5></a>`
  // between two native list rows of `ATB348I`, and `SREFTN` names nothing.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("631", 243));
    append(tokens, line({t("SRFTNFTNUNIQ7", 244)}));
    append(tokens, line({origin(), t("VTAM", 245), t("is", 246), t("a", 247),
                         t("trademark.", 248)}));
    append(tokens, line({t("SREFTN", 249)}));
    append(tokens, line({origin(), t("Resumed", 250), t("text.", 251)}));
    const auto built = build({make_source(60, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "footnote envelope catalogue declined: " + built.error);
    if (built.catalog) {
      require(built.catalog->entry_named_destinations.empty(),
              "footnote envelope controls were hoisted to topic destinations");
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(),
              "footnote envelope lowering declined: " + error);
      if (document) {
        require(document->named_destinations.empty(),
                "footnote envelope ids became topic destinations");
        const auto html =
            geist::detail::render_document_html_blocks(*document, {});
        const auto markdown =
            geist::detail::render_document_markdown(*document);
        const auto anchor = html.find("id=\"FTNFTNUNIQ7\"");
        require(anchor != std::string::npos &&
                    html.find("id=\"FTNFTNUNIQ7\"", anchor + 1) ==
                        std::string::npos &&
                    html.find("EFTN") == std::string::npos,
                "footnote anchor is missing, duplicated, or its closer named "
                "one: " + html);
        // The id names the destination; the reader prints no label for it.
        // Hosted serves `<a name="FTNFTNUNIQ19"><hr><h5> |  (*) VTAM ...`:
        // the footnote's own `(*)` is its only mark.
        require(html.find(">FTNFTNUNIQ7<") == std::string::npos &&
                    markdown.find("[^") == std::string::npos &&
                    markdown.find("<a id=\"FTNFTNUNIQ7\"></a>") !=
                        std::string::npos,
                "footnote printed its id or left Markdown links no anchor: " +
                    markdown);
        const auto field = markdown.find("first field");
        const auto note = markdown.find("VTAM is a trademark");
        const auto resumed = markdown.find("Resumed text.");
        require(field != std::string::npos && note > field && resumed > note,
                "footnote was not placed where the source draws it: " +
                    markdown);
      }
    }
    // A closer with no open envelope is not a footnote the family can place.
    auto stray = header("Sample", "Messages");
    append(stray, entry("632", 252));
    append(stray, line({t("SREFTN", 249)}));
    require(!build({make_source(61, stray)}, "Sample Messages").catalog,
            "a footnote closer with no open footnote was admitted");
  }

  // Two `cselect` controls queued on consecutive control lines both select
  // the first drawn line after them: N2AH1MST record 563 lines 11-13 store
  // `cselect 22 4 FTNFTNUNIQ22`, `cselect 31 4 FTNFTNUNIQ23`, then
  // `architecture (*) (SAA (*))`; hosted links both marks.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("633", 253));
    append(tokens, line({t("cselect", 191), t("7", 254), t("3", 112),
                         t("FTNA", 255)}));
    append(tokens, line({t("cselect", 191), t("17", 256), t("3", 112),
                         t("FTNB", 257)}));
    append(tokens, line({origin(), t("Two", 258), t("(*)", 259),
                         t("marks", 260), t("(*)", 259)}));
    const auto built = build({make_source(62, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "queued selector catalogue declined: " + built.error);
    if (built.catalog) {
      const auto &links =
          built.catalog->entries.front().fields.front().line.body.links;
      require(links.size() == 2 &&
                  (links.size() != 2 ||
                   (links[0].target == "FTNA" && links[1].target == "FTNB" &&
                    links[0].end <= links[1].begin &&
                    links[0].source.token_begin !=
                        links[1].source.token_begin)),
              "queued selectors did not both keep distinct source claims");
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(),
              "queued selector lowering declined: " + error);
      if (document) {
        const auto html =
            geist::detail::render_document_html_blocks(*document, {});
        require(html.find("href=\"#FTNA\">(*)</a>") != std::string::npos &&
                    html.find("href=\"#FTNB\">(*)</a>") != std::string::npos,
                "a queued selector link was lost: " + html);
      }
      auto moved = *built.catalog;
      auto &moved_links =
          moved.entries.front().fields.front().line.body.links;
      if (moved_links.size() == 2) {
        std::swap(moved_links[0].target, moved_links[1].target);
        require(!geist::detail::verify_trap_catalog_ir(
                    built.sources, built.layout, *built.ownership, moved,
                    &error),
                "swapped queued selector targets were accepted");
      }
    }
  }

  // A bare `SRMSG` inside an entry is a destination where it stands, not a
  // topic-level one: hosted N2AH1MST 2.0 serves `<a name="MSG">` immediately
  // before the footnote it precedes.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("634", 261));
    append(tokens, line({t("SRMSG", 110)}));
    append(tokens, line({t("SRFTNFTNUNIQ8", 263)}));
    append(tokens, line({origin(), t("Later", 262), t("text.", 251)}));
    append(tokens, line({t("SREFTN", 249)}));
    const auto built = build({make_source(63, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "positional SRMSG catalogue declined: " + built.error);
    if (built.catalog) {
      std::string error;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(),
              "positional SRMSG lowering declined: " + error);
      if (document) {
        const auto markdown =
            geist::detail::render_document_markdown(*document);
        const auto field = markdown.find("first field");
        const auto anchor = markdown.find("<a id=\"MSG\"></a>");
        const auto later = markdown.find("Later text.");
        require(field != std::string::npos && anchor > field && later > anchor,
                "bare SRMSG was hoisted out of its entry: " + markdown);
      }
    }
  }

  // A drawn figure row keeps the style its own `CFONT` states, at the columns
  // its display cells prove, and leaves out the revision bar in its margin.
  // Hosted N2AH1MST 10.0 (DT 19910329000100) serves FIGFIGUNIQ20's rows as
  // ` |          <B>ACTIVE</B> <B>LU'S</B> ...` for `cfont 12 6 2 19 4 2 ...`
  // above record 533 line 17; the bar is the same revision mark the entry's
  // reflowed rows drop.
  {
    const Token bar{TokenWords{'|'}, 252};
    const Token rule{TokenWords{0x2502}, 253};
    const Token blank{words(" "), 254};
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("632", 255));
    append(tokens, line({t("SRFIGFIGUNIQ6", 256)}));
    // ` |    Styled row`: `Styled` at column 6.
    append(tokens,
           line({t("cfont", 111), t("6", 258), t("6", 258), t("2", 113)}));
    append(tokens,
           line({blank, bar, origin(), t("Styled", 259), t("row", 211)}));
    // The same shape one column off: `Plain` is at column 6, not 7.
    append(tokens,
           line({t("cfont", 111), t("7", 260), t("5", 237), t("2", 113)}));
    append(tokens,
           line({blank, bar, origin(), t("Plain", 261), t("row", 211)}));
    // A box rule elsewhere in the row makes the left bar part of a drawing.
    append(tokens, line({blank, bar, origin(), t("Boxed", 262), rule}));
    append(tokens, line({t("SREFIG", 241)}));
    append(tokens, line({origin(), t("after", 141), t("figure.", 242)}));
    const auto built = build({make_source(61, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "styled figure catalogue declined: " + built.error);
    if (built.catalog) {
      const geist::detail::TrapEmbeddedRegionIR *drawn = nullptr;
      for (const auto &entry : built.catalog->entries)
        for (const auto &region : entry.embedded_regions)
          drawn = &region;
      require(drawn != nullptr && drawn->lines.size() == 3 &&
                  drawn->line_highlights.size() == 3,
              "styled figure lost its rows");
      if (drawn != nullptr && drawn->lines.size() == 3 &&
          drawn->line_highlights.size() == 3) {
        require(drawn->lines[0] == "      Styled row" &&
                    drawn->lines[1] == "      Plain row",
                "figure revision bar was kept: '" + drawn->lines[0] + "'");
        require(drawn->lines[2].find('|') == 1,
                "box rule row lost its left rule: '" + drawn->lines[2] + "'");
        require(drawn->line_highlights[0].size() == 1 &&
                    drawn->line_highlights[0][0].begin == 6 &&
                    drawn->line_highlights[0][0].end == 12,
                "figure row lost its proven CFONT style");
        require(drawn->line_highlights[1].empty(),
                "figure row admitted a CFONT span its cells do not prove");
        require(drawn->line_highlights[2].empty(),
                "unstyled figure row gained a style");
      }
      std::string error;
      auto changed = *built.catalog;
      for (auto &entry : changed.entries)
        for (auto &region : entry.embedded_regions)
          if (!region.line_highlights.empty() &&
              !region.line_highlights[0].empty())
            region.line_highlights[0][0].begin -= 1;
      require(!geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership, changed,
                  &error),
              "mutated figure row style was accepted");
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(), "styled figure lowering declined: " + error);
      if (document) {
        const auto html =
            geist::detail::render_document_html_blocks(*document, {});
        require(html.find(">      <strong class=\"geist-strong\">Styled"
                          "</strong> row\n      Plain row\n") !=
                    std::string::npos,
                "styled figure rows did not render in place: " + html);
      }
    }
  }

  // A bullet row behind a revision bar is still a native list row. N2AH1MST
  // record 599 line 9 is ` ` + `U+2502` + 17 blanks + `U+2666` + two blanks
  // + `TPADD`; hosted 10.0 (DT 19910329000100) serves ` |   °   TPADD`. A
  // second glyph in the leading run is not a revision margin.
  {
    const Token blank{words(" "), 254};
    const Token rule{TokenWords{0x2502}, 253};
    const Token bullet{TokenWords{0x2666}, 214};
    const auto revised = [&](const Token &mark, const Token &word) {
      return line({blank, mark, t("                 ", 264), bullet,
                   t("  ", 20), word});
    };
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("633", 263));
    // An unrevised sibling: a twenty-blank run puts its bullet in the same
    // drawn column as the revised rows, whose bar is followed by a blank the
    // row draws between the bar and the seventeen-blank run.
    append(tokens, line({t("                    ", 269), bullet, t("  ", 20),
                         t("TPLIST", 270)}));
    append(tokens, revised(rule, t("TPADD", 265)));
    append(tokens, revised(rule, t("TPALIAS", 266)));
    const auto built = build({make_source(62, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "revised list extraction declined: " + built.error);
    if (built.catalog) {
      const auto &blocks = built.catalog->entries.front()
                               .fields.front()
                               .line.body.flow.blocks;
      require(!blocks.empty() &&
                  std::holds_alternative<geist::detail::TrapFlowListIR>(
                      blocks.back().value) &&
                  std::get<geist::detail::TrapFlowListIR>(blocks.back().value)
                          .items.size() == 3,
              "bullet rows behind a revision bar were flattened or nested");
    }
    auto boxed = header("Sample", "Messages");
    append(boxed, entry("634", 267));
    append(boxed, revised(t("x", 268), t("TPADD", 265)));
    append(boxed, revised(t("x", 268), t("TPALIAS", 266)));
    const auto other = build({make_source(63, boxed)}, "Sample Messages");
    if (other.catalog)
      for (const auto &block :
           other.catalog->entries.front().fields.front().line.body.flow.blocks)
        require(!std::holds_alternative<geist::detail::TrapFlowListIR>(
                    block.value),
                "a drawn glyph in the leading run was read as a revision bar");
  }

  // A list item continued in the next record stays in its item. N2AH1MST
  // record 492 line 27 opens `Create a new parmlib member ...` behind a
  // bullet, and record 493 line 0 continues it at column 14; hosted 10.0
  // (DT 19910329000100) serves both rows in one bullet. A row back at the
  // entry's margin after the list is the entry's own paragraph again.
  {
    const Token bullet{TokenWords{0x2666}, 214};
    auto first = header("Sample", "Messages");
    append(first, entry("635", 271));
    append(first, line({origin(), bullet, t("  ", 20), t("Create", 272),
                        t("member", 273)}));
    std::vector<Token> second;
    append(second, line({t("      ", 226), t("Then", 274), t("ask", 275)}));
    append(second, line({}));
    append(second, line({origin(), t("Back", 276), t("out.", 277)}));
    const auto built = build({make_source(64, first), make_source(65, second)},
                             "Sample Messages");
    require(built.catalog.has_value(),
            "cross-record list item declined: " + built.error);
    if (built.catalog) {
      const auto &blocks = built.catalog->entries.front()
                               .fields.front()
                               .line.body.flow.blocks;
      const geist::detail::TrapFlowListIR *list = nullptr;
      for (const auto &block : blocks)
        if (const auto *candidate =
                std::get_if<geist::detail::TrapFlowListIR>(&block.value))
          list = candidate;
      require(list != nullptr && list->items.size() == 1 &&
                  list->items.front().blocks.size() == 1 &&
                  std::holds_alternative<geist::detail::TrapFlowParagraphIR>(
                      list->items.front().blocks.front().value) &&
                  std::get<geist::detail::TrapFlowParagraphIR>(
                      list->items.front().blocks.front().value)
                          .text == "Create member Then ask",
              "list item continued in the next record left its item");
      require(!blocks.empty() &&
                  std::holds_alternative<geist::detail::TrapFlowParagraphIR>(
                      blocks.back().value) &&
                  std::get<geist::detail::TrapFlowParagraphIR>(
                      blocks.back().value)
                          .text == "Back out.",
              "paragraph at the entry margin was pulled into the list");
      std::string error;
      require(geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership,
                  *built.catalog, &error),
              "cross-record list item failed verification: " + error);
    }
  }

  // A record that opens on a bullet row opens a new item. N2AH1MST record
  // 1484 begins with a ten-blank run, `U+2666`, two blanks and `Not the
  // first record in the member`, the second item of the list whose first
  // item `Missing` closes record 1483. The bullet lies in no segment payload.
  {
    const Token bullet{TokenWords{0x2666}, 214};
    auto first = header("Sample", "Messages");
    append(first, entry("636", 278));
    append(first, line({origin(), bullet, t("  ", 20), t("Missing", 279)}));
    std::vector<Token> second;
    append(second, line({origin(), bullet, t("  ", 20), t("Not", 280),
                         t("first", 281)}));
    append(second, line({}));
    append(second, line({origin(), t("Back", 276), t("out.", 277)}));
    const auto built = build({make_source(66, first), make_source(67, second)},
                             "Sample Messages");
    require(built.catalog.has_value(),
            "record-leading bullet declined: " + built.error);
    if (built.catalog) {
      const auto &blocks = built.catalog->entries.front()
                               .fields.front()
                               .line.body.flow.blocks;
      const geist::detail::TrapFlowListIR *list = nullptr;
      for (const auto &block : blocks)
        if (const auto *candidate =
                std::get_if<geist::detail::TrapFlowListIR>(&block.value))
          list = candidate;
      require(list != nullptr && list->items.size() == 2 &&
                  list->items.back().marker_source.logical_record == 67,
              "a record opening on a bullet row did not open a new item");
      std::string error;
      require(geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership,
                  *built.catalog, &error),
              "record-leading bullet failed verification: " + error);
    }
  }
  // Revision margins (SC34-425 2.4.21, 2.4.27, APPENDIX1.5.3). A revised row
  // opens with a one-cell blank token and a `|` token; the assembler draws
  // a blank after the bar, so the row's first word stands at column 3, the
  // column its CFONT names. The bar is the reader's margin, not entry text.
  const auto margin = [] {
    return std::vector<Token>{t(" ", 290), t("|", 291)};
  };
  const auto revised = [&](std::vector<Token> content) {
    auto row = margin();
    row.insert(row.end(), content.begin(), content.end());
    return line(row);
  };
  const auto revised_entry = [&](const std::string &head_font_column) {
    std::vector<Token> tokens;
    append(tokens, line({t("SRMSG", 110), t("640", 292)}));
    append(tokens, line({t("cfont", 111), t(head_font_column, 112),
                         t("3", 112), t("2", 113)}));
    append(tokens, revised({t("640", 292)}));
    append(tokens,
           line({t("cfont", 111), t("3", 112), t("4", 293), t("2", 113),
                 t("8", 294), t("9", 295), t("2", 113)}));
    append(tokens, revised({t("User", 296), t("Response:", 297),
                            t("Contact", 298), t("support.", 299)}));
    return tokens;
  };

  // The headline and the label behind a bar type, and the bar is in neither.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, revised_entry("3"));
    const auto built = build({make_source(70, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "a headline behind a revision bar declined: " + built.error);
    if (built.catalog && built.catalog->entries.size() == 1) {
      const auto &only = built.catalog->entries.front();
      require(only.headline.body.text == "640",
              "the revised headline is wrong: [" + only.headline.body.text +
                  "]");
      require(only.fields.size() == 1 &&
                  only.fields.front().label_text == "User Response:" &&
                  only.fields.front().line.body.text ==
                      "User Response: Contact support.",
              "the revised field did not keep its label and text");
      require(entry_text(only).find('|') == std::string::npos,
              "the revision bar leaked into the entry: [" + entry_text(only) +
                  "]");
      std::string error;
      require(geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership,
                  *built.catalog, &error),
              "the revised entry failed verification: " + error);
    }
  }

  // Mutation: the same headline under a CFONT one column off. With the bar
  // left out the projected text alone would still read `640`; the row's
  // cells say column 4 is `40`, so the family declines.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, revised_entry("4"));
    const auto built = build({make_source(71, tokens)}, "Sample Messages");
    require(!built.catalog.has_value(),
            "a revised headline under a misaligned CFONT was admitted");
    require(built.error.find("does not cover the columns it names") !=
                std::string::npos,
            "the misaligned revised headline declined for the wrong "
            "reason: " +
                built.error);
  }

  // Mutation, the APPENDIX1.5.3 guard: a CFONT that claims the bar's own
  // column keeps the bar in the row's text, and nothing may then map as
  // `| Us` -- neither a label nor a run. (The span starts left of the
  // catalogue's label column, so the row is plain body text, as any row
  // whose CFONT the labels cannot read.)
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("641", 300));
    append(tokens,
           line({t("cfont", 111), t("1", 301), t("4", 293), t("2", 113),
                 t("8", 294), t("9", 295), t("2", 113)}));
    append(tokens, revised({t("User", 296), t("Response:", 297),
                            t("Contact", 298), t("support.", 299)}));
    const auto built = build({make_source(72, tokens)}, "Sample Messages");
    if (built.catalog) {
      for (const auto &item : built.catalog->entries)
        for (const auto &field : item.fields) {
          require(field.label_text.find('|') == std::string::npos &&
                      field.line.spans_text.find('|') == std::string::npos,
                  "a styled bar was mapped into a label: [" +
                      field.label_text + "]");
          for (const auto &highlight : field.line.body.highlights)
            require(field.line.body.text.compare(highlight.begin, 1, "|") !=
                        0,
                    "a styled bar was mapped into a run");
          require(field.label_text != "User Response:",
                  "a label was read from a CFONT that names other columns");
        }
    }
  }

  // A native list bullet straight after the bar's blank (SC34-425 2.4.15.1
  // record 1565 line 33) is still the list marker, at column 3.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("642", 302));
    append(tokens, revised({{TokenWords{0x2666}, 214}, t("  ", 20),
                            t("Bulleted", 303), t("item", 216)}));
    const auto built = build({make_source(73, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "a revised bullet row declined: " + built.error);
    if (built.catalog) {
      const auto &blocks =
          built.catalog->entries.front().fields.front().line.body.flow.blocks;
      const geist::detail::TrapFlowListIR *list = nullptr;
      for (const auto &block : blocks)
        if (const auto *candidate =
                std::get_if<geist::detail::TrapFlowListIR>(&block.value))
          list = candidate;
      require(list != nullptr && list->marker_column == 3 &&
                  list->items.size() == 1,
              "a bullet behind a revision bar was not read as a list item");
    }
  }

  // The introduction's revised rows lose their bar too.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, revised({t("Revised", 304), t("introduction.", 305)}));
    append(tokens, line({}));
    append(tokens, line({origin(), t("Plain", 308), t("row.", 117)}));
    append(tokens, entry("643", 311));
    const auto built = build({make_source(74, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "a revised introduction declined: " + built.error);
    if (built.catalog) {
      std::string joined;
      for (const auto &paragraph : built.catalog->introduction)
        joined += "[" + paragraph.text + "]";
      require(joined == "[Revised introduction.][Plain row.]",
              "the revised introduction projected " + joined);
    }
  }

  // An indented CFONT row at the indent of the row above it continues that
  // row's paragraph: nothing in the source states a paragraph break. This
  // is SC24-546 A.0, where record 967 lines 19-20 are `cfont 5 4 E ...`
  // over `     HELP MSG DMSnnnE  (where nnn is the CMS error number` and
  // record 968 lines 0-1 are `cfont 5 3 E ...` over
  // `     and error type is either 'E' or'T')`; hosted DT 19940323131240
  // serves both lines inside one `<p>`. SC34-425 2.4.12.4's consecutive
  // `cfont 7 6 2` `LANG=` rows are one hosted `<p>` the same way.
  {
    auto first = header("Sample", "Messages");
    append(first, line({origin(), t("Use", 350), t("this:", 351)}));
    append(first, line({}));
    append(first, line({t("cfont", 111), t("5", 340), t("4", 293),
                        t("E", 342)}));
    append(first, line({t("     ", 352), t("HELP", 353), t("(where", 354),
                        t("number", 355)}));
    std::vector<Token> second;
    append(second, line({t("cfont", 111), t("5", 340), t("3", 112),
                         t("E", 342)}));
    append(second, line({t("     ", 352), t("and", 356), t("type)", 357)}));
    append(second, line({}));
    append(second, entry("647", 358));
    const auto built = build({make_source(79, first), make_source(80, second)},
                             "Sample Messages");
    require(built.catalog.has_value(),
            "the continued example introduction declined: " + built.error);
    if (built.catalog) {
      std::string joined;
      for (const auto &paragraph : built.catalog->introduction)
        joined += "[" + paragraph.text + "]";
      require(joined.find("[HELP (where number and type)]") !=
                  std::string::npos,
              "a same-indent CFONT continuation row split its paragraph: " +
                  joined);
      // Neither row is wholly example text (each span styles one word), and
      // the first row is the paragraph's widest, so the formatter had no
      // room left on it: the boundary is an ordinary reflow join.
      for (const auto &paragraph : built.catalog->introduction)
        require(paragraph.line_breaks.empty(),
                "a filled row gained a line break: " + paragraph.text);
    }
  }

  // Helpers for the row line-break cases below: the text of each
  // introduction paragraph with `|` at every recorded line break.
  const auto broken_text = [](const geist::detail::TrapCatalogIR &catalog) {
    std::string joined;
    for (const auto &paragraph : catalog.introduction) {
      auto text = paragraph.text;
      for (auto at = paragraph.line_breaks.rbegin();
           at != paragraph.line_breaks.rend(); ++at)
        if (*at < text.size())
          text[*at] = '|';
      joined += "[" + text + "]";
    }
    return joined;
  };
  const auto lowered = [](const geist::detail::TrapCatalogIR &catalog,
                          std::string &markdown, std::string &html) {
    std::string error;
    const auto document = geist::detail::lower_trap_catalog_to_document_ir(
        {"12", "Sample Messages"}, catalog, &error);
    require(document.has_value(), "line-break lowering declined: " + error);
    if (!document)
      return;
    std::size_t breaks = 0;
    for (const auto &block : document->blocks)
      if (const auto *paragraph =
              std::get_if<geist::detail::ParagraphBlockIR>(&block.node))
        for (const auto &inline_node : paragraph->content)
          if (std::holds_alternative<geist::detail::HardBreakInlineIR>(
                  inline_node.node)) {
            ++breaks;
            require(inline_node.origin.detail ==
                        "trap catalog introduction row line break" &&
                        !inline_node.origin.slices.empty(),
                    "a row line break has no source provenance");
          }
    require(breaks != 0, "no hard break reached the DocumentIR");
    markdown = geist::detail::render_document_markdown(*document);
    html = geist::detail::render_document_html_blocks(*document, {});
  };

  // Two example rows, the second in the next record, each wholly under its
  // own `E` CFONT span at the row's first drawn cell: example text is never
  // filled, so the boundary is a line break inside the one paragraph. This
  // is SC24-546 A.0 record 967 line 20 `     HELP MSG DMSnnnE  (where nnn
  // is the CMS error number` over record 968 line 1 `     and error type is
  // either 'E' or'T')`; hosted DT 19940323131240 serves the two as separate
  // lines of one `<p>`.
  {
    auto first = header("Sample", "Messages");
    append(first, line({origin(), t("Use", 350), t("this:", 351)}));
    append(first, line({}));
    append(first, line({t("cfont", 111), t("5", 340), t("18", 400),
                        t("E", 342)}));
    append(first, line({t("     ", 352), t("HELP", 353), t("(where", 354),
                        t("number", 355)}));
    std::vector<Token> second;
    append(second, line({t("cfont", 111), t("5", 340), t("9", 401),
                         t("E", 342)}));
    append(second, line({t("     ", 352), t("and", 356), t("type)", 357)}));
    append(second, line({}));
    append(second, entry("647", 358));
    const auto built = build({make_source(79, first), make_source(80, second)},
                             "Sample Messages");
    require(built.catalog.has_value(),
            "the example-row introduction declined: " + built.error);
    if (built.catalog) {
      const auto joined = broken_text(*built.catalog);
      require(joined.find("[HELP (where number|and type)]") !=
                  std::string::npos,
              "two example rows did not keep their line break: " + joined);
      require(geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership,
                  *built.catalog, nullptr),
              "the example-row introduction did not verify");
      std::string markdown, html;
      lowered(*built.catalog, markdown, html);
      require(markdown.find("HELP \\(where number  \nand type\\)") !=
                  std::string::npos,
              "the example rows did not render a Markdown hard break: " +
                  markdown);
      require(html.find("HELP (where number<br>and type)") !=
                  std::string::npos,
              "the example rows did not render <br>: " + html);
    }
  }

  // Indented rows each opened by their own CFONT, not example text, where
  // the earlier row left room for the next row's first word before the
  // paragraph's widest row: the source ended those rows on purpose. This is
  // SC34-425 2.4.12.4 record 1517, `cfont 7 6 2` over `       LANG=A
  // Assembler only`, `       LANG=T ...`, `       LANG=R REXX or similar
  // languages ...`; hosted DT 19921112160049 serves each on its own line of
  // one `<p>`.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({origin(), t("Values", 402), t("are:", 403)}));
    append(tokens, line({}));
    append(tokens, line({t("cfont", 111), t("7", 404), t("6", 405),
                         t("2", 113)}));
    append(tokens, line({t("       ", 406), t("LANG=A", 407),
                         t("Assembler", 408), t("only", 409)}));
    append(tokens, line({t("cfont", 111), t("7", 404), t("6", 405),
                         t("2", 113)}));
    append(tokens, line({t("       ", 406), t("LANG=T", 410), t("TEXT", 411)}));
    append(tokens, line({t("cfont", 111), t("7", 404), t("6", 405),
                         t("2", 113)}));
    append(tokens, line({t("       ", 406), t("LANG=R", 412), t("REXX", 413),
                         t("or", 414), t("similar", 415),
                         t("languages", 416), t("which", 417),
                         t("use", 418), t("comment", 419),
                         t("delimiters", 420)}));
    append(tokens, line({}));
    append(tokens, entry("648", 421));
    const auto built = build({make_source(81, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "the keyword-row introduction declined: " + built.error);
    if (built.catalog) {
      const auto joined = broken_text(*built.catalog);
      require(joined.find("[LANG=A Assembler only|LANG=T TEXT|LANG=R REXX or "
                          "similar languages which use comment "
                          "delimiters]") != std::string::npos,
              "keyword rows with room left did not break: " + joined);
      std::string markdown, html;
      lowered(*built.catalog, markdown, html);
      require(html.find("LANG=A Assembler only<br>LANG=T TEXT<br>LANG=R") !=
                  std::string::npos,
              "the keyword rows did not render <br>: " + html);
    }
  }

  // Rows that must still join. A row opened by its CFONT after a row that
  // filled the paragraph's width is a wrapped sentence whose next word the
  // source styles (SC34-425 2.4.31 `see the DCF` / `SCRIPT/VS
  // documentation`), and rows with no CFONT are ordinary reflowed prose
  // whatever their length.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({origin(), t("Intro.", 430)}));
    append(tokens, line({}));
    append(tokens, line({t("     ", 352), t("The", 431), t("parser", 432),
                         t("reads", 433), t("every", 434), t("row", 435),
                         t("of", 436), t("the", 437)}));
    append(tokens, line({t("cfont", 111), t("5", 340), t("6", 405),
                         t("2", 113)}));
    append(tokens, line({t("     ", 352), t("MEMBER", 438), t("and", 356),
                         t("stops.", 439)}));
    append(tokens, line({}));
    append(tokens, line({t("     ", 352), t("Short", 440), t("row.", 441)}));
    append(tokens, line({t("     ", 352), t("Next", 442), t("row.", 441)}));
    append(tokens, line({}));
    append(tokens, entry("650", 443));
    const auto built = build({make_source(84, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "the reflowed introduction declined: " + built.error);
    if (built.catalog) {
      const auto joined = broken_text(*built.catalog);
      require(joined.find("[The parser reads every row of the MEMBER and "
                          "stops.][Short row. Next row.]") != std::string::npos,
              "reflowed prose rows gained a line break: " + joined);
    }
  }

  // Introduction glyphs the decided framing proves are drawn. A `)` that
  // closes the paragraph's own `(` at a line end stays even though the next
  // line's length byte spells blanks (SC34-425 2.4.15.1 record 1557 line
  // 17), and so does a `-` with more drawn cells after it on its line
  // (record 1559 line 18, `001201-` + blanks + `BADCPY1`).
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({origin(), t("Column", 320), t("ignored", 321),
                         t("(treated", 322), t("as", 323), t("comment", 324),
                         {TokenWords{1}, 0}, t(")", 325)}));
    // A length byte whose dictionary word is a blank run, as in the source.
    const std::vector<Token> next_line{t("Next", 326), t("row.", 327)};
    tokens.push_back({words("        "),
                      static_cast<std::uint16_t>(next_line.size())});
    append(tokens, next_line);
    append(tokens, line({}));
    append(tokens, line({t("cfont", 111), t("5", 340), t("7", 341),
                         t("E", 342), t("47", 343), t("7", 341),
                         t("E", 342)}));
    append(tokens, revised({t("  ", 16), t("001201", 328), {TokenWords{1}, 0},
                            t("-", 329), t(std::string(34, ' '), 330),
                            t("BADCPY1", 331)}));
    append(tokens, line({}));
    append(tokens, entry("645", 332));
    const auto built = build({make_source(76, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "the glyph introduction declined: " + built.error);
    if (built.catalog) {
      std::string introduction;
      for (const auto &paragraph : built.catalog->introduction)
        introduction += "[" + paragraph.text + "]";
      require(introduction.find("comment)") != std::string::npos,
              "a closing parenthesis the framing proves drawn was dropped: " +
                  introduction);
      require(introduction.find("001201-") != std::string::npos,
              "a mid-line hyphen the framing proves drawn was dropped: " +
                  introduction);
    }
  }

  // An attached glyph that ends a record before the catalog's own record is
  // the sentence's own, not the catalog boundary (SC34-425 2.4.27 record
  // 1627 line 26 ` |     invoked:`).
  {
    auto first = header("Sample", "Messages");
    append(first, line({origin(), t("Concatenated", 333), t("when", 334),
                        t("invoked", 335), {TokenWords{1}, 0}, t(":", 336)}));
    std::vector<Token> second;
    append(second, line({}));
    append(second, line({origin(), t("Example", 337), t("text.", 338)}));
    append(second, entry("646", 339));
    const auto built = build({make_source(77, first), make_source(78, second)},
                             "Sample Messages");
    require(built.catalog.has_value(),
            "the two-record introduction declined: " + built.error);
    if (built.catalog) {
      std::string introduction;
      for (const auto &paragraph : built.catalog->introduction)
        introduction += "[" + paragraph.text + "]";
      require(introduction.find("invoked:") != std::string::npos,
              "a record-final colon was taken for the catalog boundary: " +
                  introduction);
    }
  }

  // Bullets in the introduction are a native list (SC34-425 2.4.10.1 records
  // 1489-1490, 2.4.13 records 1523-1524, 2.4.16.1 records 1573-1574). A
  // record may open on a bullet row that LayoutIR leaves unrowed, after the
  // blank line that ends the previous record; its wrapped line belongs to
  // the same item, a deeper paragraph continues the item, and a paragraph
  // back at the origin ends the list.
  const Token intro_bullet{TokenWords{0x2666}, 214};
  const auto intro_list_sources = [&](std::uint32_t first_record,
                                      std::vector<Token> extra_second) {
    auto first = header("Sample", "Messages");
    append(first, line({origin(), t("Rules", 350), t("follow:", 351)}));
    append(first, line({}));
    append(first, line({origin(), intro_bullet, t("  ", 20), t("Alpha", 352),
                        t("item.", 353)}));
    append(first, line({}));
    std::vector<Token> second;
    append(second, line({origin(), intro_bullet, t("  ", 20), t("Beta", 354),
                         t("starts", 355)}));
    append(second, line({t("       ", 307), t("and", 356), t("wraps.", 357)}));
    append(second, extra_second);
    append(second, line({}));
    append(second, line({t("       ", 307), t("Indented", 358),
                         t("continuation.", 359)}));
    append(second, line({}));
    append(second, line({origin(), t("Plain", 360), t("closing.", 361)}));
    append(second, line({}));
    append(second, entry("650", 362));
    return std::vector<DecodedLogicalRecordSource>{
        make_source(first_record, first),
        make_source(first_record + 1, second)};
  };
  {
    const auto built = build(intro_list_sources(80, {}), "Sample Messages");
    require(built.catalog.has_value(),
            "an introduction bullet list declined: " + built.error);
    if (built.catalog) {
      std::string shape;
      for (const auto &paragraph : built.catalog->introduction)
        shape += std::string("[") +
                 (paragraph.list_marker ? "*" : "") +
                 (paragraph.list_continuation ? "+" : "") + paragraph.text +
                 "]";
      require(shape == "[Rules follow:][*Alpha item.][*Beta starts and "
                       "wraps.][+Indented continuation.][Plain closing.]",
              "the introduction list has the wrong shape: " + shape);
      std::string error;
      require(geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership,
                  *built.catalog, &error),
              "the introduction list failed verification: " + error);
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(),
              "the introduction list did not lower: " + error);
      if (document) {
        const auto markdown =
            geist::detail::render_document_markdown(*document);
        require(markdown.find("- Alpha item\\.\n- Beta starts and wraps\\.\n\n"
                              "    Indented continuation\\.\n\nPlain "
                              "closing\\.") != std::string::npos,
                "the introduction list rendered wrongly:\n" + markdown);
        const auto html =
            geist::detail::render_document_html_blocks(*document, {});
        require(html.find("<li class=\"geist-list-item\">Alpha item.</li>") !=
                    std::string::npos,
                "the introduction list has no HTML list item:\n" + html);
      }
    }
  }

  // Row line breaks survive a compact-list split: the first item's two
  // example rows keep their break, at an offset moved into the item's own
  // text, and the boundary to the next bullet row is the list's, not a
  // break.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({origin(), t("Rules", 350), t("follow:", 351)}));
    append(tokens, line({}));
    append(tokens, line({origin(), intro_bullet, t("  ", 20),
                         t("Alpha:", 403)}));
    append(tokens, line({t("cfont", 111), t("7", 306), t("15", 404),
                         t("E", 342)}));
    append(tokens, line({t("       ", 307), t("HELP", 405), t("one", 406),
                         t("more", 407)}));
    append(tokens, line({t("cfont", 111), t("7", 306), t("7", 341),
                         t("E", 342)}));
    append(tokens, line({t("       ", 307), t("and", 408), t("two", 409)}));
    append(tokens, line({origin(), intro_bullet, t("  ", 20), t("Gamma", 363),
                         t("item.", 410)}));
    append(tokens, line({}));
    append(tokens, entry("657", 411));
    const auto built = build({make_source(85, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "a compact list with row breaks declined: " + built.error);
    if (built.catalog) {
      std::string shape;
      for (const auto &paragraph : built.catalog->introduction)
        shape += std::string(paragraph.list_marker ? "*" : "");
      const auto joined = broken_text(*built.catalog);
      require(shape == "**" &&
                  joined.find("HELP one more|and two]") != std::string::npos &&
                  joined.find("[Gamma item.]") != std::string::npos,
              "a compact list item lost or moved its row line break: " +
                  shape + " " + joined);
      std::string error;
      require(geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership,
                  *built.catalog, &error),
              "the compact list with row breaks failed verification: " +
                  error);
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(),
              "the compact list with row breaks did not lower: " + error);
      if (document) {
        const auto html =
            geist::detail::render_document_html_blocks(*document, {});
        require(html.find("more<br") != std::string::npos,
                "the list item's row break did not reach HTML: " + html);
      }
    }
  }

  // A compact list: a second bullet row with no empty line before it is the
  // next item of the same list, though the prose reads both rows as one
  // paragraph (SC34-425 2.4.21 record 1601 lines 31-35, four bullet rows
  // that hosted serves as four bullets in one `<p>`). The deeper paragraph
  // after the empty line continues the last item.
  {
    const auto built = build(
        intro_list_sources(82, line({origin(), intro_bullet, t("  ", 20),
                                     t("Gamma", 363)})),
        "Sample Messages");
    require(built.catalog.has_value(),
            "a compact introduction list declined: " + built.error);
    if (built.catalog) {
      std::string shape;
      for (const auto &paragraph : built.catalog->introduction)
        shape += std::string("[") +
                 (paragraph.list_marker ? "*" : "") +
                 (paragraph.list_continuation ? "+" : "") + paragraph.text +
                 "]";
      require(shape == "[Rules follow:][*Alpha item.][*Beta starts and "
                       "wraps.][*Gamma][+Indented continuation.][Plain "
                       "closing.]",
              "the compact introduction list has the wrong shape: " + shape);
      std::string error;
      require(geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership,
                  *built.catalog, &error),
              "the compact introduction list failed verification: " + error);
    }
  }

  // Mutation: a bullet row straight after the paragraph's own text, with no
  // empty line, is not the opening row of an item; the introduction
  // declines rather than guess where the list starts.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({origin(), t("Rules", 350), t("follow:", 351)}));
    append(tokens, line({origin(), intro_bullet, t("  ", 20), t("Alpha", 352),
                         t("item.", 353)}));
    append(tokens, line({}));
    append(tokens, entry("656", 402));
    const auto built = build({make_source(84, tokens)}, "Sample Messages");
    require(!built.catalog.has_value(),
            "a bullet inside an introduction paragraph was admitted");
    require(built.error.find("list bullet") != std::string::npos,
            "the misplaced bullet declined for the wrong reason: " +
                built.error);
  }

  // `cselect` rows of the introduction are links on the row they mark, as in
  // an entry (SC34-425 2.4.32 record 1641 lines 30-34: two queued selectors,
  // the row `See Table 18 ... Figure 88 shows a sample`, the destination
  // line `SRSPTTPRE` and the row `input list.`). The destination line draws
  // nothing and its payload row is prose.
  const auto intro_selector_tokens = [&](const std::string &second_column) {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({t("cselect", 191), t("7", 254), t("3", 112),
                         t("TBLA", 370)}));
    append(tokens, line({t("cselect", 191), t(second_column, 371),
                         t("3", 112), t("FIGB", 372)}));
    append(tokens, line({origin(), t("See", 373), t("Tab", 374),
                         t("for", 375), t("Fig", 376), t("shows", 377)}));
    append(tokens, line({t("SRSPTX", 378)}));
    append(tokens, line({origin(), t("sample", 379), t("list.", 380)}));
    append(tokens, line({}));
    append(tokens, line({origin(), t("Closing", 397), t("words.", 398)}));
    append(tokens, line({}));
    append(tokens, entry("651", 381));
    return tokens;
  };
  {
    const auto built =
        build({make_source(90, intro_selector_tokens("15"))}, "Sample Messages");
    require(built.catalog.has_value(),
            "an introduction selector declined: " + built.error);
    if (built.catalog && !built.catalog->introduction.empty()) {
      const auto &paragraph = built.catalog->introduction.front();
      require(paragraph.text == "See Tab for Fig shows sample list.",
              "the selected introduction paragraph is wrong: [" +
                  paragraph.text + "]");
      require(paragraph.links.size() == 2 &&
                  paragraph.text.substr(paragraph.links[0].begin,
                                        paragraph.links[0].end -
                                            paragraph.links[0].begin) ==
                      "Tab" &&
                  paragraph.links[0].target == "TBLA" &&
                  paragraph.text.substr(paragraph.links[1].begin,
                                        paragraph.links[1].end -
                                            paragraph.links[1].begin) ==
                      "Fig" &&
                  paragraph.links[1].target == "FIGB",
              "the introduction selectors did not mark their own cells");
      std::string error;
      require(geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership,
                  *built.catalog, &error),
              "the introduction selectors failed verification: " + error);
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(),
              "the introduction selectors did not lower: " + error);
      if (document) {
        const auto html =
            geist::detail::render_document_html_blocks(*document, {});
        require(html.find("href=\"#TBLA\">Tab</a>") != std::string::npos &&
                    html.find("href=\"#FIGB\">Fig</a>") != std::string::npos,
                "an introduction selector link was lost: " + html);
        const auto markdown =
            geist::detail::render_document_markdown(*document);
        require(markdown.find("[Tab](<#TBLA>)") != std::string::npos,
                "an introduction selector link was lost:\n" + markdown);
      }
    }
  }

  // Mutation: a selector whose columns run past its row's cells declines;
  // it is never placed on whatever text happens to be nearby.
  {
    const auto built =
        build({make_source(91, intro_selector_tokens("40"))}, "Sample Messages");
    require(!built.catalog.has_value(),
            "an introduction selector past its row was admitted");
    require(built.error.find("selector") != std::string::npos,
            "the misplaced introduction selector declined for the wrong "
            "reason: " +
                built.error);
  }

  // A destination line between a selector and its row draws nothing, so the
  // selector still marks the row (SC34-425 2.4.33 record 1647 lines 6-8:
  // `cselect 33 11 FIGADAOUT`, `SRSPTTPST`, `   the format of the ADAOUT
  // file.  Figure 89 shows ...`).
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({origin(), t("Lead", 382), t("text.", 390)}));
    append(tokens, line({}));
    append(tokens, line({t("cselect", 191), t("7", 254), t("3", 112),
                         t("FIGE", 399)}));
    append(tokens, line({t("SRSPTY", 400)}));
    append(tokens, line({origin(), t("See", 373), t("Fig", 376),
                         t("here.", 388)}));
    append(tokens, line({}));
    append(tokens, line({origin(), t("Closing", 397), t("words.", 398)}));
    append(tokens, line({}));
    append(tokens, entry("655", 401));
    const auto built = build({make_source(96, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "a selector behind a destination line declined: " + built.error);
    if (built.catalog && built.catalog->introduction.size() > 1) {
      const auto &paragraph = built.catalog->introduction[1];
      require(paragraph.links.size() == 1 &&
                  paragraph.links[0].target == "FIGE" &&
                  paragraph.text.substr(paragraph.links[0].begin,
                                        paragraph.links[0].end -
                                            paragraph.links[0].begin) ==
                      "Fig",
              "a selector behind a destination line missed its row: [" +
                  paragraph.text + "]");
    }
  }

  // A selector that closes its record marks the next record's first row
  // (SC34-425 2.4.33 record 1648 line 29 `cselect 13 9 FIGADAOUT`, record
  // 1649 line 0 `   Notice in Figure 89 that ...`).
  {
    auto first = header("Sample", "Messages");
    append(first, line({origin(), t("Lead", 382), t("text", 383)}));
    append(first, line({t("cselect", 191), t("13", 384), t("3", 112),
                        t("FIGC", 385)}));
    std::vector<Token> second;
    append(second, line({origin(), t("Notice", 386), t("in", 387),
                         t("Fig", 376), t("here.", 388)}));
    append(second, line({}));
    append(second, entry("652", 389));
    const auto built = build({make_source(92, first), make_source(93, second)},
                             "Sample Messages");
    require(built.catalog.has_value(),
            "a record-closing introduction selector declined: " + built.error);
    if (built.catalog && !built.catalog->introduction.empty()) {
      const auto &paragraph = built.catalog->introduction.back();
      require(paragraph.links.size() == 1 &&
                  paragraph.links[0].target == "FIGC" &&
                  paragraph.text.substr(paragraph.links[0].begin,
                                        paragraph.links[0].end -
                                            paragraph.links[0].begin) ==
                      "Fig",
              "a record-closing selector did not mark the next record's "
              "row: [" +
                  paragraph.text + "]");
    }
  }

  // `SRFIG<id>` ... `SREFIG` in the introduction is a drawn region, as in an
  // entry (SC34-425 2.4.14.1 record 1535 `SRFIGRSRVWD`, 2.4.32 `FIGADAIN`):
  // its rows and caption are drawn lines, never introduction prose.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({origin(), t("Lead", 382), t("text.", 390)}));
    append(tokens, line({}));
    append(tokens, line({t("SRFIGFIGD", 391)}));
    append(tokens,
           line({t("cfont", 111), t("3", 112), t("5", 237), t("2", 113)}));
    append(tokens, line({origin(), t("Drawn", 238), t("figure", 239)}));
    append(tokens, line({origin(), t("Caption", 392), t("one.", 393)}));
    append(tokens, line({t("SREFIG", 241)}));
    append(tokens, line({origin(), t("After", 394), t("figure.", 242)}));
    append(tokens, line({}));
    append(tokens, entry("653", 395));
    const auto built = build({make_source(94, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "an introduction figure declined: " + built.error);
    if (built.catalog) {
      std::string paragraphs;
      for (const auto &paragraph : built.catalog->introduction)
        paragraphs += "[" + paragraph.text + "]";
      require(paragraphs == "[Lead text.][After figure.]",
              "the introduction figure leaked into its prose: " + paragraphs);
      require(built.catalog->introduction_regions.size() == 1 &&
                  built.catalog->introduction_regions.front().start.kind ==
                      geist::detail::TrapEmbeddedControlKindIR::figure_start &&
                  built.catalog->introduction_regions.front().lines.size() ==
                      2,
              "the introduction figure was not kept as a drawn region");
      std::string error;
      require(geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership,
                  *built.catalog, &error),
              "the introduction figure failed verification: " + error);
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          {"12", "Sample Messages"}, *built.catalog, &error);
      require(document.has_value(),
              "the introduction figure did not lower: " + error);
      if (document) {
        const auto markdown =
            geist::detail::render_document_markdown(*document);
        require(markdown.find("<a id=\"FIGFIGD\"></a>") != std::string::npos,
                "the introduction figure lost its anchor:\n" + markdown);
      }
    }
  }

  // Mutation: a figure closer with no figure open in the introduction.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({origin(), t("Lead", 382), t("text.", 390)}));
    append(tokens, line({t("SREFIG", 241)}));
    append(tokens, line({origin(), t("After", 394), t("figure.", 242)}));
    append(tokens, line({}));
    append(tokens, entry("654", 396));
    const auto built = build({make_source(95, tokens)}, "Sample Messages");
    require(!built.catalog.has_value(),
            "a stray introduction figure closer was admitted");
  }

  // A subject-index line inside an entry is index metadata (SC34-425
  // 2.4.11.1 record 1509 line 25, `SI translators, FLMLPGEN`, which hosted
  // serves as nothing). The entry keeps the term and prints none of it.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("644", 312));
    append(tokens, line({t("SI", 313), t("translators,", 314),
                         t("SAMPLE", 315)}));
    append(tokens, entry("647", 316));
    const auto built = build({make_source(75, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "an entry carrying a subject index line declined: " + built.error);
    if (built.catalog && built.catalog->entries.size() == 2) {
      const auto &first = built.catalog->entries.front();
      require(first.index_terms.size() == 1 &&
                  first.index_terms.front().term == "translators, SAMPLE",
              "the subject index term was not kept on its entry: [" +
                  (first.index_terms.empty() ? std::string() : first.index_terms.front().term) + "]");
      require(entry_text(first) == "644 Description: first field",
              "the subject index line ran into its entry: [" +
                  entry_text(first) + "]");
      std::string error;
      require(geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership,
                  *built.catalog, &error),
              "the indexed entry failed verification: " + error);
      geist::detail::TopicIdentityIR topic;
      const auto document = geist::detail::lower_trap_catalog_to_document_ir(
          topic, *built.catalog, &error);
      require(document.has_value(),
              "the indexed entry did not lower: " + error);
      if (document) {
        const auto markdown =
            geist::detail::render_document_markdown(*document);
        require(markdown.find("translators") == std::string::npos &&
                    markdown.find("SAMPLE") == std::string::npos,
                "the subject index term was printed:\n" + markdown);
      }
    }
  }

  // Mutation: a subject-index segment the flattened splitter extended over
  // a drawn row after the SI line. That row has no slot, so the entry
  // declines rather than drop or misplace it.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("648", 317));
    append(tokens, line({t("SI", 313), t("translators,", 314),
                         t("SAMPLE", 315)}));
    append(tokens, line({origin(), t("Trailing", 318), t("words.", 319)}));
    const auto built = build({make_source(79, tokens)}, "Sample Messages");
    require(!built.catalog.has_value(),
            "a subject index segment carrying drawn text was admitted");
    require(built.error.find("subject index") != std::string::npos,
            "the extended subject index line declined for the wrong "
            "reason: " +
                built.error);
  }

  // A headline CFONT that closes its record styles the next record's first
  // row even when that row's length byte spells `cfont`: N2AH1MST record 657
  // line 37 is `cfont 3 7 2 11 9 2 ...` and record 658 line 0 is the length
  // byte 37 (`25` at 0x492F1, the dictionary word `cfont`), a three-blank
  // run and `AVM007I SUBSYSTEM ssid ...`. Hosted 11.0 serves that row as
  // the entry headline.
  {
    auto first = header("Sample", "Messages");
    append(first, entry("655", 450));
    append(first, line({t("SRMSG", 110), t("656", 451)}));
    append(first,
           line({t("cfont", 111), t("3", 112), t("3", 112), t("2", 113)}));
    std::vector<Token> second;
    second.push_back({words("cfont"), 2});
    second.push_back(origin());
    second.push_back(t("656", 451));
    append(second,
           line({t("cfont", 111), t("3", 112), t("12", 114), t("2", 113)}));
    append(second, line({origin(), t("Description:", 115), t("first", 117),
                         t("field", 118)}));
    const auto built = build({make_source(96, first), make_source(97, second)},
                             "Sample Messages");
    require(built.catalog.has_value(),
            "a headline row opened by a cfont-spelled length byte declined: " +
                built.error);
    if (built.catalog && built.catalog->entries.size() == 2) {
      const auto &second_entry = built.catalog->entries.back();
      require(entry_text(second_entry) == "656 Description: first field",
              "the record-opening headline row was misread: [" +
                  entry_text(second_entry) + "]");
      std::string error;
      require(geist::detail::verify_trap_catalog_ir(
                  built.sources, built.layout, *built.ownership,
                  *built.catalog, &error),
              "the record-opening headline failed verification: " + error);
    }
  }

  // Mutation: the same record-opening row with no CFONT waiting for it is
  // text before the entry's headline and still declines.
  {
    auto first = header("Sample", "Messages");
    append(first, entry("657", 452));
    append(first, line({t("SRMSG", 110), t("658", 453)}));
    std::vector<Token> second;
    second.push_back({words("cfont"), 2});
    second.push_back(origin());
    second.push_back(t("658", 453));
    append(second,
           line({t("cfont", 111), t("3", 112), t("12", 114), t("2", 113)}));
    append(second, line({origin(), t("Description:", 115), t("first", 117),
                         t("field", 118)}));
    const auto built = build({make_source(98, first), make_source(99, second)},
                             "Sample Messages");
    require(!built.catalog.has_value(),
            "an unstyled record-opening row was admitted as a headline");
  }

  // A field that restates the label of the field directly before it is a
  // second field under that label (N2AH1MST AVM035I, record 669 lines 29
  // and 41, both `System Action:`; hosted 11.0 serves both as labels). The
  // entry still repeats the catalogue's vocabulary.
  {
    const auto action_row = [](const std::string &word, std::uint16_t value) {
      std::vector<Token> tokens;
      append(tokens, line({t("cfont", 111), t("3", 112), t("7", 454),
                           t("2", 113)}));
      append(tokens, line({origin(), t("Action:", 455), t(word, value)}));
      return tokens;
    };
    auto tokens = header("Sample", "Messages");
    append(tokens, entry("659", 456));
    append(tokens, action_row("one.", 457));
    append(tokens, entry("660", 458));
    append(tokens, action_row("two.", 459));
    append(tokens, action_row("three.", 460));
    const auto built = build({make_source(100, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "a field restating the label before it declined: " + built.error);
    if (built.catalog && built.catalog->entries.size() == 2) {
      const auto &second_entry = built.catalog->entries.back();
      require(second_entry.fields.size() == 3 &&
                  second_entry.fields[1].label_text == "Action:" &&
                  second_entry.fields[2].label_text == "Action:" &&
                  second_entry.fields[2].in_vocabulary,
              "the restated label was not kept as its own field");
    }

    // Mutation: the label returns after another field in between. That is
    // not a restatement, and the entry no longer repeats the vocabulary.
    auto returning = header("Sample", "Messages");
    append(returning, entry("661", 461));
    append(returning, action_row("one.", 457));
    append(returning, entry("662", 462));
    append(returning, action_row("two.", 459));
    append(returning,
           line({t("cfont", 111), t("3", 112), t("12", 114), t("2", 113)}));
    append(returning, line({origin(), t("Description:", 115), t("again", 463)}));
    append(returning, action_row("three.", 460));
    const auto rejected =
        build({make_source(101, returning)}, "Sample Messages");
    require(!rejected.catalog.has_value(),
            "a label returning after another field was admitted");
  }

  // A headline that ends in `:` is still the headline when it opens with
  // the entry's own id, even before any label has fixed the catalogue's
  // label column: N2AH1MST record 757 line 13 is the first entry's
  // `   CBP100I VALIDATE PROCESSING ASSUMED.  INVALID INPUT PARAMETER STRING:`
  // under `cfont 3 7 2 ... 65 7 2`, and hosted 15.0 serves it as the
  // headline of `CBP100I`.
  {
    auto tokens = header("Sample", "Messages");
    append(tokens, line({t("SRMSG", 110), t("663", 464)}));
    append(tokens, line({t("cfont", 111), t("3", 112), t("3", 112),
                         t("2", 113), t("7", 454), t("7", 454), t("2", 113)}));
    append(tokens, line({origin(), t("663", 464), t("STRING:", 466)}));
    append(tokens,
           line({t("cfont", 111), t("3", 112), t("12", 114), t("2", 113)}));
    append(tokens, line({origin(), t("Description:", 115), t("first", 117),
                         t("field", 118)}));
    append(tokens, entry("664", 467));
    const auto built = build({make_source(102, tokens)}, "Sample Messages");
    require(built.catalog.has_value(),
            "a colon-ending headline that opens with its id declined: " +
                built.error);
    if (built.catalog && built.catalog->entries.size() == 2)
      require(built.catalog->entries.front().headline.body.text ==
                      "663 STRING:" &&
                  built.catalog->entries.front().fields.size() == 1,
              "the colon-ending headline was misread: [" +
                  built.catalog->entries.front().headline.body.text + "]");

    // Mutation: the entry's first row is a label that does not open with
    // the id. That is a field before any headline, and it still declines.
    auto labelled = header("Sample", "Messages");
    append(labelled, line({t("SRMSG", 110), t("665", 468)}));
    append(labelled,
           line({t("cfont", 111), t("3", 112), t("12", 114), t("2", 113)}));
    append(labelled, line({origin(), t("Description:", 115), t("first", 117),
                           t("field", 118)}));
    append(labelled, entry("666", 469));
    const auto rejected =
        build({make_source(103, labelled)}, "Sample Messages");
    require(!rejected.catalog.has_value(),
            "an entry opening on a field label was admitted");
  }

  std::cout << "trap catalog IR synthetic tests passed\n";
  return 0;
}
