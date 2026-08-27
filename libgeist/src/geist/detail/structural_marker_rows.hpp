#pragma once

#include "geist/detail/layout_ir.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;

// Typed structural row-marker evidence for flattened `ST` bodies that still
// render through the legacy GML path (fixed-layout front matter such as
// IBMMMSTR `FRONT_1`, SRMSG catalog introductions such as SC31-711 `4.1.2`).
//
// A physical row whose marker slot sits at the three-space native origin
// carries a structural marker: the width-1 dictionary word in that slot is
// row geometry, not prose (the same origin-3 rule `message_prose_rows.cpp`
// uses to separate structural from lexical markers). The flattened decoded
// string keeps such a word glued to the previous row's text, followed by the
// slot padding and then this row's visible text, for example
// `... 10577.an    The following terms ...` (IBMMMSTR LR6 marker bytes
// 0x1079c, marker_value 23) or `... actions.can    The traps in ...`
// (SC31-711 LR101 marker bytes 0x10950, marker_value 44).
//
// This replaces the literal marker-word spelling list the legacy cleanup
// used to carry: a word is erased only when a typed row proves that exact
// word is its slot marker and the row's visible text follows it.
struct StructuralMarkerRowIR {
  DisplayRunId run = 0;
  std::size_t row_index = 0;
  MarkerSlotIR marker;
  std::string visible_text;
};

// Rows of verified LayoutIR whose marker slot sits at the three-space origin
// and whose decoded marker is a purely alphabetic dictionary word. Returns an
// empty list (fail closed) when the layout does not verify.
std::vector<StructuralMarkerRowIR> extract_structural_marker_rows_ir(
    const std::vector<DecodedLogicalRecordSource>& records);

// Lazily extracts the structural marker rows of one topic, so that a legacy
// body which never presents a marker-word candidate never pays for the
// layout pass.
class StructuralMarkerRowEvidence {
public:
  explicit StructuralMarkerRowEvidence(
      const std::vector<DecodedLogicalRecordSource>& records);

  // Length of the alphabetic word that starts at `cursor` in a flattened ST
  // body when (a) the word is attached to the previous row's terminal
  // punctuation (`.`, `)`, `:`), (b) it is immediately followed by the
  // four-space slot padding, and (c) a typed structural marker row carries
  // exactly that word as its slot marker with its visible text following the
  // padding. Returns 0 otherwise. Conditions (a) and (b) are the legacy
  // geometry the cleanup always required; (c) replaces its spelling list.
  std::size_t marker_word_length_at(const std::string& value,
                                    std::size_t cursor) const;

  const std::vector<StructuralMarkerRowIR>& rows() const;

private:
  const std::vector<DecodedLogicalRecordSource>* records_ = nullptr;
  mutable std::optional<std::vector<StructuralMarkerRowIR>> rows_;
};

} // namespace geist::detail
