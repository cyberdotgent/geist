#pragma once

#include "geist/detail/layout_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

enum class SourceDisposition {
  control_operand,
  layout_origin,
  layout_padding,
  marker_slot,
  visible_content,
  opaque,
};

struct OwnedSourceCellIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  std::uint16_t word = 0;
  SourceDisposition disposition = SourceDisposition::opaque;
  DisplayRunId run = 0;
  std::size_t row_index = 0;
};

enum class RowCellRole {
  boundary,
  origin,
  padding,
  content,
};

// A source cell positioned in one physical row. Boundary cells participate in
// the row's source geometry but deliberately have no display column: assigning
// one would prematurely interpret the marker as visible prefix/suffix text.
struct PositionedRowCellIR {
  DisplayRunId run = 0;
  std::size_t row_index = 0;
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  std::uint16_t word = 0;
  RowCellRole role = RowCellRole::content;
  std::optional<std::size_t> display_column;
};

struct OwnershipIR {
  std::vector<OwnedSourceCellIR> cells;
  std::vector<PositionedRowCellIR> row_cells;
  std::vector<std::string> conflicts;
};

OwnershipIR build_ownership_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout);
bool verify_ownership_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout,
    const OwnershipIR& ownership,
    std::string* error = nullptr);
std::string format_ownership_ir(const OwnershipIR& ownership);
std::string format_owned_source_cell_ir(const OwnedSourceCellIR& cell);
std::string format_positioned_row_cell_ir(const PositionedRowCellIR& cell);

} // namespace geist::detail
