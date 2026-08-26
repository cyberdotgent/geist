#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/generated_list_topic_ir.hpp"

#include <optional>
#include <string>

namespace geist::detail {

std::optional<DocumentIR> lower_generated_list_topic_to_document_ir(
    TopicIdentityIR identity, const GeneratedListTopicIR& list,
    std::string* error = nullptr);
bool verify_generated_list_topic_document_ir(
    const GeneratedListTopicIR& list, const DocumentIR& document,
    std::string* error = nullptr);

} // namespace geist::detail
