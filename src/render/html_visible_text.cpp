// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/render/html_visible_text.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace geist::detail {
namespace {

bool boundary(const std::string_view tag) {
  static constexpr std::string_view tags[] = {
      "address", "article", "aside", "blockquote", "br",       "caption",
      "dd",      "div",     "dl",    "dt",         "fieldset", "figcaption",
      "figure",  "footer",  "form",  "h1",         "h2",       "h3",
      "h4",      "h5",      "h6",    "header",     "hr",       "li",
      "main",    "nav",     "ol",    "p",          "pre",      "section",
      "table",   "tbody",   "td",    "tfoot",      "th",       "thead",
      "tr",      "ul"};
  return std::find(std::begin(tags), std::end(tags), tag) != std::end(tags);
}

bool supported_tag(const std::string_view tag) {
  static constexpr std::string_view tags[] = {
      "a",  "aside", "br",         "code",   "dd",     "div",   "dl",
      "dt", "em",    "figcaption", "figure", "h1",     "h2",    "h3",
      "h4", "h5",    "h6",         "img",    "li",     "nav",   "ol",
      "p",  "pre",   "section",    "span",   "strong", "table", "tbody",
      "td", "th",    "thead",      "tr",     "ul"};
  return std::find(std::begin(tags), std::end(tags), tag) != std::end(tags);
}

bool supported_attribute(const std::string_view tag,
                         const std::string_view name) {
  if (name == "class")
    return true;
  if (name == "id")
    return tag == "a" || tag == "span" || tag == "aside" || tag == "div";
  if (name == "href")
    return tag == "a";
  if (name == "src" || name == "alt")
    return tag == "img";
  if (name == "scope")
    return tag == "th";
  if (name == "value")
    return tag == "li";
  static constexpr std::string_view attributes[] = {
      "data-geist-degradation", "data-geist-degraded",
      "data-geist-depth",       "data-geist-empty",
      "data-geist-generated",   "data-geist-opaque-kind",
      "data-geist-reason",      "data-geist-severity",
      "data-geist-topic"};
  return std::find(std::begin(attributes), std::end(attributes), name) !=
         std::end(attributes);
}

bool space(const char ch) {
  return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t';
}

// The renderer escapes &, < and > in text. Numeric references and the other
// usual named references are accepted too, so an entity cannot introduce a
// false word boundary (or make an attribute look like text).
bool entity(const std::string_view value, char &decoded) {
  if (value == "amp")
    decoded = '&';
  else if (value == "lt")
    decoded = '<';
  else if (value == "gt")
    decoded = '>';
  else if (value == "quot")
    decoded = '"';
  else if (value == "apos" || value == "#39")
    decoded = '\'';
  else if (value == "nbsp")
    decoded = ' ';
  else if (!value.empty() && value.front() == '#') {
    std::size_t at = 1;
    unsigned base = 10;
    if (at < value.size() && (value[at] == 'x' || value[at] == 'X')) {
      base = 16;
      ++at;
    }
    if (at == value.size())
      return false;
    unsigned code = 0;
    for (; at < value.size(); ++at) {
      const auto ch = value[at];
      const unsigned digit = ch >= '0' && ch <= '9'   ? ch - '0'
                             : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                             : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
                                                      : 16;
      if (digit >= base || code > (0x10ffffu - digit) / base)
        return false;
      code = code * base + digit;
    }
    // Conservation counts ASCII alphanumeric runs. Non-ASCII text separates
    // those runs on both sides, so no lossy transliteration is appropriate.
    decoded = code > 0 && code < 128 ? static_cast<char>(code) : ' ';
  } else
    return false;
  return true;
}

std::string text_entities(const std::string_view text) {
  std::string out;
  for (std::size_t at = 0; at < text.size();) {
    const auto end = text[at] == '&' ? text.find(';', at + 1) : text.npos;
    char decoded = 0;
    if (end != text.npos &&
        entity(text.substr(at + 1, end - at - 1), decoded)) {
      out.push_back(decoded);
      at = end + 1;
    } else {
      if (text[at] == '&')
        throw std::logic_error("unsupported entity in emitted HTML");
      out.push_back(text[at++]);
    }
  }
  return out;
}

// Read only the requested attribute, without treating other attributes as
// text. Tokenization is quote-aware so a URL containing `alt=` or `>` cannot
// accidentally become an image label or an ordered-list marker.
std::map<std::string, std::string>
parse_attributes(const std::string_view tag,
                 const std::string_view attributes) {
  std::map<std::string, std::string> result;
  for (std::size_t at = 0; at < attributes.size();) {
    if (!space(attributes[at]))
      throw std::logic_error("malformed emitted HTML attributes");
    while (at < attributes.size() && space(attributes[at]))
      ++at;
    if (at == attributes.size())
      break;
    const auto begin = at;
    while (at < attributes.size() && attributes[at] != '=' &&
           !space(attributes[at]))
      ++at;
    const auto name = attributes.substr(begin, at - begin);
    if (!supported_attribute(tag, name))
      throw std::logic_error("unsupported emitted HTML attribute: " +
                             std::string(name));
    while (at < attributes.size() && space(attributes[at]))
      ++at;
    if (at == attributes.size() || attributes[at] != '=')
      throw std::logic_error("malformed emitted HTML attribute assignment");
    ++at;
    while (at < attributes.size() && space(attributes[at]))
      ++at;
    if (at == attributes.size() ||
        (attributes[at] != '\'' && attributes[at] != '"'))
      throw std::logic_error("unquoted emitted HTML attribute");
    const auto quote = attributes[at++];
    const auto value = at;
    while (at < attributes.size() && attributes[at] != quote)
      ++at;
    if (at == attributes.size())
      throw std::logic_error("unterminated emitted HTML attribute");
    if (!result
             .emplace(std::string(name),
                      text_entities(attributes.substr(value, at - value)))
             .second)
      throw std::logic_error("duplicate emitted HTML attribute");
    ++at;
  }
  return result;
}

std::optional<std::uint64_t> ordinal(const std::string &value) {
  if (value.empty())
    return std::nullopt;
  std::uint64_t result = 0;
  for (const auto ch : value) {
    if (ch < '0' || ch > '9' ||
        result > (std::numeric_limits<std::uint64_t>::max() - (ch - '0')) / 10)
      return std::nullopt;
    result = result * 10 + (ch - '0');
  }
  return result;
}

} // namespace

