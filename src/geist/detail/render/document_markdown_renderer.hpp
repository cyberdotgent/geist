// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/lowering/document_ir.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct DocumentMarkdownRendererOptions {
  // Return an output-specific destination, or nullopt to use the stable
  // context-free fallback. Topic/resource mappings supplied by an exporter
  // belong here rather than in DocumentIR semantic lowering.
  std::function<std::optional<std::string>(const CrossReferenceTargetIR &)>
      resolve_cross_reference;
};

// What one run of rendered output is.  `content` is a projection of source
// text the node owns and must resolve back to that node's origin slices;
// `syntax` is Markdown the renderer adds around content (delimiters, escapes,
// bullets, pipes, link destinations); `generated` is reader-style text the
// renderer invents and no BOO byte states (the `Subtopics:` lead line).
enum class DocumentTraceRoleIR {
  content,
  syntax,
  generated,
};

// One step of the path from the document root to the node that produced a
// span. `kind` is a stable structural name, never rendered content.
struct DocumentNodePathStepIR {
  std::string kind;
  std::size_t index = 0;
};

std::string
format_document_node_path(const std::vector<DocumentNodePathStepIR> &path);

// A half-open byte range of the rendered Markdown together with the node that
// produced it. Spans are emitted in output order, are non-overlapping, and
// together cover the whole rendered output.
struct DocumentTraceSpanIR {
  std::size_t output_begin = 0;
  std::size_t output_end = 0;
  DocumentTraceRoleIR role = DocumentTraceRoleIR::syntax;
  // Stable class of this run, e.g. "text", "heading marker", "table pipe".
  std::string reason;
  std::vector<DocumentNodePathStepIR> path;
  // The origin of the node named by `path`, when that node has one.
  std::optional<DocumentNodeOriginIR> origin;
};

struct DocumentRenderTraceIR {
  std::vector<DocumentTraceSpanIR> spans;
};

// Render a verified document as stable Markdown. Typed nodes are rendered
// without consulting decoder or semantic-layer state. A sole whole-topic
std::string escape_markdown_text(const std::string &value);

std::string render_document_markdown(
    const DocumentIR &document,
    const DocumentMarkdownRendererOptions &options = {},
    DocumentRenderTraceIR *trace = nullptr);

} // namespace geist::detail
