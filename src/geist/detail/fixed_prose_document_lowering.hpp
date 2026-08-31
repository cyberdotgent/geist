#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/fixed_prose_topic_ir.hpp"

#include <optional>
#include <string>

namespace geist::detail {

std::optional<DocumentIR> lower_fixed_prose_topic_to_document_ir(
    TopicIdentityIR topic, const FixedProseTopicIR& prose,
    std::string* error = nullptr);
bool verify_fixed_prose_topic_document_ir(
    const FixedProseTopicIR& prose, const DocumentIR& document,
    std::string* error = nullptr);

} // namespace geist::detail
