#pragma once

#include "geist/detail/font_span_ir.hpp"
#include "geist/detail/ownership_ir.hpp"

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

struct GlossaryIntroductionIR {
  std::string title;
  GlossaryParagraphIR lead;
  std::vector<GlossaryParagraphIR> sources;
  GlossaryParagraphIR cross_reference_lead;
  std::vector<GlossaryParagraphIR> cross_references;
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
