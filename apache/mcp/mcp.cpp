// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "mcp.hpp"

#include "../machine/json.hpp"

#include "geist/version.hpp"

#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace geist_apache::mcp {
namespace {

using machine::JsonValue;
using Object = JsonValue::Object;
using Array = JsonValue::Array;

constexpr std::string_view modern_version = "2026-07-28";
constexpr std::string_view legacy_version = "2025-11-25";

Object object(std::initializer_list<std::pair<std::string, JsonValue>> fields) {
  Object result;
  for (auto &field : fields)
    result.emplace(field.first, field.second);
  return result;
}

JsonValue server_info() {
  return object({{"name", "geist"}, {"version", geist::library_version()}});
}

JsonValue capabilities() {
  return object({
      {"tools", object({{"listChanged", false}})},
      {"resources", object({{"subscribe", false}, {"listChanged", false}})},
  });
}

Response raw_response(int status, std::string body = {}) {
  Response result;
  result.status = status;
  result.content_type = body.empty() ? std::string{} : "application/json";
  result.body = std::move(body);
  return result;
}

Response json_response(int status, JsonValue value, std::string_view) {
  auto result = raw_response(status, machine::write_json(value));
  return result;
}

JsonValue error_value(const JsonValue &id, int code, std::string message,
                      std::optional<JsonValue> data = std::nullopt) {
  Object error = object({{"code", code}, {"message", std::move(message)}});
  if (data)
    error.emplace("data", std::move(*data));
  return object({{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(error)}});
}

Response error_response(int status, const JsonValue &id, int code,
                        std::string message,
                        std::string_view version = modern_version,
                        std::optional<JsonValue> data = std::nullopt) {
  return json_response(
      status, error_value(id, code, std::move(message), std::move(data)),
      version);
}

bool token(std::string_view values, std::string_view wanted) {
  const auto equal_ascii = [](std::string_view left, std::string_view right) {
    if (left.size() != right.size())
      return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
      if (std::tolower(static_cast<unsigned char>(left[index])) !=
          std::tolower(static_cast<unsigned char>(right[index])))
        return false;
    }
    return true;
  };
  std::size_t start = 0;
  while (start <= values.size()) {
    auto end = values.find(',', start);
    if (end == std::string_view::npos)
      end = values.size();
    auto part = values.substr(start, end - start);
    while (!part.empty() &&
           std::isspace(static_cast<unsigned char>(part.front())))
      part.remove_prefix(1);
    while (!part.empty() &&
           std::isspace(static_cast<unsigned char>(part.back())))
      part.remove_suffix(1);
    const auto semicolon = part.find(';');
    const auto media = part.substr(0, semicolon);
    bool disabled = false;
    if (semicolon != std::string_view::npos) {
      auto parameters = part.substr(semicolon + 1);
      while (!parameters.empty()) {
        const auto next = parameters.find(';');
        auto parameter = parameters.substr(0, next);
        while (!parameter.empty() &&
               std::isspace(static_cast<unsigned char>(parameter.front())))
          parameter.remove_prefix(1);
        if (parameter == "q=0" || parameter == "q=0.0" ||
            parameter == "q=0.00" || parameter == "q=0.000")
          disabled = true;
        if (next == std::string_view::npos)
          break;
        parameters.remove_prefix(next + 1);
      }
    }
    if (!disabled && (equal_ascii(media, wanted) || media == "*/*"))
      return true;
    if (end == values.size())
      break;
    start = end + 1;
  }
  return false;
}

