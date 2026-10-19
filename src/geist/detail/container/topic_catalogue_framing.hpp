// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/core/internal.hpp"

namespace geist::detail {

// Lexical SH admission remains the compatibility rule for undecided framing.
// A framed candidate must instead begin at its native instruction origin.
bool is_topic_header_record(const DecodedLogicalRecordSource &record);

// Uses retained initial-walk source evidence when available, otherwise performs
// source decoding only after the inexpensive lexical prefilter. No control-
// segment or semantic/layout pipeline is needed for admission.
bool is_topic_header_record(const LogicalDecodeContext &context,
                            std::size_t record_index);

} // namespace geist::detail
