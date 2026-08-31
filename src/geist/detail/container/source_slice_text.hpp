#pragma once

#include "geist/detail/ir/book_ir.hpp"
#include "geist/detail/container/provenance_ir.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace geist {
struct BooDirectory;
}

namespace geist::detail {

// Projects one provenance slice back to the display text its BOO bytes hold,
// decoding `[slice.byte_begin, slice.byte_end)` out of the file again with no
// record, layout, or semantic context.  This is the reverse direction of the
// render pipeline: it proves that a slice names the bytes a rendered element
// actually came from, rather than restating what a lowering already believed.
//
// Returns nothing, with `error` set, when the window does not tile into whole
// tokens or when a sub-token character range falls outside the decoded word.
std::optional<std::string> decode_source_slice_text(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    const std::map<std::uint16_t, TokenWords>& token_strings,
    const DocumentSourceSliceIR& slice,
    std::string* error = nullptr);

// Selects the tokens `slice` names out of an already decoded record and
// projects them.  The selected tokens must tile `[byte_begin, byte_end)`
// exactly, which is what proves the slice's byte offsets.
std::optional<std::string> project_source_slice_text(
    const LogicalRecordIR& record, const DocumentSourceSliceIR& slice,
    std::string* error = nullptr);

} // namespace geist::detail
