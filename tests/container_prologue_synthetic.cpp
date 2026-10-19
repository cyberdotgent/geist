// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/core/internal.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {
void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "container_prologue_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}
} // namespace

int main() {
  // These are directory-field tests, not examples of working older/newer
  // books. In particular, no version-1.3 dictionary is constructed here.
  for (const unsigned page : {1u, 2u}) {
    for (const std::uint8_t variant : {0xf0, 0xf1, 0xf2, 0xf3, 0xf9}) {
      for (const std::uint16_t parameter : {0u, 0x015fu, 0x01f4u, 0x1234u}) {
        std::vector<std::uint8_t> bytes((page + 1) * geist::boo_page_size);
        bytes[1] = static_cast<std::uint8_t>(page);
        const auto base = page * geist::boo_page_size;
        bytes[base + 0x10] = 0x40;
        bytes[base + 0x11] = 0xf1;
        bytes[base + 0x12] = 0x4b;
        bytes[base + 0x13] = variant;
        bytes[base + 0x14] = 0xd0;
        bytes[base + 0x15] = 0xe8; // Preserve the field, not a hard-coded F0.
        bytes[base + 0x17] = 1;    // One logical page: the directory itself.
        bytes[base + 0x4c] = static_cast<std::uint8_t>(parameter >> 8);
        bytes[base + 0x4d] = static_cast<std::uint8_t>(parameter);
        const auto parsed =
            geist::detail::read_container_prologue(bytes, "synthetic.boo");
        require(parsed.directory.version_variant == variant,
                "version variant was changed or rejected");
        require(parsed.directory.version_text ==
                    " 1." + std::to_string(variant - 0xf0),
                "version text was not preserved");
        require(parsed.directory.token_threshold == 0xd0,
                "token threshold changed");
        require(parsed.directory.extended_token_prefix == 0xe8,
                "extended-token prefix was not read at directory offset 0x15");
        require(
            parsed.directory.dictionary_text_parameter == parameter,
            "dictionary text parameter was not preserved as a big-endian word");
      }
    }
  }
}
