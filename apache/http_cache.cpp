// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "http_cache.hpp"
#include "assets.hpp"
#include "geist/version.hpp"

#include <algorithm>
#include <apr_sha1.h>
#include <apr_strings.h>
#include <http_protocol.h>

namespace geist_apache {

std::string http_cache_key(std::initializer_list<std::string> fields) {
  apr_sha1_ctx_t context;
  apr_sha1_init(&context);
  for (const auto &field : fields) {
    const auto length = std::to_string(field.size()) + ":";
    apr_sha1_update(&context, length.data(), length.size());
    // apr_sha1_update's length argument is unsigned int. Chunk large fields
    // rather than truncating the length of an embedded asset or response.
    for (std::size_t offset = 0; offset < field.size();) {
      const auto count = static_cast<unsigned int>(
          std::min<std::size_t>(field.size() - offset, 1048576));
      apr_sha1_update(&context, field.data() + offset, count);
      offset += count;
    }
  }
  unsigned char digest[APR_SHA1_DIGESTSIZE];
  apr_sha1_final(digest, &context);
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (const auto byte : digest) {
    result += hex[byte >> 4];
    result += hex[byte & 15];
  }
  return result;
}

std::string http_file_identity(const apr_finfo_t &info) {
  return http_cache_key(
      {std::to_string(info.mtime), std::to_string(info.size),
       std::to_string((info.valid & APR_FINFO_CTIME) ? info.ctime : 0),
       std::to_string((info.valid & APR_FINFO_DEV) ? info.device : 0),
       std::to_string((info.valid & APR_FINFO_INODE) ? info.inode : 0)});
}

std::string http_renderer_identity(request_rec *r, const DirConfig *config) {
  return http_cache_key(
      {"geist-http-1", GEIST_MODULE_VERSION, GEIST_MODULE_REVISION,
       geist::library_version(), geist::library_revision(),
       geist_httpd::kAssetRevision, r->uri ? r->uri : "",
       r->filename ? r->filename : "",
       std::to_string(static_cast<int>(config ? config->theme : Theme::unset)),
       std::to_string(static_cast<int>(config ? config->download : Tri::unset)),
       std::to_string(
           static_cast<int>(config ? config->hide_version : Tri::unset)),
       config && config->index_title ? config->index_title : ""});
}

int http_cache_conditions(request_rec *r, const std::string &identity,
                          const char *policy, bool last_modified) {
  apr_table_setn(r->headers_out, "ETag",
                 apr_psprintf(r->pool, "W/\"%s\"", identity.c_str()));
  // Do not opt authenticated responses into shared caching.
  apr_table_setn(r->headers_out, "Cache-Control",
                 r->user || apr_table_get(r->headers_in, "Authorization")
                     ? "private, max-age=300"
                     : policy);
  const char *modified = apr_table_get(r->headers_in, "If-Modified-Since");
  const char *unmodified = apr_table_get(r->headers_in, "If-Unmodified-Since");
  if (!last_modified) {
    apr_table_unset(r->headers_out, "Last-Modified");
    apr_table_unset(r->headers_in, "If-Modified-Since");
    apr_table_unset(r->headers_in, "If-Unmodified-Since");
  }
  const int status = ap_meets_conditions(r);
  if (!last_modified) {
    if (modified)
      apr_table_setn(r->headers_in, "If-Modified-Since", modified);
    if (unmodified)
      apr_table_setn(r->headers_in, "If-Unmodified-Since", unmodified);
  }
  // A failed precondition must not acquire a long-lived freshness policy.
  if (status != OK && status != HTTP_NOT_MODIFIED) {
    apr_table_unset(r->headers_out, "ETag");
    apr_table_unset(r->headers_out, "Cache-Control");
  }
  return status;
}

} // namespace geist_apache
