#include "geist/detail/generated_list_topic_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

using CellKey = std::tuple<std::uint32_t, std::size_t, std::size_t,
                           SelectorSourceCellKind>;
using OwnedKey =
    std::tuple<std::uint32_t, std::size_t, std::size_t>;

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

std::string range_text(const DecodedLogicalRecordSource& record,
                       const OutputRangeIR& range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size()) return {};
  return text.substr(range.begin, range.end - range.begin);
}

DocumentSourceSliceIR source_slice(const DecodedLogicalRecordSource& record,
                                   const ControlSegmentIR& segment) {
  DocumentSourceSliceIR result;
  result.logical_record = record.logical_record;
  result.segment_index = segment.segment_index;
  if (segment.source_tokens.empty()) return result;
  result.token_begin = segment.source_tokens.front();
  result.token_end = segment.source_tokens.back() + 1;
  result.byte_begin = record.ir.tokens[result.token_begin].byte_range.begin;
  result.byte_end = record.ir.tokens[result.token_end - 1].byte_range.end;
  return result;
}

const DecodedLogicalRecordSource* find_record(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::uint32_t logical_record) {
  const auto found = std::find_if(records.begin(), records.end(),
                                  [&](const auto& record) {
                                    return record.logical_record ==
                                           logical_record;
                                  });
  return found == records.end() ? nullptr : &*found;
}

bool decoder_artifact(
    const std::vector<DecodedLogicalRecordSource>& records,
    const SelectorDisplayCellIR& cell) {
  if (!cell.source ||
      cell.source->kind != SelectorSourceCellKind::token_word)
    return false;
  const auto* record = find_record(records, cell.source->logical_record);
  if (record == nullptr || cell.source->token_index >= record->ir.tokens.size())
    return false;
  const auto& unmapped =
      record->ir.tokens[cell.source->token_index].unmapped_word_indices;
  return std::find(unmapped.begin(), unmapped.end(),
                   cell.source->word_index) != unmapped.end();
}

