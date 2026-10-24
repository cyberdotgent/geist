// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/glossary_catalog_ir.hpp"
#include "test_failures.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/layout_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    geist_test::record_failure();
    return;
  }
}


std::optional<geist::detail::GlossaryCatalogIR>
extract(const geist::detail::LogicalDecodeContext &context, std::uint32_t first,
        std::uint32_t end, std::string *error = nullptr) {
  const auto sources =
      geist::detail::decode_logical_record_sources(context, first, end);
  const auto layout = geist::detail::extract_layout_ir(sources);
  std::string inner;
  const auto ownership =
      geist::detail::build_verified_ownership_ir(sources, layout, &inner);
  if (!ownership) {
    if (error != nullptr)
      *error = "source layout/ownership is not canonical: " + inner;
    return std::nullopt;
  }
  return geist::detail::extract_glossary_catalog_ir(sources, layout, *ownership,
                                                    error);
}

using geist::detail::TokenWords;

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

struct Token {
  TokenWords words;
  std::uint16_t encoded_value = 0;
};

// One display line: a length byte (spelled `spelling`) whose encoded value
// is the line's byte count -- every token here is one byte wide -- and the
// line's content tokens.
void line(std::vector<Token> &into, const std::vector<std::string> &content,
          const std::string &spelling = " ") {
  into.push_back({words(spelling),
                  static_cast<std::uint16_t>(content.size())});
  std::uint16_t value = 0x40;
  for (const auto &text : content)
    into.push_back({text == "\x01." ? TokenWords{1, '.'} : words(text),
                    value++});
}

geist::detail::DecodedLogicalRecordSource
make_source(std::uint32_t logical_record, const std::vector<Token> &tokens) {
  geist::detail::DecodedLogicalRecordSource source;
  source.logical_record = logical_record;
  source.ir.logical_record = logical_record;
  auto byte = static_cast<std::uint32_t>(logical_record * 1000);
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    const geist::detail::EncodedLogicalToken encoded{
        tokens[index].encoded_value, 1};
    const auto spacing = !tokens[index].words.empty() &&
                         tokens[index].words.front() < 4;
    source.tokens.push_back(tokens[index].words);
    source.encoded_tokens.push_back(encoded);
    source.ir.tokens.push_back(
        {index, encoded, tokens[index].words, {byte, byte + 1}, spacing,
         spacing ? tokens[index].words.front() : std::uint16_t{3}});
    byte += 1;
  }
  source.ir.payload_range = {
      static_cast<std::uint32_t>(logical_record * 1000), byte};
  geist::detail::assign_display_line_framing(source.ir);
  source.assembled =
      geist::detail::assemble_logical_record_with_sources(source.tokens);
  source.control_segments = geist::detail::decode_control_segments(
      source.logical_record, source.assembled, source.encoded_tokens,
      source.ir.display_lines);
  geist::detail::demote_display_line_owned_controls(source);
  return source;
}

} // namespace

