// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/font_span_ir.hpp"

#include <cstddef>
#include <vector>

namespace geist::detail {

// One styled run of a verbatim display row: `[begin, end)` are UTF-8 byte
// offsets into the row's projected text.
struct RowFontHighlightIR {
  std::size_t begin = 0;
  std::size_t end = 0;
  FontStyleIR style = FontStyleIR::unknown;
};

// Maps a `CFONT` that stands alone on a markup line onto the drawn row
// directly below it.  A CFONT column is an index into that row's own display
// cells (doc/boo-spec/markup.adoc, "Spans And The Display Row"); `cells` are
// the row's cells and `offsets` the UTF-8 byte offset of every cell in the
// row's text (one more entry than `cells`, the text's end).
//
// A span is admitted only when the cells prove it: it lies inside the row,
// opens and closes on drawn glyphs, and each end meets a blank, the row's
// edge or an abutting span -- the shape that keeps `cfont 3 4 2` from mapping
// as `| Us`.  One unproven span leaves the whole row plain (an empty
// result): the text is right either way, and a style the cells do not
// support would be a claim the source never made.  A zero-length span styles
// nothing and is skipped.
std::vector<RowFontHighlightIR>
display_row_font_highlights(const std::vector<DisplayLineCellIR>& cells,
                            const std::vector<std::size_t>& offsets,
                            const FontControlSpansIR& font);

} // namespace geist::detail
