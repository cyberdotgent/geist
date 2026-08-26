#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace geist::detail {

struct FixedDisplaySourceColumn {
  std::size_t fragment = 0;
  std::size_t column = 0;
};

// A physical display row assembled without discarding source-column
// coordinates. Structural marker fields are blanked in place, so CFONT and
// CSELECT offsets continue to address the original columns.
struct FixedDisplayRow {
  std::string text;
  std::vector<FixedDisplaySourceColumn> source_columns;
};

// A source fragment whose display position was established before logical
// record assembly flattened the fixed-width stream.  These objects are meant
// to be short lived: a topic renderer can derive them for the selected topic,
// reconstruct its physical rows, and discard them without adding an eager
// per-book layout cache.
struct FixedDisplayFragment {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t source_word_begin = 0;
  std::size_t display_column = 0;
  std::string text;
  bool starts_row = false;
};

struct FixedDisplayWordSource {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
};

struct ReconstructedFixedDisplayRow {
  std::string text;
  // One entry per byte in text. Padding introduced to reach a fragment's
  // display column has no source and is marked false in source_present.
  std::vector<FixedDisplayWordSource> sources;
  std::vector<bool> source_present;
};

enum class FixedFormPhysicalRowKind {
  row_start,
  continuation,
  border,
  spacer,
};

// A form row after source-aware reconstruction has removed structural marker
// slots and sliced the surviving grid at its discovered separator offsets.
// The kind is derived from the source slot that owned the physical row, not
// from the visible cell text.
struct FixedFormPhysicalRow {
  FixedFormPhysicalRowKind kind = FixedFormPhysicalRowKind::spacer;
  std::vector<std::string> cells;
};

FixedDisplayRow assemble_fixed_display_row(
    const std::vector<std::string>& fragments);
void blank_fixed_display_marker_fields(FixedDisplayRow& row,
                                       bool allow_adjacent = false,
                                       const std::vector<bool>&
                                           protected_columns = {});

// Compose provenance-backed fragments into physical display rows. A fragment
// can start a row in a later logical record; otherwise it remains owned by the
// current row. This deliberately performs no heuristic boundary detection.
// The decoder/provenance layer establishes starts_row while source fragments
// are still distinguishable, and layout consumers only compose coordinates.
std::vector<ReconstructedFixedDisplayRow> reconstruct_fixed_display_rows(
    const std::vector<FixedDisplayFragment>& fragments);

// Fold physical form lines into logical rows. Continuations retain their
// columns, including simultaneous field/value continuations. Borders flush a
// pending row and spacers have no semantic ownership.
std::vector<std::vector<std::string>> aggregate_fixed_form_rows(
    const std::vector<FixedFormPhysicalRow>& physical_rows,
    std::size_t column_count);

} // namespace geist::detail
