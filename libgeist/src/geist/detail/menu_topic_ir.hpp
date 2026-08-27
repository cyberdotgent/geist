#pragma once

#include "geist/detail/book_topic_catalog_ir.hpp"
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

struct MenuTopicParagraphIR {
  std::string text;
  DocumentSourceSliceIR source;
  std::vector<MenuSourceCellIR> cells;
};

struct MenuTargetValidationEntryIR {
  std::string target;
  std::string label;
  enum class ExistenceEvidence {
    topic_header,
    toc_entry,
    topic_header_and_toc
  } existence = ExistenceEvidence::topic_header;
  enum class LabelEvidence { topic_title, toc_title, topic_title_and_toc }
      label_evidence = LabelEvidence::topic_title;
};

// Book-level semantic evidence that a raw CMITEM target exists and that its
// source label needs no catalog-assisted repair.  Keeping this separate from
// raw extraction prevents structural recognition from silently promoting an
// unvalidated target to a typed topic reference.
struct MenuTargetValidationIR {
  std::vector<MenuTargetValidationEntryIR> items;
};

std::optional<MenuTargetValidationIR> validate_source_menu_targets(
    const MenuIR &source_menu, const BookTopicCatalogIR &catalog,
    std::string *error = nullptr);

// A complete, output-neutral topic whose only visible body object is a menu.
// The target retains the raw BOO topic identity; filename and fragment policy
// belongs to a renderer's resolver.
struct MenuTopicIR {
  std::string heading_level;
  std::string title;
  DocumentSourceSliceIR title_source;
  std::vector<MenuSourceCellIR> title_cells;
  std::vector<MenuTopicParagraphIR> introductions;
  std::optional<MenuTopicAnchorIR> anchor;
  std::vector<MenuTopicItemIR> items;
  std::vector<MenuTopicSegmentIR> segments;
};

std::optional<MenuTopicIR>
extract_menu_topic_ir(const std::vector<DecodedLogicalRecordSource> &records,
                      const MenuTargetValidationIR &target_validation,
                      const LayoutIR &layout, const OwnershipIR &ownership,
                      std::string *error = nullptr);
bool verify_menu_topic_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const MenuTargetValidationIR &target_validation,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const MenuTopicIR &topic, std::string *error = nullptr);
std::string format_menu_topic_ir(const MenuTopicIR &topic);

} // namespace geist::detail
