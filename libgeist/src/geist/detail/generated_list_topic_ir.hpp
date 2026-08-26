#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/selector_display_ir.hpp"

#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

enum class GeneratedListTopicKindIR { figures, tables };

struct GeneratedListTopicSegmentIR {
  BookControlKind kind = BookControlKind::text;
  std::string opcode;
  DocumentSourceSliceIR source;
};

struct GeneratedListTopicIR {
  GeneratedListTopicKindIR kind = GeneratedListTopicKindIR::figures;
  std::string title;
  DocumentSourceSliceIR heading_source;
  std::vector<SelectorDisplayRowIR> entries;
  std::vector<GeneratedListTopicSegmentIR> segments;
};

std::optional<GeneratedListTopicIR> extract_generated_list_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const SelectorCatalogIR& selectors, const LayoutIR& layout,
    const OwnershipIR& ownership, std::string* error = nullptr);
bool verify_generated_list_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const SelectorCatalogIR& selectors, const LayoutIR& layout,
    const OwnershipIR& ownership, const GeneratedListTopicIR& topic,
    std::string* error = nullptr);
std::string format_generated_list_topic_ir(const GeneratedListTopicIR& topic);

} // namespace geist::detail