int main() {
  // A `cz BREAK` inside a definition draws the lines after it: S20H-1642-01
  // GLOSSARY record 758 lines 29-32 are `   field.  (1) An area in a record
  // ...`, `cz BREAK 3`, `   (2) In the IBM 5250 data stream, ...` and
  // `   presentation space having ...`; hosted (DT 19950803160504) serves
  // `<br>` and both lines. Those lines are the payload of the `cz` segment,
  // which the row model makes no row for, so a catalog that keeps the
  // definition without them drops their words; it must decline instead.
  {
    std::vector<Token> tokens;
    line(tokens, {"chdlevel", ":GLOSSARY"});
    line(tokens, {"ST", "  ", "Glossary"});
    line(tokens, {"SRGLS", "field"});
    line(tokens, {"cz", "FLOW", "GD", "3", "3"});
    line(tokens, {"cfont", "3", "5", "2"});
    line(tokens, {"   ", "field", "\x01.", " ", "(1)", "An", "area."},
         "          ");
    line(tokens, {"cz", "BREAK", "3"});
    line(tokens, {"   ", "(2)", "In", "the", "stream."});
    line(tokens, {"   ", "presentation", "space."});
    line(tokens, {"SRGLS", "fixed"});
    line(tokens, {"cfont", "3", "5", "2"});
    line(tokens, {"   ", "fixed", "\x01.", " ", "Not", "moving."},
         "          ");
    const std::vector<geist::detail::DecodedLogicalRecordSource> sources{
        make_source(758, tokens)};
    const auto layout = geist::detail::extract_layout_ir(sources);
    std::string error;
    const auto ownership =
        geist::detail::build_verified_ownership_ir(sources, layout, &error);
    require(ownership.has_value(), "break glossary ownership was rejected");
    if (ownership) {
      const auto catalog = geist::detail::extract_glossary_catalog_ir(
          sources, layout, *ownership, &error);
      bool kept = false;
      if (catalog)
        for (const auto &entry : catalog->entries)
          if (entry.definition.prose.find("stream") != std::string::npos)
            kept = true;
      require(!catalog || kept,
              "a glossary kept a definition without the lines its cz BREAK "
              "draws");
    }
  }
  {
    geist::detail::DecodedLogicalRecordSource record;
    record.logical_record = 9;
    record.tokens = {{'x'},
                     {' ', ' ', ' '},
                     {3, 'W', 'h', 'a', 't', '?'},
                     {'?', '?', '?'},
                     {3, 'N', 'e', 'x', 't'}};
    record.assembled =
        geist::detail::assemble_logical_record_with_sources(record.tokens);
    geist::detail::PhysicalRowIR row;
    row.logical_record = 9;
    row.token_begin = 0;
    row.token_end = record.tokens.size();
    std::vector<geist::detail::GlossaryCatalogCellIR> cells;
    const auto add_token = [&](std::size_t token,
                               geist::detail::SourceDisposition disposition) {
      const auto first = record.tokens[token].front() < 4 ? 1u : 0u;
      for (auto word = first; word < record.tokens[token].size(); ++word)
        cells.push_back(
            {9, token, word, record.tokens[token][word], disposition, 1, 0});
    };
    add_token(0, geist::detail::SourceDisposition::marker_slot);
    add_token(1, geist::detail::SourceDisposition::layout_origin);
    add_token(2, geist::detail::SourceDisposition::visible_content);
    add_token(3, geist::detail::SourceDisposition::layout_padding);
    add_token(4, geist::detail::SourceDisposition::visible_content);
    std::string projection_error;
    const auto projected = geist::detail::project_glossary_semantic_row_text(
        record, row, cells, &projection_error);
    require(projected && *projected == "What? Next",
            "exact row projection removed a lexical question mark or retained "
            "question padding");

    auto relabeled = cells;
    for (auto &cell : relabeled)
      if (cell.token_index == 3)
        cell.disposition = geist::detail::SourceDisposition::visible_content;
    const auto padding_made_visible =
        geist::detail::project_glossary_semantic_row_text(record, row,
                                                          relabeled);
    require(padding_made_visible &&
                padding_made_visible->find("???") != std::string::npos,
            "row projection ignored exact padding-token disposition");

    relabeled = cells;
    const auto lexical_question =
        std::find_if(relabeled.begin(), relabeled.end(), [](const auto &cell) {
          return cell.token_index == 2 && cell.word == '?';
        });
    require(lexical_question != relabeled.end(),
            "synthetic lexical question cell is absent");
    lexical_question->disposition =
        geist::detail::SourceDisposition::layout_padding;
    require(!geist::detail::project_glossary_semantic_row_text(record, row,
                                                               relabeled),
            "row projection admitted mixed token dispositions");
  }

  std::cout << "glossary catalog IR synthetic tests passed\n";
  return 0;
}