bool json_content_type(std::string_view value) {
  const auto end = value.find(';');
  auto type = value.substr(0, end);
  while (!type.empty() &&
         std::isspace(static_cast<unsigned char>(type.front())))
    type.remove_prefix(1);
  while (!type.empty() && std::isspace(static_cast<unsigned char>(type.back())))
    type.remove_suffix(1);
  constexpr std::string_view json = "application/json";
  if (type.size() != json.size())
    return false;
  for (std::size_t index = 0; index < type.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(type[index])) != json[index])
      return false;
  }
  if (end != std::string_view::npos) {
    auto parameters = value.substr(end + 1);
    while (!parameters.empty()) {
      const auto next = parameters.find(';');
      auto parameter = parameters.substr(0, next);
      while (!parameter.empty() &&
             std::isspace(static_cast<unsigned char>(parameter.front())))
        parameter.remove_prefix(1);
      while (!parameter.empty() &&
             std::isspace(static_cast<unsigned char>(parameter.back())))
        parameter.remove_suffix(1);
      constexpr std::string_view prefix = "charset=";
      if (parameter.size() >= prefix.size()) {
        bool charset = true;
        for (std::size_t index = 0; index < prefix.size(); ++index) {
          charset = charset &&
                    std::tolower(static_cast<unsigned char>(parameter[index])) ==
                        prefix[index];
        }
        if (charset) {
          auto value = parameter.substr(prefix.size());
          if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
          std::string lowered(value);
          for (char &character : lowered)
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
          if (lowered != "utf-8" && lowered != "utf8") return false;
        }
      }
      if (next == std::string_view::npos) break;
      parameters.remove_prefix(next + 1);
    }
  }
  return true;
}

bool identity_encoding(std::string_view value) {
  if (value.empty()) return true;
  constexpr std::string_view identity = "identity";
  if (value.size() != identity.size()) return false;
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(value[index])) !=
        identity[index]) return false;
  }
  return true;
}

std::optional<std::string> base64_decode(std::string_view input) {
  static constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (input.size() % 4 != 0)
    return std::nullopt;
  std::string result;
  result.reserve(input.size() / 4 * 3);
  for (std::size_t at = 0; at < input.size(); at += 4) {
    std::uint32_t value = 0;
    int padding = 0;
    for (int index = 0; index < 4; ++index) {
      const char ch = input[at + static_cast<std::size_t>(index)];
      if (ch == '=') {
        if (index < 2)
          return std::nullopt;
        ++padding;
        value <<= 6;
      } else {
        if (padding != 0)
          return std::nullopt;
        const auto found = alphabet.find(ch);
        if (found == std::string_view::npos)
          return std::nullopt;
        value = (value << 6) | static_cast<std::uint32_t>(found);
      }
    }
    result.push_back(static_cast<char>((value >> 16) & 0xff));
    if (padding < 2)
      result.push_back(static_cast<char>((value >> 8) & 0xff));
    if (padding < 1)
      result.push_back(static_cast<char>(value & 0xff));
  }
  return result;
}

std::optional<std::string> decoded_header_name(const std::string &value) {
  constexpr std::string_view prefix = "=?base64?";
  constexpr std::string_view suffix = "?=";
  if (value.rfind(prefix, 0) != 0) {
    if (value.empty() ||
        std::isspace(static_cast<unsigned char>(value.front())) ||
        std::isspace(static_cast<unsigned char>(value.back())))
      return std::nullopt;
    for (const unsigned char byte : value) {
      if (byte < 0x20 || byte > 0x7e)
        return std::nullopt;
    }
    return value;
  }
  if (value.size() < prefix.size() + suffix.size() ||
      value.substr(value.size() - suffix.size()) != suffix)
    return std::nullopt;
  return base64_decode(std::string_view(value).substr(
      prefix.size(), value.size() - prefix.size() - suffix.size()));
}

std::string base64_encode(const std::vector<std::uint8_t> &input) {
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve((input.size() + 2) / 3 * 4);
  for (std::size_t at = 0; at < input.size(); at += 3) {
    const auto remaining = input.size() - at;
    std::uint32_t value = static_cast<std::uint32_t>(input[at]) << 16;
    if (remaining > 1)
      value |= static_cast<std::uint32_t>(input[at + 1]) << 8;
    if (remaining > 2)
      value |= input[at + 2];
    result.push_back(alphabet[(value >> 18) & 0x3f]);
    result.push_back(alphabet[(value >> 12) & 0x3f]);
    result.push_back(remaining > 1 ? alphabet[(value >> 6) & 0x3f] : '=');
    result.push_back(remaining > 2 ? alphabet[value & 0x3f] : '=');
  }
  return result;
}

std::string uri_encode(std::string_view input) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  for (const unsigned char ch : input) {
    const bool unreserved = (ch >= 'A' && ch <= 'Z') ||
                            (ch >= 'a' && ch <= 'z') ||
                            (ch >= '0' && ch <= '9') || ch == '-' ||
                            ch == '.' || ch == '_' || ch == '~';
    if (unreserved) {
      result.push_back(static_cast<char>(ch));
    } else {
      result.push_back('%');
      result.push_back(hex[ch >> 4]);
      result.push_back(hex[ch & 0xf]);
    }
  }
  return result;
}

