// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/ir/message_topic_ir.hpp"
#include "geist/detail/ir/selector_ir.hpp"

namespace geist::detail {

// Native display lines supply paragraph and spacing evidence; physical row
// fragmentation is provenance, never an admission/cardinality requirement.
std::optional<MessageIntroductionIR> extract_message_introduction_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const VerifiedOwnershipIR &ownership,
    const SelectorCatalogIR &selectors, const MessageTopicIR &topic,
    std::string *error = nullptr);

} // namespace geist::detail
