// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/ir/selector_ir.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct SelectorRefIR {
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  std::size_t ordinal = 0;
};

enum class SelectorSourceCellKind {
  token_word,
  inserted_space,
};

struct SelectorSourceCellRefIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  SelectorSourceCellKind kind = SelectorSourceCellKind::token_word;
  SourceByteRange token_bytes;
};

enum class SelectorTargetKind {
  internal_anchor,
  picture_resource,
  external_deferred,
};

struct SelectorTargetIR {
  SelectorTargetKind kind = SelectorTargetKind::internal_anchor;
  std::string raw_target;
  std::string resolved_target;
};

enum class SelectorDisplayCellOrigin {
  source,
  restored_native_margin,
  restored_native_marker,
  restored_generated_prefix,
  restored_box_padding,
};

struct SelectorDisplayCellIR {
  std::uint16_t word = 0;
  SelectorDisplayCellOrigin origin = SelectorDisplayCellOrigin::source;
  std::optional<SelectorSourceCellRefIR> source;
};

enum class SelectorRowAssociation {
  inline_payload,
  deferred_same_record,
  deferred_next_record,
  multiple_queued,
};

struct SelectorRowOwnerIR {
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  DisplayRunId run = 0;
  std::size_t physical_row_index = 0;
  std::size_t token_begin = 0;
  std::size_t token_end = 0;
};

struct SelectorSpanIR {
  SelectorRefIR selector;
  SelectorTargetIR target;
  std::size_t cell_begin = 0;
  std::size_t cell_end = 0;
};

struct SelectorDisplayRowIR {
  std::uint64_t id = 0;
  SelectorRowOwnerIR owner;
  SelectorRowAssociation association = SelectorRowAssociation::inline_payload;
  std::vector<SelectorDisplayCellIR> cells;
  // Decoder padding suppressed before reconstructing a generated-list native
  // margin remains explicitly conserved and source-proven here.
  std::vector<SelectorDisplayCellIR> suppressed_prefix_cells;
  // Source order is retained here. Renderers may sort spans by cell_begin but
  // must never merge adjacent selectors merely because their targets match.
  std::vector<SelectorSpanIR> spans;
  bool hard_boundary = false;
};

enum class SelectorBindingKind {
  display_span,
  resource_object,
  table_owned,
};

struct SelectorBindingIR {
  SelectorRefIR selector;
  SelectorBindingKind kind = SelectorBindingKind::display_span;
  // A display-row ID for display_span, otherwise zero.
  std::uint64_t owner_id = 0;
};

struct SelectorObjectIR {
  std::uint64_t id = 0;
  SelectorRefIR selector;
  SelectorTargetIR target;
};

struct SelectorDisplayIR {
  std::vector<SelectorDisplayRowIR> rows;
  std::vector<SelectorObjectIR> objects;
  // Every raw selector must occur exactly once in bindings, including those
  // deliberately handed to future table composition.
  std::vector<SelectorBindingIR> bindings;
};

std::optional<SelectorDisplayIR> extract_selector_display_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const SelectorCatalogIR &selectors, const LayoutIR &layout,
    const VerifiedOwnershipIR &ownership, std::string *error = nullptr);

bool verify_selector_display_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const SelectorCatalogIR &selectors, const LayoutIR &layout,
    const VerifiedOwnershipIR &ownership, const SelectorDisplayIR &display,
    std::string *error = nullptr);

std::string format_selector_display_ir(const SelectorDisplayIR &display);

} // namespace geist::detail