int hex_value(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  return -1;
}

std::optional<std::string> uri_decode(std::string_view input) {
  std::string result;
  for (std::size_t at = 0; at < input.size(); ++at) {
    if (input[at] != '%') {
      result.push_back(input[at]);
      continue;
    }
    if (at + 2 >= input.size())
      return std::nullopt;
    const int high = hex_value(input[at + 1]);
    const int low = hex_value(input[at + 2]);
    if (high < 0 || low < 0)
      return std::nullopt;
    const char decoded = static_cast<char>((high << 4) | low);
    if (decoded == '/' || decoded == '\0')
      return std::nullopt;
    result.push_back(decoded);
    at += 2;
  }
  return result;
}

JsonValue book_summary(const service::BookSummary &value) {
  return object({{"id", value.id},
                 {"title", value.title},
                 {"documentNumber", value.document_number},
                 {"built", value.built},
                 {"size", value.size},
                 {"readable", value.readable}});
}

JsonValue book_detail(const service::BookDetail &value) {
  auto result = book_summary(value).as_object();
  result.emplace("language", value.language);
  result.emplace("formatVersion", value.format_version);
  result.emplace("buildVersion", value.build_version);
  result.emplace("copyright", value.copyright);
  result.emplace("security", value.security);
  result.emplace("date", value.date);
  Array authors;
  for (const auto &author : value.authors)
    authors.emplace_back(author);
  result.emplace("authors", std::move(authors));
  result.emplace("topicCount", static_cast<std::uint64_t>(value.topic_count));
  result.emplace("tocCount", static_cast<std::uint64_t>(value.toc_count));
  result.emplace("resourceCount",
                 static_cast<std::uint64_t>(value.resource_count));
  return result;
}

JsonValue topic_summary(const service::TopicSummary &value) {
  return object(
      {{"id", value.id},
       {"title", value.title},
       {"headingLevel", value.heading_level},
       {"sourceIndex", static_cast<std::uint64_t>(value.source_index)},
       {"topicNumber", static_cast<std::uint64_t>(value.topic_number)},
       {"startLogicalRecord",
        static_cast<std::uint64_t>(value.start_logical_record)},
       {"endLogicalRecord",
        static_cast<std::uint64_t>(value.end_logical_record)},
       {"inToc", value.in_toc},
       {"tocLevel", static_cast<std::uint64_t>(value.toc_level)},
       {"tocStyle", static_cast<std::uint64_t>(value.toc_style)}});
}

JsonValue source_coordinates(const service::SourceCoordinates &source) {
  return object({
      {"startLogicalRecord",
       static_cast<std::uint64_t>(source.start_logical_record)},
      {"endLogicalRecord",
       static_cast<std::uint64_t>(source.end_logical_record)},
      {"logicalRecord", static_cast<std::uint64_t>(source.logical_record)},
      {"segmentIndex", static_cast<std::uint64_t>(source.segment_index)},
      {"tokenBegin", static_cast<std::uint64_t>(source.token_begin)},
      {"tokenEnd", static_cast<std::uint64_t>(source.token_end)},
      {"byteBegin", static_cast<std::uint64_t>(source.byte_begin)},
      {"byteEnd", static_cast<std::uint64_t>(source.byte_end)},
  });
}

JsonValue diagnostic(const service::Diagnostic &value) {
  Array degradations;
  for (const auto &degradation : value.degradations) {
    degradations.emplace_back(object({
        {"block", degradation.block},
        {"reason", degradation.reason},
        {"detail", degradation.detail},
        {"source", source_coordinates(degradation.source)},
    }));
  }
  return object({{"severity", value.severity},
                 {"route", value.route},
                 {"family", value.family},
                 {"reason", value.reason},
                 {"detail", value.detail},
                 {"source", source_coordinates(value.source)},
                 {"degradations", std::move(degradations)},
                 {"degradationCount",
                  static_cast<std::uint64_t>(value.degradation_count)}});
}

JsonValue topic_detail(const service::TopicDetail &value) {
  auto result = topic_summary(value).as_object();
  result.emplace("diagnostic", diagnostic(value.diagnostic));
  return result;
}

