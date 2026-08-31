#pragma once

#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/ir/trap_catalog_ir.hpp"

#include <optional>
#include <string>

namespace geist::detail {

// Lowers a verified section-label SRMSG catalog to DocumentIR: heading,
// named anchors, introduction paragraphs, then one anchored definition item
// per entry (`**id** — **Label:** text …`) whose label words keep their CFONT
// highlight styles. Every inline carries the entry's row/token provenance.
std::optional<DocumentIR>
lower_trap_catalog_to_document_ir(TopicIdentityIR topic,
                                  const TrapCatalogIR &catalog,
                                  std::string *error = nullptr);

bool verify_trap_catalog_document_ir(const TrapCatalogIR &catalog,
                                     const DocumentIR &document,
                                     std::string *error = nullptr);

} // namespace geist::detail
