// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"

#include <optional>
#include <string>

namespace geist::detail {

// Shared CFONT/link lowering for source-owned text in prose and figures.
std::optional<InlineSequenceIR> lower_prose_inline_content(
    const std::vector<ProseInlineIR>& inlines, std::string* error = nullptr);

std::optional<DocumentIR> lower_prose_topic_to_document_ir(
    TopicIdentityIR topic, const ProseTopicIR& prose,
    std::string* error = nullptr);
bool verify_prose_topic_document_ir(const ProseTopicIR& prose,
                                    const DocumentIR& document,
                                    std::string* error = nullptr);

} // namespace geist::detail
