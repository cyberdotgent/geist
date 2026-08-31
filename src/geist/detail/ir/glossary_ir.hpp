// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/layout/font_span_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace geist::detail {

// A byte range of GlossaryParagraphIR::text whose emphasis is proven by one
// CFONT operand triple. The triple's display-column geometry must conserve a
// whole display word of the governed physical row exactly; otherwise the
// control leaves no emphasis claim at all (fail closed, never partial).
struct GlossaryEmphasisIR {
  std::size_t begin = 0;
  std::size_t end = 0;
  FontStyleIR style = FontStyleIR::unknown;
  // Operand provenance of the font control that owns the style.
  DocumentSourceSliceIR source;
};

inline bool operator==(const GlossaryEmphasisIR& left,
                       const GlossaryEmphasisIR& right) noexcept {
  return left.begin == right.begin && left.end == right.end &&
         left.style == right.style && left.source == right.source;
}

struct GlossaryParagraphIR {
  std::string text;
  std::vector<std::pair<DisplayRunId, std::size_t>> source_rows;
  std::vector<GlossaryEmphasisIR> emphasis;
};

// What stands between a glossary topic's title and its first `SRGLS` term.
//
// Two shapes, and the flag says which one is asserted.  `citation_list` is the
// fully articulated form: a lead sentence, a bulleted list of the standards
// the glossary draws its definitions from, a second lead, and the definitions
// of the cross-reference verbs ("Contrast with:", "Synonym for:", ...).  That
// shape is only claimed when every one of those parts is proven.
//
// `paragraphs` is the general form: the introduction is conserved as the
// prose paragraphs the source rows spell, in order, asserting nothing about
// what any of them mean.  Most glossaries in the corpus have a short
// introduction or none at all, and requiring the articulated shape of all of
// them is what made this family decline 19 of 20 glossary topics -- the
// recognizer had been fitted to the one book that has it.
enum class GlossaryIntroductionShapeIR {
  paragraphs,
  citation_list,
};

struct GlossaryIntroductionIR {
  GlossaryIntroductionShapeIR shape = GlossaryIntroductionShapeIR::paragraphs;
  std::string title;
  GlossaryParagraphIR lead;
  // citation_list only.
  std::vector<GlossaryParagraphIR> sources;
  GlossaryParagraphIR cross_reference_lead;
  std::vector<GlossaryParagraphIR> cross_references;
  // paragraphs only: the introduction body after the title, in source order.
  std::vector<GlossaryParagraphIR> paragraphs;
};

std::optional<GlossaryIntroductionIR> extract_glossary_introduction_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    std::string* error = nullptr);
bool verify_glossary_introduction_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const GlossaryIntroductionIR& introduction, std::string* error = nullptr);
std::string
format_glossary_introduction_ir(const GlossaryIntroductionIR& introduction);

} // namespace geist::detail
