// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/ir/glossary_catalog_ir.hpp"

#include <optional>
#include <string>

namespace geist::detail {

std::optional<DocumentIR> lower_glossary_catalog_to_document_ir(
    TopicIdentityIR topic, const GlossaryCatalogIR& catalog,
    std::string* error = nullptr);

bool verify_glossary_catalog_document_ir(
    const GlossaryCatalogIR& catalog, const DocumentIR& document,
    std::string* error = nullptr);

} // namespace geist::detail
