// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "../apache/machine/json.hpp"
#include "../apache/mcp/mcp.hpp"
#include "../apache/service/catalog.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

using geist_apache::machine::JsonValue;
using geist_apache::machine::parse_json;
using geist_apache::mcp::Request;
using geist_apache::mcp::Response;

int failures = 0;

void require(bool condition, const std::string &message) {
  if (condition)
    return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

const JsonValue *path(const JsonValue &value,
                      std::initializer_list<const char *> parts) {
  auto *current = &value;
  for (const auto *part : parts) {
    current = current->find(part);
    if (current == nullptr)
      return nullptr;
  }
  return current;
}

Request modern(std::string method, std::string body, std::string name = {}) {
  Request request;
  request.method = "POST";
  request.content_type = "application/json; charset=utf-8";
  request.accept = "application/json, text/event-stream";
  request.protocol_version = "2026-07-28";
  request.mcp_method = std::move(method);
  if (!name.empty())
    request.mcp_name = std::move(name);
  request.body = std::move(body);
  return request;
}

JsonValue json(const Response &response) {
  require(response.content_type == "application/json", "MCP response is JSON");
  return parse_json(response.body);
}

std::string meta() {
  return R"("_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientInfo":{"name":"test","version":"1"},"io.modelcontextprotocol/clientCapabilities":{}})";
}

void test_transport(const geist_apache::service::Catalog &catalog) {
  Request get;
  get.method = "GET";
  auto response = geist_apache::mcp::handle(get, catalog);
  require(response.status == 405 && response.allow == "POST",
          "Streamable HTTP endpoint is POST-only");

  auto request = modern("tools/list", "{}");
  request.origin_valid = false;
  response = geist_apache::mcp::handle(request, catalog);
  require(response.status == 403,
          "caller-supplied Origin rejection is honored");

  request = modern("tools/list", "{}");
  request.body.assign(1024 * 1024 + 1, 'x');
  response = geist_apache::mcp::handle(request, catalog);
  require(response.status == 413, "body size is capped at one MiB");

  request =
      modern("tools/list",
             R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})");
  request.accept = "application/json";
  response = geist_apache::mcp::handle(request, catalog);
  require(response.status == 406, "modern clients advertise JSON and SSE");

  request = modern("tools/list", "{}");
  request.content_encoding = "gzip";
  response = geist_apache::mcp::handle(request, catalog);
  require(response.status == 415, "encoded request bodies are rejected");

  request = modern("tools/list", "{}");
  request.content_type = "application/json; charset=iso-8859-1";
  response = geist_apache::mcp::handle(request, catalog);
  require(response.status == 415, "non-UTF-8 JSON bodies are rejected");
}

