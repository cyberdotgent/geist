#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace geist::detail {

// Decoder-independent source coordinates carried by rendered document nodes.
// A zero-length range is valid for an insertion point.  Consumers must not
// assume that every semantic node has all three coordinate kinds.
struct DocumentSourceSliceIR {
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  std::size_t token_begin = 0;
  std::size_t token_end = 0;
  std::uint32_t byte_begin = 0;
  std::uint32_t byte_end = 0;
};

struct DocumentSourceRowIR {
  std::uint64_t display_run = 0;
  std::size_t row_index = 0;
};

enum class DocumentDerivationIR {
  decoded,
  semantic_lowering,
  synthesized,
  legacy_adapter,
};

struct DocumentNodeOriginIR {
  DocumentDerivationIR derivation = DocumentDerivationIR::decoded;
  std::vector<DocumentSourceSliceIR> slices;
  std::vector<DocumentSourceRowIR> rows;
  // Stable reason or lowerer name, not rendered content.
  std::string detail;
};

} // namespace geist::detail
