// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/render/document_markdown_renderer.hpp"

namespace geist::detail {

// Project actual serialized bytes according to their emission classification.
// Reject incomplete/invalid traces rather than guessing Markdown grammar.
std::string markdown_visible_text(const std::string &markdown,
                                  const DocumentRenderTraceIR &trace);

// Require the cached topic bytes to be exactly the traced serializer output
// plus the known diagnostic prefix; then project that actual output window.
std::string verified_markdown_visible_text(
    const std::string& actual, const std::string& serialized,
    const DocumentRenderTraceIR& trace, const std::string& diagnostic_prefix);

} // namespace geist::detail
