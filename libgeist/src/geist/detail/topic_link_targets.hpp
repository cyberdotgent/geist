#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/link_target.hpp"

#include <string>
#include <vector>

namespace geist::detail {

// What a typed topic names, read off its Document IR.
std::vector<LinkTarget> document_link_targets(const DocumentIR& document);

// What a topic names according to the legacy GML projection.  Kept for the
// topics that still render through the legacy string route, and used by
// `bootrace --links` to report where the two answers differ.
std::vector<LinkTarget> gml_link_targets(
    const std::vector<std::string>& records);

} // namespace geist::detail
