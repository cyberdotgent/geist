// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/cross_reference_target.hpp"

#include "geist/detail/ir/selector_link_ir.hpp"

namespace geist::detail {

CrossReferenceTargetIR selector_cross_reference_target(
    CrossReferenceTargetKindIR kind, const std::string& value,
    const std::vector<std::string>& alternatives) {
  CrossReferenceTargetIR target{kind, value};
  target.alternatives = alternatives;
  if (alternatives.size() >= 6) {
    target.value = alternatives[5];
    if (kind == CrossReferenceTargetKindIR::book_contents) {
      target.document_number = alternatives[3];
      target.document_level = alternatives[4];
    } else if (kind == CrossReferenceTargetKindIR::book_heading) {
      target.document_number = alternatives[3];
      target.document_level = alternatives[4];
      target.heading_anchor = alternatives[1];
    } else if (kind == CrossReferenceTargetKindIR::book_spot) {
      target.document_number = alternatives[3];
      target.document_level = alternatives[4];
      target.spot_anchor = alternatives[1];
    } else if (kind == CrossReferenceTargetKindIR::book_anchor) {
      target.document_number = alternatives[3];
      target.document_level = alternatives[4];
      target.book_anchor = cross_book_anchor(alternatives);
    } else if (kind == CrossReferenceTargetKindIR::external) {
      target.url = alternatives[3];
    }
  }
  return target;
}

} // namespace geist::detail
