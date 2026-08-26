#pragma once

#include "geist/detail/ownership_ir.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace geist::detail {

struct GlossaryParagraphIR {
  std::string text;
  std::vector<std::pair<DisplayRunId, std::size_t>> source_rows;
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
