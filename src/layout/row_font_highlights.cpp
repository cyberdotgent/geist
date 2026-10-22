// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/layout/row_font_highlights.hpp"

#include <algorithm>

namespace geist::detail {
namespace {

bool drawn_glyph(const std::uint16_t word) { return word > ' '; }

} // namespace

// N2AH1MST record 533 line 17 is ` |          ACTIVE LU'S ...` under
// `cfont 12 6 2 19 4 2 ...`, and hosted BookServer (DT 19910329000100) serves
// `<B>ACTIVE</B> <B>LU'S</B>` at exactly those columns; line 21 splits one
// word as `cfont 12 6 2 18 8 V` -> `<B>[LLUN=</B><var>unitname</var>`.
std::vector<RowFontHighlightIR>
display_row_font_highlights(const std::vector<DisplayLineCellIR>& cells,
                            const std::vector<std::size_t>& offsets,
                            const FontControlSpansIR& font) {
  if (offsets.size() != cells.size() + 1) return {};
  std::vector<FontSpanIR> spans;
  for (const auto& span : font.spans)
    if (span.length != 0) spans.push_back(span);
  std::sort(spans.begin(), spans.end(),
            [](const auto& left, const auto& right) {
              return left.column < right.column;
            });
  std::vector<RowFontHighlightIR> highlights;
  for (std::size_t index = 0; index < spans.size(); ++index) {
    const auto& span = spans[index];
    const auto end = span.column + span.length;
    if (end > cells.size() || !drawn_glyph(cells[span.column].word) ||
        !drawn_glyph(cells[end - 1].word))
      return {};
    if (index != 0 &&
        spans[index - 1].column + spans[index - 1].length > span.column)
      return {};
    const bool abuts_before =
        index != 0 &&
        spans[index - 1].column + spans[index - 1].length == span.column;
    const bool abuts_after =
        index + 1 < spans.size() && spans[index + 1].column == end;
    if (span.column != 0 && drawn_glyph(cells[span.column - 1].word) &&
        !abuts_before)
      return {};
    if (end < cells.size() && drawn_glyph(cells[end].word) && !abuts_after)
      return {};
    highlights.push_back({offsets[span.column], offsets[end], span.style});
  }
  return highlights;
}

} // namespace geist::detail
