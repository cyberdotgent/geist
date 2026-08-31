// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/ir/message_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace geist::detail {

struct MessageStructuredSourceCellIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  std::uint16_t word = 0;
  RowCellRole role = RowCellRole::content;
  std::optional<std::size_t> display_column;
};

struct MessageStructuredCellIR {
  std::string text;
  std::size_t column = 0;
  std::vector<MessageSourceRowIR> source_rows;
  std::vector<MessageStructuredSourceCellIR> source_cells;
};

struct MessageStructuredTableRowIR {
  std::vector<MessageStructuredCellIR> cells;
  std::vector<MessageStructuredSourceCellIR> structural_cells;
};

struct MessageStructuredPreformattedLineIR {
  std::string text;
  MessageSourceRowIR source_row;
  std::vector<MessageStructuredSourceCellIR> source_cells;
};

// A fixed-field listing inside a message section.  The recovered columns are
// kept for consumers and provenance and own every source cell of the region,
// but the block renders `lines`: hosted BookServer serves the listing as
// plain preformatted lines inside the topic's `<pre>`, one per display row
// with each wrapped continuation on its own line, and emits no HTML `<table>`
// (SC31-711 5.0, DT 19941010174546).
struct MessageStructuredTableBlockIR {
  MessageStructuredTableRowIR header;
  std::vector<MessageStructuredTableRowIR> rows;
  // The region as the reader prints it, in source order.  These lines carry
  // no claim of their own: the rows above already own every cell.
  std::vector<MessageStructuredPreformattedLineIR> lines;
};

struct MessageStructuredListItemIR {
  std::string text;
  std::vector<MessageSourceRowIR> source_rows;
  std::vector<MessageStructuredSourceCellIR> source_cells;
  std::vector<MessageStructuredSourceCellIR> structural_cells;
};

struct MessageStructuredListBlockIR {
  MessageStructuredCellIR lead_in;
  std::vector<MessageStructuredListItemIR> items;
};

struct MessageStructuredPreformattedBlockIR {
  std::vector<MessageStructuredPreformattedLineIR> lines;
  // False means this block is deliberately not eligible for semantic
  // promotion: decoded source exists outside positioned row-cell ownership.
  bool provenance_complete = true;
  std::string fallback_reason;
};

using MessageStructuredBlockNodeIR =
    std::variant<MessageStructuredTableBlockIR, MessageStructuredListBlockIR,
                 MessageStructuredPreformattedBlockIR>;

struct MessageSectionBlockIR {
  std::size_t entry_index = 0;
  std::size_t section_index = 0;
  MessageStructuredBlockNodeIR node;
};

struct MessageSectionBlocksIR {
  std::vector<MessageSectionBlockIR> blocks;
};

MessageSectionBlocksIR
extract_message_section_blocks_ir(const LayoutIR &layout,
                                  const OwnershipIR &ownership,
                                  const MessageCatalogIR &catalog);
// Re-extracts the canonical blocks and validates every claimed source cell's
// identity, role, display column, and uniqueness.
bool verify_message_section_blocks_ir(const LayoutIR &layout,
                                      const OwnershipIR &ownership,
                                      const MessageCatalogIR &catalog,
                                      const MessageSectionBlocksIR &blocks,
                                      std::string *error = nullptr);
std::string
format_message_section_blocks_ir(const MessageSectionBlocksIR &blocks);

} // namespace geist::detail
