#include "geist/detail/ownership_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>

namespace geist::detail {
namespace {

using CellKey = std::tuple<std::uint32_t, std::size_t, std::size_t>;

bool structural_padding(const TokenWords& token) {
  return !token.empty() &&
         std::all_of(token.begin(), token.end(), [](const auto word) {
           return word == ' ' || word == '?' || word == 0x2666 || word < 0x20;
         });
}

bool intersects(const OutputRangeIR& range, std::size_t begin,
                std::size_t end) {
  return begin < range.end && range.begin < end;
}

const char* disposition_name(SourceDisposition disposition) {
  switch (disposition) {
  case SourceDisposition::control_operand: return "control_operand";
  case SourceDisposition::layout_origin: return "layout_origin";
  case SourceDisposition::layout_padding: return "layout_padding";
  case SourceDisposition::marker_slot: return "marker_slot";
  case SourceDisposition::visible_content: return "visible_content";
  case SourceDisposition::opaque: return "opaque";
  }
  return "invalid";
}

const char* role_name(RowCellRole role) {
  switch (role) {
  case RowCellRole::boundary: return "boundary";
  case RowCellRole::origin: return "origin";
  case RowCellRole::padding: return "padding";
  case RowCellRole::content: return "content";
  }
  return "invalid";
}

RowCellRole row_role(SourceDisposition disposition) {
  switch (disposition) {
  case SourceDisposition::marker_slot: return RowCellRole::boundary;
  case SourceDisposition::layout_origin: return RowCellRole::origin;
  case SourceDisposition::layout_padding: return RowCellRole::padding;
  case SourceDisposition::visible_content: return RowCellRole::content;
  default: return RowCellRole::content;
  }
}

std::map<CellKey, std::size_t> row_display_columns(
    const DecodedLogicalRecordSource& record, const PhysicalRowIR& row,
    std::size_t origin_token) {
  std::map<CellKey, std::size_t> columns;
  bool started = false;
  std::size_t column = 0;
  for (const auto& source : record.assembled.sources) {
    if (!started) {
      started = source.kind == LogicalWordSourceKind::token_word &&
                source.token_index == origin_token && source.word_index == 0;
      if (!started) continue;
    }
    if (source.kind == LogicalWordSourceKind::token_word &&
        source.token_index >= row.token_end)
      break;
    if (source.kind == LogicalWordSourceKind::token_word &&
        source.token_index >= origin_token &&
        source.token_index < row.token_end) {
      columns.emplace(CellKey{record.logical_record, source.token_index,
                              source.word_index},
                      column);
    }
    ++column;
  }
  return columns;
}

} // namespace

OwnershipIR build_ownership_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout) {
  OwnershipIR result;
  std::map<CellKey, std::size_t> cells;
  for (const auto& record : records) {
    for (std::size_t token = 0; token < record.tokens.size(); ++token) {
      for (std::size_t word = 0; word < record.tokens[token].size(); ++word) {
        auto disposition = SourceDisposition::opaque;
        if (word == 0 && record.tokens[token][word] < 4)
          disposition = SourceDisposition::control_operand;
        const auto index = result.cells.size();
        result.cells.push_back({record.logical_record, token, word,
                                record.tokens[token][word], disposition, 0, 0});
        cells.emplace(CellKey{record.logical_record, token, word}, index);
      }
    }
  }

  const auto assign = [&](std::uint32_t record, std::size_t token,
                          std::size_t word, SourceDisposition disposition,
                          DisplayRunId run = 0, std::size_t row = 0) {
    const auto found = cells.find({record, token, word});
    if (found == cells.end()) {
      result.conflicts.push_back("ownership references a missing source cell");
      return;
    }
    auto& cell = result.cells[found->second];
    if (cell.disposition != SourceDisposition::opaque &&
        cell.disposition != disposition) {
      result.conflicts.push_back(
          "source cell received incompatible ownership dispositions");
      return;
    }
    if (cell.run != 0 && (cell.run != run || cell.row_index != row)) {
      result.conflicts.push_back("source cell was assigned to multiple rows");
      return;
    }
    cell.disposition = disposition;
    cell.run = run;
    cell.row_index = row;
  };

  // Classify control opcode/operand cells through the assembled output map.
  for (const auto& record : records) {
    std::vector<std::size_t> byte_offsets(record.assembled.words.size() + 1);
    for (std::size_t output = 0; output < record.assembled.words.size();
         ++output) {
      byte_offsets[output + 1] =
          byte_offsets[output] +
          token_words_to_ascii({record.assembled.words[output]}).size();
    }
    for (std::size_t output = 0; output < record.assembled.sources.size();
         ++output) {
      const auto& source = record.assembled.sources[output];
      if (source.kind != LogicalWordSourceKind::token_word) continue;
      const auto structural = std::any_of(
          record.control_segments.begin(), record.control_segments.end(),
          [&](const auto& segment) {
            return intersects(segment.opcode_range, byte_offsets[output],
                              byte_offsets[output + 1]) ||
                   intersects(segment.operand_range, byte_offsets[output],
                              byte_offsets[output + 1]);
          });
      if (structural)
        assign(record.logical_record, source.token_index, source.word_index,
               SourceDisposition::control_operand);
    }
  }

  for (const auto& run : layout.runs) {
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto& row = run.rows[row_index];
      const auto source = std::find_if(
          records.begin(), records.end(), [&](const auto& record) {
            return record.logical_record == row.logical_record;
          });
      if (source == records.end()) {
        result.conflicts.push_back("physical row has no source record");
        continue;
      }
      const auto marker_token = row.marker ? row.marker->token_index
                                           : source->tokens.size();
      const auto origin_token = row.marker ? row.token_begin + 1
                                           : row.token_begin;
      for (std::size_t token = row.token_begin;
           token < row.token_end && token < source->tokens.size(); ++token) {
        for (std::size_t word = 0; word < source->tokens[token].size(); ++word) {
          if (word == 0 && source->tokens[token][word] < 4) continue;
          auto disposition = SourceDisposition::visible_content;
          if (token == marker_token)
            disposition = SourceDisposition::marker_slot;
          else if (token == origin_token)
            disposition = SourceDisposition::layout_origin;
          else if (structural_padding(source->tokens[token]))
            disposition = SourceDisposition::layout_padding;
          assign(row.logical_record, token, word, disposition, run.id,
                 row_index);
        }
      }
    }
  }

  // Project row-owned cells into display coordinates without interpreting any
  // marker spelling. Decoder-inserted spaces advance the column but have no
  // source cell of their own, so gaps in this ledger are intentional.
  for (const auto& run : layout.runs) {
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto& row = run.rows[row_index];
      const auto source = std::find_if(
          records.begin(), records.end(), [&](const auto& record) {
            return record.logical_record == row.logical_record;
          });
      if (source == records.end()) continue;
      const auto origin_token = row.marker ? row.token_begin + 1
                                           : row.token_begin;
      const auto columns = row_display_columns(*source, row, origin_token);
      for (const auto& cell : result.cells) {
        if (cell.run != run.id || cell.row_index != row_index) continue;
        PositionedRowCellIR positioned;
        positioned.run = run.id;
        positioned.row_index = row_index;
        positioned.logical_record = cell.logical_record;
        positioned.token_index = cell.token_index;
        positioned.word_index = cell.word_index;
        positioned.word = cell.word;
        positioned.role = row_role(cell.disposition);
        if (positioned.role != RowCellRole::boundary) {
          const auto found = columns.find(
              {cell.logical_record, cell.token_index, cell.word_index});
          if (found == columns.end()) {
            result.conflicts.push_back(
                "row-owned source cell has no mechanical display column");
            continue;
          }
          positioned.display_column = found->second;
        }
        result.row_cells.push_back(std::move(positioned));
      }
    }
  }
  return result;
}

