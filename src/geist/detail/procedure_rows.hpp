#pragma once

#include "geist/detail/internal.hpp"

#include <string>
#include <vector>

namespace geist::detail {

// One flag per decoded segment. A true flag is backed by an increasing
// source-local CFONT step number, not merely by rendered text resembling N.
std::vector<std::vector<bool>> numbered_procedure_step_segments(
    const std::vector<std::string>& decoded_records,
    const std::vector<DecodedLogicalRecordSource>& sources);

} // namespace geist::detail
