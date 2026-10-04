// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "shelf_snapshot.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace geist_apache {
namespace {
constexpr std::uint8_t magic[] = {'G', 'S', 'T', 'I', 'D', 'X', '0', '1'};
constexpr std::size_t max_string_bytes = 65536;
constexpr std::size_t max_identities = 100000;

// Detect accidental corruption. Cache files are not authenticated: their
// directory must remain writable only by the trusted worker identity.
std::uint64_t checksum(const std::vector<std::uint8_t>& bytes, std::size_t size) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
  return hash;
}

bool valid_filename(const std::string& name) {
  return !name.empty() && name != "." && name != ".." &&
         name.find_first_of("/\\") == std::string::npos &&
         name.find('\0') == std::string::npos;
}

struct Writer {
  std::vector<std::uint8_t> bytes;
  void number(std::uint64_t value, unsigned width) {
    if (bytes.size() > max_snapshot_bytes - width)
      throw std::length_error("shelf snapshot too large");
    for (unsigned i = 0; i < width; ++i) {
      bytes.push_back(static_cast<std::uint8_t>(value));
      value >>= 8;
    }
  }
  void text(const std::string& value) {
    if (value.size() > max_string_bytes ||
        bytes.size() > max_snapshot_bytes - 4 - value.size())
      throw std::length_error("shelf snapshot string too large");
    number(value.size(), 4);
    bytes.insert(bytes.end(), value.begin(), value.end());
  }
  void signed_number(std::int64_t value) {
    if (value < 0) throw std::invalid_argument("negative shelf file metadata");
    number(static_cast<std::uint64_t>(value), 8);
  }
};

struct Reader {
  const std::vector<std::uint8_t>& bytes;
  std::size_t offset = 0;
  bool number(std::uint64_t& value, unsigned width) {
    if (width > bytes.size() - offset) return false;
    value = 0;
    for (unsigned i = 0; i < width; ++i)
      value |= std::uint64_t(bytes[offset++]) << (8 * i);
    return true;
  }
  bool signed_number(std::int64_t& value) {
    std::uint64_t number_value;
    if (!number(number_value, 8) ||
        number_value > std::uint64_t(std::numeric_limits<std::int64_t>::max()))
      return false;
    value = static_cast<std::int64_t>(number_value);
    return true;
  }
  bool text(std::string& value) {
    std::uint64_t length;
    if (!number(length, 4) || length > max_string_bytes ||
        length > bytes.size() - offset) return false;
    value.assign(reinterpret_cast<const char*>(bytes.data() + offset),
                 static_cast<std::size_t>(length));
    offset += static_cast<std::size_t>(length);
    return true;
  }
};
} // namespace

bool FileStamp::operator==(const FileStamp& other) const {
  return mtime == other.mtime && ctime == other.ctime && size == other.size &&
         device == other.device && inode == other.inode;
}

std::vector<std::uint8_t> encode_snapshot(const ShelfSnapshot& snapshot) {
  if (snapshot.identities.size() > max_identities)
    throw std::length_error("too many shelf identities");
  Writer out;
  out.bytes.assign(std::begin(magic), std::end(magic));
  out.number(1, 4);
  out.text(snapshot.directory);
  out.text(snapshot.producer);
  out.number(snapshot.identities.size(), 4);
  std::set<std::string> names;
  for (const auto& identity : snapshot.identities) {
    const auto& entry = identity.entry;
    if (!entry.readable || !valid_filename(entry.filename) ||
        !names.insert(entry.filename).second || entry.size != identity.stamp.size)
      throw std::invalid_argument("invalid shelf identity");
    out.text(entry.filename);
    out.signed_number(identity.stamp.mtime);
    out.signed_number(identity.stamp.ctime);
    out.signed_number(identity.stamp.size);
    out.number(identity.stamp.device, 8);
    out.number(identity.stamp.inode, 8);
    out.text(entry.title);
    out.text(entry.document_number);
    out.text(entry.built);
  }
  out.number(checksum(out.bytes, out.bytes.size()), 8);
  return std::move(out.bytes);
}

bool decode_snapshot(const std::vector<std::uint8_t>& bytes,
                     const std::string& directory, const std::string& producer,
                     ShelfSnapshot& snapshot) {
  if (bytes.size() < sizeof(magic) + 8 || bytes.size() > max_snapshot_bytes ||
      !std::equal(std::begin(magic), std::end(magic), bytes.begin())) return false;
  Reader trailer{bytes, bytes.size() - 8};
  std::uint64_t expected;
  if (!trailer.number(expected, 8) || expected != checksum(bytes, bytes.size() - 8))
    return false;
  Reader in{bytes, sizeof(magic)};
  ShelfSnapshot fresh;
  std::uint64_t version, count;
  if (!in.number(version, 4) || version != 1 || !in.text(fresh.directory) ||
      fresh.directory != directory || !in.text(fresh.producer) ||
      fresh.producer != producer || !in.number(count, 4) ||
      count > max_identities || count > (bytes.size() - in.offset) / 56)
    return false;
  std::set<std::string> names;
  for (std::uint64_t i = 0; i < count; ++i) {
    ShelfIdentity identity;
    auto& entry = identity.entry;
    if (!in.text(entry.filename) || !valid_filename(entry.filename) ||
        !names.insert(entry.filename).second ||
        !in.signed_number(identity.stamp.mtime) ||
        !in.signed_number(identity.stamp.ctime) ||
        !in.signed_number(identity.stamp.size) ||
        !in.number(identity.stamp.device, 8) ||
        !in.number(identity.stamp.inode, 8) || !in.text(entry.title) ||
        !in.text(entry.document_number) || !in.text(entry.built)) return false;
    entry.size = identity.stamp.size;
    fresh.identities.push_back(std::move(identity));
  }
  if (in.offset != bytes.size() - 8) return false;
  snapshot = std::move(fresh);
  return true;
}

} // namespace geist_apache