JsonValue resource_summary(const service::ResourceSummary &value) {
  return object({{"id", value.id},
                 {"name", value.name},
                 {"kind", value.kind},
                 {"storedFormat", value.stored_format},
                 {"description", value.description},
                 {"layout", value.layout},
                 {"size", value.size}});
}

JsonValue tool_result(JsonValue structured, bool modern) {
  const auto text = machine::write_json(structured);
  Object result = object({
      {"content", Array{object({{"type", "text"}, {"text", text}})}},
      {"structuredContent", std::move(structured)},
      {"isError", false},
  });
  if (modern)
    result.emplace("resultType", "complete");
  return result;
}

JsonValue tool_failure(std::string message, bool modern) {
  Object result = object({
      {"content",
       Array{object({{"type", "text"}, {"text", std::move(message)}})}},
      {"isError", true},
  });
  if (modern)
    result.emplace("resultType", "complete");
  return result;
}

JsonValue annotations() {
  return object({{"readOnlyHint", true},
                 {"destructiveHint", false},
                 {"idempotentHint", true},
                 {"openWorldHint", false}});
}

JsonValue string_property(std::string description = {}) {
  Object result = object({{"type", "string"}});
  if (!description.empty())
    result.emplace("description", std::move(description));
  return result;
}

JsonValue schema(Object properties, Array required = {}) {
  return object({{"type", "object"},
                 {"properties", std::move(properties)},
                 {"required", std::move(required)},
                 {"additionalProperties", false}});
}

JsonValue tool(std::string name, std::string description, JsonValue input) {
  return object({{"name", std::move(name)},
                 {"description", std::move(description)},
                 {"inputSchema", std::move(input)},
                 {"annotations", annotations()}});
}

Array tools() {
  return {
      tool("list_books", "List books in this BOO shelf.", schema({})),
      tool("get_book", "Get metadata for one shelf book.",
           schema(object({{"book",
                           string_property("Exact shelf-relative filename.")}}),
                  Array{"book"})),
      tool("list_topics",
           "List every indexed topic in a book; optionally match ID or title.",
           schema(object({{"book", string_property()},
                          {"query", string_property()}}),
                  Array{"book"})),
      tool("get_topic",
           "Render an indexed topic and return its fidelity diagnostic.",
           schema(object({{"book", string_property()},
                          {"topic", string_property()},
                          {"format",
                           object({{"type", "string"},
                                   {"enum", Array{"markdown", "html"}}})}}),
                  Array{"book", "topic"})),
      tool("list_resources", "List stored media resources in a book.",
           schema(object({{"book", string_property()}}), Array{"book"})),
      tool("get_resource",
           "Read exact stored bytes or an explicit web representation.",
           schema(object({{"book", string_property()},
                          {"resource", string_property()},
                          {"representation",
                           object({{"type", "string"},
                                   {"enum", Array{"stored", "web"}}})}}),
                  Array{"book", "resource"})),
  };
}

const JsonValue *required_string(const Object &value, std::string_view key) {
  const auto found = value.find(key);
  if (found == value.end() || !found->second.is_string())
    return nullptr;
  return &found->second;
}

class InvalidToolParams : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

bool exact_arguments(const Object &arguments,
                     std::initializer_list<std::string_view> allowed) {
  for (const auto &argument : arguments) {
    bool found = false;
    for (const auto name : allowed)
      found = found || argument.first == name;
    if (!found)
      return false;
  }
  return true;
}

bool optional_string(const Object &arguments, std::string_view name) {
  const auto found = arguments.find(name);
  return found == arguments.end() || found->second.is_string();
}

