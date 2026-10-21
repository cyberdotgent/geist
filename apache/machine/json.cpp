// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <system_error>
#include <utility>

namespace geist_apache::machine {
namespace {

bool is_digit(char value) {
  return value >= '0' && value <= '9';
}

bool valid_number(std::string_view text) {
  std::size_t at = 0;
  if (at < text.size() && text[at] == '-') ++at;
  if (at == text.size()) return false;
  if (text[at] == '0') {
    ++at;
    if (at < text.size() && is_digit(text[at])) return false;
  } else {
    if (text[at] < '1' || text[at] > '9') return false;
    while (at < text.size() && is_digit(text[at])) ++at;
  }
  if (at < text.size() && text[at] == '.') {
    ++at;
    if (at == text.size() || !is_digit(text[at])) return false;
    while (at < text.size() && is_digit(text[at])) ++at;
  }
  if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
    ++at;
    if (at < text.size() && (text[at] == '+' || text[at] == '-')) ++at;
    if (at == text.size() || !is_digit(text[at])) return false;
    while (at < text.size() && is_digit(text[at])) ++at;
  }
  return at == text.size();
}

std::size_t utf8_length(std::string_view input, std::size_t at) {
  const auto first = static_cast<unsigned char>(input[at]);
  if (first < 0x80) return 1;
  std::size_t length = 0;
  std::uint32_t codepoint = 0;
  std::uint32_t minimum = 0;
  if (first >= 0xc2 && first <= 0xdf) {
    length = 2; codepoint = first & 0x1f; minimum = 0x80;
  } else if (first >= 0xe0 && first <= 0xef) {
    length = 3; codepoint = first & 0x0f; minimum = 0x800;
  } else if (first >= 0xf0 && first <= 0xf4) {
    length = 4; codepoint = first & 0x07; minimum = 0x10000;
  } else {
    return 0;
  }
  if (length > input.size() - at) return 0;
  for (std::size_t i = 1; i < length; ++i) {
    const auto byte = static_cast<unsigned char>(input[at + i]);
    if ((byte & 0xc0) != 0x80) return 0;
    codepoint = (codepoint << 6) | (byte & 0x3f);
  }
  if (codepoint < minimum || codepoint > 0x10ffff ||
      (codepoint >= 0xd800 && codepoint <= 0xdfff)) return 0;
  return length;
}

void append_utf8(std::string& output, std::uint32_t codepoint) {
  if (codepoint <= 0x7f) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ff) {
    output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0xffff) {
    output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else {
    output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  }
}

class Parser {
public:
  Parser(std::string_view input, ParseOptions options)
      : input_(input), options_(options) {}

  JsonValue parse() {
    skip_space();
    JsonValue result = value(1);
    skip_space();
    if (at_ != input_.size()) fail("trailing data");
    return result;
  }

private:
  [[noreturn]] void fail(const char* message) const {
    throw JsonError(at_, message);
  }

  void skip_space() {
    while (at_ < input_.size() && (input_[at_] == ' ' || input_[at_] == '\t' ||
        input_[at_] == '\n' || input_[at_] == '\r')) ++at_;
  }

  bool take(char wanted) {
    if (at_ < input_.size() && input_[at_] == wanted) {
      ++at_;
      return true;
    }
    return false;
  }

  bool take_word(std::string_view wanted) {
    if (input_.substr(at_, wanted.size()) != wanted) return false;
    at_ += wanted.size();
    return true;
  }

  JsonValue value(std::size_t depth) {
    if (depth > options_.max_depth) fail("maximum nesting depth exceeded");
    if (at_ == input_.size()) fail("expected a value");
    switch (input_[at_]) {
      case 'n':
        if (!take_word("null")) fail("invalid literal");
        return JsonValue(nullptr);
      case 't':
        if (!take_word("true")) fail("invalid literal");
        return JsonValue(true);
      case 'f':
        if (!take_word("false")) fail("invalid literal");
        return JsonValue(false);
      case '"': return JsonValue(string());
      case '[': return array(depth);
      case '{': return object(depth);
      default: return number();
    }
  }

  std::uint16_t hex_quad() {
    if (input_.size() - at_ < 4) fail("incomplete Unicode escape");
    std::uint16_t result = 0;
    for (unsigned i = 0; i < 4; ++i) {
      const char ch = input_[at_++];
      result <<= 4;
      if (ch >= '0' && ch <= '9') result |= static_cast<std::uint16_t>(ch - '0');
      else if (ch >= 'a' && ch <= 'f') result |= static_cast<std::uint16_t>(ch - 'a' + 10);
      else if (ch >= 'A' && ch <= 'F') result |= static_cast<std::uint16_t>(ch - 'A' + 10);
      else fail("invalid Unicode escape");
    }
    return result;
  }

