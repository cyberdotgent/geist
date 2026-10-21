// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/core/internal.hpp"
#include "geist/detail/lowering/document_ir.hpp"

#include <optional>
#include <vector>

namespace geist::detail {

// The renderer-neutral representation of a decoded topic for which no
// semantic family proved a body.  `document` is present whenever decoding
// succeeded, including for a genuine heading-only topic.  This carrier says
// nothing about semantic certainty: TopicLoweringOutcomeIR records that the
// selected representation is source rows, so its public diagnostic remains
// best-effort rather than being relabelled typed merely because it is now a
// DocumentIR.
struct SourceRowsDocumentLoweringIR {
  bool source_decoded = false;
  bool has_body = false;
  std::optional<DocumentIR> document;
};

SourceRowsDocumentLoweringIR lower_source_rows_to_document_ir(
    TopicIdentityIR topic,
    const std::vector<DecodedLogicalRecordSource> &sources);

// Content is an IR property, not something inferred from one serializer's
// bytes.  Headings and anchors provide identity/navigation but are not a
// topic body.
bool document_ir_has_body(const DocumentIR &document);

} // namespace geist::detail