JsonValue call_tool(const Object &params, const service::Catalog &catalog,
                    bool modern) {
  const auto *name_value = required_string(params, "name");
  if (!name_value)
    throw InvalidToolParams("Missing tool name.");
  const auto arguments_at = params.find("arguments");
  const Object empty;
  if (arguments_at != params.end() && !arguments_at->second.is_object())
    throw InvalidToolParams("Tool arguments must be an object.");
  const auto &args =
      arguments_at == params.end() ? empty : arguments_at->second.as_object();
  const auto &name = name_value->as_string();
  if (name == "list_books") {
    if (!args.empty())
      throw InvalidToolParams("list_books takes no arguments.");
    Array books;
    for (const auto &book : catalog.books())
      books.push_back(book_summary(book));
    return tool_result(object({{"books", std::move(books)}}), modern);
  }
  if (name != "get_book" && name != "list_topics" && name != "get_topic" &&
      name != "list_resources" && name != "get_resource")
    throw InvalidToolParams("Unknown tool name.");
  const auto *book = required_string(args, "book");
  if (!book)
    throw InvalidToolParams("Missing string argument: book.");
  if (name == "get_book") {
    if (!exact_arguments(args, {"book"}))
      throw InvalidToolParams("Invalid get_book arguments.");
    const auto value = catalog.book(book->as_string());
    return value ? tool_result(object({{"book", book_detail(*value)}}), modern)
                 : tool_failure("Book not found.", modern);
  }
  if (name == "list_topics") {
    if (!exact_arguments(args, {"book", "query"}) ||
        !optional_string(args, "query"))
      throw InvalidToolParams("Invalid list_topics arguments.");
    auto values = catalog.topics(book->as_string());
    if (!values)
      return tool_failure("Book not found.", modern);
    std::string query;
    if (const auto *item = required_string(args, "query"))
      query = item->as_string();
    Array topics;
    for (const auto &value : *values) {
      if (!query.empty() && value.id.find(query) == std::string::npos &&
          value.title.find(query) == std::string::npos)
        continue;
      topics.push_back(topic_summary(value));
    }
    return tool_result(object({{"topics", std::move(topics)}}), modern);
  }
  if (name == "get_topic") {
    if (!exact_arguments(args, {"book", "topic", "format"}) ||
        !optional_string(args, "format"))
      throw InvalidToolParams("Invalid get_topic arguments.");
    const auto *topic = required_string(args, "topic");
    if (!topic)
      throw InvalidToolParams("Missing string argument: topic.");
    auto format = service::TopicFormat::markdown;
    if (const auto *requested = required_string(args, "format")) {
      if (requested->as_string() == "html")
        format = service::TopicFormat::html;
      else if (requested->as_string() != "markdown")
        throw InvalidToolParams("Format must be markdown or html.");
    }
    const auto value =
        catalog.topic_content(book->as_string(), topic->as_string(), format);
    if (!value)
      return tool_failure("Book or topic not found.", modern);
    return tool_result(
        object({
            {"topic", topic_detail(value->topic)},
            {"format",
             format == service::TopicFormat::html ? "html" : "markdown"},
            {"mediaType", value->media_type},
            {"content", value->content},
        }),
        modern);
  }
  if (name == "list_resources") {
    if (!exact_arguments(args, {"book"}))
      throw InvalidToolParams("Invalid list_resources arguments.");
    const auto values = catalog.resources(book->as_string());
    if (!values)
      return tool_failure("Book not found.", modern);
    Array resources;
    for (const auto &value : *values)
      resources.push_back(resource_summary(value));
    return tool_result(object({{"resources", std::move(resources)}}), modern);
  }
  if (name == "get_resource") {
    if (!exact_arguments(args, {"book", "resource", "representation"}) ||
        !optional_string(args, "representation"))
      throw InvalidToolParams("Invalid get_resource arguments.");
    const auto *resource = required_string(args, "resource");
    if (!resource)
      throw InvalidToolParams("Missing string argument: resource.");
    auto representation = service::ResourceRepresentation::stored;
    if (const auto *requested = required_string(args, "representation")) {
      if (requested->as_string() == "web")
        representation = service::ResourceRepresentation::web;
      else if (requested->as_string() != "stored")
        throw InvalidToolParams("Representation must be stored or web.");
    }
    std::optional<service::ResourceContent> value;
    try {
      value = catalog.resource_content(book->as_string(), resource->as_string(),
                                       representation);
    } catch (const service::UnsupportedRepresentation &) {
      return tool_failure("Requested resource representation is unavailable.",
                          modern);
    }
    if (!value)
      return tool_failure("Book, resource, or representation not found.",
                          modern);
    return tool_result(
        object({
            {"resource", resource_summary(value->resource)},
            {"representation",
             representation == service::ResourceRepresentation::web ? "web"
                                                                    : "stored"},
            {"mediaType", value->media_type},
            {"dataBase64", base64_encode(value->bytes)},
        }),
        modern);
  }
  throw InvalidToolParams("Unknown tool name.");
}

