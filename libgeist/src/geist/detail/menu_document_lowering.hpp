#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/menu_topic_ir.hpp"

#include <optional>
#include <string>

namespace geist::detail {

// Lowers the already validated menu semantics to output-neutral document
// objects. No BOO control text is interpreted at this boundary.
std::optional<DocumentIR>
lower_menu_topic_to_document_ir(TopicIdentityIR identity,
                                const MenuTopicIR &menu,
                                std::string *error = nullptr);

bool verify_menu_topic_document_ir(const MenuTopicIR &menu,
                                   const DocumentIR &document,
                                   std::string *error = nullptr);

} // namespace geist::detail
