// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/message_introduction_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace geist::detail;
TokenWords words(const std::string &value) {
  return {value.begin(), value.end()};
}
void require(bool value, const std::string &message) {
  if (!value) {
    std::cerr << message << '\n';
    geist_test::record_failure();
  }
}
void line(DecodedLogicalRecordSource &record, std::vector<TokenWords> tokens) {
  record.encoded_tokens.push_back(
      {static_cast<std::uint16_t>(tokens.size()), 1});
  record.tokens.push_back(words("<"));
  for (auto &token : tokens) {
    record.encoded_tokens.push_back({0x80, 1});
    record.tokens.push_back(std::move(token));
  }
}
void refresh(DecodedLogicalRecordSource &record) {
  record.ir.logical_record = record.logical_record;
  std::uint32_t byte = record.logical_record * 100;
  const auto begin = byte;
  for (std::size_t at = 0; at < record.tokens.size(); ++at) {
    const auto &token = record.tokens[at];
    const auto spacing = !token.empty() && token.front() < 4;
    record.ir.tokens.push_back({at,
                                record.encoded_tokens[at],
                                token,
                                {byte, byte + 1},
                                spacing,
                                spacing ? token.front() : std::uint16_t{3}});
    ++byte;
  }
  record.ir.payload_range = {begin, byte};
  assign_display_line_framing(record.ir);
  record.assembled = assemble_logical_record_with_sources(record.tokens);
  record.control_segments =
      decode_control_segments(record.logical_record, record.assembled,
                              record.encoded_tokens, record.ir.display_lines);
  demote_display_line_owned_controls(record);
}
MessageTopicIR topic_rows(const LayoutIR &layout,
                          const OwnershipIR &ownership) {
  MessageTopicIR topic;
  for (const auto &run : layout.runs)
    for (std::size_t row = 0; row < run.rows.size(); ++row) {
      const auto &physical = run.rows[row];
      MessageTopicRowIR item;
      item.marker = physical.marker;
      item.source_row = {run.id, row};
      item.native_origin = physical.native_origin;
      item.visible_text = physical.visible_text;
      for (const auto &cell : ownership.cells)
        if (cell.run == run.id && cell.row_index == row)
          item.cells.push_back({cell.logical_record, cell.token_index,
                                cell.word_index, cell.word, cell.disposition});
      topic.introduction_row_indices.push_back(topic.rows.size());
      topic.rows.push_back(std::move(item));
    }
  return topic;
}
std::string text(const MessageIntroductionIR &intro) {
  std::string result;
  for (const auto &paragraph : intro.paragraphs) {
    for (const auto &atom : paragraph.atoms)
      result += atom.text;
    result += '\n';
  }
  return result;
}
void geometry_independent() {
  DecodedLogicalRecordSource record;
  record.logical_record = 10;
  line(record, {words("   "), words("First"), words("slot"), words("       "),
                words("second")});
  refresh(record);
  const std::vector<DecodedLogicalRecordSource> records{record};
  const auto original = extract_layout_ir(records);
  require(original.runs.size() == 1 && original.runs[0].rows.size() == 2,
          "compact synthetic introduction did not split");
  auto whole = original;
  whole.runs[0].rows[0].token_end = whole.runs[0].rows.back().token_end;
  whole.runs[0].rows.resize(1);
  for (const auto &layout : {original, whole}) {
    const auto ownership = build_verified_ownership_ir(records, layout);
    require(bool(ownership), "synthetic introduction ownership rejected");
    if (!ownership)
      continue;
    const auto selectors =
        extract_selector_catalog_ir(records).value_or(SelectorCatalogIR{});
    const auto topic = topic_rows(layout, *ownership);
    std::string error;
    const auto intro = extract_message_introduction_ir(
        records, layout, *ownership, selectors, topic, &error);
    require(intro && text(*intro) == "First slot second\n",
            "introduction depended on physical fragment count: " + error);
  }
}
void paragraphs_and_links(std::size_t count, bool whitespace_blank = false) {
  DecodedLogicalRecordSource opening;
  opening.logical_record = 20;
  for (std::size_t at = 0; at < count; ++at) {
    if (at != 0)
      line(opening, whitespace_blank
                        ? std::vector<TokenWords>{words("  "), words(" ")}
                        : std::vector<TokenWords>{});
    if (at != 1)
      line(opening, {words("cselect 3 5 TARGET" + std::to_string(at))});
    line(opening, {words("   "), words("Alpha"), words("suffix")});
  }
  refresh(opening);
  const std::vector<DecodedLogicalRecordSource> records{opening};
  const auto layout = extract_layout_ir(records);
  const auto ownership = build_verified_ownership_ir(records, layout);
  const auto selectors = extract_selector_catalog_ir(records);
  require(bool(ownership) && bool(selectors),
          "paragraph synthetic pipeline failed");
  if (!ownership || !selectors)
    return;
  const auto topic = topic_rows(layout, *ownership);
  std::string error;
  const auto intro = extract_message_introduction_ir(
      records, layout, *ownership, *selectors, topic, &error);
  require(intro && intro->paragraphs.size() == count,
          "native paragraph count rejected: " + error);
  if (!intro)
    return;
  std::size_t links = 0;
  for (const auto &paragraph : intro->paragraphs)
    for (const auto &atom : paragraph.atoms)
      if (atom.kind == MessageIntroductionAtomKindIR::selector) {
        ++links;
        require(atom.text == "Alpha" && atom.target &&
                    !atom.cell_indices.empty(),
                "selector label/claims changed");
      }
  require(links == count - (count > 1 ? 1 : 0),
          "arbitrary selector count changed");
  auto missing = topic;
  for (auto &row : missing.rows)
    if (!row.cells.empty()) {
      const auto at =
          std::find_if(row.cells.begin(), row.cells.end(),
                       [](const auto &cell) { return cell.word == 'A'; });
      if (at != row.cells.end()) {
        row.cells.erase(at);
        break;
      }
    }
  require(!extract_message_introduction_ir(records, layout, *ownership,
                                           *selectors, missing, &error),
          "missing source claim was accepted");
  auto forged = topic;
  bool changed = false;
  for (auto &row : forged.rows)
    for (auto &cell : row.cells)
      if (!changed && cell.word == 'A') {
        cell.disposition = SourceDisposition::layout_padding;
        changed = true;
      }
  require(changed &&
              !extract_message_introduction_ir(records, layout, *ownership,
                                               *selectors, forged, &error),
          "forged source disposition was accepted");
  forged = topic;
  if (!forged.rows.empty())
    forged.rows[0].source_row.display_run += 999;
  require(!extract_message_introduction_ir(records, layout, *ownership,
                                           *selectors, forged, &error),
          "forged source row provenance was accepted");
  if (count >= 3) {
    auto missing_line = topic;
    std::size_t body_lines = 0;
    for (auto at = missing_line.introduction_row_indices.begin();
         at != missing_line.introduction_row_indices.end(); ++at)
      if (std::any_of(missing_line.rows[*at].cells.begin(),
                      missing_line.rows[*at].cells.end(),
                      [](const auto &cell) { return cell.word == 'A'; }) &&
          ++body_lines == 2) {
        missing_line.introduction_row_indices.erase(at);
        break;
      }
    require(body_lines == 2, "middle native body line was not found");
    require(!extract_message_introduction_ir(records, layout, *ownership,
                                             *selectors, missing_line, &error),
            "an omitted complete native body line was accepted");
  }
  auto undecided = records;
  undecided[0].ir.display_lines_parse = false;
  require(!extract_message_introduction_ir(undecided, layout, *ownership,
                                           *selectors, topic, &error),
          "undecided native framing was guessed");
}