JsonValue resource_descriptor(std::string uri, std::string name,
                              std::string mime = "application/json") {
  return object({{"uri", std::move(uri)},
                 {"name", std::move(name)},
                 {"mimeType", std::move(mime)}});
}

JsonValue list_resources(const service::Catalog &catalog, bool modern) {
  Array resources{resource_descriptor("geist://shelf", "BOO shelf")};
  for (const auto &book : catalog.books()) {
    const auto prefix = "geist://shelf/book/" + uri_encode(book.id);
    resources.push_back(resource_descriptor(prefix, book.title));
    resources.push_back(resource_descriptor(prefix + "/toc",
                                            book.title + " table of contents"));
  }
  Object result = object({{"resources", std::move(resources)}});
  if (modern) {
    result.emplace("resultType", "complete");
    result.emplace("ttlMs", 0);
    result.emplace("cacheScope", "private");
  }
  return result;
}

JsonValue list_templates(bool modern) {
  Array templates{
      object({{"uriTemplate", "geist://shelf/book/{book}"},
              {"name", "Book metadata"},
              {"mimeType", "application/json"}}),
      object({{"uriTemplate", "geist://shelf/book/{book}/toc"},
              {"name", "Book table of contents"},
              {"mimeType", "application/json"}}),
      object(
          {{"uriTemplate", "geist://shelf/book/{book}/topic/{topic}/markdown"},
           {"name", "Topic Markdown"},
           {"mimeType", "text/markdown"}}),
      object({{"uriTemplate", "geist://shelf/book/{book}/topic/{topic}/html"},
              {"name", "Topic HTML"},
              {"mimeType", "text/html"}}),
      object({{"uriTemplate",
               "geist://shelf/book/{book}/resource/{resource}/stored"},
              {"name", "Exact stored resource"},
              {"mimeType", "application/octet-stream"}}),
      object(
          {{"uriTemplate", "geist://shelf/book/{book}/resource/{resource}/web"},
           {"name", "Web resource representation"},
           {"mimeType", "application/octet-stream"}}),
  };
  Object result = object({{"resourceTemplates", std::move(templates)}});
  if (modern) {
    result.emplace("resultType", "complete");
    result.emplace("ttlMs", 0);
    result.emplace("cacheScope", "private");
  }
  return result;
}

std::vector<std::string_view> split_path(std::string_view path) {
  std::vector<std::string_view> result;
  while (!path.empty()) {
    const auto slash = path.find('/');
    result.push_back(path.substr(0, slash));
    if (slash == std::string_view::npos)
      break;
    path.remove_prefix(slash + 1);
  }
  return result;
}

std::optional<JsonValue> read_resource(std::string_view uri,
                                       const service::Catalog &catalog,
                                       bool modern) {
  constexpr std::string_view prefix = "geist://shelf";
  Object content;
  content.emplace("uri", std::string(uri));
  if (uri == prefix) {
    Array books;
    for (const auto &value : catalog.books())
      books.push_back(book_summary(value));
    content.emplace("mimeType", "application/json");
    content.emplace("text",
                    machine::write_json(object({{"books", std::move(books)}})));
  } else {
    if (uri.rfind(prefix, 0) != 0 || uri.size() <= prefix.size() ||
        uri[prefix.size()] != '/')
      return std::nullopt;
    const auto parts = split_path(uri.substr(prefix.size() + 1));
    if (parts.size() < 2 || parts[0] != "book")
      return std::nullopt;
    const auto book = uri_decode(parts[1]);
    if (!book)
      return std::nullopt;
    if (parts.size() == 2) {
      const auto value = catalog.book(*book);
      if (!value)
        return std::nullopt;
      content.emplace("mimeType", "application/json");
      content.emplace("text", machine::write_json(book_detail(*value)));
    } else if (parts.size() == 3 && parts[2] == "toc") {
      const auto values = catalog.toc(*book);
      if (!values)
        return std::nullopt;
      Array topics;
      for (const auto &value : *values)
        topics.push_back(topic_summary(value));
      content.emplace("mimeType", "application/json");
      content.emplace(
          "text", machine::write_json(object({{"topics", std::move(topics)}})));
    } else if (parts.size() == 5 && parts[2] == "topic" &&
               (parts[4] == "markdown" || parts[4] == "html")) {
      const auto topic = uri_decode(parts[3]);
      if (!topic)
        return std::nullopt;
      const auto format = parts[4] == "html" ? service::TopicFormat::html
                                             : service::TopicFormat::markdown;
      const auto value = catalog.topic_content(*book, *topic, format);
      if (!value)
        return std::nullopt;
      content.emplace("mimeType", value->media_type);
      content.emplace("text", value->content);
    } else if (parts.size() == 5 && parts[2] == "resource" &&
               (parts[4] == "stored" || parts[4] == "web")) {
      const auto resource = uri_decode(parts[3]);
      if (!resource)
        return std::nullopt;
      const auto representation = parts[4] == "web"
                                      ? service::ResourceRepresentation::web
                                      : service::ResourceRepresentation::stored;
      const auto value =
          catalog.resource_content(*book, *resource, representation);
      if (!value)
        return std::nullopt;
      content.emplace("mimeType", value->media_type);
      content.emplace("blob", base64_encode(value->bytes));
    } else {
      return std::nullopt;
    }
  }
  Object result = object({{"contents", Array{std::move(content)}}});
  if (modern) {
    result.emplace("resultType", "complete");
    result.emplace("ttlMs", 0);
    result.emplace("cacheScope", "private");
  }
  return JsonValue(std::move(result));
}

