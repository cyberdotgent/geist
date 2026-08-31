// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/ir/comment_delivery_ir.hpp"
#include "geist/detail/lowering/document_ir.hpp"

#include <optional>
#include <string>

namespace geist::detail {

// Lowers the bounded comments/back-matter semantic model into output-neutral
// document nodes. This pass consumes source fields and marker dispositions;
// it never reparses the flattened physical-row text.
std::optional<DocumentIR>
lower_comment_delivery_to_document_ir(TopicIdentityIR topic,
                                      const CommentDeliveryIR &delivery,
                                      std::string *error = nullptr);

// Requires an exact canonical lowering, including source slices and physical
// row evidence. This is deliberately independent of any Markdown renderer.
bool verify_comment_delivery_document_ir(const CommentDeliveryIR &delivery,
                                         const DocumentIR &document,
                                         std::string *error = nullptr);

} // namespace geist::detail