void cross_record_and_unsupported_semantics() {
  DecodedLogicalRecordSource first;
  first.logical_record = 30;
  line(first, {words("   "), words("Alpha")});
  refresh(first);
  DecodedLogicalRecordSource second;
  second.logical_record = 31;
  line(second, {words("   "), words("beta")});
  refresh(second);
  const std::vector<DecodedLogicalRecordSource> records{first, second};
  const auto layout = extract_layout_ir(records);
  const auto ownership = build_verified_ownership_ir(records, layout);
  require(bool(ownership), "cross-record introduction ownership failed");
  if (!ownership)
    return;
  std::string error;
  const auto intro = extract_message_introduction_ir(
      records, layout, *ownership, SelectorCatalogIR{},
      topic_rows(layout, *ownership), &error);
  require(intro && text(*intro) == "Alpha beta\n",
          "cross-record paragraph joining changed: " + error);

  DecodedLogicalRecordSource styled;
  styled.logical_record = 40;
  line(styled, {words("cfont 3 5 1")});
  line(styled, {words("   "), words("Alpha")});
  refresh(styled);
  const std::vector<DecodedLogicalRecordSource> styles{styled};
  const auto styled_layout = extract_layout_ir(styles);
  const auto styled_ownership =
      build_verified_ownership_ir(styles, styled_layout);
  require(bool(styled_ownership), "styled introduction ownership failed");
  if (!styled_ownership)
    return;
  require(!extract_message_introduction_ir(
              styles, styled_layout, *styled_ownership, SelectorCatalogIR{},
              topic_rows(styled_layout, *styled_ownership), &error) &&
              error.find("font semantics") != std::string::npos,
          "unsupported source font semantics were silently discarded");
}
} // namespace
int main() {
  geometry_independent();
  paragraphs_and_links(1);
  paragraphs_and_links(3);
  paragraphs_and_links(3, true);
  paragraphs_and_links(6);
  cross_record_and_unsupported_semantics();
  return geist_test::failure_count() == 0 ? 0 : 1;
}
