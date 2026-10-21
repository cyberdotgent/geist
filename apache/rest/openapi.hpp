// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string_view>

namespace geist_apache::rest {

inline constexpr std::string_view openapi_media_type =
    "application/json; charset=utf-8";

// The machine-readable contract served from each enabled shelf. The relative
// server URL deliberately leaves the shelf mount point under Apache's control.
std::string_view openapi_document();

} // namespace geist_apache::rest
