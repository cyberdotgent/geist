// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/lowering/document_ir.hpp"

#include <string>
#include <vector>

namespace geist::detail {

// The document target of a `CSELECT`: an in-book anchor, or a `LNK`
// selector's destination read from its alternative tuple (doc/boo-spec/
// markup.adoc, "LNK selector alternatives") -- alternative 6 the value,
// 4 and 5 the document number and level of a book reference, 2 the heading
// or spot anchor inside it, 4 the URL of an external link.
CrossReferenceTargetIR selector_cross_reference_target(
    CrossReferenceTargetKindIR kind, const std::string& value,
    const std::vector<std::string>& alternatives);

} // namespace geist::detail
