// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "machine/json.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using geist_apache::machine::JsonError;
using geist_apache::machine::JsonValue;
using geist_apache::machine::ParseOptions;
using geist_apache::machine::parse_json;
using geist_apache::machine::write_json;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void reject(const std::string& text, const char* message,
    ParseOptions options = {}) {
  try {
    (void)parse_json(text, options);
  } catch (const JsonError&) {
    return;
  }
  throw std::runtime_error(message);
}

void accepts_and_preserves_types() {
  const auto value = parse_json(
      "{\"null\":null,\"bool\":true,\"number\":-12.5e+3,"
      "\"text\":\"A\\u00df\\u6771\\ud834\\udd1e\",\"array\":[0,false]}");
  require(value.is_object(), "object not parsed");
  require(value.find("null") && value.find("null")->is_null(), "null lost");
  require(value.find("bool") && value.find("bool")->as_bool(), "boolean lost");
  require(value.find("number") && value.find("number")->number_text() == "-12.5e+3",
      "number spelling lost");
  require(value.find("text") && value.find("text")->as_string() ==
      std::string("A\xc3\x9f\xe6\x9d\xb1\xf0\x9d\x84\x9e"), "Unicode escape lost");
  require(value.find("array") && value.find("array")->as_array().size() == 2,
      "array lost");
}

void rejects_invalid_syntax() {
  const char* invalid[] = {
      "", " ", "nul", "True", "+1", "01", "-01", "1.", ".1", "1e", "1e+",
      "[1,]", "[,1]", "{\"a\":1,}", "{a:1}", "{}x", "\"unterminated",
      "\"\\x00\"", "\"\\u12xz\"", "\"\\ud800\"", "\"\\udc00\"",
      "\"\\ud800\\u0041\"", "{\"a\":1,\"a\":2}"};
  for (const char* text : invalid) reject(text, "invalid JSON accepted");

  reject(std::string("\"") + char(0x1f) + "\"", "raw control accepted");
  reject(std::string("\"") + char(0xc0) + char(0x80) + "\"", "overlong UTF-8 accepted");
  reject(std::string("\"") + char(0xed) + char(0xa0) + char(0x80) + "\"",
      "UTF-8 surrogate accepted");
  reject(std::string("\"") + char(0xf4) + char(0x90) + char(0x80) + char(0x80) + "\"",
      "out-of-range UTF-8 accepted");
  reject(std::string("{\"") + char(0x80) + "\":0}", "invalid UTF-8 key accepted");
}

void enforces_bounds() {
  ParseOptions options;
  options.max_depth = 2;
  (void)parse_json("[0]", options);
  reject("[[0]]", "nesting bound ignored", options);

  options = {};
  options.max_string_bytes = 3;
  (void)parse_json("\"abc\"", options);
  reject("\"abcd\"", "raw string bound ignored", options);
  reject("\"\\u20acx\"", "decoded string bound ignored", options);

  options = {};
  options.max_container_entries = 2;
  (void)parse_json("[0,1]", options);
  reject("[0,1,2]", "array entry bound ignored", options);
  reject("{\"a\":0,\"b\":1,\"c\":2}", "object entry bound ignored", options);
}

void serializes_correct_json() {
  JsonValue::Object object;
  object.emplace("control", JsonValue(std::string("\b\f\n\r\t\0", 6)));
  object.emplace("quote", JsonValue("\"\\"));
  object.emplace("utf8", JsonValue(std::string("\xe6\x9d\xb1")));
  object.emplace("signed", JsonValue(std::int64_t(-7)));
  object.emplace("unsigned", JsonValue(std::uint64_t(9)));
  object.emplace("real", JsonValue(1.25));
  object.emplace("array", JsonValue(JsonValue::Array{JsonValue(nullptr), JsonValue(true)}));

  const std::string encoded = write_json(JsonValue(std::move(object)));
  const auto decoded = parse_json(encoded);
  require(decoded.find("control")->as_string() == std::string("\b\f\n\r\t\0", 6),
      "control escaping did not round trip");
  require(decoded.find("quote")->as_string() == "\"\\", "quote escaping did not round trip");
  require(decoded.find("utf8")->as_string() == std::string("\xe6\x9d\xb1"),
      "UTF-8 serialization did not round trip");
  require(decoded.find("signed")->number_text() == "-7", "signed integer changed");
  require(decoded.find("unsigned")->number_text() == "9", "unsigned integer changed");
  require(decoded.find("real")->number_text() == "1.25", "real number changed");

  try {
    (void)write_json(JsonValue(std::string(1, char(0x80))));
  } catch (const std::invalid_argument&) {
    goto invalid_utf8_rejected;
  }
  throw std::runtime_error("serializer accepted invalid UTF-8");
invalid_utf8_rejected:

  try {
    (void)JsonValue(std::numeric_limits<double>::infinity());
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("non-finite JSON number accepted");
}

} // namespace

int main() {
  try {
    accepts_and_preserves_types();
    rejects_invalid_syntax();
    enforces_bounds();
    serializes_correct_json();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
