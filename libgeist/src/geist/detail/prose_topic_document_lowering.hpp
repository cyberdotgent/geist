#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/prose_topic_ir.hpp"

#include <optional>
#include <string>

namespace geist::detail {

std::optional<DocumentIR> lower_prose_topic_to_document_ir(
    TopicIdentityIR topic, const ProseTopicIR& prose,
    std::string* error = nullptr);
bool verify_prose_topic_document_ir(const ProseTopicIR& prose,
                                    const DocumentIR& document,
                                    std::string* error = nullptr);

} // namespace geist::detail
