#include "geist/detail/glossary_catalog_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

using SegmentKey = std::pair<std::uint32_t, std::size_t>;

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
                                   std::size_t segment_index,
                                   const OutputRangeIR& output) {
  DocumentSourceSliceIR result;
  result.logical_record = record.logical_record;
  result.segment_index = segment_index;
  const auto words = decoded_byte_range_to_word_range(record.assembled, output);
  const auto tokens = source_tokens_intersecting_output(
      record.assembled, words.begin, words.end);
  if (tokens.empty()) return result;
  result.token_begin = tokens.front();
  result.token_end = tokens.back() + 1;
  result.byte_begin = record.ir.tokens[result.token_begin].byte_range.begin;
  result.byte_end = record.ir.tokens[result.token_end - 1].byte_range.end;
  return result;
}

DocumentSourceSliceIR row_slice(const DecodedLogicalRecordSource& record,
                                const PhysicalRowIR& row) {
  DocumentSourceSliceIR result;
  result.logical_record = row.logical_record;
  result.segment_index = row.segment_index;
  result.token_begin = row.token_begin;
  result.token_end = row.token_end;
  if (row.token_begin < row.token_end && row.token_end <= record.ir.tokens.size()) {
    result.byte_begin = record.ir.tokens[row.token_begin].byte_range.begin;
    result.byte_end = record.ir.tokens[row.token_end - 1].byte_range.end;
  }
  return result;
}

bool has_source(const DocumentSourceSliceIR& source) {
  return source.logical_record != 0 && source.token_begin < source.token_end &&
         source.byte_begin < source.byte_end;
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

bool begins_term(const std::string& text, const std::string& term) {
  if (text.size() <= term.size()) return false;
  return lower(text.substr(0, term.size())) == lower(term) &&
         text[term.size()] == '.';
}

std::optional<std::string> semantic_term(
    const std::string& raw,
    const std::vector<GlossaryDefinitionRowIR>& rows) {
  auto candidate = trim_ascii(raw);
  const auto text = trim_ascii(rows.front().visible_text);
  const auto matches = [&](const std::string& value) {
    if (begins_term(text, value)) return true;
    return lower(text) == lower(value) && rows.size() > 1 && rows[1].marker &&
           rows[1].marker->decoded_text == ".";
  };
  while (!candidate.empty() && !matches(candidate)) {
    if (!std::isalnum(static_cast<unsigned char>(candidate.back())) &&
        candidate.back() != ')' && candidate.back() != ']') {
      candidate.pop_back();
      candidate = trim_ascii(std::move(candidate));
      continue;
    }
    const auto space = candidate.find_last_of(" \t\r\n");
    if (space == std::string::npos) return std::nullopt;
    candidate = trim_ascii(candidate.substr(0, space));
  }
  if (candidate.empty()) return std::nullopt;
  return candidate;
}

bool is_glossary_control(const ControlSegmentIR& segment) {
  return ascii_equals_case_insensitive(segment.opcode, "srgls");
}

bool is_nested_start(const std::string& opcode, const std::string& kind) {
  const auto value = lower(opcode);
  return value.rfind("sr" + kind, 0) == 0 && value != "sre" + kind;
}

bool is_nested_end(const std::string& opcode, const std::string& kind) {
  return ascii_equals_case_insensitive(opcode, "sre" + kind);
}

std::string slice_projection(const DocumentSourceSliceIR& source) {
  std::ostringstream out;
  out << source.logical_record << ':' << source.segment_index << ':'
      << source.token_begin << '-' << source.token_end << ':'
      << source.byte_begin << '-' << source.byte_end;
  return out.str();
}

bool same_slice(const DocumentSourceSliceIR& left,
                const DocumentSourceSliceIR& right) {
  return left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.token_begin == right.token_begin &&
         left.token_end == right.token_end &&
         left.byte_begin == right.byte_begin && left.byte_end == right.byte_end;
}

bool same_marker(const MarkerSlotIR& left, const MarkerSlotIR& right) {
  return left.logical_record == right.logical_record &&
         left.token_index == right.token_index &&
         left.encoded_value == right.encoded_value &&
         left.encoded_width == right.encoded_width &&
         left.byte_range.begin == right.byte_range.begin &&
         left.byte_range.end == right.byte_range.end &&
         left.decoded_text == right.decoded_text;
}

bool same_cell(const GlossaryCatalogCellIR& left,
               const GlossaryCatalogCellIR& right) {
  return left.logical_record == right.logical_record &&
         left.token_index == right.token_index &&
         left.word_index == right.word_index && left.word == right.word &&
         left.disposition == right.disposition && left.run == right.run &&
         left.row_index == right.row_index;
}

bool same_row(const GlossaryDefinitionRowIR& left,
              const GlossaryDefinitionRowIR& right) {
  if (left.visible_text != right.visible_text ||
      left.marker.has_value() != right.marker.has_value() ||
      left.native_origin != right.native_origin ||
      left.break_before != right.break_before ||
      left.source_row.display_run != right.source_row.display_run ||
      left.source_row.row_index != right.source_row.row_index ||
      !same_slice(left.source, right.source) ||
      left.cells.size() != right.cells.size())
    return false;
  if (left.marker && !same_marker(*left.marker, *right.marker)) return false;
  for (std::size_t index = 0; index < left.cells.size(); ++index)
    if (!same_cell(left.cells[index], right.cells[index])) return false;
  return true;
}

bool same_rows(const std::vector<GlossaryDefinitionRowIR>& left,
               const std::vector<GlossaryDefinitionRowIR>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index)
    if (!same_row(left[index], right[index])) return false;
  return true;
}

