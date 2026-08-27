#pragma once

#include "geist/detail/message_ir.hpp"

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

struct MessageStructuredTableBlockIR {
  MessageStructuredTableRowIR header;
  std::vector<MessageStructuredTableRowIR> rows;
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

struct MessageStructuredPreformattedLineIR {
  std::string text;
  MessageSourceRowIR source_row;
  std::vector<MessageStructuredSourceCellIR> source_cells;
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