std::optional<GeneratedListEntryIR> make_entry(
    const std::vector<DecodedLogicalRecordSource>& records,
    const OwnershipIR& ownership, const SelectorDisplayRowIR& row,
    bool repeated_target_continuation, std::string* error) {
  if (row.spans.size() != 1 || row.spans.front().cell_begin >=
                                   row.spans.front().cell_end ||
      row.spans.front().cell_end > row.cells.size()) {
    fail(error, "generated-list entry has invalid selector geometry");
    return std::nullopt;
  }
  std::map<OwnedKey, const OwnedSourceCellIR*> owned;
  for (const auto& cell : ownership.cells)
    owned.emplace(OwnedKey{cell.logical_record, cell.token_index,
                           cell.word_index},
                  &cell);
  const auto owned_cell = [&](const SelectorDisplayCellIR& cell)
      -> const OwnedSourceCellIR* {
    if (!cell.source ||
        cell.source->kind != SelectorSourceCellKind::token_word)
      return nullptr;
    const auto found = owned.find(
        {cell.source->logical_record, cell.source->token_index,
         cell.source->word_index});
    return found == owned.end() ? nullptr : found->second;
  };
  const auto first_row_marker = [&](const SelectorDisplayCellIR& cell) {
    const auto* source = owned_cell(cell);
    return source != nullptr &&
           source->disposition == SourceDisposition::marker_slot &&
           source->run == row.owner.run &&
           source->row_index == row.owner.physical_row_index;
  };
  const auto label_anchor = [&](const SelectorDisplayCellIR& cell) {
    if (decoder_artifact(records, cell) || first_row_marker(cell))
      return false;
    const auto* source = owned_cell(cell);
    return source != nullptr &&
           (source->disposition == SourceDisposition::visible_content ||
            source->disposition == SourceDisposition::opaque);
  };
  const auto payload_word_count = [&](const SelectorDisplayCellIR& cell) {
    if (!cell.source ||
        cell.source->kind != SelectorSourceCellKind::token_word)
      return std::size_t{0};
    const auto* record = find_record(records, cell.source->logical_record);
    if (record == nullptr || cell.source->token_index >= record->ir.tokens.size())
      return std::size_t{0};
    const auto& token = record->ir.tokens[cell.source->token_index];
    return token.decoded_words.size() -
           static_cast<std::size_t>(token.has_spacing_control);
  };

  const auto& span = row.spans.front();
  auto first = std::size_t{0};
  while (first < row.cells.size() && !label_anchor(row.cells[first])) ++first;
  const auto first_visible = std::find_if(
      row.cells.begin() + static_cast<std::ptrdiff_t>(first), row.cells.end(),
      [&](const auto& cell) {
        const auto* source = owned_cell(cell);
        return source != nullptr &&
               source->disposition == SourceDisposition::visible_content &&
               !decoder_artifact(records, cell) && !first_row_marker(cell);
      });
  const auto first_restored_marker = std::find_if(
      row.cells.begin() + static_cast<std::ptrdiff_t>(first), row.cells.end(),
      [](const auto& cell) {
        return cell.origin ==
               SelectorDisplayCellOrigin::restored_native_marker;
      });
  // A deferred row may carry opaque bytes from the prior record.  They are
  // decoration when the actual row payload begins before any restored
  // separator marker (IEAC6MST FIGURES 81); otherwise they contain the
  // source-owned ordinal preceding that separator (GG24-395 FIGURES 59).
  if (first < row.cells.size()) {
    const auto* source = owned_cell(row.cells[first]);
    if (source != nullptr && source->disposition == SourceDisposition::opaque &&
        first_visible != row.cells.end() &&
        (first_restored_marker == row.cells.end() ||
         first_visible < first_restored_marker))
      first = static_cast<std::size_t>(
          std::distance(row.cells.begin(), first_visible));
  }

  // Some generated lists encode a native line marker as an isolated token
  // between padding and the ordinal rather than as MarkerSlotIR.  Its exact
  // one-cell token geometry plus a following content token proves the role;
  // the decoded value is irrelevant.
  if (!repeated_target_continuation && first < row.cells.size() &&
      payload_word_count(row.cells[first]) == 1) {
    auto next = first + 1;
    while (next < row.cells.size() && !label_anchor(row.cells[next])) ++next;
    const auto* candidate = owned_cell(row.cells[first]);
    const auto* following =
        next < row.cells.size() ? owned_cell(row.cells[next]) : nullptr;
    const auto opaque_leader =
        candidate != nullptr && following != nullptr &&
        candidate->disposition == SourceDisposition::opaque &&
        following->disposition == SourceDisposition::opaque;
    const auto isolated_visible_leader =
        candidate != nullptr && following != nullptr &&
        candidate->disposition == SourceDisposition::visible_content &&
        following->disposition == SourceDisposition::visible_content &&
        candidate->run == following->run &&
        candidate->row_index == following->row_index &&
        payload_word_count(row.cells[next]) > 1 &&
        first_restored_marker != row.cells.end() &&
        next < static_cast<std::size_t>(
                   std::distance(row.cells.begin(), first_restored_marker));
    if (next < row.cells.size() && row.cells[first].source &&
        row.cells[next].source &&
        (row.cells[first].source->logical_record !=
             row.cells[next].source->logical_record ||
         row.cells[first].source->token_index !=
             row.cells[next].source->token_index) &&
        (isolated_visible_leader || opaque_leader)) {
      first = next;
    }
  }
  auto last = row.cells.size();
  while (last > first && !label_anchor(row.cells[last - 1])) --last;
  if (first == last) {
    fail(error, "generated-list entry has no source-proven label anchor");
    return std::nullopt;
  }

  GeneratedListEntryIR entry;
  entry.display = row;
  entry.selector = span.selector;
  entry.target = span.target;
  entry.cell_dispositions.resize(row.cells.size(),
                                 GeneratedListCellDispositionIR::structural);
  for (std::size_t index = 0; index < row.cells.size(); ++index) {
    const auto& cell = row.cells[index];
    auto disposition = GeneratedListCellDispositionIR::structural;
    if (decoder_artifact(records, cell)) {
      disposition = GeneratedListCellDispositionIR::decoder_artifact;
    } else if (index < first || first_row_marker(cell)) {
      disposition = cell.source
                        ? GeneratedListCellDispositionIR::layout_decoration
                        : GeneratedListCellDispositionIR::structural;
    } else if (index < last) {
      disposition = GeneratedListCellDispositionIR::label_fragment;
    }
    entry.cell_dispositions[index] = disposition;
  }
  entry.suppressed_prefix_dispositions.reserve(
      row.suppressed_prefix_cells.size());
  for (const auto& cell : row.suppressed_prefix_cells)
    entry.suppressed_prefix_dispositions.push_back(
        decoder_artifact(records, cell)
            ? GeneratedListCellDispositionIR::decoder_artifact
            : cell.source
                  ? GeneratedListCellDispositionIR::layout_decoration
                  : GeneratedListCellDispositionIR::structural);

  for (std::size_t begin = 0; begin < row.cells.size();) {
    if (entry.cell_dispositions[begin] !=
        GeneratedListCellDispositionIR::label_fragment) {
      ++begin;
      continue;
    }
    const auto role = begin < span.cell_end
                          ? GeneratedListLabelFragmentRoleIR::selected_payload
                          : GeneratedListLabelFragmentRoleIR::source_extension;
    auto end = begin + 1;
    while (end < row.cells.size() &&
           entry.cell_dispositions[end] ==
               GeneratedListCellDispositionIR::label_fragment &&
           (role == GeneratedListLabelFragmentRoleIR::selected_payload
                ? end < span.cell_end
                : end >= span.cell_end))
      ++end;
    GeneratedListLabelFragmentIR fragment;
    fragment.role = role;
    fragment.cell_begin = begin;
    fragment.cell_end = end;
    fragment.cells.assign(row.cells.begin() + static_cast<std::ptrdiff_t>(begin),
                          row.cells.begin() + static_cast<std::ptrdiff_t>(end));
    std::set<std::pair<std::uint32_t, std::size_t>> source_tokens;
    for (const auto& cell : fragment.cells)
      if (cell.source)
        source_tokens.emplace(cell.source->logical_record,
                              cell.source->token_index);
    for (const auto& source_token : source_tokens) {
      const auto* record = find_record(records, source_token.first);
      if (record == nullptr || source_token.second >= record->ir.tokens.size()) {
        fail(error, "generated-list label fragment references a missing token");
        return std::nullopt;
      }
      const auto segment = std::find_if(
          record->control_segments.begin(), record->control_segments.end(),
          [&](const auto& candidate) {
            return std::find(candidate.source_tokens.begin(),
                             candidate.source_tokens.end(),
                             source_token.second) !=
                   candidate.source_tokens.end();
          });
      if (segment == record->control_segments.end()) {
        fail(error,
             "generated-list label token has no typed control segment");
        return std::nullopt;
      }
      const auto& token = record->ir.tokens[source_token.second];
      fragment.source_slices.push_back(
          {source_token.first, segment->segment_index, source_token.second,
           source_token.second + 1, token.byte_range.begin,
           token.byte_range.end});
    }
    entry.label_fragments.push_back(std::move(fragment));
    begin = end;
  }
  if (entry.label_fragments.empty()) {
    fail(error, "generated-list entry has no typed label fragment");
    return std::nullopt;
  }
  return entry;
}

