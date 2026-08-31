#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/fixed_table_block_ir.hpp"

#include <string>
#include <vector>

namespace geist::detail {

// Lowers one verified fixed-table block into output-neutral document nodes:
// an AnchorBlockIR for the SRTBL object, a ParagraphBlockIR for the caption
// row when the box carries one, and a TableBlockIR whose cells keep every
// display line as text separated by hard breaks. Consumes only the typed
// block; never reparses flattened row text.
std::vector<BlockIR>
lower_fixed_table_block_to_document_ir(const FixedTableBlockIR &block);

// Requires an exact canonical lowering, including source slices and physical
// row evidence, and rejects any mutation of the lowered nodes.
bool verify_fixed_table_document_ir(const FixedTableBlockIR &block,
                                    const std::vector<BlockIR> &lowered,
                                    std::string *error = nullptr);

} // namespace geist::detail
