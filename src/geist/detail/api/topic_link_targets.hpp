#pragma once

#include "geist/detail/lowering/document_ir.hpp"
#include "geist/link_target.hpp"

#include <string>
#include <vector>

namespace geist::detail {

// What a typed topic names, read off its Document IR.
std::vector<LinkTarget> document_link_targets(const DocumentIR& document);

} // namespace geist::detail