bool same_topic(const GeneratedListTopicIR& left,
                const GeneratedListTopicIR& right) {
  if (left.kind != right.kind || left.title != right.title ||
      left.entries.size() != right.entries.size() ||
      left.segments.size() != right.segments.size() ||
      std::tie(left.heading_source.logical_record,
               left.heading_source.segment_index,
               left.heading_source.token_begin, left.heading_source.token_end,
               left.heading_source.byte_begin, left.heading_source.byte_end) !=
          std::tie(right.heading_source.logical_record,
                   right.heading_source.segment_index,
                   right.heading_source.token_begin,
                   right.heading_source.token_end,
                   right.heading_source.byte_begin,
                   right.heading_source.byte_end))
    return false;
  const auto same_ref = [](const SelectorRefIR& a, const SelectorRefIR& b) {
    return std::tie(a.logical_record, a.segment_index, a.ordinal) ==
           std::tie(b.logical_record, b.segment_index, b.ordinal);
  };
  const auto same_source = [](const SelectorSourceCellRefIR& a,
                              const SelectorSourceCellRefIR& b) {
    return std::tie(a.logical_record, a.token_index, a.word_index, a.kind,
                    a.token_bytes.begin, a.token_bytes.end) ==
           std::tie(b.logical_record, b.token_index, b.word_index, b.kind,
                    b.token_bytes.begin, b.token_bytes.end);
  };
  const auto same_cells = [&](const auto& a, const auto& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t index = 0; index < a.size(); ++index)
      if (a[index].word != b[index].word ||
          a[index].origin != b[index].origin ||
          a[index].source.has_value() != b[index].source.has_value() ||
          (a[index].source &&
           !same_source(*a[index].source, *b[index].source)))
        return false;
    return true;
  };
  for (std::size_t index = 0; index < left.entries.size(); ++index) {
    const auto& entry_a = left.entries[index];
    const auto& entry_b = right.entries[index];
    const auto& a = entry_a.display;
    const auto& b = entry_b.display;
    if (std::tie(a.id, a.owner.logical_record, a.owner.segment_index,
                 a.owner.run, a.owner.physical_row_index, a.owner.token_begin,
                 a.owner.token_end, a.association, a.hard_boundary) !=
            std::tie(b.id, b.owner.logical_record, b.owner.segment_index,
                     b.owner.run, b.owner.physical_row_index,
                     b.owner.token_begin, b.owner.token_end, b.association,
                     b.hard_boundary) ||
        !same_cells(a.cells, b.cells) ||
        !same_cells(a.suppressed_prefix_cells,
                    b.suppressed_prefix_cells) ||
        a.spans.size() != b.spans.size() ||
        !same_ref(entry_a.selector, entry_b.selector) ||
        std::tie(entry_a.target.kind, entry_a.target.raw_target,
                 entry_a.target.resolved_target) !=
            std::tie(entry_b.target.kind, entry_b.target.raw_target,
                     entry_b.target.resolved_target) ||
        entry_a.cell_dispositions != entry_b.cell_dispositions ||
        entry_a.suppressed_prefix_dispositions !=
            entry_b.suppressed_prefix_dispositions ||
        entry_a.label_fragments.size() != entry_b.label_fragments.size())
      return false;
    for (std::size_t span = 0; span < a.spans.size(); ++span) {
      const auto& x = a.spans[span];
      const auto& y = b.spans[span];
      if (!same_ref(x.selector, y.selector) ||
          std::tie(x.target.kind, x.target.raw_target,
                   x.target.resolved_target, x.cell_begin, x.cell_end) !=
              std::tie(y.target.kind, y.target.raw_target,
                       y.target.resolved_target, y.cell_begin, y.cell_end))
        return false;
    }
    for (std::size_t fragment = 0;
         fragment < entry_a.label_fragments.size(); ++fragment) {
      const auto& x = entry_a.label_fragments[fragment];
      const auto& y = entry_b.label_fragments[fragment];
      if (std::tie(x.role, x.cell_begin, x.cell_end) !=
              std::tie(y.role, y.cell_begin, y.cell_end) ||
          !same_cells(x.cells, y.cells) ||
          x.source_slices.size() != y.source_slices.size())
        return false;
      for (std::size_t slice = 0; slice < x.source_slices.size(); ++slice) {
        const auto& p = x.source_slices[slice];
        const auto& q = y.source_slices[slice];
        if (std::tie(p.logical_record, p.segment_index, p.token_begin,
                     p.token_end, p.byte_begin, p.byte_end) !=
            std::tie(q.logical_record, q.segment_index, q.token_begin,
                     q.token_end, q.byte_begin, q.byte_end))
          return false;
      }
    }
  }
  for (std::size_t index = 0; index < left.segments.size(); ++index) {
    const auto& a = left.segments[index];
    const auto& b = right.segments[index];
    if (a.kind != b.kind || a.opcode != b.opcode ||
        std::tie(a.source.logical_record, a.source.segment_index,
                 a.source.token_begin, a.source.token_end,
                 a.source.byte_begin, a.source.byte_end) !=
            std::tie(b.source.logical_record, b.source.segment_index,
                     b.source.token_begin, b.source.token_end,
                     b.source.byte_begin, b.source.byte_end))
      return false;
  }
  return true;
}

} // namespace

