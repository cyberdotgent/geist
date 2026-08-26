#pragma once

#include "geist/detail/document_ir.hpp"

#include <string>

namespace geist::detail {

// Render a verified document as stable Markdown. Typed nodes are rendered
// without consulting decoder or semantic-layer state. A sole whole-topic
// LegacyGmlRegionIR remains an indivisible adapter call because the legacy
// renderer carries state across records.
std::string render_document_markdown(const DocumentIR &document);

} // namespace geist::detail
