#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/message_section_blocks_ir.hpp"
#include "geist/detail/message_topic_ir.hpp"

#include <optional>
#include <string>

namespace geist::detail {

// Lowers the verified message topic and its verified structured section
// blocks (tables, hanging lists, explicit preformatted fallbacks) to
// DocumentIR. A section with a block keeps every other paragraph as prose in
// source order; the lowering fails closed when the block and the flattened
// section text do not conserve the same words.
std::optional<DocumentIR>
lower_message_topic_to_document_ir(TopicIdentityIR topic,
                                   const MessageTopicIR &message,
                                   const MessageSectionBlocksIR &blocks,
                                   std::string *error = nullptr);

bool verify_message_topic_document_ir(const MessageTopicIR &message,
                                      const MessageSectionBlocksIR &blocks,
                                      const DocumentIR &document,
                                      std::string *error = nullptr);

} // namespace geist::detail
