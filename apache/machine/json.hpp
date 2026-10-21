// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace geist_apache::machine {

struct ParseOptions {
  std::size_t max_depth = 64;
  std::size_t max_string_bytes = 1024 * 1024;
  std::size_t max_container_entries = 100000;
};

class JsonError : public std::runtime_error {
public:
  JsonError(std::size_t offset, const std::string& message);
  std::size_t offset() const noexcept { return offset_; }
private:
  std::size_t offset_;
};

class JsonValue {
  struct Number {
    std::string text;
  };
public:
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue, std::less<>>;

  JsonValue() noexcept;
  JsonValue(std::nullptr_t) noexcept;
  JsonValue(bool value) noexcept;
  JsonValue(int value);
  JsonValue(std::int64_t value);
  JsonValue(std::uint64_t value);
  JsonValue(double value);
  JsonValue(const char* value);
  JsonValue(std::string value);
  JsonValue(Array value);
  JsonValue(Object value);

  // Retains an incoming number's exact spelling. The spelling is checked
  // against the JSON number grammar before a value is returned.
  static JsonValue number(std::string text);

  bool is_null() const noexcept;
  bool is_bool() const noexcept;
  bool is_number() const noexcept;
  bool is_string() const noexcept;
  bool is_array() const noexcept;
  bool is_object() const noexcept;

  bool as_bool() const;
  std::string_view number_text() const;
  const std::string& as_string() const;
  const Array& as_array() const;
  Array& as_array();
  const Object& as_object() const;
  Object& as_object();
  const JsonValue* find(std::string_view key) const noexcept;
  JsonValue* find(std::string_view key) noexcept;

private:
  explicit JsonValue(Number value);
  std::variant<std::nullptr_t, bool, Number, std::string, Array, Object> value_;
};

JsonValue parse_json(std::string_view input, ParseOptions options = {});
std::string write_json(const JsonValue& value);

} // namespace geist_apache::machine