  void check_string_size(const std::string& result) const {
    if (result.size() > options_.max_string_bytes)
      fail("maximum string size exceeded");
  }

  std::string string() {
    if (!take('"')) fail("expected string");
    std::string result;
    while (at_ < input_.size()) {
      const auto byte = static_cast<unsigned char>(input_[at_]);
      if (byte == '"') {
        ++at_;
        return result;
      }
      if (byte < 0x20) fail("unescaped control character");
      if (byte == '\\') {
        ++at_;
        if (at_ == input_.size()) fail("incomplete escape");
        const char escaped = input_[at_++];
        switch (escaped) {
          case '"': result.push_back('"'); break;
          case '\\': result.push_back('\\'); break;
          case '/': result.push_back('/'); break;
          case 'b': result.push_back('\b'); break;
          case 'f': result.push_back('\f'); break;
          case 'n': result.push_back('\n'); break;
          case 'r': result.push_back('\r'); break;
          case 't': result.push_back('\t'); break;
          case 'u': {
            std::uint32_t codepoint = hex_quad();
            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
              if (!take('\\') || !take('u')) fail("missing low surrogate");
              const std::uint32_t low = hex_quad();
              if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
              codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
            } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
              fail("unpaired low surrogate");
            }
            append_utf8(result, codepoint);
            break;
          }
          default: fail("invalid escape");
        }
      } else if (byte < 0x80) {
        result.push_back(input_[at_++]);
      } else {
        const std::size_t length = utf8_length(input_, at_);
        if (!length) fail("invalid UTF-8");
        result.append(input_.substr(at_, length));
        at_ += length;
      }
      check_string_size(result);
    }
    fail("unterminated string");
  }

  JsonValue number() {
    const std::size_t start = at_;
    if (at_ < input_.size() && input_[at_] == '-') ++at_;
    if (at_ < input_.size() && input_[at_] == '0') {
      ++at_;
      if (at_ < input_.size() && is_digit(input_[at_])) fail("leading zero in number");
    } else {
      if (at_ == input_.size() || input_[at_] < '1' || input_[at_] > '9')
        fail("expected a value");
      while (at_ < input_.size() && is_digit(input_[at_])) ++at_;
    }
    if (at_ < input_.size() && input_[at_] == '.') {
      ++at_;
      if (at_ == input_.size() || !is_digit(input_[at_])) fail("invalid fraction");
      while (at_ < input_.size() && is_digit(input_[at_])) ++at_;
    }
    if (at_ < input_.size() && (input_[at_] == 'e' || input_[at_] == 'E')) {
      ++at_;
      if (at_ < input_.size() && (input_[at_] == '+' || input_[at_] == '-')) ++at_;
      if (at_ == input_.size() || !is_digit(input_[at_])) fail("invalid exponent");
      while (at_ < input_.size() && is_digit(input_[at_])) ++at_;
    }
    return JsonValue::number(std::string(input_.substr(start, at_ - start)));
  }

  JsonValue array(std::size_t depth) {
    ++at_;
    skip_space();
    JsonValue::Array result;
    if (take(']')) return JsonValue(std::move(result));
    while (true) {
      if (result.size() == options_.max_container_entries)
        fail("maximum container entries exceeded");
      result.push_back(value(depth + 1));
      skip_space();
      if (take(']')) return JsonValue(std::move(result));
      if (!take(',')) fail("expected comma or closing bracket");
      skip_space();
    }
  }

  JsonValue object(std::size_t depth) {
    ++at_;
    skip_space();
    JsonValue::Object result;
    if (take('}')) return JsonValue(std::move(result));
    while (true) {
      if (result.size() == options_.max_container_entries)
        fail("maximum container entries exceeded");
      if (at_ == input_.size() || input_[at_] != '"') fail("expected object key");
      std::string key = string();
      skip_space();
      if (!take(':')) fail("expected colon");
      skip_space();
      JsonValue member = value(depth + 1);
      if (!result.emplace(std::move(key), std::move(member)).second)
        fail("duplicate object key");
      skip_space();
      if (take('}')) return JsonValue(std::move(result));
      if (!take(',')) fail("expected comma or closing brace");
      skip_space();
    }
  }

  std::string_view input_;
  ParseOptions options_;
  std::size_t at_ = 0;
};

