// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Shared helpers for the per-book lazy_open_* integration tests.

#include "test_failures.hpp"

#include <cstddef>
#include <iostream>
#include <string>

inline void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    geist_test::record_failure();
  }
}

inline std::size_t substring_count(const std::string& value,
                                   const std::string& needle) {
  std::size_t count = 0;
  for (auto at = value.find(needle); at != std::string::npos;
       at = value.find(needle, at + needle.size()))
    ++count;
  return count;
}

inline std::string markdown_visible_text(const std::string& markdown) {
  std::string visible;
  visible.reserve(markdown.size());
  for (std::size_t index = 0; index < markdown.size(); ++index) {
    if (markdown[index] == '\\' && index + 1 < markdown.size()) {
      visible.push_back(markdown[++index]);
    } else {
      visible.push_back(markdown[index]);
    }
  }
  return visible;
}
