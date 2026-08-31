#include "geist/detail/ownership_ir.hpp"

#include "geist/detail/display_lines.hpp"
#include "geist/detail/internal.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace geist::detail {
namespace {

using CellKey = std::tuple<std::uint32_t, std::size_t, std::size_t>;

// Ordered containers keyed by CellKey dominated ownership construction: every
// lookup walked a red-black tree and compared a three-field tuple at each
// node. The keys are only ever inserted and probed, never iterated in order,
// so hashing (or, better, direct indexing) is behaviour-preserving.
struct CellKeyHash {
  std::size_t operator()(const CellKey& key) const noexcept {
    auto mix = [](std::size_t seed, std::size_t value) {
      return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) +
                     (seed >> 2));
    };
    std::size_t seed = std::get<0>(key);
    seed = mix(seed, std::get<1>(key));
    return mix(seed, std::get<2>(key));
  }
};

struct RunRowKeyHash {
  std::size_t operator()(
      const std::pair<DisplayRunId, std::size_t>& key) const noexcept {
    const std::size_t seed = static_cast<std::size_t>(key.first);
    return seed ^ (key.second + 0x9e3779b97f4a7c15ULL + (seed << 6) +
                   (seed >> 2));
  }
};

constexpr std::size_t no_index = std::numeric_limits<std::size_t>::max();

// Source-cell lookup for the ownership ledger. build_ownership_ir appends one
// cell per source word in record/token/word order, so the ledger index of a
// cell is arithmetic once the per-token base offsets are known. Records with a
// repeated logical record number cannot be resolved that way, so those fall
// back to the exact ordered map the flat index replaces.
class SourceCellIndex {
public:
  explicit SourceCellIndex(
      const std::vector<DecodedLogicalRecordSource>& records)
      : records_(&records) {
    bases_.resize(records.size());
    std::size_t next = 0;
    for (std::size_t record = 0; record < records.size(); ++record) {
      if (!first_record_.emplace(records[record].logical_record, record).second)
        duplicated_ = true;
      const auto& tokens = records[record].tokens;
      bases_[record].resize(tokens.size());
      for (std::size_t token = 0; token < tokens.size(); ++token) {
        bases_[record][token] = next;
        next += tokens[token].size();
      }
    }
    if (!duplicated_) return;
    std::size_t index = 0;
    for (const auto& record : records) {
      for (std::size_t token = 0; token < record.tokens.size(); ++token) {
        for (std::size_t word = 0; word < record.tokens[token].size(); ++word) {
          fallback_.emplace(CellKey{record.logical_record, token, word},
                            index);
          ++index;
        }
      }
    }
  }

  // Ledger index of one source cell, or no_index when the records do not
  // contain it.
  std::size_t find(std::uint32_t logical_record, std::size_t token,
                   std::size_t word) const {
    if (duplicated_) {
      const auto found = fallback_.find(CellKey{logical_record, token, word});
      return found == fallback_.end() ? no_index : found->second;
    }
    const auto record = record_index(logical_record);
    if (record == no_index) return no_index;
    const auto& tokens = (*records_)[record].tokens;
    if (token >= tokens.size() || word >= tokens[token].size())
      return no_index;
    return bases_[record][token] + word;
  }

  // First record carrying this logical record number, matching the linear
  // search it replaces.
  const DecodedLogicalRecordSource* record(std::uint32_t logical_record) const {
    const auto index = record_index(logical_record);
    return index == no_index ? nullptr : &(*records_)[index];
  }

private:
  std::size_t record_index(std::uint32_t logical_record) const {
    const auto found = first_record_.find(logical_record);
    return found == first_record_.end() ? no_index : found->second;
  }

  const std::vector<DecodedLogicalRecordSource>* records_;
  std::unordered_map<std::uint32_t, std::size_t> first_record_;
  std::vector<std::vector<std::size_t>> bases_;
  bool duplicated_ = false;
  std::map<CellKey, std::size_t> fallback_;
};

