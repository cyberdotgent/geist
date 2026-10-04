// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "shelf_snapshot.hpp"

#include <httpd.h>
#include <atomic>
#include <vector>

namespace geist_apache {

struct ShelfFile {
  std::string path;
  std::string name;
  apr_time_t mtime = 0;
  apr_off_t size = 0;
  FileStamp stamp;
};

struct ShelfStats {
  std::size_t memory_hits = 0;
  std::size_t disk_hits = 0;
  std::size_t probes = 0;
  bool saved = false;
};

std::vector<ShelfFile> scan_shelf(apr_pool_t* pool, const char* directory,
                                 const std::atomic<bool>* stop = nullptr);
std::vector<ShelfEntry> shelf_entries(server_rec* server, apr_pool_t* pool,
    const std::string& directory, const char* cache_directory,
    const std::vector<ShelfFile>& files, const std::atomic<bool>* stop = nullptr,
    ShelfStats* stats = nullptr);
void forget_missing(const std::string& directory,
                    const std::vector<ShelfFile>& files);

} // namespace geist_apache
