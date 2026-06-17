#pragma once

#include "geist/export.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace geist {

GEIST_API std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes);

} // namespace geist
