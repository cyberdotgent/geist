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

const char* conflict_kind_name(OwnershipConflictKind kind) {
  switch (kind) {
  case OwnershipConflictKind::incompatible_disposition:
    return "incompatible_disposition";
  case OwnershipConflictKind::duplicate_row_assignment:
    return "duplicate_row_assignment";
  case OwnershipConflictKind::missing_source_cell:
    return "missing_source_cell";
  case OwnershipConflictKind::no_display_column:
    return "no_display_column";
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

bool conflicts_equal(const OwnershipRunConflictIR& left,
                     const OwnershipRunConflictIR& right) {
  return left.run == right.run && left.row_index == right.row_index &&
         left.logical_record == right.logical_record &&
         left.token_index == right.token_index &&
         left.word_index == right.word_index && left.word == right.word &&
         left.kind == right.kind && left.existing == right.existing &&
         left.requested == right.requested;
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

  // Classify control opcode/operand cells through the assembled output map.
  // These cells carry no run; a later disagreement with a row is a run-scoped
  // conflict, never a silent overwrite.
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
      if (!structural) continue;
      const auto found = cells.find(
          {record.logical_record, source.token_index, source.word_index});
      if (found == cells.end()) {
        result.conflicts.push_back("ownership references a missing source cell");
        continue;
      }
      auto& cell = result.cells[found->second];
      if (cell.disposition == SourceDisposition::opaque)
        cell.disposition = SourceDisposition::control_operand;
    }
  }

  // Each display run is owned atomically: its row assignments are staged and
  // applied only when every cell in the run is free and compatible. The first
  // disagreement records a typed conflict for that run, leaves the run
  // unowned, and lets every other run keep its ownership.
  struct Staged {
    std::size_t cell = 0;
    SourceDisposition disposition = SourceDisposition::opaque;
    std::size_t row = 0;
  };
  for (const auto& run : layout.runs) {
    std::vector<Staged> staged;
    std::set<std::size_t> staged_cells;
    std::optional<OwnershipRunConflictIR> conflict;
    const auto stage = [&](const PhysicalRowIR& row, std::size_t row_index,
                           std::size_t token, std::size_t word,
                           SourceDisposition disposition) {
      if (conflict) return;
      OwnershipRunConflictIR candidate;
      candidate.run = run.id;
      candidate.row_index = row_index;
      candidate.logical_record = row.logical_record;
      candidate.token_index = token;
      candidate.word_index = word;
      candidate.requested = disposition;
      const auto found = cells.find({row.logical_record, token, word});
      if (found == cells.end()) {
        candidate.kind = OwnershipConflictKind::missing_source_cell;
        conflict = candidate;
        return;
      }
      const auto& cell = result.cells[found->second];
      candidate.word = cell.word;
      candidate.existing = cell.disposition;
      if (cell.disposition != SourceDisposition::opaque &&
          cell.disposition != disposition) {
        candidate.kind = OwnershipConflictKind::incompatible_disposition;
        conflict = candidate;
        return;
      }
      if (cell.run != 0 || !staged_cells.insert(found->second).second) {
        candidate.kind = OwnershipConflictKind::duplicate_row_assignment;
        conflict = candidate;
        return;
      }
      staged.push_back({found->second, disposition, row_index});
    };
    for (std::size_t row_index = 0;
         row_index < run.rows.size() && !conflict; ++row_index) {
      const auto& row = run.rows[row_index];
      const auto source = std::find_if(
          records.begin(), records.end(), [&](const auto& record) {
            return record.logical_record == row.logical_record;
          });
      if (source == records.end()) {
        result.conflicts.push_back("physical row has no source record");
        break;
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
          stage(row, row_index, token, word, disposition);
        }
      }
    }
    // Project the staged cells into display coordinates without interpreting
    // any marker spelling. Decoder-inserted spaces advance the column but have
    // no source cell of their own, so gaps in this ledger are intentional.
    std::vector<PositionedRowCellIR> positioned_run;
    if (!conflict) {
      for (std::size_t row_index = 0;
           row_index < run.rows.size() && !conflict; ++row_index) {
        const auto& row = run.rows[row_index];
        const auto source = std::find_if(
            records.begin(), records.end(), [&](const auto& record) {
              return record.logical_record == row.logical_record;
            });
        if (source == records.end()) break;
        const auto origin_token = row.marker ? row.token_begin + 1
                                             : row.token_begin;
        const auto columns = row_display_columns(*source, row, origin_token);
        for (const auto& entry : staged) {
          if (entry.row != row_index) continue;
          const auto& cell = result.cells[entry.cell];
          PositionedRowCellIR positioned;
          positioned.run = run.id;
          positioned.row_index = row_index;
          positioned.logical_record = cell.logical_record;
          positioned.token_index = cell.token_index;
          positioned.word_index = cell.word_index;
          positioned.word = cell.word;
          positioned.role = row_role(entry.disposition);
          if (positioned.role != RowCellRole::boundary) {
            const auto found = columns.find(
                {cell.logical_record, cell.token_index, cell.word_index});
            if (found == columns.end()) {
              OwnershipRunConflictIR candidate;
              candidate.run = run.id;
              candidate.row_index = row_index;
              candidate.logical_record = cell.logical_record;
              candidate.token_index = cell.token_index;
              candidate.word_index = cell.word_index;
              candidate.word = cell.word;
              candidate.kind = OwnershipConflictKind::no_display_column;
              candidate.existing = cell.disposition;
              candidate.requested = entry.disposition;
              conflict = candidate;
              break;
            }
            positioned.display_column = found->second;
          }
          positioned_run.push_back(std::move(positioned));
        }
      }
    }
    if (conflict) {
      result.run_conflicts.push_back(*conflict);
      continue;
    }
    for (const auto& entry : staged) {
      auto& cell = result.cells[entry.cell];
      cell.disposition = entry.disposition;
      cell.run = run.id;
      cell.row_index = entry.row;
    }
    result.row_cells.insert(result.row_cells.end(), positioned_run.begin(),
                            positioned_run.end());
  }
  return result;
}