void test_modern(const geist_apache::service::Catalog &catalog) {
  auto request =
      modern("server/discover", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":"
                                "\"server/discover\",\"params\":{" +
                                    meta() + "}}");
  auto response = geist_apache::mcp::handle(request, catalog);
  require(response.status == 200,
          "modern discovery succeeds without a session");
  auto value = json(response);
  require(path(value, {"result", "resultType"}) != nullptr &&
              path(value, {"result", "supportedVersions"}) != nullptr &&
              path(value, {"result", "_meta",
                           "io.modelcontextprotocol/serverInfo"}) != nullptr,
          "discovery has modern completion, versions, and server identity");

  request = modern(
      "tools/list",
      "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{" +
          meta() + "}}");
  response = geist_apache::mcp::handle(request, catalog);
  value = json(response);
  const auto *tools = path(value, {"result", "tools"});
  require(response.status == 200 && tools != nullptr && tools->is_array() &&
              tools->as_array().size() == 6,
          "the six agreed read-only tools are advertised");
  if (tools && tools->is_array()) {
    for (const auto &tool : tools->as_array()) {
      const auto *annotations = tool.find("annotations");
      require(annotations && annotations->find("readOnlyHint") &&
                  annotations->find("readOnlyHint")->as_bool(),
              "every tool is annotated read-only");
    }
  }

  request = modern(
      "tools/list",
      R"({"jsonrpc":"2.0","id":20,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}})");
  response = geist_apache::mcp::handle(request, catalog);
  require(response.status == 200,
          "modern clientInfo is optional when capabilities are supplied");

  request = modern(
      "tools/list",
      R"({"jsonrpc":"2.0","id":22,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientInfo":{"name":42,"version":"1"},"io.modelcontextprotocol/clientCapabilities":{}}}})");
  response = geist_apache::mcp::handle(request, catalog);
  value = json(response);
  require(response.status == 400 && path(value, {"error", "code"}) &&
              path(value, {"error", "code"})->number_text() == "-32602",
          "malformed optional clientInfo is an invalid-params error");

  request = modern(
      "tools/call",
      "{\"jsonrpc\":\"2.0\",\"id\":21,\"method\":\"tools/call\","
      "\"params\":{\"name\":\"list_books\",\"arguments\":{\"extra\":true}," +
          meta() + "}}",
      "list_books");
  response = geist_apache::mcp::handle(request, catalog);
  value = json(response);
  require(response.status == 400 && path(value, {"error", "code"}) &&
              path(value, {"error", "code"})->number_text() == "-32602",
          "invalid tool arguments return JSON-RPC invalid params");

  request = modern(
      "tools/call",
      "{\"jsonrpc\":\"2.0\",\"id\":23,\"method\":\"tools/call\","
      "\"params\":{\"name\":\"get_book\",\"arguments\":{\"book\":"
      "\"absent.boo\"}," +
          meta() + "}}",
      "get_book");
  response = geist_apache::mcp::handle(request, catalog);
  value = json(response);
  require(response.status == 200 && path(value, {"result", "isError"}) &&
              path(value, {"result", "isError"})->as_bool(),
          "valid tool calls report domain failures as tool results");

  request =
      modern("tools/call",
             "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/"
             "call\",\"params\":{\"name\":\"list_books\",\"arguments\":{}," +
                 meta() + "}}",
             "list_books");
  response = geist_apache::mcp::handle(request, catalog);
  value = json(response);
  require(response.status == 200 &&
              path(value, {"result", "structuredContent", "books"}) != nullptr,
          "list_books routes through the shared catalog");

  request.mcp_name = "list_books\xc3\xa9";
  response = geist_apache::mcp::handle(request, catalog);
  value = json(response);
  require(response.status == 400 && path(value, {"error", "code"}) &&
              path(value, {"error", "code"})->number_text() == "-32020",
          "unsafe plain Mcp-Name values require Base64 wrapping");

  request.mcp_name = "get_book";
  response = geist_apache::mcp::handle(request, catalog);
  value = json(response);
  require(response.status == 400 && path(value, {"error", "code"}) &&
              path(value, {"error", "code"})->number_text() == "-32020",
          "mirrored header mismatches use the specified error");

  request = modern(
      "tools/list",
      "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/list\",\"params\":{" +
          meta() + "}}");
  request.protocol_version = "2099-01-01";
  response = geist_apache::mcp::handle(request, catalog);
  value = json(response);
  require(response.status == 400 && path(value, {"error", "code"}) &&
              path(value, {"error", "code"})->number_text() == "-32020",
          "header/body version disagreement is a header mismatch");

  request.body =
      R"({"jsonrpc":"2.0","id":4,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2099-01-01","io.modelcontextprotocol/clientInfo":{"name":"test","version":"1"},"io.modelcontextprotocol/clientCapabilities":{}}}})";
  response = geist_apache::mcp::handle(request, catalog);
  value = json(response);
  require(response.status == 400 && path(value, {"error", "code"}) &&
              path(value, {"error", "code"})->number_text() == "-32022" &&
              path(value, {"error", "data", "supported"}),
          "matched unsupported versions identify both supported eras");

  request = modern("resources/templates/list",
                   "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"resources/"
                   "templates/list\",\"params\":{" +
                       meta() + "}}");
  response = geist_apache::mcp::handle(request, catalog);
  value = json(response);
  const auto *templates = path(value, {"result", "resourceTemplates"});
  require(templates && templates->is_array() &&
              templates->as_array().size() == 6,
          "topic formats and resource representations have URI templates");

  request = modern("resources/read",
                   "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"resources/"
                   "read\",\"params\":{\"uri\":\"geist://shelf\"," +
                       meta() + "}}",
                   "=?base64?Z2Vpc3Q6Ly9zaGVsZg==?=");
  response = geist_apache::mcp::handle(request, catalog);
  value = json(response);
  require(response.status == 200 && path(value, {"result", "contents"}) &&
              path(value, {"result", "ttlMs"}),
          "shelf resource reads return cacheable modern content");
}

