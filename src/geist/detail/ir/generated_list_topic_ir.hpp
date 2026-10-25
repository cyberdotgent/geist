// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/ir/selector_display_ir.hpp"

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

enum class GeneratedListCellDispositionIR {
  label_fragment,
  layout_decoration,
  decoder_artifact,
  structural,
};

enum class GeneratedListLabelFragmentRoleIR {
  selected_payload,
  source_extension,
};

struct GeneratedListLabelFragmentIR {
  GeneratedListLabelFragmentRoleIR role =
      GeneratedListLabelFragmentRoleIR::selected_payload;
  std::size_t cell_begin = 0;
  std::size_t cell_end = 0;
  std::vector<SelectorDisplayCellIR> cells;
  std::vector<DocumentSourceSliceIR> source_slices;
};

struct GeneratedListEntryIR {
  // Retained losslessly as evidence.  Semantic lowering consumes only the
  // typed label fragments and target below.
  SelectorDisplayRowIR display;
  SelectorRefIR selector;
  SelectorTargetIR target;
  std::vector<GeneratedListCellDispositionIR> cell_dispositions;
  std::vector<GeneratedListCellDispositionIR>
      suppressed_prefix_dispositions;
  std::vector<GeneratedListLabelFragmentIR> label_fragments;
  // The revision mark the selection row stores in its margin (cell 1 between
  // blanks, `DisplayLineIR::margin_mark`), outside the selected columns.
  // Hosted BookServer prints it in the margin in front of the link.
  std::optional<char32_t> revision_mark;
};

struct GeneratedListTopicIR {
  GeneratedListTopicKindIR kind = GeneratedListTopicKindIR::figures;
  std::string title;
  DocumentSourceSliceIR heading_source;
  std::vector<GeneratedListEntryIR> entries;
  std::vector<GeneratedListTopicSegmentIR> segments;
};

std::optional<GeneratedListTopicIR> extract_generated_list_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const SelectorCatalogIR& selectors, const LayoutIR& layout,
    const VerifiedOwnershipIR& ownership, std::string* error = nullptr);
bool verify_generated_list_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const SelectorCatalogIR& selectors, const LayoutIR& layout,
    const VerifiedOwnershipIR& ownership, const GeneratedListTopicIR& topic,
    std::string* error = nullptr);

} // namespace geist::detail
