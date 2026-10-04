// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace geist_apache {

struct FileStamp {
  std::int64_t mtime = 0;
  std::int64_t ctime = 0;
  std::int64_t size = 0;
  std::uint64_t device = 0;
  std::uint64_t inode = 0;
  bool operator==(const FileStamp& other) const;
};

struct ShelfEntry {
  std::string filename;
  std::string title;
  std::string document_number;
  std::string built;
  std::int64_t size = 0;
  bool readable = true;
};

struct ShelfIdentity {
  FileStamp stamp;
  ShelfEntry entry;
};

struct ShelfSnapshot {
  std::string directory;
  std::string producer;
  std::vector<ShelfIdentity> identities;
};

constexpr std::size_t max_snapshot_bytes = 64 * 1024 * 1024;

// Explicit little-endian integers and length-prefixed binary strings. A bad
// snapshot is a cache miss; no partially decoded state is returned.
std::vector<std::uint8_t> encode_snapshot(const ShelfSnapshot& snapshot);
bool decode_snapshot(const std::vector<std::uint8_t>& bytes,
                     const std::string& directory, const std::string& producer,
                     ShelfSnapshot& snapshot);

} // namespace geist_apache
