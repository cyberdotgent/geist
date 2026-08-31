// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

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
  // Sub-token boundaries, in bytes of the decoded word of the slice's single
  // token.  BookServer can style part of one decoded word (GC23-046 6.0
  // `cfont 43 1 1` -> `SMPWRK<I>x</I>`, SC09-138 3.3.1 `cfont 24 5 4` ->
  // `<TT>CLIST</TT>s`), so an inline may own a byte range inside a token
  // instead of the whole token.  `character_end == 0` means the slice covers
  // its tokens whole; otherwise `token_end == token_begin + 1` and the slice
  // covers `[character_begin, character_end)` of that token's decoded word.
  std::uint32_t character_begin = 0;
  std::uint32_t character_end = 0;
};

// True when the slice names a byte range inside one token's decoded word.
inline bool slice_is_partial(const DocumentSourceSliceIR &slice) noexcept {
  return slice.character_end != 0;
}

inline bool operator==(const DocumentSourceSliceIR &left,
                       const DocumentSourceSliceIR &right) noexcept {
  return left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.token_begin == right.token_begin &&
         left.token_end == right.token_end && left.byte_begin == right.byte_begin &&
         left.byte_end == right.byte_end &&
         left.character_begin == right.character_begin &&
         left.character_end == right.character_end;
}

struct DocumentSourceRowIR {
  std::uint64_t display_run = 0;
  std::size_t row_index = 0;
};

enum class DocumentDerivationIR {
  decoded,
  semantic_lowering,
  synthesized,
};

// Fidelity of the lowering that produced a node.  `degraded` marks a node
// whose family admitted the topic but could not prove this block's structure,
// so the block is emitted verbatim (preformatted) instead of as a typed
// structure.  Fail-closed is a rule about *claiming structure*, never about
// withholding content: a degraded node still carries every source word, it
// simply asserts nothing about the shape around them.
enum class DocumentFidelityIR {
  typed,
  degraded,
};

struct DocumentNodeOriginIR {
  DocumentDerivationIR derivation = DocumentDerivationIR::decoded;
  std::vector<DocumentSourceSliceIR> slices;
  std::vector<DocumentSourceRowIR> rows;
  // Stable reason or lowerer name, not rendered content.
  std::string detail;
  DocumentFidelityIR fidelity = DocumentFidelityIR::typed;
  // Machine-readable code naming the fallback this node took, set only when
  // `fidelity` is `degraded`, e.g. "fixed-table-verbatim".
  std::string degradation_code;
  // Human-readable explanation of the fallback, e.g. the message family's
  // own `fallback_reason`. May be empty when the code says everything.
  std::string degradation_detail;
};

} // namespace geist::detail