bool same_slices(const std::vector<DocumentSourceSliceIR>& left,
                 const std::vector<DocumentSourceSliceIR>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index)
    if (!same_slice(left[index], right[index])) return false;
  return true;
}

bool same_catalog(const GlossaryCatalogIR& left,
                  const GlossaryCatalogIR& right) {
  if (left.first_logical_record != right.first_logical_record ||
      left.end_logical_record != right.end_logical_record ||
      left.heading_level != right.heading_level ||
      left.sections.size() != right.sections.size() ||
      left.entries.size() != right.entries.size() ||
      !same_slice(left.terminal_source, right.terminal_source) ||
      left.segments.size() != right.segments.size())
    return false;
  for (std::size_t index = 0; index < left.sections.size(); ++index) {
    const auto& a = left.sections[index];
    const auto& b = right.sections[index];
    if (a.label != b.label || !same_slice(a.marker_source, b.marker_source) ||
        !same_rows(a.label_rows, b.label_rows))
      return false;
  }
  for (std::size_t index = 0; index < left.entries.size(); ++index) {
    const auto& a = left.entries[index];
    const auto& b = right.entries[index];
    if (a.term != b.term || a.raw_term != b.raw_term ||
        a.source_suffix != b.source_suffix ||
        !same_slice(a.term_source, b.term_source) ||
        !same_rows(a.definition.rows, b.definition.rows) ||
        !same_slices(a.definition.structural_sources,
                     b.definition.structural_sources))
      return false;
  }
  for (std::size_t index = 0; index < left.segments.size(); ++index) {
    const auto& a = left.segments[index];
    const auto& b = right.segments[index];
    if (a.kind != b.kind || a.opcode != b.opcode ||
        a.malformed != b.malformed || !same_slice(a.source, b.source))
      return false;
  }
  return true;
}

} // namespace