void stamp_modern(Object &result, bool cacheable) {
  result.emplace("resultType", "complete");
  result.emplace(
      "_meta", object({{"io.modelcontextprotocol/serverInfo", server_info()}}));
  if (cacheable) {
    result.emplace("ttlMs", 0);
    result.emplace("cacheScope", "private");
  }
}

bool modern_version_matches(const Object &params, std::string_view version) {
  const auto meta_at = params.find("_meta");
  if (meta_at == params.end() || !meta_at->second.is_object())
    return false;
  const auto &meta = meta_at->second.as_object();
  const auto *declared =
      required_string(meta, "io.modelcontextprotocol/protocolVersion");
  return declared && declared->as_string() == version;
}

bool valid_modern_meta(const Object &params) {
  const auto &meta = params.find("_meta")->second.as_object();
  const auto client = meta.find("io.modelcontextprotocol/clientInfo");
  const auto capabilities_at =
      meta.find("io.modelcontextprotocol/clientCapabilities");
  if (capabilities_at == meta.end() || !capabilities_at->second.is_object())
    return false;
  if (client == meta.end())
    return true;
  if (!client->second.is_object())
    return false;
  const auto &implementation = client->second.as_object();
  return required_string(implementation, "name") != nullptr &&
         required_string(implementation, "version") != nullptr;
}

} // namespace

