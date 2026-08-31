// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/export.hpp"

#include <cstdint>
#include <string>

namespace geist {

enum class ResourceLayout {
  legacy_v12,
  legacy_v13,
  converted_v14,
  unknown,
};

struct ResourceEntry {
  std::string id;
  std::string name;
  std::string stored_format;
  std::string kind;
  std::string description;
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
  std::uint64_t description_offset = 0;
  std::uint64_t description_size = 0;
  ResourceLayout layout = ResourceLayout::unknown;
};

GEIST_API const char* to_string(ResourceLayout layout) noexcept;

} // namespace geist
