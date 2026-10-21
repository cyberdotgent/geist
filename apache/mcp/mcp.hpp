// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../service/catalog.hpp"

#include <optional>
#include <string>

namespace geist_apache::mcp {

// Apache validates the request target and Origin before constructing this
// transport-neutral view. Header values are passed without interpretation;
// the adapter enforces the MCP body/header mirroring rules.
struct Request {
  std::string method;
  std::string content_type;
  std::string accept;
  std::optional<std::string> origin;
  bool origin_valid = true;
  std::optional<std::string> protocol_version;
  std::optional<std::string> mcp_method;
  std::optional<std::string> mcp_name;
  std::string body;
};

struct Response {
  int status = 200;
  std::string content_type;
  std::string body;
  std::string allow;
  std::string protocol_version;
};

inline constexpr std::size_t max_request_bytes = 1024 * 1024;

Response handle(const Request &request, const service::Catalog &catalog);

} // namespace geist_apache::mcp
