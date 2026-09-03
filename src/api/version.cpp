// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/version.hpp"

namespace geist {

const char* library_version() noexcept { return GEIST_LIBRARY_VERSION; }

const char* library_revision() noexcept { return GEIST_LIBRARY_REVISION; }

} // namespace geist
