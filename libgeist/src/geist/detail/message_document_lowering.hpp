#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/message_topic_ir.hpp"

#include <optional>
#include <string>

namespace geist::detail {

std::optional<DocumentIR>
lower_message_topic_to_document_ir(TopicIdentityIR topic,
                                   const MessageTopicIR &message,
                                   std::string *error = nullptr);

bool verify_message_topic_document_ir(const MessageTopicIR &message,
                                      const DocumentIR &document,
                                      std::string *error = nullptr);

} // namespace geist::detail
