#pragma once

#include "geist/detail/document_ir.hpp"

#include <string>

namespace geist::detail {

// Render a verified document through the Markdown migration boundary.  The
// first migration slice accepts one whole-topic LegacyGmlRegionIR and passes
// its records to the legacy state machine in one call.  Keeping the region
// intact is required because the legacy renderer carries state across records.
std::string render_document_markdown(const DocumentIR& document);

} // namespace geist::detail
