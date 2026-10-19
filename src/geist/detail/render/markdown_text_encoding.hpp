// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string_view>

namespace geist::detail {

// The one punctuation alphabet used by both the serializer and its emitted
// text verifier. Code/literal and raw-HTML windows have separate encodings.
inline bool is_markdown_punctuation(const char ch) {
  return std::string_view("\\`*_{}[]<>()#+-.!|~").find(ch) !=
         std::string_view::npos;
}

} // namespace geist::detail
