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
};

std::optional<TrapSelectorSource> read_trap_selector_source(
    const DecodedLogicalRecordSource& record, const ControlSegmentIR& segment,
    std::string* error);

std::optional<TrapTextLinkIR> project_trap_selector_link(
    const TrapSelectorSource& selector,
    const std::string& text, const TrapGlyphOffsets& glyph_offsets,
    DocumentSourceSliceIR source, std::string* error);

} // namespace geist::detail
