// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/ir/figure_block_ir.hpp"
#include <map>
#include <utility>

namespace geist::detail {

// Inclusive token ranges already bounded by the region recognizer. The
// visible marker is distinct from a BUILD artwork-description tail.
struct PictureFigureSourceIR {
  std::map<std::uint32_t, std::pair<std::size_t, std::size_t>> tokens;
  std::vector<FigureSegmentRefIR> segments;
  FigureSegmentRefIR marker;
};

// Reads positioned display cells, never LayoutIR's prose row splitting.
// Resource identity and description/anchor association have been checked by
// the region recognizer. All content and all claims are decided here once.
bool extract_picture_figure_content_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const SelectorCatalogIR &selectors, const PictureFigureSourceIR &source,
    FigureSourceBlockIR &figure, std::string *error);

bool same_figure_content_ir(const std::vector<FigureContentPartIR> &left,
                            const std::vector<FigureContentPartIR> &right);

} // namespace geist::detail
