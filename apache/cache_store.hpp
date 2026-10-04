// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geist_apache {

// Open relative to an anchored, non-symlink directory. Temporary files are
// exclusive, private to the worker user and renamed only when complete.
class CacheStore {
public:
  class Lock {
  public:
    explicit Lock(int fd = -1) : fd_(fd) {}
    ~Lock();
    Lock(Lock&& other) noexcept;
    Lock& operator=(Lock&& other) noexcept;
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    explicit operator bool() const { return fd_ >= 0; }
  private:
    int fd_;
  };

  explicit CacheStore(const char* directory);
  ~CacheStore();
  CacheStore(const CacheStore&) = delete;
  CacheStore& operator=(const CacheStore&) = delete;
  explicit operator bool() const { return directory_fd_ >= 0; }
  Lock try_lock(const std::string& key, bool* contended = nullptr) const;
  std::optional<std::vector<std::uint8_t>> read(
      const std::string& key, std::size_t limit) const;
  bool write(const std::string& key, const std::vector<std::uint8_t>& bytes) const;

  // A filename hash is a bucket, not proof of identity. Snapshot readers
  // must still check the full directory and producer in the payload.
  static std::string filename(const std::string& key);
private:
  int directory_fd_ = -1;
};

} // namespace geist_apache
