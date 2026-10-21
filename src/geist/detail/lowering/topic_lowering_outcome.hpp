// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include <optional>
#include <string>

namespace geist::detail {

enum class TopicRepresentationKindIR {
  none,
  structured,
  source_rows,
};

// One typed lowering attempt, kept whole so the render path can explain
// itself.  Before this existed, `TocEntry::markdown()` discarded the rejection
// string and the decline trace and silently fell through to the legacy
// renderer; `bootrace --coverage` computed the same information separately.
struct TopicLoweringOutcomeIR {
  // The one representation selected for both renderers. A source-row
  // document remains explicitly best-effort through `representation`; the
  // existence of DocumentIR does not upgrade its semantic certainty.
  std::optional<DocumentIR> document;
  TopicRepresentationKindIR representation = TopicRepresentationKindIR::none;
  bool typed_claimed = false;
  bool source_decoded = false;
  bool source_rows_have_body = false;
  std::string rejection;
  TypedLoweringTraceIR trace;
};

} // namespace geist::detail