std::optional<GlossaryCatalogIR> extract_glossary_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    std::string* error) {
  const auto reject =
      [&](std::string message) -> std::optional<GlossaryCatalogIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (records.empty()) return reject("glossary topic has no logical records");

  std::string verification_error;
  if (!verify_layout_ir(records, layout, &verification_error) ||
      !verify_ownership_ir(records, layout, ownership, &verification_error))
    return reject("source layout/ownership is not canonical: " +
                  verification_error);
  for (std::size_t index = 1; index < records.size(); ++index)
    if (records[index].logical_record != records[index - 1].logical_record + 1)
      return reject("glossary logical-record envelope is not contiguous");

  struct OrderedSegment {
    const DecodedLogicalRecordSource* record = nullptr;
    const ControlSegmentIR* segment = nullptr;
  };
  std::vector<OrderedSegment> ordered;
  for (const auto& record : records)
    for (const auto& segment : record.control_segments)
      ordered.push_back({&record, &segment});
  if (ordered.empty()) return reject("glossary topic has no control segments");

  const auto first_glossary = std::find_if(
      ordered.begin(), ordered.end(),
      [](const auto& item) { return is_glossary_control(*item.segment); });
  if (first_glossary == ordered.end())
    return reject("glossary topic has no SRGLS boundary");
  if (!is_glossary_control(*ordered.back().segment))
    return reject("glossary topic has content after its terminal SRGLS");
  if (!trim_ascii(range_text(*ordered.back().record,
                             ordered.back().segment->payload_range)).empty())
    return reject("glossary terminal SRGLS contains a term");

  bool saw_title = false;
  bool saw_glossary_heading = false;
  std::string heading_level;
  for (auto it = ordered.begin(); it != first_glossary; ++it) {
    const auto& segment = *it->segment;
    if (segment.kind == BookControlKind::unknown ||
        segment.kind == BookControlKind::select ||
        segment.kind == BookControlKind::spacing ||
        segment.kind == BookControlKind::layout_directive ||
        segment.kind == BookControlKind::menu_start ||
        segment.kind == BookControlKind::menu_item ||
        segment.kind == BookControlKind::menu_end)
      return reject("unsupported control appears in glossary introduction");
    if (segment.kind == BookControlKind::title) saw_title = true;
    if (segment.kind == BookControlKind::heading_level) {
      heading_level = trim_ascii(range_text(*it->record, segment.operand_range));
      if (!heading_level.empty() && heading_level.front() == ':')
        heading_level.erase(heading_level.begin());
      saw_glossary_heading =
          ascii_equals_case_insensitive(heading_level, "glossary");
    }
  }
  if (!saw_title || !saw_glossary_heading)
    return reject("glossary heading envelope is incomplete");

  const auto introduction =
      extract_glossary_introduction_ir(records, layout, ownership,
                                       &verification_error);
  if (!introduction)
    return reject("glossary introduction rejected: " + verification_error);

  std::map<SegmentKey, std::vector<GlossaryDefinitionRowIR>> rows;
  for (const auto& run : layout.runs) {
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto& row = run.rows[row_index];
      const auto record = std::find_if(
          records.begin(), records.end(), [&](const auto& candidate) {
            return candidate.logical_record == row.logical_record;
          });
      if (record == records.end())
        return reject("glossary row refers to an absent logical record");
      GlossaryDefinitionRowIR item;
      item.visible_text = row.visible_text;
      item.marker = row.marker;
      item.native_origin = row.native_origin;
      item.break_before = row.break_before;
      item.source_row = {run.id, row_index};
      item.source = row_slice(*record, row);
      if (!has_source(item.source))
        return reject("glossary row source provenance is incomplete");
      for (const auto& cell : ownership.cells) {
        if (cell.run == run.id && cell.row_index == row_index)
          item.cells.push_back({cell.logical_record, cell.token_index,
                                cell.word_index, cell.word, cell.disposition,
                                cell.run, cell.row_index});
      }
      if (item.cells.empty())
        return reject("glossary row has no owned source cells");
      rows[{row.logical_record, row.segment_index}].push_back(std::move(item));
    }
  }

  GlossaryCatalogIR result;
  result.first_logical_record = records.front().logical_record;
  result.end_logical_record = records.back().logical_record + 1;
  result.heading_level = std::move(heading_level);
  result.introduction = *introduction;

  int figure_depth = 0;
  int table_depth = 0;
  for (const auto& item : ordered) {
    const auto& segment = *item.segment;
    auto source = source_slice(*item.record, segment.segment_index,
                               segment.complete);
    if (!has_source(source))
      return reject("glossary segment source provenance is incomplete");
    result.segments.push_back(
        {segment.kind, segment.opcode, segment.malformed, std::move(source)});
  }

  auto cursor = static_cast<std::size_t>(first_glossary - ordered.begin());
  while (cursor < ordered.size()) {
    const auto& boundary = ordered[cursor];
    if (!is_glossary_control(*boundary.segment))
      return reject("catalog content is not preceded by SRGLS");
    const auto next = std::find_if(
        ordered.begin() + static_cast<std::ptrdiff_t>(cursor + 1),
        ordered.end(),
        [](const auto& item) { return is_glossary_control(*item.segment); });
    const auto next_index = static_cast<std::size_t>(next - ordered.begin());
    const auto raw = range_text(*boundary.record, boundary.segment->payload_range);
    const auto marker_source = source_slice(
        *boundary.record, boundary.segment->segment_index,
        boundary.segment->complete);

    std::vector<GlossaryDefinitionRowIR> content_rows;
    std::vector<DocumentSourceSliceIR> structural_sources;
    for (auto index = cursor + 1; index < next_index; ++index) {
      const auto& content = ordered[index];
      const auto opcode = lower(content.segment->opcode);
      if (content.segment->kind == BookControlKind::font ||
          content.segment->kind == BookControlKind::text) {
        const auto found = rows.find(
            {content.record->logical_record, content.segment->segment_index});
        if (found != rows.end())
          content_rows.insert(content_rows.end(), found->second.begin(),
                              found->second.end());
        continue;
      }
      if (is_nested_start(opcode, "fig")) {
        ++figure_depth;
      } else if (is_nested_end(opcode, "fig")) {
        if (--figure_depth < 0) return reject("unbalanced glossary figure end");
      } else if (is_nested_start(opcode, "tbl")) {
        ++table_depth;
      } else if (is_nested_end(opcode, "tbl")) {
        if (--table_depth < 0) return reject("unbalanced glossary table end");
      } else {
        return reject("unsupported control appears inside glossary catalog");
      }
      structural_sources.push_back(source_slice(
          *content.record, content.segment->segment_index,
          content.segment->complete));
    }

    if (trim_ascii(raw).empty()) {
      if (next == ordered.end()) {
        if (!content_rows.empty() || !structural_sources.empty())
          return reject("terminal SRGLS has trailing catalog content");
        result.terminal_source = marker_source;
        cursor = ordered.size();
        continue;
      }
      std::string label;
      for (const auto& row : content_rows) label += trim_ascii(row.visible_text);
      if (label.size() != 1 ||
          std::isupper(static_cast<unsigned char>(label.front())) == 0 ||
          !structural_sources.empty())
        return reject("empty SRGLS is not a single-letter section marker");
      result.sections.push_back(
          {std::move(label), marker_source, std::move(content_rows)});
    } else {
      if (content_rows.empty())
        return reject("glossary term has no definition rows");
      const auto term = semantic_term(raw, content_rows);
      if (!term)
        return reject("SRGLS term '" + trim_ascii(raw) +
                      "' does not match definition lead '" +
                      trim_ascii(content_rows.front().visible_text) + "'");
      GlossaryEntryIR entry;
      entry.term = *term;
      entry.raw_term = raw;
      const auto trimmed_raw = trim_ascii(raw);
      if (trimmed_raw.size() > entry.term.size())
        entry.source_suffix = trimmed_raw.substr(entry.term.size());
      entry.term_source = source_slice(*boundary.record,
                                       boundary.segment->segment_index,
                                       boundary.segment->payload_range);
      if (!has_source(entry.term_source))
        return reject("glossary term source provenance is incomplete");
      entry.definition.rows = std::move(content_rows);
      entry.definition.structural_sources = std::move(structural_sources);
      result.entries.push_back(std::move(entry));
    }
    cursor = next_index;
  }

  if (figure_depth != 0 || table_depth != 0)
    return reject("glossary embedded object envelope is unbalanced");
  if (result.entries.empty() || result.sections.empty() ||
      !has_source(result.terminal_source))
    return reject("glossary catalog semantic envelope is incomplete");
  if (error != nullptr) error->clear();
  return result;
}