Response handle(const Request &request, const service::Catalog &catalog) {
  if (!request.origin_valid)
    return raw_response(403);
  if (request.method != "POST") {
    auto result = raw_response(405);
    result.allow = "POST";
    return result;
  }
  if (request.body.size() > max_request_bytes)
    return raw_response(413);
  if (!json_content_type(request.content_type))
    return raw_response(415);
  if (!identity_encoding(request.content_encoding))
    return raw_response(415);
  if (!token(request.accept, "application/json") ||
      !token(request.accept, "text/event-stream"))
    return raw_response(406);

  JsonValue root;
  try {
    root = machine::parse_json(request.body);
  } catch (const machine::JsonError &) {
    return error_response(400, JsonValue(), -32700, "Parse error");
  }
  if (!root.is_object())
    return error_response(400, JsonValue(), -32600, "Invalid Request");
  const auto &message = root.as_object();
  const auto *jsonrpc = required_string(message, "jsonrpc");
  const auto *method_value = required_string(message, "method");
  const auto id_at = message.find("id");
  const JsonValue id = id_at == message.end() ? JsonValue() : id_at->second;
  const bool valid_id =
      id_at == message.end() || id.is_string() || id.is_number();
  if (!jsonrpc || jsonrpc->as_string() != "2.0" || !method_value ||
      !valid_id)
    return error_response(400, valid_id ? id : JsonValue(), -32600,
                          "Invalid Request");
  const auto &method = method_value->as_string();
  const auto params_at = message.find("params");
  const Object empty;
  if (params_at != message.end() && !params_at->second.is_object())
    return error_response(400, id, -32602, "Invalid params");
  const auto &params =
      params_at == message.end() ? empty : params_at->second.as_object();

  const bool initialize =
      method == "initialize" &&
      (!request.protocol_version || *request.protocol_version == legacy_version);
  if (initialize) {
    const auto *requested = required_string(params, "protocolVersion");
    const auto client = params.find("clientInfo");
    const auto client_capabilities = params.find("capabilities");
    if (!requested || client == params.end() || !client->second.is_object() ||
        client_capabilities == params.end() ||
        !client_capabilities->second.is_object())
      return error_response(400, id, -32602, "Invalid params", legacy_version);
    Object result = object(
        {{"protocolVersion", std::string(legacy_version)},
         {"capabilities", capabilities()},
         {"serverInfo", server_info()},
         {"instructions", "Read BOO shelf books, topics, and resources."}});
    return json_response(
        200,
        object({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}),
        legacy_version);
  }

  if (!request.protocol_version) {
    return error_response(400, id, -32020,
                          "Missing MCP-Protocol-Version header");
  }
  const auto &version = *request.protocol_version;
  const auto meta_at = params.find("_meta");
  if (meta_at != params.end() && meta_at->second.is_object()) {
    const auto *declared = required_string(
        meta_at->second.as_object(), "io.modelcontextprotocol/protocolVersion");
    if (declared && declared->as_string() != version)
      return error_response(400, id, -32020, "MCP metadata mismatch");
  }
  const bool modern = version == modern_version;
  const bool legacy = version == legacy_version;
  if (!modern && !legacy) {
    return error_response(
        400, id, -32022, "Unsupported protocol version", modern_version,
        object({{"requested", version},
                {"supported", Array{std::string(modern_version),
                                    std::string(legacy_version)}}}));
  }
  if (modern) {
    if (!request.mcp_method || *request.mcp_method != method)
      return error_response(400, id, -32020, "MCP header mismatch");
    if (!modern_version_matches(params, version))
      return error_response(400, id, -32020, "MCP metadata mismatch");
    if (!valid_modern_meta(params))
      return error_response(400, id, -32602, "Invalid params", version);
    if (method == "tools/call" || method == "resources/read") {
      const auto principal =
          required_string(params, method == "tools/call" ? "name" : "uri");
      const auto header = request.mcp_name
                              ? decoded_header_name(*request.mcp_name)
                              : std::nullopt;
      if (!principal || !header || *header != principal->as_string())
        return error_response(400, id, -32020, "MCP header mismatch");
    } else if (request.mcp_name) {
      return error_response(400, id, -32020, "MCP header mismatch");
    }
  }

  if (id_at == message.end()) {
    return raw_response(202);
  }

  std::optional<JsonValue> result;
  bool cacheable = false;
  if (modern && method == "server/discover") {
    result = object({
        {"supportedVersions",
         Array{std::string(modern_version), std::string(legacy_version)}},
        {"capabilities", capabilities()},
        {"instructions", "Read BOO shelf books, topics, and resources."},
    });
    cacheable = true;
  } else if (method == "tools/list") {
    result = object({{"tools", tools()}});
    cacheable = true;
  } else if (method == "tools/call") {
    try {
      result = call_tool(params, catalog, modern);
    } catch (const InvalidToolParams &error) {
      return error_response(400, id, -32602, error.what(), version);
    }
  } else if (method == "resources/list") {
    result = list_resources(catalog, modern);
  } else if (method == "resources/templates/list") {
    result = list_templates(modern);
  } else if (method == "resources/read") {
    const auto *uri = required_string(params, "uri");
    if (!uri)
      return error_response(400, id, -32602, "Invalid params", version);
    try {
      result = read_resource(uri->as_string(), catalog, modern);
    } catch (const service::UnsupportedRepresentation &) {
      return error_response(200, id, -32002,
                            "Resource representation unavailable", version);
    }
    if (!result)
      return error_response(200, id, -32002, "Resource not found", version);
  } else {
    return error_response(modern ? 404 : 200, id, -32601, "Method not found",
                          version);
  }

  auto result_object = std::move(result->as_object());
  if (modern) {
    // Helpers may already have supplied their mandated completion/cache fields.
    stamp_modern(result_object, cacheable);
  }
  return json_response(200,
                       object({{"jsonrpc", "2.0"},
                               {"id", id},
                               {"result", std::move(result_object)}}),
                       version);
}

} // namespace geist_apache::mcp
