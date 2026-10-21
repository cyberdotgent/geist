// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../service/catalog.hpp"

#include <string>
#include <utility>
#include <vector>

namespace geist_apache::rest {

struct Request {
  std::string method;
  std::string path;
  std::string query;
};

struct Response {
  int status = 500;
  std::string content_type;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
};

// Routes one shelf-relative REST request. The caller is responsible for the
// BooIndex/GeistAPI feature gate and for writing this transport-neutral result
// to Apache's request_rec.
Response route(const Request& request, const service::Catalog& catalog,
               bool mcp_enabled = false);

} // namespace geist_apache::rest