bool verify_glossary_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const GlossaryCatalogIR& catalog, std::string* error) {
  if (!verify_glossary_introduction_ir(records, layout, ownership,
                                       catalog.introduction, error))
    return false;
  const auto canonical =
      extract_glossary_catalog_ir(records, layout, ownership, error);
  if (!canonical) return false;
  if (!same_catalog(*canonical, catalog))
    return fail(error, "glossary catalog differs from canonical extraction");
  if (error != nullptr) error->clear();
  return true;
}

std::string format_glossary_catalog_ir(const GlossaryCatalogIR& catalog) {
  std::ostringstream out;
  out << "glossary_catalog records=[" << catalog.first_logical_record << ','
      << catalog.end_logical_record << ") heading=" << catalog.heading_level
      << " sections=" << catalog.sections.size()
      << " entries=" << catalog.entries.size()
      << " terminal=" << slice_projection(catalog.terminal_source)
      << " segments=" << catalog.segments.size() << '\n';
  out << format_glossary_introduction_ir(catalog.introduction);
  for (const auto& section : catalog.sections) {
    out << "section label='" << section.label << "' source="
        << slice_projection(section.marker_source) << " rows=";
    for (const auto& row : section.label_rows)
      out << slice_projection(row.source) << ',';
    out << '\n';
  }
  for (const auto& entry : catalog.entries) {
    out << "entry term='" << entry.term << "' raw='" << entry.raw_term
        << "' suffix='" << entry.source_suffix << "' source="
        << slice_projection(entry.term_source) << " rows="
        << entry.definition.rows.size() << " structural=";
    for (const auto& source : entry.definition.structural_sources)
      out << slice_projection(source) << ',';
    out << '\n';
    for (const auto& row : entry.definition.rows) {
      out << " row source=" << slice_projection(row.source)
          << " display_run=" << row.source_row.display_run
          << " row=" << row.source_row.row_index
          << " origin=" << row.native_origin
          << " break=" << static_cast<int>(row.break_before)
          << " marker='" << (row.marker ? row.marker->decoded_text : "")
          << "' text='" << row.visible_text << "' cells=";
      for (const auto& cell : row.cells)
        out << cell.logical_record << ':' << cell.token_index << ':'
            << cell.word_index << ':' << cell.word << ':'
            << static_cast<int>(cell.disposition) << ',';
      out << '\n';
    }
  }
  for (const auto& segment : catalog.segments)
    out << "segment kind=" << static_cast<int>(segment.kind)
        << " opcode=" << segment.opcode << " malformed=" << segment.malformed
        << " source=" << slice_projection(segment.source) << '\n';
  return out.str();
}

} // namespace geist::detail
