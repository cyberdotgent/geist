// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "rest.hpp"

#include "../machine/json.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>

namespace geist_apache::rest {
namespace {

using machine::JsonValue;

constexpr std::string_view json_type = "application/json; charset=utf-8";
constexpr std::string_view problem_type =
    "application/problem+json; charset=utf-8";

bool valid_utf8(std::string_view text) {
  for (std::size_t at = 0; at < text.size();) {
    const auto first = static_cast<unsigned char>(text[at]);
    if (first < 0x80) {
      ++at;
      continue;
    }
    std::size_t length = 0;
    std::uint32_t codepoint = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      length = 2;
      codepoint = first & 0x1f;
      minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      length = 3;
      codepoint = first & 0x0f;
      minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      length = 4;
      codepoint = first & 0x07;
      minimum = 0x10000;
    } else {
      return false;
    }
    if (length > text.size() - at) return false;
    for (std::size_t offset = 1; offset < length; ++offset) {
      const auto byte = static_cast<unsigned char>(text[at + offset]);
      if ((byte & 0xc0) != 0x80) return false;
      codepoint = (codepoint << 6) | (byte & 0x3f);
    }
    if (codepoint < minimum || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    at += length;
  }
  return true;
}

int hex_digit(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

std::optional<std::string> decode(std::string_view encoded,
                                  bool path_segment,
                                  bool allow_empty = false) {
  if ((!allow_empty && encoded.empty()) || encoded.size() > 8192)
    return std::nullopt;
  std::string result;
  result.reserve(encoded.size());
  for (std::size_t at = 0; at < encoded.size(); ++at) {
    unsigned char byte = static_cast<unsigned char>(encoded[at]);
    if (byte == '%') {
      if (encoded.size() - at < 3) return std::nullopt;
      const int high = hex_digit(encoded[at + 1]);
      const int low = hex_digit(encoded[at + 2]);
      if (high < 0 || low < 0) return std::nullopt;
      byte = static_cast<unsigned char>((high << 4) | low);
      at += 2;
    }
    if (byte == 0 || byte < 0x20 || byte == 0x7f) return std::nullopt;
    if (path_segment && (byte == '/' || byte == '\\')) return std::nullopt;
    result.push_back(static_cast<char>(byte));
  }
  if (!valid_utf8(result)) return std::nullopt;
  if (path_segment && (result == "." || result == "..")) return std::nullopt;
  return result;
}

std::vector<std::string_view> split_path(std::string_view path) {
  std::vector<std::string_view> result;
  std::size_t begin = 0;
  while (begin <= path.size()) {
    const auto end = path.find('/', begin);
    result.push_back(path.substr(begin, end == std::string_view::npos
                                           ? path.size() - begin
                                           : end - begin));
    if (end == std::string_view::npos) break;
    begin = end + 1;
  }
  return result;
}

using Query = std::map<std::string, std::string, std::less<>>;

std::optional<Query> parse_query(std::string_view text) {
  Query result;
  if (text.empty()) return result;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    const auto amp = text.find('&', begin);
    const auto item = text.substr(begin, amp == std::string_view::npos
                                            ? text.size() - begin
                                            : amp - begin);
    const auto equal = item.find('=');
    if (item.empty() || equal == std::string_view::npos) return std::nullopt;
    auto name = decode(item.substr(0, equal), false);
    auto value = decode(item.substr(equal + 1), false, true);
    if (!name || !value || !result.emplace(*name, *value).second)
      return std::nullopt;
    if (amp == std::string_view::npos) break;
    begin = amp + 1;
  }
  return result;
}

JsonValue book_summary(const service::BookSummary& book) {
  return JsonValue::Object{{"id", book.id},
                           {"title", book.title},
                           {"documentNumber", book.document_number},
                           {"built", book.built},
                           {"size", static_cast<std::uint64_t>(book.size)},
                           {"readable", book.readable}};
}

JsonValue book_detail(const service::BookDetail& book) {
  auto object = book_summary(book).as_object();
  object.emplace("language", book.language);
  object.emplace("formatVersion", book.format_version);
  object.emplace("buildVersion", book.build_version);
  object.emplace("copyright", book.copyright);
  object.emplace("security", book.security);
  object.emplace("date", book.date);
  JsonValue::Array authors;
  for (const auto& author : book.authors) authors.emplace_back(author);
  object.emplace("authors", std::move(authors));
  object.emplace("topicCount", static_cast<std::uint64_t>(book.topic_count));
  object.emplace("tocCount", static_cast<std::uint64_t>(book.toc_count));
  object.emplace("resourceCount",
                 static_cast<std::uint64_t>(book.resource_count));
  return object;
}

JsonValue topic_summary(const service::TopicSummary& topic) {
  return JsonValue::Object{
      {"id", topic.id},
      {"title", topic.title},
      {"headingLevel", topic.heading_level},
      {"sourceIndex", static_cast<std::uint64_t>(topic.source_index)},
      {"topicNumber", static_cast<std::uint64_t>(topic.topic_number)},
      {"startLogicalRecord",
       static_cast<std::uint64_t>(topic.start_logical_record)},
      {"endLogicalRecord",
       static_cast<std::uint64_t>(topic.end_logical_record)},
      {"inToc", topic.in_toc},
      {"tocLevel", static_cast<std::uint64_t>(topic.toc_level)},
      {"tocStyle", static_cast<std::uint64_t>(topic.toc_style)}};
}

JsonValue source_coordinates(const service::SourceCoordinates& source) {
  return JsonValue::Object{
      {"startLogicalRecord",
       static_cast<std::uint64_t>(source.start_logical_record)},
      {"endLogicalRecord",
       static_cast<std::uint64_t>(source.end_logical_record)},
      {"logicalRecord", static_cast<std::uint64_t>(source.logical_record)},
      {"segmentIndex", static_cast<std::uint64_t>(source.segment_index)},
      {"tokenBegin", static_cast<std::uint64_t>(source.token_begin)},
      {"tokenEnd", static_cast<std::uint64_t>(source.token_end)},
      {"byteBegin", static_cast<std::uint64_t>(source.byte_begin)},
      {"byteEnd", static_cast<std::uint64_t>(source.byte_end)}};
}

JsonValue diagnostic(const service::Diagnostic& diagnostic) {
  JsonValue::Array degradations;
  for (const auto& degradation : diagnostic.degradations) {
    degradations.emplace_back(JsonValue::Object{
        {"block", degradation.block},
        {"reason", degradation.reason},
        {"detail", degradation.detail},
        {"source", source_coordinates(degradation.source)}});
  }
  return JsonValue::Object{
      {"severity", diagnostic.severity},
      {"route", diagnostic.route},
      {"family", diagnostic.family},
      {"reason", diagnostic.reason},
      {"detail", diagnostic.detail},
      {"source", source_coordinates(diagnostic.source)},
      {"degradations", std::move(degradations)},
      {"degradationCount",
       static_cast<std::uint64_t>(diagnostic.degradation_count)}};
}

JsonValue topic_detail(const service::TopicDetail& topic) {
  auto object = topic_summary(topic).as_object();
  object.emplace("diagnostic", diagnostic(topic.diagnostic));
  return object;
}

JsonValue resource_summary(const service::ResourceSummary& resource) {
  return JsonValue::Object{
      {"id", resource.id},
      {"name", resource.name},
      {"kind", resource.kind},
      {"storedFormat", resource.stored_format},
      {"description", resource.description},
      {"layout", resource.layout},
      {"size", static_cast<std::uint64_t>(resource.size)}};
}

Response response(int status, std::string_view content_type,
                  std::string body = {}) {
  Response result;
  result.status = status;
  result.content_type = std::string(content_type);
  result.body = std::move(body);
  result.headers.emplace_back("X-Content-Type-Options", "nosniff");
  result.headers.emplace_back("Content-Length",
                              std::to_string(result.body.size()));
  return result;
}

Response json_response(JsonValue value) {
  return response(200, json_type, machine::write_json(value));
}

Response problem(int status, std::string title, std::string detail) {
  return response(status, problem_type,
                  machine::write_json(JsonValue::Object{
                      {"type", "about:blank"},
                      {"title", std::move(title)},
                      {"status", status},
                      {"detail", std::move(detail)}}));
}

Response not_found() {
  return problem(404, "Not Found", "The requested object does not exist.");
}

enum class Endpoint {
  unknown,
  service,
  books,
  book,
  toc,
  topics,
  topic,
  topic_content,
  resources,
  resource,
  resource_content
};

struct Match {
  Endpoint endpoint = Endpoint::unknown;
  std::string book_id;
  std::string item_id;
  bool malformed = false;
};

Match match_path(std::string_view path) {
  const auto parts = split_path(path);
  if (parts.size() < 4 || parts[0] != "" || parts[1] != ".geist" ||
      parts[2] != "api" || parts[3] != "v1")
    return {};
  if (parts.size() == 4) {
    Match result;
    result.endpoint = Endpoint::service;
    return result;
  }
  if (parts[4] != "books") return {};
  if (parts.size() == 5) {
    Match result;
    result.endpoint = Endpoint::books;
    return result;
  }

  Match result;
  auto book = decode(parts[5], true);
  if (!book) {
    result.malformed = true;
    return result;
  }
  result.book_id = std::move(*book);
  if (parts.size() == 6) {
    result.endpoint = Endpoint::book;
    return result;
  }
  if (parts.size() == 7 && parts[6] == "toc") {
    result.endpoint = Endpoint::toc;
    return result;
  }
  if (parts[6] != "topics" && parts[6] != "resources") return {};
  const bool topics = parts[6] == "topics";
  if (parts.size() == 7) {
    result.endpoint = topics ? Endpoint::topics : Endpoint::resources;
    return result;
  }
  auto item = decode(parts[7], true);
  if (!item) {
    result.malformed = true;
    return result;
  }
  result.item_id = std::move(*item);
  if (parts.size() == 8) {
    result.endpoint = topics ? Endpoint::topic : Endpoint::resource;
    return result;
  }
  if (parts.size() == 9 && parts[8] == "content") {
    result.endpoint = topics ? Endpoint::topic_content
                             : Endpoint::resource_content;
    return result;
  }
  return {};
}

bool expects_query(Endpoint endpoint) {
  return endpoint == Endpoint::topic_content ||
         endpoint == Endpoint::resource_content;
}

Response dispatch(const Match& match, const Query& query,
                  const service::Catalog& catalog, bool mcp_enabled) {
  switch (match.endpoint) {
    case Endpoint::service:
      return json_response(JsonValue::Object{
          {"service", "mod_geist"},
          {"apiVersion", "v1"},
          {"readOnly", true},
          {"openapi", "../openapi.json"},
          {"mcp", mcp_enabled ? JsonValue("../mcp")
                              : JsonValue(nullptr)}});
    case Endpoint::books: {
      JsonValue::Array books;
      for (const auto& book : catalog.books())
        books.emplace_back(book_summary(book));
      return json_response(JsonValue::Object{{"books", std::move(books)}});
    }
    case Endpoint::book: {
      auto book = catalog.book(match.book_id);
      return book ? json_response(book_detail(*book)) : not_found();
    }
    case Endpoint::toc:
    case Endpoint::topics: {
      auto topics = match.endpoint == Endpoint::toc
                        ? catalog.toc(match.book_id)
                        : catalog.topics(match.book_id);
      if (!topics) return not_found();
      JsonValue::Array values;
      for (const auto& topic : *topics)
        values.emplace_back(topic_summary(topic));
      const char* key = match.endpoint == Endpoint::toc ? "entries" : "topics";
      return json_response(JsonValue::Object{
          {"bookId", match.book_id}, {key, std::move(values)}});
    }
    case Endpoint::topic: {
      auto topic = catalog.topic(match.book_id, match.item_id);
      if (!topic) return not_found();
      auto object = topic_detail(*topic).as_object();
      object.emplace("bookId", match.book_id);
      return json_response(std::move(object));
    }
    case Endpoint::topic_content: {
      service::TopicFormat format = service::TopicFormat::markdown;
      if (const auto found = query.find("format"); found != query.end()) {
        if (found->second == "html") format = service::TopicFormat::html;
        else if (found->second != "markdown")
          return problem(406, "Not Acceptable",
                         "The requested topic format is not supported.");
      }
      auto content = catalog.topic_content(match.book_id, match.item_id, format);
      if (!content) return not_found();
      return response(200, content->media_type, std::move(content->content));
    }
    case Endpoint::resources: {
      auto resources = catalog.resources(match.book_id);
      if (!resources) return not_found();
      JsonValue::Array values;
      for (const auto& resource : *resources)
        values.emplace_back(resource_summary(resource));
      return json_response(JsonValue::Object{
          {"bookId", match.book_id}, {"resources", std::move(values)}});
    }
    case Endpoint::resource: {
      auto resource = catalog.resource(match.book_id, match.item_id);
      if (!resource) return not_found();
      auto object = resource_summary(*resource).as_object();
      object.emplace("bookId", match.book_id);
      return json_response(std::move(object));
    }
    case Endpoint::resource_content: {
      service::ResourceRepresentation representation =
          service::ResourceRepresentation::stored;
      if (const auto found = query.find("representation");
          found != query.end()) {
        if (found->second == "web")
          representation = service::ResourceRepresentation::web;
        else if (found->second != "stored")
          return problem(406, "Not Acceptable",
                         "The requested resource representation is not supported.");
      }
      if (!catalog.resource(match.book_id, match.item_id)) return not_found();
      auto content = catalog.resource_content(match.book_id, match.item_id,
                                              representation);
      if (!content) {
        return problem(406, "Not Acceptable",
                       "No explicit conversion exists for this resource.");
      }
      std::string bytes(content->bytes.begin(), content->bytes.end());
      return response(200, content->media_type, std::move(bytes));
    }
    case Endpoint::unknown:
      break;
  }
  return not_found();
}

} // namespace

static Response route_impl(const Request& request,
                           const service::Catalog& catalog,
                           bool mcp_enabled) {
  try {
    const Match match = match_path(request.path);
    if (match.malformed)
      return problem(400, "Bad Request", "The request path is malformed.");
    if (match.endpoint == Endpoint::unknown) return not_found();

    auto query = parse_query(request.query);
    if (!query)
      return problem(400, "Bad Request", "The query string is malformed.");
    if (!expects_query(match.endpoint) && !query->empty())
      return problem(400, "Bad Request",
                     "This endpoint does not accept query parameters.");
    if (match.endpoint == Endpoint::topic_content) {
      if (query->size() > 1 ||
          (!query->empty() && query->begin()->first != "format"))
        return problem(400, "Bad Request", "Unknown query parameter.");
    } else if (match.endpoint == Endpoint::resource_content) {
      if (query->size() > 1 ||
          (!query->empty() && query->begin()->first != "representation"))
        return problem(400, "Bad Request", "Unknown query parameter.");
    }

    if (request.method != "GET" && request.method != "HEAD") {
      auto result = problem(405, "Method Not Allowed",
                            "Only GET and HEAD are supported.");
      result.headers.emplace_back("Allow", "GET, HEAD");
      return result;
    }
    return dispatch(match, *query, catalog, mcp_enabled);
  } catch (const service::UnsupportedRepresentation&) {
    return problem(406, "Not Acceptable",
                   "No explicit web conversion exists for this resource.");
  } catch (...) {
    return problem(500, "Internal Server Error",
                   "The BOO data could not be read.");
  }
}

Response route(const Request& request, const service::Catalog& catalog,
               bool mcp_enabled) {
  auto result = route_impl(request, catalog, mcp_enabled);
  if (request.method == "HEAD") result.body.clear();
  return result;
}

} // namespace geist_apache::rest