void test_legacy(const geist_apache::service::Catalog &catalog) {
  Request initialize;
  initialize.method = "POST";
  initialize.content_type = "application/json";
  initialize.accept = "application/json, text/event-stream";
  initialize.body =
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})";
  auto response = geist_apache::mcp::handle(initialize, catalog);
  auto value = json(response);
  require(response.status == 200 &&
              path(value, {"result", "protocolVersion"}) &&
              !path(value, {"result", "resultType"}),
          "legacy initialize negotiates without a request version header");

  Request initialized;
  initialized.method = "POST";
  initialized.content_type = "application/json";
  initialized.accept = "application/json, text/event-stream";
  initialized.protocol_version = "2025-11-25";
  initialized.body =
      R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
  response = geist_apache::mcp::handle(initialized, catalog);
  require(response.status == 202 && response.body.empty(),
          "legacy initialized notification has an empty 202 response");

  initialized.body =
      R"({"jsonrpc":"2.0","method":"tools/list","params":{}})";
  response = geist_apache::mcp::handle(initialized, catalog);
  require(response.status == 202 && response.body.empty(),
          "every accepted legacy notification has an empty 202 response");

  Request invalid_id = initialized;
  invalid_id.body =
      R"({"jsonrpc":"2.0","id":{},"method":"tools/list","params":{}})";
  response = geist_apache::mcp::handle(invalid_id, catalog);
  auto invalid = json(response);
  require(response.status == 400 && path(invalid, {"error", "code"}) &&
              path(invalid, {"error", "code"})->number_text() == "-32600" &&
              path(invalid, {"id"}) && path(invalid, {"id"})->is_null(),
          "JSON-RPC ids reject object values");

  invalid_id.body =
      R"({"jsonrpc":"2.0","id":null,"method":"tools/list","params":{}})";
  response = geist_apache::mcp::handle(invalid_id, catalog);
  invalid = json(response);
  require(response.status == 400 && path(invalid, {"error", "code"}) &&
              path(invalid, {"error", "code"})->number_text() == "-32600" &&
              path(invalid, {"id"}) && path(invalid, {"id"})->is_null(),
          "MCP request ids reject null values");

  Request list;
  list.method = "POST";
  list.content_type = "application/json";
  list.accept = "application/json, text/event-stream";
  list.protocol_version = "2025-11-25";
  list.body = R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})";
  response = geist_apache::mcp::handle(list, catalog);
  value = json(response);
  require(response.status == 200 && path(value, {"result", "tools"}) &&
              !path(value, {"result", "resultType"}),
          "legacy calls retain the pre-modern result shape");
}

} // namespace

int main() {
#ifndef GEIST_FIXTURE_DIR
#error "GEIST_FIXTURE_DIR must name the tests/fixtures directory"
#endif
  const auto document =
      std::make_shared<const geist::BooDocument>(geist::BooDocument::open(
          std::filesystem::path(GEIST_FIXTURE_DIR) / "packet.boo"));
  geist_apache::service::BookSource source;
  source.summary.id = "packet.boo";
  source.summary.title = "Packet";
  source.summary.readable = true;
  source.open = [document] { return document; };
  const geist_apache::service::Catalog catalog({std::move(source)});

  test_transport(catalog);
  test_modern(catalog);
  test_legacy(catalog);
  if (failures != 0)
    return 1;
  std::cout << "Apache MCP synthetic assertions complete\n";
  return 0;
}
