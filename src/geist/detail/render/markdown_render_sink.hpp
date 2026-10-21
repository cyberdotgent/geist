// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/render/document_markdown_renderer.hpp"

#include <utility>

namespace geist::detail {

// Shared by the typed and last-resort Markdown serializers. A trace records
// the actual output windows, never a second copy of presumed source text.
class MarkdownRenderSink {
public:
  explicit MarkdownRenderSink(DocumentRenderTraceIR *trace) noexcept
      : trace_(trace) {}

  bool tracing() const noexcept { return trace_ != nullptr; }
  const std::string &text() const noexcept { return text_; }
  std::string release() { return std::move(text_); }

  void syntax(const std::string &value, const char *reason,
              const DocumentNodeOriginIR *origin = nullptr) {
    emit(value, DocumentTraceRoleIR::syntax, MarkdownTextProjectionIR::hidden,
         reason, origin);
  }
  void boundary(const std::string &value, const char *reason,
                const DocumentNodeOriginIR *origin = nullptr) {
    emit(value, DocumentTraceRoleIR::syntax, MarkdownTextProjectionIR::boundary,
         reason, origin);
  }
  void marker(const std::string &value, const char *reason,
              const DocumentNodeOriginIR *origin = nullptr) {
    emit(value, DocumentTraceRoleIR::syntax, MarkdownTextProjectionIR::literal,
         reason, origin);
  }
  void generated(const std::string &value, const char *reason) {
    emit(value, DocumentTraceRoleIR::generated,
         MarkdownTextProjectionIR::escaped_text, reason, nullptr);
  }
  void content(const std::string &value, const char *reason,
               const DocumentNodeOriginIR *origin) {
    emit(value, DocumentTraceRoleIR::content,
         MarkdownTextProjectionIR::escaped_text, reason, origin);
  }
  void literal(const std::string &value, const char *reason,
               const DocumentNodeOriginIR *origin) {
    emit(value, DocumentTraceRoleIR::content, MarkdownTextProjectionIR::literal,
         reason, origin);
  }
  void html(const std::string &value, const char *reason,
            const DocumentNodeOriginIR *origin) {
    emit(value, DocumentTraceRoleIR::content, MarkdownTextProjectionIR::html,
         reason, origin);
  }
  // Replay an already-rendered child span while a compound container adds
  // indentation around it. This retains the child's exact trace role and
  // visible-text projection instead of collapsing the subtree to one span.
  void replay(const std::string &value, DocumentTraceRoleIR role,
              MarkdownTextProjectionIR projection, const char *reason,
              const DocumentNodeOriginIR *origin) {
    emit(value, role, projection, reason, origin);
  }
  void push(const char *kind, std::size_t index) {
    if (trace_ != nullptr)
      path_.push_back({kind, index});
  }
  void pop() {
    if (trace_ != nullptr)
      path_.pop_back();
  }

private:
  void emit(const std::string &value, DocumentTraceRoleIR role,
            MarkdownTextProjectionIR projection, const char *reason,
            const DocumentNodeOriginIR *origin) {
    if (value.empty())
      return;
    const auto begin = text_.size();
    text_ += value;
    if (trace_ == nullptr)
      return;
    DocumentTraceSpanIR span;
    span.output_begin = begin;
    span.output_end = text_.size();
    span.role = role;
    span.text_projection = projection;
    span.reason = reason;
    span.path = path_;
    if (origin != nullptr)
      span.origin = *origin;
    trace_->spans.push_back(std::move(span));
  }

  std::string text_;
  std::vector<DocumentNodePathStepIR> path_;
  DocumentRenderTraceIR *trace_ = nullptr;
};

} // namespace geist::detail
