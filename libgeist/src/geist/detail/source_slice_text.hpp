#pragma once

#include "geist/detail/book_ir.hpp"
#include "geist/detail/provenance_ir.hpp"

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

// The same proof, decoding the slice's whole logical-record payload out of the
// file first and then selecting exactly the tokens the slice's byte range
// covers.  A record whose display lines carry their own length prefixes is
// re-lined by the record decoder (`Format/logical-controls.md`), so a window
// walk that starts inside such a record can land off the token grid; this
// overload reproduces the decoder that produced the slice and still proves the
// byte range, because the selected tokens must tile `[byte_begin, byte_end)`
// exactly.
std::optional<std::string> decode_source_slice_text_in_record(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    const std::map<std::uint16_t, TokenWords>& token_strings,
    std::size_t payload_begin,
    std::size_t payload_end,
    const DocumentSourceSliceIR& slice,
    std::string* error = nullptr);

// Selects the tokens `slice` names out of an already decoded record and
// projects them.  The selected tokens must tile `[byte_begin, byte_end)`
// exactly, which is what proves the slice's byte offsets.
std::optional<std::string> project_source_slice_text(
    const LogicalRecordIR& record, const DocumentSourceSliceIR& slice,
    std::string* error = nullptr);

// The comparison form both sides of a provenance proof are reduced to: ASCII
// case is kept, but every run of whitespace collapses away.  Column padding,
// spacing controls, and soft-wrap joins are display projections that carry no
// source identity of their own.
std::string collapse_source_text(const std::string& value);

} // namespace geist::detail
