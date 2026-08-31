// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/ir/publication_ir.hpp"

#include <optional>
#include <string>

namespace geist::detail {

// Lowers a verified publication catalog into output-neutral document blocks.
// Publication paragraphs remain paragraphs: PublicationCatalogIR does not
// distinguish a citation title from body text, so forcing it into a
// PublicationListBlockIR would duplicate or invent content.
std::optional<DocumentIR>
lower_publication_catalog_to_document_ir(TopicIdentityIR topic,
                                         const PublicationCatalogIR &catalog,
                                         std::string *error = nullptr);

// Requires the exact canonical lowering, including every available physical
// source-row reference. This verifier is independent of any output renderer.
bool verify_publication_catalog_document_ir(const PublicationCatalogIR &catalog,
                                            const DocumentIR &document,
                                            std::string *error = nullptr);

} // namespace geist::detail
