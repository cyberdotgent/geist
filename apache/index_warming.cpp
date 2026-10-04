// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "index_warming.hpp"
#include "cache_store.hpp"
#include "config.hpp"
#include "shelf_index.hpp"

#include <http_config.h>
#include <http_core.h>
#include <http_log.h>
#include <http_protocol.h>
#include <http_request.h>
#include <apr_buckets.h>
#include <apr_strings.h>
#include <apr_time.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <utility>
#include <unistd.h>

extern "C" module AP_MODULE_DECLARE_DATA geist_module;
APLOG_USE_MODULE(geist);

namespace geist_apache {
namespace {
namespace fs = std::filesystem;

struct Pool {
  apr_pool_t* value = nullptr;
  Pool() {
    // No parent pool: request threads must not share the allocator used by
    // this thread's synthetic directory requests and scans.
    if (apr_pool_create_unmanaged(&value) != APR_SUCCESS)
      throw std::runtime_error("cannot create index warmer pool");
  }
  ~Pool() { apr_pool_destroy(value); }
};

std::vector<std::string> roots_for(server_rec* server) {
  auto* core = static_cast<core_server_config*>(
      ap_get_core_module_config(server->module_config));
  std::set<std::string> roots;
  // Walk the document root even if enabled only through .htaccess. This
  // discovers directories, not book identities, when BooIndex is Off.
  if (core->ap_document_root) roots.insert(core->ap_document_root);
  auto** sections = reinterpret_cast<ap_conf_vector_t**>(core->sec_dir->elts);
  for (int i = 0; i < core->sec_dir->nelts; ++i) {
    auto* config = static_cast<DirConfig*>(ap_get_module_config(sections[i], &geist_module));
    auto* dir = static_cast<core_dir_config*>(ap_get_core_module_config(sections[i]));
    if (!config || config->index != Tri::on || !dir->d || dir->r) continue;
    fs::path root(dir->d);
    if (dir->d_is_fnmatch) {
      const std::string pattern(dir->d);
      const auto wildcard = pattern.find_first_of("*?[");
      const auto slash = pattern.rfind('/', wildcard);
      root = slash == std::string::npos ? fs::path() : fs::path(pattern.substr(0, slash));
    }
    // A global <Directory /> is a policy, not a request to scan the OS.
    if (root.is_absolute() && root != root.root_path()) roots.insert(root.lexically_normal().string());
  }
  auto* config = static_cast<ServerConfig*>(ap_get_module_config(server->module_config, &geist_module));
  if (config && config->preload) {
    for (int i = 0; i < config->preload->nelts; ++i)
      roots.insert(APR_ARRAY_IDX(config->preload, i, const char*));
  }
  return {roots.begin(), roots.end()};
}

struct WalkTask {
  std::string root;
  const char* cache_directory;
  const char* generation;
  std::vector<server_rec*> servers;
};

std::vector<WalkTask> tasks_for(server_rec* servers) {
  std::map<std::string, std::vector<server_rec*>> groups;
  for (auto* server = servers; server; server = server->next) {
    auto* config = static_cast<ServerConfig*>(ap_get_module_config(server->module_config, &geist_module));
    if (config && config->cache_dir && config->generation)
      groups[config->cache_dir].push_back(server);
  }
  std::vector<WalkTask> tasks;
  for (const auto& group : groups) {
    std::set<std::string> roots;
    for (auto* server : group.second)
      for (auto root : roots_for(server)) {
        root = fs::path(root).lexically_normal().string();
        while (root.size() > 1 && root.back() == '/') root.pop_back();
        roots.insert(std::move(root));
      }
    std::vector<std::string> compact;
    for (const auto& root : roots) {
      bool covered = false;
      for (const auto& parent : compact)
        if (root == parent || root.compare(0, parent.size() + 1, parent + "/") == 0)
          covered = true;
      if (!covered) compact.push_back(root);
    }
    auto* config = static_cast<ServerConfig*>(ap_get_module_config(
        group.second.front()->module_config, &geist_module));
    for (const auto& root : compact)
      tasks.push_back({root, config->cache_dir, config->generation, group.second});
  }
  return tasks;
}

// Resolve only filesystem configuration. No handlers, authentication or
// rendering run on these internal requests. Apache itself handles Directory,
// DirectoryMatch, AllowOverride and .htaccess inheritance.
bool enabled_for(server_rec* server, apr_pool_t* pool, const std::string& directory) {
  auto* connection = static_cast<conn_rec*>(apr_pcalloc(pool, sizeof(conn_rec)));
  connection->pool = pool;
  connection->base_server = server;
  connection->conn_config = ap_create_conn_config(pool);
  connection->bucket_alloc = apr_bucket_alloc_create(pool);
  connection->client_ip = apr_pstrdup(pool, "127.0.0.1");
  apr_sockaddr_info_get(&connection->client_addr, "127.0.0.1", APR_INET, 0, 0, pool);
  connection->local_addr = connection->client_addr;
  auto* request = ap_create_request(connection);
  request->filename = apr_pstrcat(request->pool, directory.c_str(), "/", nullptr);
  request->uri = apr_pstrdup(request->pool, "/");
  request->unparsed_uri = request->uri;
  request->method = "GET";
  request->method_number = M_GET;
  const int status = ap_directory_walk(request);
  auto* config = static_cast<DirConfig*>(ap_get_module_config(request->per_dir_config, &geist_module));
  auto* core = static_cast<core_dir_config*>(ap_get_core_module_config(request->per_dir_config));
  // Request-dependent <If> expressions cannot be evaluated as if the
  // directory had a single URL/client. Leave those shelves to real requests.
  const bool conditional = core->sec_if && core->sec_if->nelts != 0;
  const bool enabled = status == OK && config && config->index == Tri::on && !conditional;
  apr_pool_destroy(request->pool);
  return enabled;
}

struct Warmer {
  std::atomic<bool> stop{false};
  std::thread thread;

