// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/prose/prose_topic_internal.hpp"

#include <algorithm>
#include <optional>
#include <set>

namespace geist::detail::prose_internal {
namespace {

struct NativeRow {
  std::size_t record = 0;
  const DisplayLineIR *line = nullptr;
};

// With `padding`, a row whose native display text is the line's text after
// leading blanks the line holds no cell for also matches; `*padding` is the
// number of those blanks.
std::optional<NativeRow>
native_row(const std::vector<DecodedLogicalRecordSource> &records,
           const Line &line, std::size_t *padding = nullptr) {
  if (padding != nullptr)
    *padding = 0;
  std::optional<NativeRow> native;
  for (const auto &cell : line.cells) {
    if (cell.record == npos)
      continue;
    if (cell.record >= records.size())
      return std::nullopt;
    const auto *row = display_line_of_token(records[cell.record], cell.token);
    if (row == nullptr || cell.token == row->prefix_token)
      return std::nullopt;
    if (native && (native->record != cell.record || native->line != row))
      return std::nullopt;
    native = NativeRow{cell.record, row};
  }
  // The native row is authoritative, not a layout fragment or an accidentally
  // merged paragraph. Include its complete origin, padding and trailing cells.
  if (!native)
    return std::nullopt;
  const auto display = display_line_text(records[native->record], *native->line);
  const auto text = line_text(line);
  if (display == text)
    return native;
  if (padding == nullptr || display.size() <= text.size() ||
      display.compare(display.size() - text.size(), text.size(), text) != 0 ||
      display.find_first_not_of(' ') < display.size() - text.size())
    return std::nullopt;
  *padding = display.size() - text.size();
  return native;
}

bool styled_example(const std::vector<DecodedLogicalRecordSource> &records,
                    const Line &line) {
  if (line.box || line.bullet || !native_row(records, line) ||
      std::any_of(line.links.begin(), line.links.end(),
                  [](const auto &link) { return link.picture; }))
    return false;
  const auto drawn_box_cell = [&](const Cell &cell) {
    if (cell.record == npos || cell.record >= records.size() ||
        cell.token >= records[cell.record].tokens.size())
      return false;
    const auto view = view_token(records, cell.record, cell.token);
    return !view.body.empty() &&
           std::all_of(view.body.begin(), view.body.end(), box_word);
  };
  bool example = false;
  auto first_text = line.text_begin;
  while (first_text < line.cells.size() && line.cells[first_text].space)
    ++first_text;
  const auto artwork =
      first_text < line.cells.size() && drawn_box_cell(line.cells[first_text]);
  std::vector<bool> covered(line.cells.size(), false);
  for (const auto &span : line.fonts) {
    if (span.style == FontStyleIR::unknown ||
        span.style == FontStyleIR::underscore || span.begin >= span.end ||
        span.begin >= line.cells.size())
      return false;
    auto first = span.begin;
    auto last = std::min(span.end, line.cells.size());
    while (first < last && line.cells[first].space)
      ++first;
    while (last > first && line.cells[last - 1].space)
      --last;
    if (first == last)
      return false;
    // A flat cross-reference cannot retain an overlapping emphasis font.
    // Example fonts remain representable through the surrounding monospace
    // region, but do not promise lossless styling for other link decoration.
    if (!font_style_is_example(span.style))
      for (const auto &link : line.links)
        for (auto cell = first; cell < last; ++cell)
          if (!line.cells[cell].space && cell >= link.begin && cell < link.end)
            return false;
    // APL cells are code-page-selected program/display symbols.  In the
    // witnessed PL/I message examples they form the positioned diagnostic
    // arrow row between two XMP rows; retaining that native row is semantic,
    // because reflowing it moves the arrow away from the offending column.
    example = example || font_style_is_example(span.style) ||
              span.style == FontStyleIR::apl_symbol;
    for (auto cell = first; cell < last; ++cell)
      covered[cell] = true;
  }
  if (!example && !artwork)
    return false;
  // Syntax-diagram rows commonly begin with a box run and style only some or
  // none of their labels. The native framing makes the complete row
  // authoritative, so keep all of its cells rather than reflowing away their
  // source columns. An ordinary styled example retains the narrower rule:
  // only drawn box cells may lie outside its example spans.
  for (auto cell = line.text_begin; cell < line.cells.size(); ++cell)
    if (!artwork && !line.cells[cell].space && !covered[cell] &&
        !drawn_box_cell(line.cells[cell]))
      return false;
  return true;
}

// Native blank rows between two example rows belong to the same literal
// region. Font/index instructions between them draw nothing; neither those
// instructions nor guessed paragraph spacing manufacture blank rows.
bool hidden_instruction(const DecodedLogicalRecordSource &record,
                        const DisplayLineIR &line) {
  if (is_subject_index_display_line(record, line))
    return true;
  for (const auto &segment : record.control_segments) {
    if (segment.display_text || segment.malformed ||
        (segment.kind != BookControlKind::font &&
         segment.kind != BookControlKind::select))
      continue;
    const auto tokens = operand_tokens(record, segment);
    if (tokens.empty() || tokens.front() != line.prefix_token + 1)
      continue;
    bool complete = true;
    for (auto token = line.prefix_token + 1; token < line.token_end; ++token)
      if (!std::binary_search(tokens.begin(), tokens.end(), token))
        complete = false;
    if (complete)
      return true;
  }
  return false;
}

// With `ledger`, a display line none of whose content tokens is display text
// -- a directive, an envelope or index line -- also draws nothing.
std::optional<std::vector<NativeRow>>
intervening_blank_rows(const std::vector<DecodedLogicalRecordSource> &records,
                       const NativeRow &previous, const NativeRow &next,
                       Ledger *ledger = nullptr) {
  const auto draws_nothing = [&](std::size_t record,
                                 const DisplayLineIR &line) {
    if (hidden_instruction(records[record], line))
      return true;
    if (ledger == nullptr)
      return false;
    for (auto token = line.prefix_token + 1; token < line.token_end; ++token)
      if (ledger->at(record, token).role == ProseTokenRoleIR::text)
        return false;
    return true;
  };
  if (previous.record > next.record ||
      (previous.record == next.record &&
       previous.line->prefix_token >= next.line->prefix_token))
    return std::nullopt;
  std::vector<NativeRow> result;
  for (auto record = previous.record; record <= next.record; ++record) {
    const auto *lines = record_display_lines(records[record]);
    if (lines == nullptr ||
        (record > previous.record && records[record - 1].logical_record + 1 !=
                                         records[record].logical_record))
      return std::nullopt;
    for (const auto &line : *lines) {
      if (record == previous.record &&
          line.prefix_token <= previous.line->prefix_token)
        continue;
      if (record == next.record && line.prefix_token >= next.line->prefix_token)
        continue;
      const auto text = display_line_text(records[record], line);
      if (std::all_of(text.begin(), text.end(),
                      [](const auto ch) { return ch == ' ' || ch == '\t'; }))
        result.push_back({record, &line});
      else if (!draws_nothing(record, line))
        return std::nullopt;
    }
  }
  return result;
}

} // namespace

std::size_t
styled_example_range_end(const std::vector<DecodedLogicalRecordSource> &records,
                         const std::vector<Line> &lines, std::size_t begin,
                         std::size_t limit, Ledger &) {
  if (begin >= limit || !styled_example(records, lines[begin]))
    return begin;
  auto end = begin + 1;
  auto previous = *native_row(records, lines[begin]);
  while (end < limit && !lines[end].anchor_before &&
         styled_example(records, lines[end])) {
    const auto next = *native_row(records, lines[end]);
    if (!intervening_blank_rows(records, previous, next))
      break;
    previous = next;
    ++end;
  }
  return end;
}

bool build_styled_example_block(
    const std::vector<DecodedLogicalRecordSource> &records,
    const std::vector<Line> &lines, std::size_t begin, std::size_t end,
    ProseBlockIR &block, Ledger &ledger, std::size_t block_index, bool &matched,
    std::string *error) {
  matched = false;
  if (begin >= end || end > lines.size() ||
      styled_example_range_end(records, lines, begin, end, ledger) != end)
    return true;
  matched = true;
  block.kind = ProseBlockKindIR::preformatted;
  block.verbatim_kind = "prose-native-styled-example";
  const auto append_row = [&](const NativeRow &native, std::string text) {
    block.preformatted_lines.push_back(std::move(text));
    block.preformatted_line_inline_ends.push_back(block.inlines.size());
    block.preformatted_line_sources.push_back(
        token_slice(records[native.record], native.line->prefix_token,
                    native.line->token_end));
  };
  std::optional<NativeRow> previous;
  for (auto row = begin; row < end; ++row) {
    const auto native = *native_row(records, lines[row]);
    if (previous) {
      const auto blanks = *intervening_blank_rows(records, *previous, native);
      for (const auto &blank_row : blanks) {
        auto text =
            display_line_text(records[blank_row.record], *blank_row.line);
        if (!text.empty()) {
          ProseInlineIR blank;
          blank.text = text;
          block.inlines.push_back(std::move(blank));
        }
        append_row(blank_row, std::move(text));
      }
    }
    auto line = lines[row];
    line.text_begin = 0;
    const auto inline_begin = block.inlines.size();
    if (!build_block(records, {line}, 0, 1, block, ledger, block_index, error,
                     true))
      return false;
    std::string display_text;
    for (auto item = inline_begin; item < block.inlines.size(); ++item)
      display_text += block.inlines[item].text;
    append_row(native, std::move(display_text));
    previous = native;
  }
  std::set<std::pair<std::size_t, std::size_t>> refs;
  for (auto row = begin; row < end; ++row)
    for (const auto &cell : lines[row].cells)
      if (cell.record != npos &&
          ledger.at(cell.record, cell.token).role == ProseTokenRoleIR::text)
        refs.emplace(cell.record, cell.token);
  const std::vector<std::pair<std::size_t, std::size_t>> all_refs(refs.begin(),
                                                                  refs.end());
  block.slices = slices_for(records, all_refs);
  return true;
}

bool build_native_verbatim_block(
    const std::vector<DecodedLogicalRecordSource> &records,
    const std::vector<Line> &lines, std::size_t begin, std::size_t end,
    ProseBlockIR &block, Ledger &ledger, std::size_t block_index, bool &matched,
    std::vector<std::pair<std::size_t, std::size_t>> &line_rows) {
  matched = false;
  line_rows.clear();
  // Every text row must be one whole native display row, and only empty
  // rows or rows that draw nothing may stand between two of them.
  std::vector<std::size_t> rows;
  std::vector<NativeRow> natives;
  std::vector<std::size_t> paddings;
  for (auto row = begin; row < end && row < lines.size(); ++row) {
    const auto &line = lines[row];
    if (line.cells.size() <= line.text_begin)
      continue;
    std::size_t pad = 0;
    const auto native = native_row(records, line, &pad);
    if (!native)
      return true;
    if (!natives.empty() &&
        !intervening_blank_rows(records, natives.back(), *native, &ledger))
      return true;
    rows.push_back(row);
    natives.push_back(*native);
    paddings.push_back(pad);
  }
  if (rows.empty())
    return true;
  const auto saved = ledger.entries;
  ProseBlockIR built;
  built.kind = ProseBlockKindIR::preformatted;
  const auto append_row = [&](const NativeRow &native, std::string text) {
    built.preformatted_lines.push_back(std::move(text));
    built.preformatted_line_inline_ends.push_back(built.inlines.size());
    built.preformatted_line_sources.push_back(
        token_slice(records[native.record], native.line->prefix_token,
                    native.line->token_end));
  };
  std::string ignored;
  for (std::size_t at = 0; at < rows.size(); ++at) {
    std::vector<NativeRow> blanks;
    if (at > 0)
      blanks = *intervening_blank_rows(records, natives[at - 1], natives[at],
                                        &ledger);
    for (const auto &blank_row : blanks) {
        auto text =
            display_line_text(records[blank_row.record], *blank_row.line);
        if (!text.empty()) {
          ProseInlineIR blank;
        blank.text = text;
        built.inlines.push_back(std::move(blank));
      }
      append_row(blank_row, std::move(text));
    }
    auto line = lines[rows[at]];
    // A row whose leading blanks the line builder read as its origin keeps
    // them: the reader prints the row from its first column, and the
    // operands' columns count from there too.
    line.cells.insert(line.cells.begin(), paddings[at],
                      Cell{npos, 0, " ", true});
    line.text_begin = 0;
    const auto inline_begin = built.inlines.size();
    if (!build_block(records, {line}, 0, 1, built, ledger, block_index,
                     &ignored, true)) {
      // The row's controls do not partition it: leave the ledger as it was
      // and let the caller keep its plain reading.
      ledger.entries = saved;
      line_rows.clear();
      return true;
    }
    std::string display_text;
    for (auto item = inline_begin; item < built.inlines.size(); ++item)
      display_text += built.inlines[item].text;
    line_rows.emplace_back(rows[at], built.preformatted_lines.size());
    append_row(natives[at], std::move(display_text));
  }
  std::set<std::pair<std::size_t, std::size_t>> refs;
  for (const auto row : rows)
    for (const auto &cell : lines[row].cells)
      if (cell.record != npos &&
          ledger.at(cell.record, cell.token).role == ProseTokenRoleIR::text)
        refs.emplace(cell.record, cell.token);
  const std::vector<std::pair<std::size_t, std::size_t>> all_refs(refs.begin(),
                                                                  refs.end());
  built.slices = slices_for(records, all_refs);
  built.origin = lines[rows.front()].origin;
  block = std::move(built);
  matched = true;
  return true;
}

} // namespace geist::detail::prose_internal
