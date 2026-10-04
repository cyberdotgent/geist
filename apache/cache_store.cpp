// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "cache_store.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace geist_apache {
namespace {
class Descriptor {
public:
  explicit Descriptor(int value) : fd(value) {}
  ~Descriptor() { if (fd >= 0) close(fd); }
  int fd;
};

bool regular_private_file(int fd, struct stat& info) {
  return fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) &&
         info.st_nlink == 1;
}
} // namespace

CacheStore::CacheStore(const char* directory) {
  if (directory != nullptr && *directory != '\0') {
    std::string path(directory);
    // A trailing slash makes POSIX resolve the final symlink before open.
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    directory_fd_ = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  }
}

CacheStore::~CacheStore() {
  if (directory_fd_ >= 0) close(directory_fd_);
}

CacheStore::Lock::~Lock() { if (fd_ >= 0) close(fd_); }
CacheStore::Lock::Lock(Lock&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
CacheStore::Lock& CacheStore::Lock::operator=(Lock&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) close(fd_);
    fd_ = std::exchange(other.fd_, -1);
  }
  return *this;
}

std::string CacheStore::filename(const std::string& key) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char byte : key) hash = (hash ^ byte) * 1099511628211ULL;
  char name[32];
  std::snprintf(name, sizeof(name), "%016llx.cache",
                static_cast<unsigned long long>(hash));
  return name;
}

CacheStore::Lock CacheStore::try_lock(const std::string& key, bool* contended) const {
  if (contended) *contended = false;
  if (!*this) return Lock();
  const auto name = filename("lock:" + key);
  int fd = openat(directory_fd_, name.c_str(),
                  O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
  struct stat info;
  if (!regular_private_file(fd, info)) {
    if (fd >= 0) close(fd);
    return Lock();
  }
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
    if (contended && (errno == EWOULDBLOCK || errno == EAGAIN)) *contended = true;
    if (fd >= 0) close(fd);
    return Lock();
  }
  // Lock files are never unlinked: replacing their inode breaks exclusion.
  return Lock(fd);
}

std::optional<std::vector<std::uint8_t>> CacheStore::read(
    const std::string& key, std::size_t limit) const {
  if (!*this) return std::nullopt;
  const auto name = filename(key);
  Descriptor file(openat(directory_fd_, name.c_str(),
                          O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  struct stat info;
  if (!regular_private_file(file.fd, info) || info.st_size < 0 ||
      static_cast<std::uint64_t>(info.st_size) > limit) return std::nullopt;
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(info.st_size));
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = ::read(file.fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return std::nullopt;
    offset += static_cast<std::size_t>(count);
  }
  // Our own writers never mutate an inode. Reject growth by other writers.
  std::uint8_t extra;
  if (::read(file.fd, &extra, 1) != 0) return std::nullopt;
  return bytes;
}

bool CacheStore::write(const std::string& key,
                        const std::vector<std::uint8_t>& bytes) const {
  if (!*this) return false;
  static std::atomic<unsigned long> serial{0};
  const auto name = filename(key);
  const auto temporary = name + ".tmp-" + std::to_string(getpid()) + "-" +
                         std::to_string(serial.fetch_add(1));
  Descriptor file(openat(directory_fd_, temporary.c_str(),
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  if (file.fd < 0) return false;
  std::size_t offset = 0;
  bool success = true;
  while (offset < bytes.size()) {
    const auto count = ::write(file.fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) { success = false; break; }
    offset += static_cast<std::size_t>(count);
  }
  success = success && fsync(file.fd) == 0;
  if (success)
    success = renameat(directory_fd_, temporary.c_str(), directory_fd_, name.c_str()) == 0;
  if (success) {
    // A failed directory sync can lose the new cache after a power failure;
    // it still leaves complete bytes and a miss is always recoverable.
    (void)fsync(directory_fd_);
  } else {
    unlinkat(directory_fd_, temporary.c_str(), 0);
  }
  return success;
}

} // namespace geist_apache