bool ownership_run_conflicted(const OwnershipIR& ownership, DisplayRunId run) {
  return std::any_of(ownership.run_conflicts.begin(),
                     ownership.run_conflicts.end(),
                     [&](const auto& conflict) { return conflict.run == run; });
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
    if (cell.run != 0 && ownership_run_conflicted(ownership, cell.run))
      return fail("conflicted display run still owns a source cell");
    auto& counts = row_counts[{cell.run, cell.row_index}];
    if (cell.disposition == SourceDisposition::visible_content)
      ++counts.visible;
    if (cell.disposition == SourceDisposition::marker_slot)
      ++counts.markers;
  }
  for (const auto& conflict : ownership.run_conflicts) {
    if (std::none_of(layout.runs.begin(), layout.runs.end(),
                     [&](const auto& run) { return run.id == conflict.run; }))
      return fail("ownership conflict references a run outside the layout");
  }
  for (const auto& run : layout.runs) {
    if (ownership_run_conflicted(ownership, run.id)) continue;
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
  if (ownership.run_conflicts.size() != canonical.run_conflicts.size() ||
      !std::equal(ownership.run_conflicts.begin(),
                  ownership.run_conflicts.end(),
                  canonical.run_conflicts.begin(), conflicts_equal))
    return fail("run-scoped ownership conflicts differ from source geometry");
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
  for (const auto& conflict : ownership.run_conflicts)
    out << "run_conflict " << format_ownership_run_conflict_ir(conflict)
        << '\n';
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

std::string format_ownership_run_conflict_ir(
    const OwnershipRunConflictIR& conflict) {
  std::ostringstream out;
  out << "run=" << conflict.run << " row=" << conflict.row_index
      << " record=" << conflict.logical_record
      << " token=" << conflict.token_index << " word=" << conflict.word_index
      << " value=" << conflict.word
      << " kind=" << conflict_kind_name(conflict.kind)
      << " existing=" << disposition_name(conflict.existing)
      << " requested=" << disposition_name(conflict.requested);
  return out.str();
}

} // namespace geist::detail
