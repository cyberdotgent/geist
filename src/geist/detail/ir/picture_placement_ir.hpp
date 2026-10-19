// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "geist/detail/layout/display_lines.hpp"
#include <optional>

namespace geist::detail {
struct PicturePlacementSpanIR {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::string label;
};
bool picture_placeholder_matches(const std::string &label,
                                 const std::string &resource);
// Columns, not UTF-8 byte offsets. Padding is permitted only inside the
// selected interval; no neighbouring text is borrowed to make a match.
std::optional<PicturePlacementSpanIR>
picture_placement_span(const std::vector<DisplayLineCellIR> &cells,
                       std::size_t column, std::size_t length,
                       const std::string &resource);
} // namespace geist::detail
