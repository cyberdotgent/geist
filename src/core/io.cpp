#include "geist/detail/core/internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace geist::detail {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open BOO file: " + path.string());
  }

  input.seekg(0, std::ios::end);
  const auto end = input.tellg();
  if (end < 0) {
    throw std::runtime_error("failed to determine BOO file size: " +
                             path.string());
  }

  input.seekg(0, std::ios::beg);

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
      throw std::runtime_error("failed to read complete BOO file: " +
                               path.string());
    }
  }

  return bytes;
}

std::uint16_t read_be16(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset) {
  if (offset + 2 > bytes.size()) {
    throw std::runtime_error("unexpected end of BOO file while reading u16");
  }

  return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

std::uint32_t read_be24(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset) {
  if (offset + 3 > bytes.size()) {
    throw std::runtime_error("unexpected end of BOO file while reading u24");
  }

  return (static_cast<std::uint32_t>(bytes[offset]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         static_cast<std::uint32_t>(bytes[offset + 2]);
}

std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset) {
  if (offset + 4 > bytes.size()) {
    throw std::runtime_error("unexpected end of BOO file while reading u32");
  }

  return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
         static_cast<std::uint32_t>(bytes[offset + 3]);
}


bool byte_range_is_valid(const std::vector<std::uint8_t>& bytes,
                         std::uint64_t offset,
                         std::uint64_t size) {
  return offset <= bytes.size() && size <= bytes.size() - offset;
}

} // namespace geist::detail
