#pragma once

#include "geist/detail/container/control_ir.hpp"
#include "geist/detail/layout/layout_ir.hpp"

#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct SelectorMarkerIR {
  std::size_t token_index = 0;
  std::size_t origin_token_index = 0;
  std::uint16_t encoded_value = 0;
  std::uint8_t encoded_width = 0;
  SourceByteRange byte_range;
  // Decoded UTF-8 byte coordinates, matching ControlSegmentIR ranges.
  OutputRangeIR output_range;
  std::string decoded_text;
  std::size_t native_origin = 0;
};

struct SelectorIR {
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  std::size_t selector_ordinal = 0;
  bool canonical_operands = false;
  std::string rejection_reason;
  std::size_t column = 0;
  std::size_t length = 0;
  std::string target;
  OutputRangeIR complete_range;
  OutputRangeIR operand_range;
  OutputRangeIR payload_range;
  std::vector<std::size_t> source_tokens;
  std::vector<SourceByteRange> source_byte_ranges;
  std::string display_payload;
  bool inside_table = false;
  // A source-proven marker/origin slot. Whether a compatibility renderer may
  // consume it is a projection policy, not part of selector decoding.
  std::optional<SelectorMarkerIR> display_marker_slot;
};

struct SelectorCatalogIR {
  std::vector<SelectorIR> selectors;
};

std::optional<SelectorCatalogIR> extract_selector_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::string* error = nullptr);
bool verify_selector_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const SelectorCatalogIR& catalog,
    std::string* error = nullptr);
std::string format_selector_catalog_ir(const SelectorCatalogIR& catalog);

} // namespace geist::detail
