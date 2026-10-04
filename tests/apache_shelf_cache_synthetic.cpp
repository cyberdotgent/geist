// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "cache_store.hpp"
#include "shelf_snapshot.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

using namespace geist_apache;
namespace fs = std::filesystem;

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void reseal(std::vector<std::uint8_t>& bytes) {
  std::uint64_t checksum = 14695981039346656037ULL;
  for (std::size_t i = 0; i < bytes.size() - 8; ++i)
    checksum = (checksum ^ bytes[i]) * 1099511628211ULL;
  for (unsigned i = 0; i < 8; ++i) {
    bytes[bytes.size() - 8 + i] = static_cast<std::uint8_t>(checksum);
    checksum >>= 8;
  }
}

void codec() {
  ShelfSnapshot source{"/books", "decoder-1", {
      {{1, 2, 100, 3, 4}, {"a.boo", "Title & <tag>", "DOC-1", "date time", 100, true}},
      {{5, 6, 200, 3, 7}, {"b.boo", "Second", "DOC-2", "", 200, true}}}};
  const auto bytes = encode_snapshot(source);
  ShelfSnapshot output;
  require(decode_snapshot(bytes, source.directory, source.producer, output), "round trip failed");
  require(output.identities.size() == 2 &&
      output.identities[0].entry.title == source.identities[0].entry.title &&
      output.identities[1].stamp == source.identities[1].stamp, "round trip lost fields");
  require(!decode_snapshot(bytes, "/another", source.producer, output), "wrong directory accepted");
  require(!decode_snapshot(bytes, source.directory, "decoder-2", output), "wrong producer accepted");
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    const std::vector<std::uint8_t> truncated(bytes.begin(), bytes.begin() + length);
    require(!decode_snapshot(truncated, source.directory, source.producer, output), "truncation accepted");
  }
  auto corrupt = bytes;
  corrupt.back() ^= 1;
  require(!decode_snapshot(corrupt, source.directory, source.producer, output), "bad checksum accepted");
  corrupt = bytes;
  corrupt[8] = 2;
  reseal(corrupt);
  require(!decode_snapshot(corrupt, source.directory, source.producer, output), "unknown version accepted");
  corrupt = bytes;
  for (unsigned i = 12; i < 16; ++i) corrupt[i] = 0xff;
  reseal(corrupt);
  require(!decode_snapshot(corrupt, source.directory, source.producer, output), "unbounded string accepted");
  corrupt = bytes;
  const std::string second = "b.boo";
  auto where = std::search(corrupt.begin(), corrupt.end(), second.begin(), second.end());
  *where = 'a';
  reseal(corrupt);
  require(!decode_snapshot(corrupt, source.directory, source.producer, output), "duplicate filename accepted");
  *where = '/';
  reseal(corrupt);
  require(!decode_snapshot(corrupt, source.directory, source.producer, output), "filename path accepted");
  corrupt = bytes;
  corrupt.insert(corrupt.end() - 8, 0);
  reseal(corrupt);
  require(!decode_snapshot(corrupt, source.directory, source.producer, output), "trailing bytes accepted");
  require(output.identities.size() == 2, "failed decode modified published data");
}

void storage(const fs::path& root) {
  fs::create_directory(root / "cache");
  CacheStore store((root / "cache").c_str());
  require(static_cast<bool>(store), "cache directory rejected");
  const std::vector<std::uint8_t> first{0, 1, 2}, second{3, 4};
  require(store.write("shelf:/books", first), "write failed");
  require(store.read("shelf:/books", 3) == std::optional<std::vector<std::uint8_t>>(first), "read failed");
  require(!store.read("shelf:/books", 2), "size cap ignored");
  require(store.write("shelf:/books", second), "replacement failed");
  require(store.read("shelf:/books", 3) == std::optional<std::vector<std::uint8_t>>(second), "replacement torn");
  {
    auto lock = store.try_lock("walk:/books");
    require(static_cast<bool>(lock), "lock failed");
    auto competing = store.try_lock("walk:/books");
    require(!competing, "competing lock accepted");
  }
  require(static_cast<bool>(store.try_lock("walk:/books")), "lock did not release");
  const auto file = root / "cache" / CacheStore::filename("shelf:/books");
  const auto sentinel = root / "sentinel";
  fs::rename(file, sentinel);
  fs::create_symlink(sentinel, file);
  require(!store.read("shelf:/books", 3), "cache symlink followed");
  require(store.write("shelf:/books", first), "safe symlink replacement failed");
  require(fs::file_size(sentinel) == second.size(), "write followed cache symlink");
  fs::create_symlink(root / "cache", root / "link");
  CacheStore linked((root / "link").c_str());
  require(!linked, "cache directory symlink followed");
  CacheStore trailing_link(((root / "link").string() + "/").c_str());
  require(!trailing_link, "trailing slash bypassed directory symlink protection");
  const auto fifo = root / "cache" / CacheStore::filename("fifo");
  require(mkfifo(fifo.c_str(), 0600) == 0, "fifo setup failed");
  require(!store.read("fifo", 100), "cache FIFO accepted");
  fs::create_symlink(sentinel, root / "cache" / CacheStore::filename("lock:evil"));
  require(!store.try_lock("evil"), "lock symlink followed");
  CacheStore missing((root / "missing").c_str());
  require(!missing && !missing.write("x", first) && !missing.read("x", 3), "missing cache not a miss");
}
} // namespace

int main() {
  char temporary[] = "/tmp/geist-cache-test-XXXXXX";
  const char* name = mkdtemp(temporary);
  if (!name) return 1;
  const fs::path root(name);
  int result = 0;
  try { codec(); storage(root); }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
  fs::remove_all(root);
  return result;
}