bool verify_ownership_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    std::string* error) {
  const auto fail = [&](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  if (!ownership.conflicts.empty()) return fail(ownership.conflicts.front());
  const auto expected = std::accumulate(
      records.begin(), records.end(), std::size_t{0}, [](auto total,
                                                         const auto& record) {
        for (const auto& token : record.tokens) total += token.size();
        return total;
      });
  if (ownership.cells.size() != expected)
    return fail("ownership ledger does not cover every source cell");
  std::set<CellKey> unique;
  struct RowCounts {
    std::size_t visible = 0;
    std::size_t markers = 0;
  };
  std::map<std::pair<DisplayRunId, std::size_t>, RowCounts> row_counts;
  std::map<CellKey, const OwnedSourceCellIR*> owned_cells;
  for (const auto& cell : ownership.cells) {
    if (!unique.emplace(cell.logical_record, cell.token_index, cell.word_index)
             .second)
      return fail("ownership ledger contains duplicate source cells");
    owned_cells.emplace(
        CellKey{cell.logical_record, cell.token_index, cell.word_index}, &cell);
    auto& counts = row_counts[{cell.run, cell.row_index}];
    if (cell.disposition == SourceDisposition::visible_content)
      ++counts.visible;
    if (cell.disposition == SourceDisposition::marker_slot)
      ++counts.markers;
  }
  for (const auto& run : layout.runs) {
    for (std::size_t row = 0; row < run.rows.size(); ++row) {
      const auto found = row_counts.find({run.id, row});
      if (found == row_counts.end() || found->second.visible == 0)
        return fail("physical row owns no visible source cell");
      const auto& physical = run.rows[row];
      if (physical.marker && found->second.markers == 0)
        return fail("marker row owns no marker source cell");
    }
  }
  const auto canonical = build_ownership_ir(records, layout);
  if (!canonical.conflicts.empty())
    return fail("could not reconstruct positioned row-cell ledger");
  if (ownership.row_cells.size() != canonical.row_cells.size())
    return fail("positioned row-cell ledger does not conserve row ownership");
  std::set<CellKey> positioned_unique;
  for (std::size_t index = 0; index < ownership.row_cells.size(); ++index) {
    const auto& cell = ownership.row_cells[index];
    const auto& expected_cell = canonical.row_cells[index];
    if (!positioned_unique
             .emplace(cell.logical_record, cell.token_index, cell.word_index)
             .second)
      return fail("positioned row-cell ledger contains duplicate source cells");
    if (cell.run != expected_cell.run ||
        cell.row_index != expected_cell.row_index ||
        cell.logical_record != expected_cell.logical_record ||
        cell.token_index != expected_cell.token_index ||
        cell.word_index != expected_cell.word_index ||
        cell.word != expected_cell.word || cell.role != expected_cell.role ||
        cell.display_column != expected_cell.display_column)
      return fail("positioned row-cell ledger differs from source geometry");
    if ((cell.role == RowCellRole::boundary) ==
        cell.display_column.has_value())
      return fail("boundary/display cell has an invalid column state");
    const auto owned = owned_cells.find(
        {cell.logical_record, cell.token_index, cell.word_index});
    if (owned == owned_cells.end() || owned->second->run != cell.run ||
        owned->second->row_index != cell.row_index ||
        owned->second->word != cell.word ||
        row_role(owned->second->disposition) != cell.role)
      return fail("positioned row cell does not match source ownership");
  }
  if (error != nullptr) error->clear();
  return true;
}

std::string format_ownership_ir(const OwnershipIR& ownership) {
  std::ostringstream out;
  for (const auto& cell : ownership.cells) {
    if (cell.run == 0 && cell.disposition == SourceDisposition::opaque) continue;
    out << format_owned_source_cell_ir(cell) << '\n';
  }
  for (const auto& cell : ownership.row_cells)
    out << "positioned " << format_positioned_row_cell_ir(cell) << '\n';
  for (const auto& conflict : ownership.conflicts)
    out << "conflict=" << conflict << '\n';
  return out.str();
}

std::string format_owned_source_cell_ir(const OwnedSourceCellIR& cell) {
  std::ostringstream out;
  out << "record=" << cell.logical_record << " token=" << cell.token_index
      << " word=" << cell.word_index << " value=" << cell.word
      << " disposition=" << disposition_name(cell.disposition);
  if (cell.run != 0)
    out << " run=" << cell.run << " row=" << cell.row_index;
  return out.str();
}

std::string format_positioned_row_cell_ir(const PositionedRowCellIR& cell) {
  std::ostringstream out;
  out << "run=" << cell.run << " row=" << cell.row_index
      << " record=" << cell.logical_record << " token=" << cell.token_index
      << " word=" << cell.word_index << " value=" << cell.word
      << " role=" << role_name(cell.role) << " column=";
  if (cell.display_column)
    out << *cell.display_column;
  else
    out << "none";
  return out.str();
}

} // namespace geist::detail
