// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <httpd.h>

namespace geist_apache {
struct ServerConfig {
  const char* cache_dir = nullptr;
  const char* generation = nullptr;
  // Compatibility with old configurations; these are asynchronous roots now.
  apr_array_header_t* preload = nullptr;
};

enum class Tri { unset, off, on };
enum class Theme { unset, automatic, light, dark };

struct DirConfig {
  Tri download = Tri::unset;
  Theme theme = Theme::unset;
  // Listing names and titles is an operator's disclosure decision. Unset
  // inherits, and the effective default is Off.
  Tri index = Tri::unset;
  const char* index_title = nullptr;
  Tri hide_version = Tri::unset;
};
} // namespace geist_apache
