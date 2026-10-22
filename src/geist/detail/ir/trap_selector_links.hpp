// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/ir/trap_catalog_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"

#include <map>
#include <utility>

namespace geist::detail {

// Exact source token/word to projected UTF-8 byte coordinate, emitted by the
// payload projector. Never reconstructed by searching the rendered text.
using TrapGlyphOffsets = std::map<std::pair<std::size_t, std::size_t>, std::size_t>;

struct TrapSelectorSource {
  std::string target;
  std::vector<DisplayLineCellIR> cells;
  // The length byte of the native row the selector marks.
  std::size_t row_prefix_token = 0;
  // Set when further selector or `cfont` control lines stand between this
  // selector and its row: the selector waits for the first row drawn after
  // them.
  bool queued = false;
  // The logical record that draws the marked row: the selector's own, or
  // the next one where the selector closes its record.
  std::uint32_t target_record = 0;
};

// `next_record`, when given, is the logical record after `record`: a
// selector on the last display line of its record then marks the first
// drawn line of the next record. Without it such a selector declines.
std::optional<TrapSelectorSource> read_trap_selector_source(
    const DecodedLogicalRecordSource& record, const ControlSegmentIR& segment,
    std::string* error,
    const DecodedLogicalRecordSource* next_record = nullptr);

std::optional<TrapTextLinkIR> project_trap_selector_link(
    const TrapSelectorSource& selector,
    const std::string& text, const TrapGlyphOffsets& glyph_offsets,
    DocumentSourceSliceIR source, std::string* error);

} // namespace geist::detail
