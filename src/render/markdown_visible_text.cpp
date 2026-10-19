// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/render/markdown_visible_text.hpp"

#include "geist/detail/render/html_visible_text.hpp"
#include "geist/detail/render/markdown_text_encoding.hpp"

#include <stdexcept>

namespace geist::detail {

std::string verified_markdown_visible_text(
    const std::string &actual, const std::string &serialized,
    const DocumentRenderTraceIR &trace, const std::string &diagnostic_prefix) {
  if (actual != diagnostic_prefix + serialized)
    throw std::logic_error("serialized Markdown disagrees with topic output");
  return markdown_visible_text(actual.substr(diagnostic_prefix.size()), trace);
}

std::string markdown_visible_text(const std::string &markdown,
                                  const DocumentRenderTraceIR &trace) {
  std::string out;
  std::size_t next = 0;
  for (const auto &span : trace.spans) {
    if (span.output_begin != next || span.output_end <= span.output_begin ||
        span.output_end > markdown.size())
      throw std::logic_error(
          "Markdown text projection requires a complete trace");
    next = span.output_end;
    const auto bytes =
        markdown.substr(span.output_begin, span.output_end - span.output_begin);
    switch (span.text_projection) {
    case MarkdownTextProjectionIR::hidden:
      break;
    case MarkdownTextProjectionIR::boundary:
      out.push_back('\n');
      break;
    case MarkdownTextProjectionIR::literal:
      out += bytes;
      break;
    case MarkdownTextProjectionIR::html:
      out += html_visible_text(bytes);
      break;
    case MarkdownTextProjectionIR::escaped_text:
      // The serializer escapes punctuation with a backslash and escapes an
      // ampersand as &amp;. Decode exactly one layer; literal code never uses
      // this branch, and no Markdown link/destination parsing is involved.
      for (std::size_t at = 0; at < bytes.size(); ++at) {
        if (bytes[at] == '\\') {
          if (at + 1 == bytes.size() || !is_markdown_punctuation(bytes[at + 1]))
            throw std::logic_error("unsupported emitted Markdown escape");
          out.push_back(bytes[++at]);
        } else if (bytes.compare(at, 5, "&amp;") == 0) {
          out.push_back('&');
          at += 4;
        } else if (bytes[at] == '&' || is_markdown_punctuation(bytes[at]))
          throw std::logic_error(
              "unescaped punctuation in emitted Markdown text");
        else
          out.push_back(bytes[at]);
      }
      break;
    default:
      throw std::logic_error("unsupported Markdown text projection");
    }
  }
  if (next != markdown.size())
    throw std::logic_error(
        "Markdown text projection requires a complete trace");
  return out;
}

} // namespace geist::detail