std::optional<GeneratedListTopicIR> extract_generated_list_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const SelectorCatalogIR& selectors, const LayoutIR& layout,
    const OwnershipIR& ownership, std::string* error) {
  const auto reject = [&](std::string message)
      -> std::optional<GeneratedListTopicIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (records.empty()) return reject("generated-list source is empty");
  std::string inner_error;
  if (!verify_selector_catalog_ir(records, selectors, &inner_error) ||
      !verify_layout_ir(records, layout, &inner_error) ||
      !verify_ownership_ir(records, layout, ownership, &inner_error))
    return reject("generated-list prerequisite IR rejected: " + inner_error);
  const auto display = extract_selector_display_ir(
      records, selectors, layout, ownership, &inner_error);
  if (!display || !verify_selector_display_ir(
                      records, selectors, layout, ownership, *display,
                      &inner_error))
    return reject("generated-list display rejected: " + inner_error);
  if (display->rows.empty() || display->rows.size() != selectors.selectors.size() ||
      display->bindings.size() != selectors.selectors.size() ||
      !display->objects.empty())
    return reject("generated-list selectors do not map one-to-one to rows");
  for (const auto& row : display->rows)
    if (!row.hard_boundary || row.spans.size() != 1)
      return reject("generated-list row is not one complete hard entry");
  for (const auto& binding : display->bindings)
    if (binding.kind != SelectorBindingKind::display_span)
      return reject("generated-list selector has a non-row disposition");

  GeneratedListTopicIR result;
  bool saw_heading = false;
  bool saw_title = false;
  bool saw_entry = false;
  bool saw_spacing = false;
  bool saw_break = false;
  bool saw_list_open = false;
  bool saw_list_close = false;
  std::map<std::tuple<std::uint32_t, std::size_t, std::size_t>,
           SourceDisposition>
      dispositions;
  for (const auto& cell : ownership.cells)
    dispositions.emplace(std::make_tuple(cell.logical_record, cell.token_index,
                                         cell.word_index),
                         cell.disposition);
  const auto has_owned_visible = [&](const DecodedLogicalRecordSource& record,
                                     const ControlSegmentIR& segment) {
    const auto words = decoded_byte_range_to_word_range(record.assembled,
                                                        segment.payload_range);
    for (auto output = words.begin; output < words.end; ++output) {
      const auto& source = record.assembled.sources[output];
      if (source.kind != LogicalWordSourceKind::token_word) continue;
      const auto found = dispositions.find(
          {record.logical_record, source.token_index, source.word_index});
      if (found != dispositions.end() &&
          found->second == SourceDisposition::visible_content)
        return true;
    }
    return false;
  };
  for (const auto& record : records) {
    for (const auto& segment : record.control_segments) {
      if (segment.source_tokens.empty())
        return reject("generated-list envelope segment lacks provenance");
      result.segments.push_back(
          {segment.kind, segment.opcode, source_slice(record, segment)});
      if (!saw_title) {
        if (segment.kind == BookControlKind::heading_level) {
          if (saw_heading || segment.malformed)
            return reject("generated-list CHDLEVEL is duplicated or malformed");
          const auto operand =
              ascii_lower(trim_ascii(range_text(record, segment.operand_range)));
          if (operand == ":figlist") {
            result.kind = GeneratedListTopicKindIR::figures;
            result.title = "Figures";
          } else if (operand == ":tlist") {
            result.kind = GeneratedListTopicKindIR::tables;
            result.title = "Tables";
          } else {
            return reject("generated-list CHDLEVEL is not FIGLIST/TLIST");
          }
          saw_heading = true;
          continue;
        }
        if (segment.kind == BookControlKind::title) {
          if (!saw_heading || segment.malformed)
            return reject("generated-list ST precedes its CHDLEVEL");
          if (!ascii_equals_case_insensitive(
                  trim_ascii(range_text(record, segment.payload_range)),
                  result.title))
            return reject("generated-list ST title disagrees with CHDLEVEL");
          result.heading_source = source_slice(record, segment);
          saw_title = true;
          continue;
        }
        switch (segment.kind) {
        case BookControlKind::topic_start:
        case BookControlKind::topic_number:
        case BookControlKind::parent:
        case BookControlKind::forward_level:
        case BookControlKind::back_level:
        case BookControlKind::summary:
        case BookControlKind::source_file: break;
        default:
          return reject("control outside the generated-list metadata envelope");
        }
        if (segment.payload_range.begin != segment.payload_range.end &&
            !((segment.kind == BookControlKind::forward_level ||
               segment.kind == BookControlKind::back_level) &&
              !has_owned_visible(record, segment)))
          return reject("generated-list metadata contains trailing content at " +
                        std::to_string(record.logical_record) + ':' +
                        std::to_string(segment.segment_index) + " opcode=" +
                        segment.opcode + " payload='" +
                        range_text(record, segment.payload_range) + "'");
      } else {
        if (segment.kind == BookControlKind::select) saw_entry = true;
        if (segment.kind == BookControlKind::spacing) {
          if (segment.malformed || saw_entry || saw_spacing ||
              segment.payload_range.begin != segment.payload_range.end)
            return reject("malformed or misplaced generated-list spacing control");
          saw_spacing = true;
          continue;
        }
        if (segment.kind == BookControlKind::layout_directive) {
          const auto operand = ascii_lower(
              trim_ascii(range_text(record, segment.operand_range)));
          const auto list = result.kind == GeneratedListTopicKindIR::figures
                                ? "figlist"
                                : "tlist";
          if (segment.malformed ||
              segment.payload_range.begin != segment.payload_range.end)
            return reject("malformed generated-list directive");
          if (operand == "break 3") {
            if (saw_entry || saw_break)
              return reject("misplaced or duplicate generated-list break");
            saw_break = true;
          } else if (operand == "off " + std::string(list)) {
            if (saw_entry || saw_list_open)
              return reject("misplaced or duplicate generated-list opener");
            saw_list_open = true;
          } else if (operand == "off e" + std::string(list) + " 0 0") {
            if (!saw_entry || saw_list_close)
              return reject("misplaced or duplicate generated-list closer");
            saw_list_close = true;
          } else {
            return reject("generated-list directive disagrees with list kind");
          }
          continue;
        }
        if (segment.kind != BookControlKind::select &&
            segment.kind != BookControlKind::text &&
            segment.kind != BookControlKind::font) {
          return reject("control outside the generated-list entry envelope at " +
                        std::to_string(record.logical_record) + ':' +
                        std::to_string(segment.segment_index) + " opcode=" +
                        segment.opcode + " payload='" +
                        range_text(record, segment.payload_range) + "'");
        }
      }
    }
  }
  if (!saw_heading || !saw_title)
    return reject("generated-list heading envelope is incomplete");
  if (saw_break != saw_list_open || saw_list_open != saw_list_close)
    return reject("generated-list directive envelope is incomplete");

  std::set<CellKey> row_cells;
  for (const auto& row : display->rows) {
    for (const auto& cell : row.cells)
      if (cell.source)
        row_cells.emplace(cell.source->logical_record, cell.source->token_index,
                          cell.source->word_index, cell.source->kind);
    for (const auto& cell : row.suppressed_prefix_cells)
      if (cell.source)
        row_cells.emplace(cell.source->logical_record, cell.source->token_index,
                          cell.source->word_index, cell.source->kind);
  }
  for (const auto& record : records) {
    for (const auto& segment : record.control_segments) {
      if (!saw_title) continue;
      if (segment.kind == BookControlKind::title) {
        saw_title = true;
        continue;
      }
      if (segment.kind != BookControlKind::select &&
          segment.kind != BookControlKind::text &&
          segment.kind != BookControlKind::font &&
          segment.kind != BookControlKind::spacing &&
          segment.kind != BookControlKind::layout_directive)
        continue;
      const auto words = decoded_byte_range_to_word_range(record.assembled,
                                                          segment.payload_range);
      for (auto output = words.begin; output < words.end; ++output) {
        const auto& source = record.assembled.sources[output];
        if (source.kind != LogicalWordSourceKind::token_word) continue;
        const auto disposition = dispositions.find(
            {record.logical_record, source.token_index, source.word_index});
        if (disposition == dispositions.end() ||
            disposition->second != SourceDisposition::visible_content)
          continue;
        const auto kind = source.kind == LogicalWordSourceKind::token_word
                              ? SelectorSourceCellKind::token_word
                              : SelectorSourceCellKind::inserted_space;
        if (row_cells.count({record.logical_record, source.token_index,
                             source.word_index, kind}) == 0)
          return reject("generated-list visible payload cell is unowned at " +
                        std::to_string(record.logical_record) + ':' +
                        std::to_string(segment.segment_index) + " word=" +
                        std::to_string(record.assembled.words[output]) +
                        " token=" + std::to_string(source.token_index) + ':' +
                        std::to_string(source.word_index));
      }
    }
  }
  result.entries.reserve(display->rows.size());
  for (std::size_t index = 0; index < display->rows.size(); ++index) {
    const auto& row = display->rows[index];
    const auto repeated_target =
        index != 0 && !row.spans.empty() &&
        !display->rows[index - 1].spans.empty() &&
        row.spans.front().target.kind ==
            display->rows[index - 1].spans.front().target.kind &&
        row.spans.front().target.raw_target ==
            display->rows[index - 1].spans.front().target.raw_target &&
        row.spans.front().target.resolved_target ==
            display->rows[index - 1].spans.front().target.resolved_target;
    auto entry = make_entry(records, ownership, row, repeated_target,
                            &inner_error);
    if (!entry)
      return reject("generated-list entry semantics rejected: " +
                    inner_error);
    result.entries.push_back(std::move(*entry));
  }
  if (error != nullptr) error->clear();
  return result;
}

