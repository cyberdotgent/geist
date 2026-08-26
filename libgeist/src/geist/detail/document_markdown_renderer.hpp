#pragma once

#include "geist/detail/document_ir.hpp"

#include <functional>
#include <optional>
#include <string>

namespace geist::detail {

struct DocumentMarkdownRendererOptions {
  // Return an output-specific destination, or nullopt to use the stable
  // context-free fallback. Topic/resource mappings supplied by an exporter
  // belong here rather than in DocumentIR semantic lowering.
  std::function<std::optional<std::string>(const CrossReferenceTargetIR &)>
      resolve_cross_reference;
};

// Render a verified document as stable Markdown. Typed nodes are rendered
// without consulting decoder or semantic-layer state. A sole whole-topic
// LegacyGmlRegionIR remains an indivisible adapter call because the legacy
// renderer carries state across records.
std::string render_document_markdown(
    const DocumentIR &document,
    const DocumentMarkdownRendererOptions &options = {});

} // namespace geist::detail
