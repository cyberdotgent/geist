#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace geist::detail {

struct FixedDisplaySourceColumn {
  std::size_t fragment = 0;
  std::size_t column = 0;
};

// A physical display row assembled without discarding source-column
// coordinates. Structural marker fields are blanked in place, so CFONT and
// CSELECT offsets continue to address the original columns.
struct FixedDisplayRow {
  std::string text;
  std::vector<FixedDisplaySourceColumn> source_columns;
};

FixedDisplayRow assemble_fixed_display_row(
    const std::vector<std::string>& fragments);
void blank_fixed_display_marker_fields(FixedDisplayRow& row,
                                       bool allow_adjacent = false);

} // namespace geist::detail