std::string html_visible_text(const std::string &html) {
  std::string out;
  out.reserve(html.size());
  struct List {
    bool ordered;
    std::uint64_t next = 1;
  };
  std::vector<List> lists;
  std::vector<std::string> elements;
  for (std::size_t at = 0; at < html.size();) {
    if (html.compare(at, 4, "<!--") == 0) {
      const auto end = html.find("-->", at + 4);
      if (end == std::string::npos)
        throw std::logic_error("unterminated emitted HTML comment");
      at = end + 3;
    } else if (html[at] == '<') {
      auto cursor = at + 1;
      const bool closing = cursor < html.size() && html[cursor] == '/';
      if (closing)
        ++cursor;
      const auto begin = cursor;
      while (cursor < html.size() &&
             ((html[cursor] >= 'a' && html[cursor] <= 'z') ||
              (html[cursor] >= '0' && html[cursor] <= '9')))
        ++cursor;
      const std::string_view tag(html.data() + begin, cursor - begin);
      if (!supported_tag(tag))
        throw std::logic_error("unsupported emitted HTML tag: " +
                               std::string(tag));
      const auto attributes_begin = cursor;
      char quote = 0;
      for (; cursor < html.size(); ++cursor) {
        const auto ch = html[cursor];
        if (quote != 0) {
          if (ch == quote)
            quote = 0;
        } else if (ch == '\'' || ch == '"')
          quote = ch;
        else if (ch == '>')
          break;
      }
      if (cursor == html.size())
        throw std::logic_error("unterminated emitted HTML tag");
      if (boundary(tag))
        out.push_back('\n');
      const auto attributes =
          parse_attributes(tag, std::string_view(html.data() + attributes_begin,
                                                 cursor - attributes_begin));
      const bool empty = tag == "img" || tag == "br";
      if (closing) {
        if (!attributes.empty() || empty || elements.empty() ||
            elements.back() != tag)
          throw std::logic_error("unbalanced emitted HTML tag");
        elements.pop_back();
      } else if (!empty)
        elements.push_back(std::string(tag));
      if (!closing && tag == "img")
        if (const auto alt = attributes.find("alt"); alt != attributes.end())
          out += alt->second;
      // Geist emits decimal ordered lists with optional per-item `value`.
      // The browser supplies these visible markers rather than a text node.
      // It emits neither `start` nor `reversed` nor CSS counter overrides.
      if (tag == "ol" || tag == "ul") {
        if (closing) {
          if (!lists.empty())
            lists.pop_back();
        } else
          lists.push_back({tag == "ol", 1});
      } else if (!closing && tag == "li" && !lists.empty() &&
                 lists.back().ordered) {
        auto &list = lists.back();
        const auto value = attributes.find("value");
        if (value != attributes.end()) {
          const auto parsed = ordinal(value->second);
          if (parsed)
            list.next = *parsed;
        }
        out += std::to_string(list.next) + ". ";
        if (list.next < std::numeric_limits<std::uint64_t>::max())
          ++list.next;
      }
      at = cursor + 1;
    } else if (html[at] == '&') {
      const auto end = html.find(';', at + 1);
      char decoded = 0;
      if (end != std::string::npos &&
          entity(std::string_view(html.data() + at + 1, end - at - 1),
                 decoded)) {
        out.push_back(decoded);
        at = end + 1;
      } else
        throw std::logic_error("unsupported entity in emitted HTML text");
    } else
      out.push_back(html[at++]);
  }
  if (!elements.empty())
    throw std::logic_error("unclosed emitted HTML tag");
  return out;
}

} // namespace geist::detail
