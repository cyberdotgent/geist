// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "rest/openapi.hpp"
#include "machine/json.hpp"

#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

// This deliberately small recognizer validates the complete JSON grammar but
// does not retain values. It keeps the contract test dependency-free.
class JsonRecognizer {
public:
  explicit JsonRecognizer(std::string_view input) : input_(input) {}

  bool recognize() {
    whitespace();
    if (!value())
      return false;
    whitespace();
    return offset_ == input_.size();
  }

private:
  bool value() {
    whitespace();
    if (offset_ == input_.size())
      return false;
    switch (input_[offset_]) {
    case '{':
      return object();
    case '[':
      return array();
    case '"':
      return string();
    case 't':
      return literal("true");
    case 'f':
      return literal("false");
    case 'n':
      return literal("null");
    default:
      return number();
    }
  }

  bool object() {
    ++offset_;
    whitespace();
    if (take('}'))
      return true;
    do {
      whitespace();
      if (!string())
        return false;
      whitespace();
      if (!take(':') || !value())
        return false;
      whitespace();
      if (take('}'))
        return true;
    } while (take(','));
    return false;
  }

  bool array() {
    ++offset_;
    whitespace();
    if (take(']'))
      return true;
    do {
      if (!value())
        return false;
      whitespace();
      if (take(']'))
        return true;
    } while (take(','));
    return false;
  }

  bool string() {
    if (!take('"'))
      return false;
    while (offset_ < input_.size()) {
      const unsigned char byte = static_cast<unsigned char>(input_[offset_++]);
      if (byte == '"')
        return true;
      if (byte < 0x20)
        return false;
      if (byte != '\\')
        continue;
      if (offset_ == input_.size())
        return false;
      const char escape = input_[offset_++];
      if (escape == 'u') {
        for (int digit = 0; digit < 4; ++digit) {
          if (offset_ == input_.size() ||
              !std::isxdigit(static_cast<unsigned char>(input_[offset_++])))
            return false;
        }
      } else if (std::string_view("\"\\/bfnrt").find(escape) ==
                 std::string_view::npos) {
        return false;
      }
    }
    return false;
  }

  bool number() {
    const std::size_t start = offset_;
    take('-');
    if (take('0')) {
      if (offset_ < input_.size() &&
          std::isdigit(static_cast<unsigned char>(input_[offset_])))
        return false;
    } else {
      if (offset_ == input_.size() || input_[offset_] < '1' ||
          input_[offset_] > '9')
        return false;
      while (offset_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[offset_])))
        ++offset_;
    }
    if (take('.')) {
      const std::size_t fraction = offset_;
      while (offset_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[offset_])))
        ++offset_;
      if (fraction == offset_)
        return false;
    }
    if (offset_ < input_.size() &&
        (input_[offset_] == 'e' || input_[offset_] == 'E')) {
      ++offset_;
      if (offset_ < input_.size() &&
          (input_[offset_] == '+' || input_[offset_] == '-'))
        ++offset_;
      const std::size_t exponent = offset_;
      while (offset_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[offset_])))
        ++offset_;
      if (exponent == offset_)
        return false;
    }
    return offset_ != start;
  }

  bool literal(std::string_view text) {
    if (input_.substr(offset_, text.size()) != text)
      return false;
    offset_ += text.size();
    return true;
  }

  bool take(char wanted) {
    if (offset_ == input_.size() || input_[offset_] != wanted)
      return false;
    ++offset_;
    return true;
  }

  void whitespace() {
    while (offset_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[offset_])))
      ++offset_;
  }

  std::string_view input_;
  std::size_t offset_ = 0;
};

void contains(std::string_view document, std::string_view text,
              const char *message) {
  require(document.find(text) != std::string_view::npos, message);
}

std::size_t occurrences(std::string_view document, std::string_view text) {
  std::size_t count = 0;
  for (std::size_t offset = 0;
       (offset = document.find(text, offset)) != std::string_view::npos;
       offset += text.size())
    ++count;
  return count;
}

} // namespace

int main() {
  try {
    const std::string_view document = geist_apache::rest::openapi_document();
    require(JsonRecognizer(document).recognize(),
            "OpenAPI document is not JSON");
    contains(document, "\"openapi\": \"3.1.2\"", "wrong OpenAPI version");
    contains(document, "\"url\": \".\"", "server is not shelf-relative");

    constexpr std::pair<std::string_view, std::string_view> expected_paths[] = {
        {"/api/v1", "getService"},
        {"/api/v1/books", "listBooks"},
        {"/api/v1/books/{bookId}", "getBook"},
        {"/api/v1/books/{bookId}/toc", "getTableOfContents"},
        {"/api/v1/books/{bookId}/topics", "listTopics"},
        {"/api/v1/books/{bookId}/topics/{topicId}", "getTopic"},
        {"/api/v1/books/{bookId}/topics/{topicId}/content",
         "getTopicContent"},
        {"/api/v1/books/{bookId}/resources", "listResources"},
        {"/api/v1/books/{bookId}/resources/{resourceId}", "getResource"},
        {"/api/v1/books/{bookId}/resources/{resourceId}/content",
         "getResourceContent"}};
    const auto parsed = geist_apache::machine::parse_json(document);
    const auto *paths_value = parsed.find("paths");
    require(paths_value != nullptr && paths_value->is_object(),
            "OpenAPI paths is not an object");
    const auto &paths = paths_value->as_object();
    require(paths.size() == std::size(expected_paths),
            "OpenAPI document does not contain exactly ten unique paths");
    for (const auto &[path, operation_id] : expected_paths) {
      const auto path_at = paths.find(path);
      require(path_at != paths.end(), "REST path missing from OpenAPI document");
      const auto *get = path_at->second.find("get");
      const auto *actual_id = get != nullptr ? get->find("operationId") : nullptr;
      require(actual_id != nullptr && actual_id->is_string() &&
                  actual_id->as_string() == operation_id,
              "REST path is bound to the wrong OpenAPI operation");
    }

    require(occurrences(document, "\"get\": {") == 10,
            "not every REST path has one GET operation");
    require(occurrences(document, "\"head\": {") == 10,
            "not every REST path has one HEAD operation");
    contains(document, "\"operationId\": \"headTopicContent\"",
             "HEAD operations are not documented");
    contains(document,
             "including topics absent from the authored table of contents",
             "all-indexed-topic semantics are not documented");
    contains(document, "exact container bytes",
             "stored resource semantics are not documented");
    contains(document, "explicit supported web conversion",
             "web conversion semantics are not documented");
    contains(document, "\"format\": \"binary\"",
             "raw resource response bodies are not described as binary");
    require(document.find("\"contentEncoding\": \"binary\"") ==
                std::string_view::npos,
            "raw resource bodies must not claim a text content encoding");
    contains(document, "\"application/problem+json\"",
             "problem responses are not documented");
    contains(document, "\"degradationCount\"",
             "degradation count is missing from diagnostics");
    contains(document, "\"startLogicalRecord\"",
             "topic source framing is not documented");
    contains(document, "\"inToc\"", "topic TOC membership is not documented");
    contains(document,
             "Filesystem paths and document numbers are not book identifiers",
             "book identity rules are not documented");
    require(geist_apache::rest::openapi_media_type ==
                "application/json; charset=utf-8",
            "wrong OpenAPI media type");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