bool structural_padding(const TokenWords& token) {
  return !token.empty() &&
         std::all_of(token.begin(), token.end(), [](const auto word) {
           return word == ' ' || word == '?' || word == 0x2666 || word < 0x20;
         });
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

const char* field_role_name(SourceFieldRole role) {
  switch (role) {
  case SourceFieldRole::undecided: return "undecided";
  case SourceFieldRole::positioned: return "positioned";
  case SourceFieldRole::supplemental: return "supplemental";
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

// Display columns for one row's source cells. The keys only ever span this
// record and the half-open token range [origin_token, row.token_end), so the
// ledger is a dense table rather than an ordered map: the lookups are the same
// first-writer-wins lookups, without the tuple comparisons.
class RowDisplayColumns {
public:
  RowDisplayColumns(const DecodedLogicalRecordSource& record,
                    const PhysicalRowIR& row, std::size_t origin_token)
      : logical_record_(record.logical_record), token_begin_(origin_token) {
    if (origin_token < row.token_end)
      columns_.resize(row.token_end - origin_token);
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
        auto& words = columns_[source.token_index - origin_token];
        if (words.size() <= source.word_index)
          words.resize(source.word_index + 1, no_index);
        // The ordered map this replaces kept the first entry per key.
        if (words[source.word_index] == no_index)
          words[source.word_index] = column;
      }
      ++column;
    }
  }

  std::size_t find(std::uint32_t logical_record, std::size_t token,
                   std::size_t word) const {
    if (logical_record != logical_record_ || token < token_begin_)
      return no_index;
    const auto slot = token - token_begin_;
    if (slot >= columns_.size() || word >= columns_[slot].size())
      return no_index;
    return columns_[slot][word];
  }

private:
  std::uint32_t logical_record_ = 0;
  std::size_t token_begin_ = 0;
  std::vector<std::vector<std::size_t>> columns_;
};

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
  const SourceCellIndex cells(records);
  std::size_t total_cells = 0;
  for (const auto& record : records)
    for (const auto& token : record.tokens) total_cells += token.size();
  result.cells.reserve(total_cells);
  for (const auto& record : records) {
    for (std::size_t token = 0; token < record.tokens.size(); ++token) {
      for (std::size_t word = 0; word < record.tokens[token].size(); ++word) {
        auto disposition = SourceDisposition::opaque;
        if (word == 0 && record.tokens[token][word] < 4)
          disposition = SourceDisposition::control_operand;
        result.cells.push_back({record.logical_record, token, word,
                                record.tokens[token][word], disposition, 0, 0,
                                source_field_role(record, token)});
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
          token_word_ascii_width(record.assembled.words[output]);
    }
    // A word is structural when it meets any segment's opcode or operand
    // range, so scanning every segment per word is scanning their union.
    // Merging the ranges once keeps the same answer at a single cursor walk:
    // the byte offsets increase with the output index.
    std::vector<std::pair<std::size_t, std::size_t>> structural_ranges;
    structural_ranges.reserve(record.control_segments.size() * 2);
    for (const auto& segment : record.control_segments) {
      for (const auto& range : {segment.opcode_range, segment.operand_range}) {
        if (range.begin < range.end)
          structural_ranges.emplace_back(range.begin, range.end);
      }
    }
    std::sort(structural_ranges.begin(), structural_ranges.end());
    std::size_t merged = 0;
    for (const auto& range : structural_ranges) {
      if (merged != 0 && range.first <= structural_ranges[merged - 1].second) {
        structural_ranges[merged - 1].second =
            std::max(structural_ranges[merged - 1].second, range.second);
        continue;
      }
      structural_ranges[merged++] = range;
    }
    structural_ranges.resize(merged);
    std::size_t cursor = 0;
    for (std::size_t output = 0; output < record.assembled.sources.size();
         ++output) {
      const auto& source = record.assembled.sources[output];
      if (source.kind != LogicalWordSourceKind::token_word) continue;
      const auto begin = byte_offsets[output];
      const auto end = byte_offsets[output + 1];
      while (cursor < structural_ranges.size() &&
             structural_ranges[cursor].second <= begin)
        ++cursor;
      const auto structural = cursor < structural_ranges.size() &&
                              structural_ranges[cursor].first < end;
      if (!structural) continue;
      const auto found =
          cells.find(record.logical_record, source.token_index,
                     source.word_index);
      if (found == no_index) {
        result.conflicts.push_back("ownership references a missing source cell");
        continue;
      }
      auto& cell = result.cells[found];
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
  // One stamp per ledger cell replaces a per-run ordered set of staged cell
  // indices: a cell is already staged for this run when its stamp matches the
  // run's generation.
  std::vector<std::size_t> staged_stamp(result.cells.size(), 0);
  std::size_t generation = 0;
  for (const auto& run : layout.runs) {
    std::vector<Staged> staged;
    ++generation;
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
      const auto found = cells.find(row.logical_record, token, word);
      if (found == no_index) {
        candidate.kind = OwnershipConflictKind::missing_source_cell;
        conflict = candidate;
        return;
      }
      const auto& cell = result.cells[found];
      candidate.word = cell.word;
      candidate.existing = cell.disposition;
      if (cell.disposition != SourceDisposition::opaque &&
          cell.disposition != disposition) {
        candidate.kind = OwnershipConflictKind::incompatible_disposition;
        conflict = candidate;
        return;
      }
      const auto already_staged = staged_stamp[found] == generation;
      if (cell.run != 0 || already_staged) {
        candidate.kind = OwnershipConflictKind::duplicate_row_assignment;
        conflict = candidate;
        return;
      }
      staged_stamp[found] = generation;
      staged.push_back({found, disposition, row_index});
    };
    for (std::size_t row_index = 0;
         row_index < run.rows.size() && !conflict; ++row_index) {
      const auto& row = run.rows[row_index];
      const auto* source = cells.record(row.logical_record);
      if (source == nullptr) {
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
    positioned_run.reserve(staged.size());
    // Staged entries are appended in row order, so each row's entries are one
    // contiguous stretch: a cursor replaces rescanning every staged entry per
    // row.
    std::size_t staged_cursor = 0;
    if (!conflict) {
      for (std::size_t row_index = 0;
           row_index < run.rows.size() && !conflict; ++row_index) {
        const auto& row = run.rows[row_index];
        const auto* source = cells.record(row.logical_record);
        if (source == nullptr) break;
        const auto origin_token = row.marker ? row.token_begin + 1
                                             : row.token_begin;
        const RowDisplayColumns columns(*source, row, origin_token);
        while (staged_cursor < staged.size() &&
               staged[staged_cursor].row < row_index)
          ++staged_cursor;
        for (std::size_t index = staged_cursor;
             index < staged.size() && staged[index].row == row_index;
             ++index) {
          const auto& entry = staged[index];
          const auto& cell = result.cells[entry.cell];
          PositionedRowCellIR positioned;
          positioned.run = run.id;
          positioned.row_index = row_index;
          positioned.logical_record = cell.logical_record;
          positioned.token_index = cell.token_index;
          positioned.word_index = cell.word_index;
          positioned.word = cell.word;
          positioned.role = row_role(entry.disposition);
          positioned.field_role = cell.field_role;
          if (positioned.role != RowCellRole::boundary) {
            const auto found = columns.find(
                cell.logical_record, cell.token_index, cell.word_index);
            if (found == no_index) {
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
            positioned.display_column = found;
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

SourceFieldRole source_field_role(const DecodedLogicalRecordSource& record,
                                  std::size_t token) {
  if (!record_framing_is_decided(record.ir) ||
      token >= record.ir.tokens.size())
    return SourceFieldRole::undecided;
  switch (record.ir.tokens[token].framing) {
  case TokenFramingRole::line_length:
    return SourceFieldRole::supplemental;
  case TokenFramingRole::line_content:
    return SourceFieldRole::positioned;
  case TokenFramingRole::unframed:
    break;
  }
  return SourceFieldRole::undecided;
}

bool ownership_run_conflicted(const OwnershipIR& ownership, DisplayRunId run) {
  return std::any_of(ownership.run_conflicts.begin(),
                     ownership.run_conflicts.end(),
                     [&](const auto& conflict) { return conflict.run == run; });
}

namespace {

// The shared body of verification. `canonical` is the ledger the source
// geometry mechanically implies; passing it in lets a freshly built ledger be
// verified without building the same ledger a second time. build_ownership_ir
// is a pure function of (records, layout), so for a ledger that was just built
// from those inputs the rebuild is the identical value by construction and
// every comparison against it is the one it would have made anyway.
bool verify_ownership_ir_against(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const OwnershipIR& canonical, std::string* error) {
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
  struct RowCounts {
    std::size_t visible = 0;
    std::size_t markers = 0;
  };
  std::unordered_map<std::pair<DisplayRunId, std::size_t>, RowCounts,
                     RunRowKeyHash>
      row_counts;
  std::unordered_map<CellKey, const OwnedSourceCellIR*, CellKeyHash>
      owned_cells;
  owned_cells.reserve(ownership.cells.size());
  std::unordered_set<DisplayRunId> conflicted_runs;
  for (const auto& conflict : ownership.run_conflicts)
    conflicted_runs.insert(conflict.run);
  for (const auto& cell : ownership.cells) {
    // One index serves both roles: the ledger is duplicate-free exactly when
    // every cell key is inserted for the first time.
    if (!owned_cells
             .emplace(CellKey{cell.logical_record, cell.token_index,
                              cell.word_index},
                      &cell)
             .second)
      return fail("ownership ledger contains duplicate source cells");
    if (cell.run != 0 && conflicted_runs.count(cell.run) != 0)
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
    if (conflicted_runs.count(run.id) != 0) continue;
    for (std::size_t row = 0; row < run.rows.size(); ++row) {
      const auto found = row_counts.find({run.id, row});
      if (found == row_counts.end() || found->second.visible == 0)
        return fail("physical row owns no visible source cell");
      const auto& physical = run.rows[row];
      if (physical.marker && found->second.markers == 0)
        return fail("marker row owns no marker source cell");
    }
  }
  if (!canonical.conflicts.empty())
    return fail("could not reconstruct positioned row-cell ledger");
  if (ownership.run_conflicts.size() != canonical.run_conflicts.size() ||
      !std::equal(ownership.run_conflicts.begin(),
                  ownership.run_conflicts.end(),
                  canonical.run_conflicts.begin(), conflicts_equal))
    return fail("run-scoped ownership conflicts differ from source geometry");
  if (ownership.row_cells.size() != canonical.row_cells.size())
    return fail("positioned row-cell ledger does not conserve row ownership");
  std::unordered_set<CellKey, CellKeyHash> positioned_unique;
  positioned_unique.reserve(ownership.row_cells.size());
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
        cell.display_column != expected_cell.display_column ||
        cell.field_role != expected_cell.field_role)
      return fail("positioned row-cell ledger differs from source geometry");
    if ((cell.role == RowCellRole::boundary) ==
        cell.display_column.has_value())
      return fail("boundary/display cell has an invalid column state");
    const auto owned = owned_cells.find(
        {cell.logical_record, cell.token_index, cell.word_index});
    if (owned == owned_cells.end() || owned->second->run != cell.run ||
        owned->second->row_index != cell.row_index ||
        owned->second->word != cell.word ||
        owned->second->field_role != cell.field_role ||
        row_role(owned->second->disposition) != cell.role)
      return fail("positioned row cell does not match source ownership");
  }
  if (error != nullptr) error->clear();
  return true;
}

} // namespace

bool verify_ownership_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    std::string* error) {
  return verify_ownership_ir_against(records, layout, ownership,
                                     build_ownership_ir(records, layout),
                                     error);
}

namespace {

std::uint32_t record_number(
    const std::vector<DecodedLogicalRecordSource>& records, std::size_t index) {
  return index < records.size() ? records[index].logical_record : 0;
}

} // namespace

VerifiedOwnershipIR::VerifiedOwnershipIR(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, OwnershipIR ownership)
    : records_(&records),
      layout_(&layout),
      record_count_(records.size()),
      run_count_(layout.runs.size()),
      first_logical_record_(record_number(records, 0)),
      last_logical_record_(
          record_number(records, records.empty() ? 0 : records.size() - 1)),
      ownership_(std::move(ownership)) {}

bool VerifiedOwnershipIR::covers(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout) const {
  return &records == records_ && &layout == layout_ &&
         records.size() == record_count_ &&
         layout.runs.size() == run_count_ &&
         record_number(records, 0) == first_logical_record_ &&
         record_number(records, records.empty() ? 0 : records.size() - 1) ==
             last_logical_record_;
}

std::optional<VerifiedOwnershipIR> build_verified_ownership_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, std::string* error) {
  auto ownership = build_ownership_ir(records, layout);
  if (!verify_ownership_ir_against(records, layout, ownership, ownership,
                                   error))
    return std::nullopt;
  return VerifiedOwnershipIR(records, layout, std::move(ownership));
}


bool ownership_verified_for(
    const VerifiedOwnershipIR& ownership,
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, std::string* error) {
  if (ownership.covers(records, layout)) {
    if (error != nullptr) error->clear();
    return true;
  }
  return verify_ownership_ir(records, layout, ownership.ir(), error);
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
      << " disposition=" << disposition_name(cell.disposition)
      << " field=" << field_role_name(cell.field_role);
  if (cell.run != 0)
    out << " run=" << cell.run << " row=" << cell.row_index;
  return out.str();
}

std::string format_positioned_row_cell_ir(const PositionedRowCellIR& cell) {
  std::ostringstream out;
  out << "run=" << cell.run << " row=" << cell.row_index
      << " record=" << cell.logical_record << " token=" << cell.token_index
      << " word=" << cell.word_index << " value=" << cell.word
      << " role=" << role_name(cell.role)
      << " field=" << field_role_name(cell.field_role) << " column=";
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
