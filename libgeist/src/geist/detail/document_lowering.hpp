#pragma once

#include "geist/detail/document_ir.hpp"

#include <string>
#include <vector>

namespace geist::detail {

// Output-neutral first migration step: retain every normalized legacy record
// in one state-safe region.  A renderer adapter can delegate this single block
// to the existing state machine without changing output.
DocumentIR lower_legacy_topic_to_document_ir(
    TopicIdentityIR topic, std::vector<std::string> normalized_records);

} // namespace geist::detail
