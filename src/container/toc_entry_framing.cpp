#include "geist/detail/container/toc_entry_framing.hpp"

#include "geist/detail/ir/book_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"

namespace geist::detail {

std::vector<std::size_t> display_line_start_output_offsets(
    const DecodedLogicalRecordSource& record) {
  std::vector<std::size_t> offsets;
  const auto* lines = record_display_lines(record);
  if (lines == nullptr) {
    return offsets;
  }
  const auto token_offsets = assembled_token_output_offsets(record.assembled);
  offsets.reserve(lines->size());
  for (const auto& line : *lines) {
    if (line.prefix_token < token_offsets.size()) {
      offsets.push_back(token_offsets[line.prefix_token]);
    }
  }
  return offsets;
}

} // namespace geist::detail
