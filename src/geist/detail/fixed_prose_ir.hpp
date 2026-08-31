#pragma once

#include "geist/detail/ownership_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

// One source-proven marker/origin pair removed from an ST physical prose row.
// The ranges remain in decoded-record coordinates so the compatibility path
// can project the verified structure without searching rendered text.
struct FixedProseRowIR {
  DisplayRunId run = 0;
  std::size_t row = 0;
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  std::size_t marker_token = 0;
  std::size_t origin_token = 0;
  std::uint16_t marker_value = 0;
  std::uint8_t marker_width = 0;
  SourceByteRange marker_bytes;
  std::uint16_t origin_value = 0;
  std::uint8_t origin_width = 0;
  SourceByteRange origin_bytes;
  OutputRangeIR projected_range;
};

struct FixedProseIR {
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  OutputRangeIR segment_range;
  OutputRangeIR payload_range;
  OutputRangeIR title_range;
  OutputRangeIR body_range;
  OutputRangeIR title_body_boundary;
  std::string title;
  std::string paragraph;
  std::vector<FixedProseRowIR> rows;
};

std::optional<FixedProseIR> extract_fixed_prose_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const VerifiedOwnershipIR& ownership,
    std::string* error = nullptr);
bool verify_fixed_prose_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const VerifiedOwnershipIR& ownership,
    const FixedProseIR& prose, std::string* error = nullptr);
std::string format_fixed_prose_ir(const FixedProseIR& prose);

} // namespace geist::detail