  void run(server_rec* servers) {
    try {
      for (const auto& task : tasks_for(servers)) {
        if (stop.load()) return;
        auto* server = task.servers.front();
        const auto& root = task.root;
        CacheStore store(task.cache_directory);
        if (!store) {
          ap_log_error(APLOG_MARK, APLOG_WARNING, 0, server,
              "mod_geist: cache directory %s unavailable; background warming skipped",
              task.cache_directory);
          continue;
        }
        const auto walk_key = "walk:" + root;
        // All children inherit the same configuration-generation marker.
        // A new startup/reload walks again; later children don't repeat it.
        const auto marker_key = walk_key + ":generation";
        const std::string marker = root + "\n" + task.generation;
        const std::vector<std::uint8_t> marker_bytes(marker.begin(), marker.end());
        CacheStore::Lock lock;
        while (!stop.load()) {
          if (store.read(marker_key, 65536) == std::optional<std::vector<std::uint8_t>>(marker_bytes))
            break;
          bool contended = false;
          lock = store.try_lock(walk_key, &contended);
          if (lock) break;
          if (!contended) {
            ap_log_error(APLOG_MARK, APLOG_WARNING, 0, server,
                "mod_geist: cannot lock background walk for %s", root.c_str());
            break;
          }
          // A previous generation may still be finishing, or the elected
          // child may exit early. Retrying here never delays request threads.
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!lock || stop.load()) continue;
        if (store.read(marker_key, 65536) == std::optional<std::vector<std::uint8_t>>(marker_bytes))
          continue;
        const auto started = apr_time_now();
        ShelfStats totals;
        std::size_t shelves = 0, visited = 0;
        bool complete = true;
        std::vector<std::pair<fs::path, unsigned>> pending{{fs::path(root), 0}};
        while (!pending.empty() && !stop.load()) {
          auto [path, depth] = std::move(pending.back());
          pending.pop_back();
          if (++visited > 1000000 || depth > 256) { complete = false; break; }
          std::error_code error;
          // Never recurse into directory symlinks (including a root link).
          if (!fs::is_directory(fs::symlink_status(path, error)) || error) continue;
          const auto directory = path.string();
          Pool pool;
          // Shared roots are walked once. A shelf is warmed if any vhost
          // sharing this cache enables it; HTML/access policy stays in the
          // real request, and plain metadata is reusable by every vhost.
          const bool enabled = std::any_of(task.servers.begin(), task.servers.end(),
              [&](server_rec* candidate) { return enabled_for(candidate, pool.value, directory); });
          if (enabled) {
            const auto files = scan_shelf(pool.value, directory.c_str(), &stop);
            ShelfStats stats;
            shelf_entries(server, pool.value, directory, task.cache_directory, files, &stop, &stats);
            if (!files.empty()) ++shelves;
            totals.disk_hits += stats.disk_hits;
            totals.memory_hits += stats.memory_hits;
            totals.probes += stats.probes;
            forget_missing(directory, files);
          }
          // Off suppresses probing this shelf, but a descendant can turn
          // On again, so it must not prune filesystem discovery.
          fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, error), end;
          while (!error && it != end && !stop.load()) {
            if (pending.size() + visited >= 1000000) { complete = false; break; }
            if (fs::is_directory(it->symlink_status(error)) && !error)
              pending.emplace_back(it->path(), depth + 1);
            it.increment(error);
          }
          if (!complete) break;
        }
        if (stop.load()) return;
        if (complete) (void)store.write(marker_key, marker_bytes);
        ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, server,
            "mod_geist: warmed %" APR_SIZE_T_FMT " shelf(s) under %s: %"
            APR_SIZE_T_FMT " disk hit(s), %" APR_SIZE_T_FMT
            " memory hit(s), %" APR_SIZE_T_FMT " probe(s) in %" APR_TIME_T_FMT " ms%s",
            shelves, root.c_str(), totals.disk_hits, totals.memory_hits, totals.probes,
            apr_time_as_msec(apr_time_now() - started), complete ? "" : " (walk limit reached)");
      }
    } catch (const std::exception& error) {
      ap_log_error(APLOG_MARK, APLOG_ERR, 0, servers,
          "mod_geist: background index warming failed: %s", error.what());
    } catch (...) {
      ap_log_error(APLOG_MARK, APLOG_ERR, 0, servers,
          "mod_geist: background index warming failed");
    }
  }
};

apr_status_t stop_warmer(void* data) {
  auto* warmer = static_cast<Warmer*>(data);
  warmer->stop.store(true);
  if (warmer->thread.joinable()) warmer->thread.join();
  delete warmer;
  return APR_SUCCESS;
}
} // namespace

void start_index_warming(apr_pool_t* child_pool, server_rec* servers) {
  try {
    auto warmer = std::make_unique<Warmer>();
    auto* state = warmer.get();
    warmer->thread = std::thread([state, servers] { state->run(servers); });
    apr_pool_pre_cleanup_register(child_pool, warmer.release(), stop_warmer);
  } catch (const std::exception& error) {
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, servers,
        "mod_geist: cannot start background index warming: %s", error.what());
  }
}

} // namespace geist_apache
