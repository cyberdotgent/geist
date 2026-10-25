// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/core/internal.hpp"
#include "geist/detail/lowering/document_ir.hpp"

#include <vector>

namespace geist::detail {

// Gives the body text of a typed family's document the revision marks its
// source rows store.
//
// A revised body row stores its mark in cell 1 of its display line
// (`DisplayLineIR::margin_mark`, `MarginMarkPlaceIR::row_margin`), and
// hosted BookServer prints it in the row's margin whatever the topic's
// family (doc/boo-spec/markup.adoc, "Revision marks as stored").  The prose
// family carries the mark row by row while it builds its runs; the other
// families lower whole rows into inlines whose origins name the source
// tokens they came from.  This pass reads those origins: an inline whose
// drawn tokens all stand on rows revised under one mark is revised under
// that mark, an inline whose tokens stand only on unrevised rows is not, and
// an inline that mixes the two -- or names no drawn token -- decides
// nothing.  Each maximal run of inlines under one mark becomes a
// `RevisedInlineIR`; a list item whose whole content is one such run carries
// the mark on the item instead.
//
// Only rows count: a display line whose cell 0 is not blank is a control or
// title line, which has no margin.  A sequence that already holds a revised
// run (the family carried its marks itself) is left alone, as are headings
// (`HeadingBlockIR::revision_mark`), preformatted rows (the mark is drawn in
// the row), figures, menus and index groups.
void carry_row_revision_marks(
    DocumentIR &document,
    const std::vector<DecodedLogicalRecordSource> &sources);

}  // namespace geist::detail
