// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

// What this build of the library is.
//
// These are functions rather than macros on purpose: a consumer that links
// the shared library may be running against a different libgeist than the
// headers it compiled against -- which is the normal case once a
// distribution packages the two apart -- and only a call can report the
// library that actually got loaded.

#include "geist/export.hpp"

namespace geist {

// The release version, as `<major>.<minor>.<patch>`.
GEIST_API const char* library_version() noexcept;

// The build's provenance: `git describe --always --dirty`, so a development
// build is distinguishable from a release and a dirty tree from a clean one.
// `"unknown"` when the library was built outside a git checkout.
GEIST_API const char* library_revision() noexcept;

} // namespace geist
