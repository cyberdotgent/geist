#pragma once

#include "geist/detail/internal.hpp"
#include "geist/detail/source_rows.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct ImplicitGridHeaderSpan {
  std::size_t offset = 0;
  std::size_t length = 0;
};

struct ImplicitGridRow {
  std::string key;
  std::string value;
  bool continuation = false;
};

struct ImplicitGrid {
  std::size_t key_origin = 0;
  std::size_t value_origin = 0;
  bool owns_source_tail = false;
  std::vector<ImplicitGridRow> physical_rows;
  std::vector<std::vector<std::string>> semantic_rows;
};

bool is_implicit_grid_header_geometry(
    const std::vector<ImplicitGridHeaderSpan>& header_spans);

// Extract a source-owned two-column form that has no drawn box. Activation
// requires both a plural CFONT header geometry and repeated encoded row
// controls; visible spacing alone is deliberately insufficient.
std::optional<ImplicitGrid> extract_implicit_grid(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::vector<ImplicitGridHeaderSpan>& header_spans);

std::optional<ImplicitGrid> extract_terminal_styled_grid(
    const DecodedLogicalRecordSource& record,
    const DecodedMarkupSegmentSpan& segment,
    const std::vector<ImplicitGridHeaderSpan>& header_spans,
    const std::vector<std::string>& headings);

} // namespace geist::detail
