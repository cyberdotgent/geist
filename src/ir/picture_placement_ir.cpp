// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/picture_placement_ir.hpp"
#include "geist/detail/ir/figure_block_ir.hpp"
#include <algorithm>

namespace geist::detail {
bool picture_placeholder_matches(const std::string &label,
                                 const std::string &resource) {
  return trim_ascii_whitespace(label) == figure_picture_placeholder(resource);
}
std::optional<PicturePlacementSpanIR>
picture_placement_span(const std::vector<DisplayLineCellIR> &cells,
                       std::size_t column, std::size_t length,
                       const std::string &resource) {
  if (column >= cells.size() || length == 0)
    return std::nullopt;
  PicturePlacementSpanIR span;
  span.begin = column;
  span.end = column + std::min(length, cells.size() - column);
  for (auto i = span.begin; i < span.end; ++i)
    span.label += figure_display_glyph(cells[i].word);
  if (!picture_placeholder_matches(span.label, resource))
    return std::nullopt;
  return span;
}
} // namespace geist::detail
