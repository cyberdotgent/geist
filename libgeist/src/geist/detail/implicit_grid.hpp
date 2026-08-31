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

// Extract a source-owned two-column form that has no drawn box. Activation
// requires both a plural CFONT header geometry and repeated encoded row
// controls; visible spacing alone is deliberately insufficient.
std::optional<ImplicitGrid> extract_implicit_grid(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::vector<ImplicitGridHeaderSpan>& header_spans);

} // namespace geist::detail
