// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/message_introduction_ir.hpp"

#include "geist/detail/ir/selector_display_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"

#include <algorithm>
#include <map>
#include <tuple>

namespace geist::detail {
namespace {
using Key = std::tuple<std::uint32_t, std::size_t, std::size_t>;
using Position = std::pair<std::uint32_t, std::size_t>;
constexpr auto absent = static_cast<std::size_t>(-1);
struct Glyph {
  std::uint16_t word = 0;
  std::size_t cell = absent;
  std::size_t link = absent;
};
bool space(std::uint16_t word) {
  return word == ' ' || word == '\t' || word == '\r' || word == '\n';
}
} // namespace

std::optional<MessageIntroductionIR> extract_message_introduction_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const VerifiedOwnershipIR &ownership,
    const SelectorCatalogIR &selectors, const MessageTopicIR &topic,
    std::string *error) {
  const auto reject =
      [&](const std::string &message) -> std::optional<MessageIntroductionIR> {
    if (error)
      *error = message;
    return std::nullopt;
  };
  if (!ownership_verified_for(ownership, records, layout))
    return reject("message introduction ownership does not match its source");

  MessageIntroductionIR result;
  std::map<Key, const OwnedSourceCellIR *> source_ownership;
  const OwnershipIR &source_ledger = ownership;
  for (const auto &cell : source_ledger.cells)
    source_ownership.emplace(
        Key{cell.logical_record, cell.token_index, cell.word_index}, &cell);
  std::map<Key, std::size_t> cells;
  std::optional<Position> first;
  std::optional<Position> last;
  for (std::size_t local = 0; local < topic.introduction_row_indices.size();
       ++local) {
    const auto index = topic.introduction_row_indices[local];
    if (index >= topic.rows.size())
      return reject("message introduction row index is outside its ledger");
    for (const auto &run : layout.runs)
      if (run.id == topic.rows[index].source_row.display_run &&
          run.control_kind == BookControlKind::font && !run.rows.empty()) {
        const auto record =
            std::find_if(records.begin(), records.end(), [&](const auto &item) {
              return item.logical_record == run.rows.front().logical_record;
            });
        if (record != records.end() &&
            run.rows.front().segment_index < record->control_segments.size() &&
            !is_display_line_length_opcode(
                *record,
                record->control_segments[run.rows.front().segment_index]))
          return reject("message introduction font semantics are unsupported");
      }
    for (const auto &source : topic.rows[index].cells) {
      const auto canonical = source_ownership.find(
          {source.logical_record, source.token_index, source.word_index});
      if (canonical == source_ownership.end() ||
          canonical->second->disposition != source.disposition ||
          canonical->second->run != topic.rows[index].source_row.display_run ||
          canonical->second->row_index !=
              topic.rows[index].source_row.row_index)
        return reject(
            "message introduction cell differs from its ownership ledger");
      const auto record =
          std::find_if(records.begin(), records.end(), [&](const auto &item) {
            return item.logical_record == source.logical_record;
          });
      if (record == records.end() || !record_display_lines(*record))
        return reject("message introduction has undecided display framing");
      if (source.token_index >= record->tokens.size() ||
          source.word_index >= record->tokens[source.token_index].size() ||
          record->tokens[source.token_index][source.word_index] != source.word)
        return reject("message introduction cell differs from its source");
      MessageIntroductionCellIR cell;
      cell.logical_record = source.logical_record;
      cell.token_index = source.token_index;
      cell.word_index = source.word_index;
      cell.word = source.word;
      cell.source_disposition = source.disposition;
      cell.introduction_row = local;
      if (source_field_role(*record, source.token_index) ==
              SourceFieldRole::positioned &&
          source.disposition != SourceDisposition::control_operand &&
          !space(source.word) && source.word >= 0x20) {
        if (source.word == 0xffff || source.word == 0x2666)
          return reject("message introduction contains an unsupported glyph");
        cell.role = MessageIntroductionCellRoleIR::text;
        const Position position{source.logical_record, source.token_index};
        if (!first || position < *first)
          first = position;
        if (!last || position > *last)
          last = position;
      }
      if (!cells
               .emplace(Key{source.logical_record, source.token_index,
                            source.word_index},
                        result.cells.size())
               .second)
        return reject("message introduction duplicates a source cell");
      result.cells.push_back(cell);
    }
  }
  if (!first || !last)
    return reject("message introduction has no source text");

  // This model contains plain paragraphs and links. Do not silently discard
  // source font semantics that its atom vocabulary cannot express.
  for (const auto &record : records)
    for (const auto &segment : record.control_segments)
      if (segment.kind == BookControlKind::font &&
          !segment.source_tokens.empty() &&
          !is_display_line_length_opcode(record, segment)) {
        const Position position{record.logical_record,
                                segment.source_tokens.front()};
        const Position end{record.logical_record, segment.source_tokens.back()};
        if (end >= *first && position <= *last)
          return reject("message introduction font semantics are unsupported");
      }

  std::string display_error;
  std::optional<SelectorDisplayIR> display = SelectorDisplayIR{};
  if (!selectors.selectors.empty())
    display = extract_selector_display_ir(records, selectors, layout, ownership,
                                          &display_error);
  else
    for (const auto &record : records)
      for (const auto &segment : record.control_segments)
        if (segment.kind == BookControlKind::select)
          return reject(
              "message introduction selector catalog omits source controls");
  if (!display ||
      (!selectors.selectors.empty() &&
       !verify_selector_display_ir(records, selectors, layout, ownership,
                                   *display, &display_error)))
    return reject("message introduction selector display rejected: " +
                  display_error);
  struct Link {
    CrossReferenceTargetIR target;
    std::size_t cells = 0;
  };
  std::vector<Link> links;
  std::vector<std::size_t> cell_links(result.cells.size(), absent);
  for (const auto &row : display->rows)
    for (const auto &span : row.spans) {
      if (span.target.kind != SelectorTargetKind::internal_anchor)
        return reject("message introduction selector target is unsupported");
      const auto link = links.size();
      links.push_back(
          {{CrossReferenceTargetKindIR::anchor, span.target.raw_target}, 0});
      for (auto at = span.cell_begin; at < span.cell_end; ++at) {
        const auto &shown = row.cells[at];
        if (space(shown.word))
          continue;
        if (!shown.source ||
            shown.source->kind != SelectorSourceCellKind::token_word)
          return reject(
              "message introduction selected text lacks source cells");
        const auto found =
            cells.find({shown.source->logical_record, shown.source->token_index,
                        shown.source->word_index});
        if (found == cells.end() ||
            result.cells[found->second].role !=
                MessageIntroductionCellRoleIR::text ||
            cell_links[found->second] != absent)
          return reject(
              "message introduction selection is outside its text or overlaps");
        cell_links[found->second] = link;
        result.cells[found->second].role =
            MessageIntroductionCellRoleIR::selector;
        ++links.back().cells;
      }
      if (links.back().cells == 0)
        return reject("message introduction selection has no source text");
    }

  std::vector<std::size_t> claims(result.cells.size());
  const auto metadata_line = [&](const DecodedLogicalRecordSource &record,
                                 const DisplayLineIR &line) {
    if (is_subject_index_display_line(record, line))
      return true;
    for (const auto &segment : record.control_segments) {
      if (segment.display_text || segment.malformed ||
          (segment.kind != BookControlKind::select &&
           segment.kind != BookControlKind::spacing &&
           segment.kind != BookControlKind::layout_directive) ||
          is_display_line_length_opcode(record, segment))
        continue;
      const auto range = decoded_byte_range_to_word_range(record.assembled,
                                                          segment.opcode_range);
      const auto tokens = source_tokens_intersecting_output(
          record.assembled, range.begin, range.end);
      if (tokens.empty() || tokens.front() != line.prefix_token + 1)
        continue;
      for (const auto &shown : display_line_cells(record, line)) {
        if (space(shown.word))
          continue;
        const auto found = source_ownership.find(
            {record.logical_record, shown.token, shown.word_index});
        if (found == source_ownership.end() ||
            found->second->disposition != SourceDisposition::control_operand)
          return false;
      }
      return true;
    }
    return false;
  };
  std::vector<std::size_t> link_atoms(links.size());
  std::vector<Glyph> paragraph;
  const auto flush = [&]() {
    std::vector<Glyph> normalized;
    bool pending_space = false;
    for (const auto &glyph : paragraph) {
      if (space(glyph.word)) {
        pending_space = !normalized.empty();
        continue;
      }
      if (pending_space) {
        const auto prior = normalized.back().link;
        normalized.push_back(
            {' ', absent, prior == glyph.link ? prior : absent});
      }
      normalized.push_back(glyph);
      pending_space = false;
    }
    paragraph.clear();
    if (normalized.empty())
      return;
    MessageIntroductionParagraphIR output;
    auto current = absent;
    for (const auto &glyph : normalized) {
      if (output.atoms.empty() || current != glyph.link) {
        MessageIntroductionAtomIR atom;
        if (glyph.link != absent) {
          atom.kind = MessageIntroductionAtomKindIR::selector;
          atom.target = links[glyph.link].target;
          ++link_atoms[glyph.link];
        }
        output.atoms.push_back(std::move(atom));
        current = glyph.link;
      }
      auto &atom = output.atoms.back();
      atom.text += token_words_to_ascii(TokenWords{glyph.word});
      if (glyph.cell != absent) {
        atom.cell_indices.push_back(glyph.cell);
        ++claims[glyph.cell];
      }
    }
    result.paragraphs.push_back(std::move(output));
  };
  bool started = false;
  for (const auto &record : records) {
    if (record.logical_record < first->first ||
        record.logical_record > last->first)
      continue;
    const auto *lines = record_display_lines(record);
    if (!lines)
      return reject("message introduction has undecided display framing");
    for (const auto &line : *lines) {
      if (Position{record.logical_record, line.token_end} <= *first ||
          Position{record.logical_record, line.prefix_token} > *last)
        continue;
      if (line.prefix_token + 1 == line.token_end) {
        if (started)
          flush();
        continue;
      }
      const auto native = display_line_cells(record, line);
      if (std::all_of(native.begin(), native.end(),
                      [](const auto &cell) { return space(cell.word); })) {
        if (started)
          flush();
        continue;
      }
      bool has_text = false;
      for (const auto &shown : native) {
        const auto found =
            cells.find({record.logical_record, shown.token, shown.word_index});
        if (found != cells.end() &&
            (result.cells[found->second].role ==
                 MessageIntroductionCellRoleIR::text ||
             result.cells[found->second].role ==
                 MessageIntroductionCellRoleIR::selector))
          has_text = true;
      }
      if (!has_text) {
        if (!metadata_line(record, line))
          return reject("message introduction native body line has no semantic "
                        "source claims");
        continue;
      }
      if (!paragraph.empty())
        paragraph.push_back({' ', absent, absent});
      started = true;
      for (const auto &shown : native) {
        if (space(shown.word)) {
          paragraph.push_back({' ', absent, absent});
          continue;
        }
        const auto found =
            cells.find({record.logical_record, shown.token, shown.word_index});
        if (found == cells.end() ||
            (result.cells[found->second].role !=
                 MessageIntroductionCellRoleIR::text &&
             result.cells[found->second].role !=
                 MessageIntroductionCellRoleIR::selector))
          return reject(
              "message introduction native text has no semantic source claim");
        paragraph.push_back(
            {shown.word, found->second, cell_links[found->second]});
      }
    }
  }
  flush();
  for (std::size_t at = 0; at < result.cells.size(); ++at) {
    const auto semantic =
        result.cells[at].role == MessageIntroductionCellRoleIR::text ||
        result.cells[at].role == MessageIntroductionCellRoleIR::selector;
    if (claims[at] != (semantic ? 1U : 0U))
      return reject("message introduction source-cell claims are not exact");
  }
  if (std::any_of(link_atoms.begin(), link_atoms.end(),
                  [](auto count) { return count != 1; }))
    return reject("message introduction selector crosses paragraph boundaries");
  if (error)
    error->clear();
  return result;
}
} // namespace geist::detail
