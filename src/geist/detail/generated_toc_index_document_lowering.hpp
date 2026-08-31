#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/generated_toc_index_ir.hpp"

#include <optional>
#include <string>

namespace geist::detail {

std::optional<DocumentIR> lower_generated_toc_index_topic_to_document_ir(
    TopicIdentityIR identity, const GeneratedTocIndexTopicIR& navigation,
    std::string* error = nullptr);
bool verify_generated_toc_index_topic_document_ir(
    const GeneratedTocIndexTopicIR& navigation, const DocumentIR& document,
    std::string* error = nullptr);

} // namespace geist::detail
