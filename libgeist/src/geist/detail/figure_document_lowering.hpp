#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/figure_block_ir.hpp"

#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

// Lowers one verified figure block to output-neutral document blocks: an
// AnchorBlockIR for the SRFIG anchor (when the figure is anchored) followed
// by a FigureBlockIR carrying the image target and the caption text.  The
// suppressed placeholder rows and box cells are carried as provenance of the
// figure block, so the composer can prove they were consumed rather than
// dropped.  No BOO control text is interpreted at this boundary.
std::optional<std::vector<BlockIR>>
lower_figure_block_to_document_blocks(const FigureSourceBlockIR &figure,
                                      std::string *error = nullptr);

// Re-lowers the canonical blocks and rejects any structural or provenance
// difference, including edits to the anchor id, target, caption, or origins.
bool verify_figure_document_blocks(const FigureSourceBlockIR &figure,
                                   const std::vector<BlockIR> &blocks,
                                   std::string *error = nullptr);

} // namespace geist::detail
