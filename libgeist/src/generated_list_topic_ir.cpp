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

bool presentable(std::uint16_t word) {
  return word >= 0x21 && word != '?' && word != 0x2666;
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
    const auto& a = left.entries[index];
    const auto& b = right.entries[index];
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
        a.spans.size() != b.spans.size())
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
      } else if (segment.kind != BookControlKind::select &&
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
  if (!saw_heading || !saw_title)
    return reject("generated-list heading envelope is incomplete");

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
          segment.kind != BookControlKind::font)
        continue;
      const auto words = decoded_byte_range_to_word_range(record.assembled,
                                                          segment.payload_range);
      for (auto output = words.begin; output < words.end; ++output) {
        if (!presentable(record.assembled.words[output])) continue;
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
  result.entries = display->rows;
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
  for (const auto& row : topic.entries) {
    out << "entry row=" << row.id << " source=" << row.owner.logical_record
        << ':' << row.owner.segment_index << " cells=" << row.cells.size();
    for (const auto& span : row.spans)
      out << " target='" << span.target.raw_target << "' span=["
          << span.cell_begin << ',' << span.cell_end << ')';
    out << '\n';
  }
  for (const auto& segment : topic.segments)
    out << "segment=" << segment.source.logical_record << ':'
        << segment.source.segment_index << " opcode='" << segment.opcode
        << "'\n";
  return out.str();
}

} // namespace geist::detail
