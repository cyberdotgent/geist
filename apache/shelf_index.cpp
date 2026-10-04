// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "shelf_index.hpp"
#include "cache_store.hpp"
#include "geist/probe.hpp"
#include "geist/version.hpp"

#include <http_config.h>
#include <http_log.h>
#include <apr_file_info.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <optional>
#include <set>

extern "C" module AP_MODULE_DECLARE_DATA geist_module;
APLOG_USE_MODULE(geist);

namespace geist_apache {
namespace {
std::mutex meta_mutex;
std::map<std::string, ShelfIdentity> metadata;

bool cancelled(const std::atomic<bool>* stop) { return stop && stop->load(); }

std::optional<FileStamp> stamp_for(apr_pool_t* pool, const std::string& path) {
  apr_finfo_t info{};
  const auto status = apr_stat(&info, path.c_str(),
      APR_FINFO_MIN | APR_FINFO_CTIME | APR_FINFO_IDENT, pool);
  const auto required = APR_FINFO_TYPE | APR_FINFO_MTIME | APR_FINFO_SIZE;
  if ((status != APR_SUCCESS && status != APR_INCOMPLETE) ||
      (info.valid & required) != required || info.filetype != APR_REG)
    return std::nullopt;
  FileStamp stamp;
  stamp.mtime = info.mtime;
  stamp.ctime = (info.valid & APR_FINFO_CTIME) ? info.ctime : 0;
  stamp.size = info.size;
  stamp.device = (info.valid & APR_FINFO_DEV) ? info.device : 0;
  stamp.inode = (info.valid & APR_FINFO_INODE) ? info.inode : 0;
  return stamp;
}

bool boo_name(std::string name) {
  if (name.size() < 4) return false;
  name = name.substr(name.size() - 4);
  std::transform(name.begin(), name.end(), name.begin(),
      [](unsigned char c) { return std::tolower(c); });
  return name == ".boo";
}
} // namespace

std::vector<ShelfFile> scan_shelf(apr_pool_t* pool, const char* directory,
                                 const std::atomic<bool>* stop) {
  std::vector<ShelfFile> files;
  apr_dir_t* dir = nullptr;
  if (apr_dir_open(&dir, directory, pool) != APR_SUCCESS) return files;
  apr_finfo_t info{};
  while (!cancelled(stop) && apr_dir_read(&info, APR_FINFO_NAME, dir) == APR_SUCCESS) {
    if (info.name == nullptr || info.name[0] == '.' || !boo_name(info.name)) continue;
    const auto path = std::string(directory) + "/" + info.name;
    const auto stamp = stamp_for(pool, path);
    if (stamp) files.push_back({path, info.name, stamp->mtime, stamp->size, *stamp});
  }
  apr_dir_close(dir);
  return files;
}

std::vector<ShelfEntry> shelf_entries(server_rec* server, apr_pool_t* pool,
    const std::string& directory, const char* cache_directory,
    const std::vector<ShelfFile>& files, const std::atomic<bool>* stop,
    ShelfStats* reported_stats) {
  ShelfStats stats;
  CacheStore store(cache_directory);
  // Include the decoder revision: even unchanged source files must be
  // re-probed after a fix that changes property extraction.
  const auto producer = std::string("shelf-1/") + geist::library_version() + "/" +
                        geist::library_revision();
  const auto key = "shelf:" + directory;
  auto lock = store.try_lock(key);
  ShelfSnapshot loaded;
  std::map<std::string, ShelfIdentity> disk;
  if (const auto bytes = store.read(key, max_snapshot_bytes)) {
    if (decode_snapshot(*bytes, directory, producer, loaded))
      for (auto& identity : loaded.identities)
        disk.emplace(identity.entry.filename, std::move(identity));
  }
  ShelfSnapshot fresh{directory, producer, {}};
  std::vector<ShelfEntry> entries;
  entries.reserve(files.size());
  bool changed = false;
  for (const auto& file : files) {
    if (cancelled(stop)) return entries;
    std::optional<ShelfIdentity> identity;
    {
      std::lock_guard<std::mutex> guard(meta_mutex);
      const auto found = metadata.find(file.path);
      if (found != metadata.end() && found->second.stamp == file.stamp) {
        identity = found->second;
        ++stats.memory_hits;
      }
    }
    if (!identity) {
      const auto found = disk.find(file.name);
      if (found != disk.end() && found->second.stamp == file.stamp) {
        identity = found->second;
        ++stats.disk_hits;
      }
    }
    if (!identity) {
      ++stats.probes;
      changed = true;
      ShelfEntry entry;
      entry.filename = file.name;
      entry.size = file.size;
      try {
        const auto summary = geist::probe_book(file.path);
        entry.title = summary.properties.title.empty()
            ? summary.properties.short_title : summary.properties.title;
        entry.document_number = summary.properties.document_number;
        entry.built = summary.directory.date.empty()
            ? std::string() : summary.directory.date + " " + summary.directory.time;
      } catch (const std::exception& error) {
        entry.readable = false;
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, server,
            "mod_geist: cannot read %s for the shelf: %s",
            file.path.c_str(), error.what());
      }
      if (entry.title.empty()) entry.title = file.name;
      identity = ShelfIdentity{file.stamp, std::move(entry)};
      // Never publish a success under a stamp from before the source changed.
      // A failed probe is retried on the next rebuild, not saved to disk.
      const auto after = stamp_for(pool, file.path);
      if (!after || !(*after == file.stamp)) identity->entry.readable = false;
    }
    if (identity->entry.readable) {
      {
        std::lock_guard<std::mutex> guard(meta_mutex);
        metadata[file.path] = *identity;
      }
      fresh.identities.push_back(*identity);
    }
    entries.push_back(identity->entry);
  }
  changed = changed || fresh.identities.size() != disk.size();
  if (lock && changed && !cancelled(stop)) {
    try {
      stats.saved = store.write(key, encode_snapshot(fresh));
      if (!stats.saved)
        ap_log_error(APLOG_MARK, APLOG_WARNING, 0, server,
            "mod_geist: could not save shelf cache for %s", directory.c_str());
    } catch (const std::exception& error) {
      ap_log_error(APLOG_MARK, APLOG_WARNING, 0, server,
          "mod_geist: shelf cache for %s skipped: %s", directory.c_str(), error.what());
    }
  }
  if (reported_stats) *reported_stats = stats;
  return entries;
}

void forget_missing(const std::string& directory, const std::vector<ShelfFile>& files) {
  std::set<std::string> present;
  for (const auto& file : files) present.insert(file.path);
  const auto prefix = directory + "/";
  std::lock_guard<std::mutex> guard(meta_mutex);
  for (auto it = metadata.lower_bound(prefix); it != metadata.end();) {
    if (it->first.compare(0, prefix.size(), prefix) != 0) break;
    if (it->first.find('/', prefix.size()) == std::string::npos &&
        present.count(it->first) == 0) it = metadata.erase(it);
    else ++it;
  }
}

} // namespace geist_apache
