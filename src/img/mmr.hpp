// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <string>

namespace geist::detail {

struct MmrCode {
  const char* bits;
  int run;
  bool terminating;
};

extern const MmrCode white_codes[];
extern const std::size_t white_code_count;
extern const MmrCode black_codes[];
extern const std::size_t black_code_count;
extern const MmrCode long_makeup_codes[];
extern const std::size_t long_makeup_code_count;

const MmrCode* find_exact_code(const MmrCode* codes,
                               std::size_t count,
                               const std::string& bits);
bool has_prefix_match(const MmrCode* codes,
                      std::size_t count,
                      const std::string& bits);

} // namespace geist::detail
