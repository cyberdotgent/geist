// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "config.hpp"
#include <apr_file_info.h>
#include <initializer_list>
#include <string>

namespace geist_apache {

inline constexpr const char *book_cache_policy =
    "public, max-age=300, s-maxage=604800";
inline constexpr const char *shelf_cache_policy =
    "public, max-age=60, s-maxage=300";

// Length-framed fields, stable across worker processes and restarts.
std::string http_cache_key(std::initializer_list<std::string> fields);
std::string http_file_identity(const apr_finfo_t &info);
std::string http_renderer_identity(request_rec *r, const DirConfig *config);

// Sets a weak ETag and evaluates HTTP preconditions before writing a body.
// Date preconditions are ignored unless the representation has a genuine
// Last-Modified timestamp (raw BOO downloads, not derived HTML or images).
int http_cache_conditions(request_rec *r, const std::string &identity,
                          const char *policy = book_cache_policy,
                          bool last_modified = false);

} // namespace geist_apache
