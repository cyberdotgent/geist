// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/ir/book_ir.hpp"
#include "geist/detail/container/control_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;

using DisplayRunId = std::uint64_t;

enum class PhysicalRowStartKind {
  control_payload,
  explicit_marker_slot,
  placeholder_wrap,
  record_continuation,
};

enum class PhysicalBreakKind {
  unknown,
  soft_wrap,
  hard_paragraph,
  hard_object,
};

struct MarkerSlotIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::uint16_t encoded_value = 0;
  std::uint8_t encoded_width = 0;
  SourceByteRange byte_range;
  std::string decoded_text;
};

struct PhysicalRowIR {
  DisplayRunId run = 0;
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  std::size_t token_begin = 0;
  std::size_t token_end = 0;
  std::size_t native_origin = 0;
  PhysicalRowStartKind start = PhysicalRowStartKind::control_payload;
  PhysicalBreakKind break_before = PhysicalBreakKind::unknown;
  std::optional<MarkerSlotIR> marker;
  bool continues_previous_record = false;
  std::string visible_text;
};

struct DisplayRunIR {
  DisplayRunId id = 0;
  BookControlKind control_kind = BookControlKind::text;
  std::vector<PhysicalRowIR> rows;
};

struct LayoutIR {
  std::vector<DisplayRunIR> runs;
};

// Extracts mechanical row candidates from typed control segments. Admission
// as a table, publication list, message list, or prose block is deliberately
// left to later semantic/conservation passes.
LayoutIR extract_layout_ir(
    const std::vector<DecodedLogicalRecordSource>& records);
bool verify_layout_ir(const std::vector<DecodedLogicalRecordSource>& records,
                      const LayoutIR& layout,
                      std::string* error = nullptr);
std::string format_physical_row_ir(const PhysicalRowIR& row);

} // namespace geist::detail
