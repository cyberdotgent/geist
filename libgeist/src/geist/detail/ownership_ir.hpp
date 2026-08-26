#pragma once

#include "geist/detail/layout_ir.hpp"

#include <cstddef>
#include <cstdint>
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

struct OwnershipIR {
  std::vector<OwnedSourceCellIR> cells;
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

} // namespace geist::detail
