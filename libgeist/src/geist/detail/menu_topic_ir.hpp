#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/layout_ir.hpp"
#include "geist/detail/menu_ir.hpp"
#include "geist/detail/ownership_ir.hpp"

#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct MenuTopicAnchorIR {
  std::string id;
  DocumentSourceSliceIR source;
};

struct MenuTopicItemIR {
  CrossReferenceTargetIR target;
  std::string label;
  DocumentSourceSliceIR source;
  std::vector<MenuSourceCellIR> target_cells;
  std::vector<MenuSourceCellIR> label_cells;
};

struct MenuTopicSegmentIR {
  BookControlKind kind = BookControlKind::text;
  std::string opcode;
  DocumentSourceSliceIR source;
};

// A complete, output-neutral topic whose only visible body object is a menu.
// The target retains the raw BOO topic identity; filename and fragment policy
// belongs to a renderer's resolver.
struct MenuTopicIR {
  std::string heading_level;
  std::string title;
  DocumentSourceSliceIR title_source;
  std::vector<MenuSourceCellIR> title_cells;
  std::optional<MenuTopicAnchorIR> anchor;
  std::vector<MenuTopicItemIR> items;
  std::vector<MenuTopicSegmentIR> segments;
};

std::optional<MenuTopicIR>
extract_menu_topic_ir(const std::vector<DecodedLogicalRecordSource> &records,
                      const MenuIR &menu, const LayoutIR &layout,
                      const OwnershipIR &ownership,
                      std::string *error = nullptr);
bool verify_menu_topic_ir(
    const std::vector<DecodedLogicalRecordSource> &records, const MenuIR &menu,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const MenuTopicIR &topic, std::string *error = nullptr);
std::string format_menu_topic_ir(const MenuTopicIR &topic);

} // namespace geist::detail