bool verify_generated_list_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const SelectorCatalogIR& selectors, const LayoutIR& layout,
    const OwnershipIR& ownership, const GeneratedListTopicIR& topic,
    std::string* error) {
  const auto canonical = extract_generated_list_topic_ir(
      records, selectors, layout, ownership, error);
  if (!canonical) return false;
  if (!same_topic(*canonical, topic))
    return fail(error, "generated-list topic differs from canonical extraction");
  if (error != nullptr) error->clear();
  return true;
}

std::string format_generated_list_topic_ir(const GeneratedListTopicIR& topic) {
  std::ostringstream out;
  out << "generated_list kind="
      << (topic.kind == GeneratedListTopicKindIR::figures ? "figures" : "tables")
      << " title='" << topic.title << "' entries=" << topic.entries.size()
      << " segments=" << topic.segments.size() << '\n';
  for (const auto& entry : topic.entries) {
    const auto& row = entry.display;
    TokenWords label_words;
    for (const auto& fragment : entry.label_fragments)
      for (const auto& cell : fragment.cells)
        label_words.push_back(cell.word);
    out << "entry row=" << row.id << " source=" << row.owner.logical_record
        << ':' << row.owner.segment_index << " cells=" << row.cells.size()
        << " target='" << entry.target.raw_target << "' fragments="
        << entry.label_fragments.size() << " label='"
        << token_words_to_ascii(label_words) << "'";
    for (const auto& fragment : entry.label_fragments)
      out << ' ' << (fragment.role ==
                              GeneratedListLabelFragmentRoleIR::selected_payload
                          ? "selected"
                          : "extension")
          << "=[" << fragment.cell_begin << ',' << fragment.cell_end << ')';
    out << '\n';
  }
  for (const auto& segment : topic.segments)
    out << "segment=" << segment.source.logical_record << ':'
        << segment.source.segment_index << " opcode='" << segment.opcode
        << "'\n";
  return out.str();
}

} // namespace geist::detail