void write_string(std::string_view value, std::string& output) {
  output.push_back('"');
  for (std::size_t at = 0; at < value.size();) {
    const auto byte = static_cast<unsigned char>(value[at]);
    switch (byte) {
      case '"': output += "\\\""; ++at; continue;
      case '\\': output += "\\\\"; ++at; continue;
      case '\b': output += "\\b"; ++at; continue;
      case '\f': output += "\\f"; ++at; continue;
      case '\n': output += "\\n"; ++at; continue;
      case '\r': output += "\\r"; ++at; continue;
      case '\t': output += "\\t"; ++at; continue;
      default: break;
    }
    if (byte < 0x20) {
      char escaped[7];
      std::snprintf(escaped, sizeof(escaped), "\\u%04x", byte);
      output += escaped;
      ++at;
    } else if (byte < 0x80) {
      output.push_back(static_cast<char>(byte));
      ++at;
    } else {
      const std::size_t length = utf8_length(value, at);
      if (!length) throw std::invalid_argument("JSON string is not valid UTF-8");
      output.append(value.substr(at, length));
      at += length;
    }
  }
  output.push_back('"');
}

void write_value(const JsonValue& value, std::string& output) {
  if (value.is_null()) output += "null";
  else if (value.is_bool()) output += value.as_bool() ? "true" : "false";
  else if (value.is_number()) output.append(value.number_text());
  else if (value.is_string()) write_string(value.as_string(), output);
  else if (value.is_array()) {
    output.push_back('[');
    bool first = true;
    for (const auto& item : value.as_array()) {
      if (!first) output.push_back(',');
      first = false;
      write_value(item, output);
    }
    output.push_back(']');
  } else {
    output.push_back('{');
    bool first = true;
    for (const auto& member : value.as_object()) {
      if (!first) output.push_back(',');
      first = false;
      write_string(member.first, output);
      output.push_back(':');
      write_value(member.second, output);
    }
    output.push_back('}');
  }
}

} // namespace

JsonError::JsonError(std::size_t offset, const std::string& message)
    : std::runtime_error(message + " at byte " + std::to_string(offset)), offset_(offset) {}

JsonValue::JsonValue() noexcept : value_(nullptr) {}
JsonValue::JsonValue(std::nullptr_t) noexcept : value_(nullptr) {}
JsonValue::JsonValue(bool value) noexcept : value_(value) {}
JsonValue::JsonValue(int value) : JsonValue(std::int64_t(value)) {}
JsonValue::JsonValue(std::int64_t value) : value_(Number{std::to_string(value)}) {}
JsonValue::JsonValue(std::uint64_t value) : value_(Number{std::to_string(value)}) {}
JsonValue::JsonValue(double value) {
  if (!std::isfinite(value)) throw std::invalid_argument("JSON numbers must be finite");
  char buffer[64];
  const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value,
      std::chars_format::general, std::numeric_limits<double>::max_digits10);
  if (converted.ec != std::errc()) throw std::invalid_argument("cannot encode JSON number");
  value_ = Number{std::string(buffer, converted.ptr)};
}
JsonValue::JsonValue(const char* value) : value_(std::string(value)) {}
JsonValue::JsonValue(std::string value) : value_(std::move(value)) {}
JsonValue::JsonValue(Array value) : value_(std::move(value)) {}
JsonValue::JsonValue(Object value) : value_(std::move(value)) {}
JsonValue::JsonValue(Number value) : value_(std::move(value)) {}

JsonValue JsonValue::number(std::string text) {
  if (!valid_number(text)) throw std::invalid_argument("invalid JSON number");
  return JsonValue(Number{std::move(text)});
}

bool JsonValue::is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(value_); }
bool JsonValue::is_bool() const noexcept { return std::holds_alternative<bool>(value_); }
bool JsonValue::is_number() const noexcept { return std::holds_alternative<Number>(value_); }
bool JsonValue::is_string() const noexcept { return std::holds_alternative<std::string>(value_); }
bool JsonValue::is_array() const noexcept { return std::holds_alternative<Array>(value_); }
bool JsonValue::is_object() const noexcept { return std::holds_alternative<Object>(value_); }

bool JsonValue::as_bool() const { return std::get<bool>(value_); }
std::string_view JsonValue::number_text() const { return std::get<Number>(value_).text; }
const std::string& JsonValue::as_string() const { return std::get<std::string>(value_); }
const JsonValue::Array& JsonValue::as_array() const { return std::get<Array>(value_); }
JsonValue::Array& JsonValue::as_array() { return std::get<Array>(value_); }
const JsonValue::Object& JsonValue::as_object() const { return std::get<Object>(value_); }
JsonValue::Object& JsonValue::as_object() { return std::get<Object>(value_); }

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
  if (!is_object()) return nullptr;
  const auto found = as_object().find(key);
  return found == as_object().end() ? nullptr : &found->second;
}

JsonValue* JsonValue::find(std::string_view key) noexcept {
  if (!is_object()) return nullptr;
  const auto found = as_object().find(key);
  return found == as_object().end() ? nullptr : &found->second;
}

JsonValue parse_json(std::string_view input, ParseOptions options) {
  return Parser(input, options).parse();
}

std::string write_json(const JsonValue& value) {
  std::string result;
  write_value(value, result);
  return result;
}

} // namespace geist_apache::machine
