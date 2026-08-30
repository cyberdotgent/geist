#include "geist/detail/document_markdown_renderer.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace geist::detail {
namespace {

enum class InlineContext {
  prose,
  single_line,
  table_cell,
};

bool is_markdown_punctuation(char ch) {
  switch (ch) {
  case '\\':
  case '`':
  case '*':
  case '_':
  case '{':
  case '}':
  case '[':
  case ']':
  case '<':
  case '>':
  case '(':
  case ')':
  case '#':
  case '+':
  case '-':
  case '.':
  case '!':
  case '|':
  case '~':
    return true;
  default:
    return false;
  }
}

std::string escape_markdown_text(const std::string &value) {
  std::string result;
  result.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    const auto ch = value[index];
    if (ch == '\r' || ch == '\n') {
      // Line structure is represented by HardBreakInlineIR.  Keeping an
      // embedded source newline here could accidentally start a new block.
      if (ch == '\r' && index + 1 < value.size() && value[index + 1] == '\n')
        ++index;
      result.push_back(' ');
    } else if (ch == '&') {
      // Prevent source text that resembles an entity from changing value.
      result += "&amp;";
    } else {
      if (is_markdown_punctuation(ch))
        result.push_back('\\');
      result.push_back(ch);
    }
  }
  return result;
}

std::string escape_html_attribute(const std::string &value) {
  std::ostringstream result;
  for (const auto raw_ch : value) {
    const auto ch = static_cast<unsigned char>(raw_ch);
    switch (raw_ch) {
    case '&':
      result << "&amp;";
      break;
    case '<':
      result << "&lt;";
      break;
    case '>':
      result << "&gt;";
      break;
    case '"':
      result << "&quot;";
      break;
    case '\'':
      result << "&#39;";
      break;
    default:
      if (ch < 0x20 || ch == 0x7f)
        result << "&#x" << std::uppercase << std::hex
               << static_cast<unsigned int>(ch) << std::dec << ';';
      else
        result << raw_ch;
    }
  }
  return result.str();
}

bool is_uri_safe(unsigned char ch) {
  return std::isalnum(ch) != 0 || ch == '-' || ch == '.' || ch == '_' ||
         ch == '~' || ch == ':' || ch == '/' || ch == '?' || ch == '#' ||
         ch == '[' || ch == ']' || ch == '@' || ch == '!' || ch == '$' ||
         ch == '&' || ch == '\'' || ch == '(' || ch == ')' || ch == '*' ||
         ch == '+' || ch == ',' || ch == ';' || ch == '=' || ch == '%';
}

std::string markdown_destination(const std::string &value) {
  std::ostringstream result;
  result << '<';
  for (const auto raw_ch : value) {
    const auto ch = static_cast<unsigned char>(raw_ch);
    if (is_uri_safe(ch)) {
      result << raw_ch;
    } else {
      result << '%' << std::uppercase << std::hex << std::setw(2)
             << std::setfill('0') << static_cast<unsigned int>(ch) << std::dec;
    }
  }
  result << '>';
  return result.str();
}

std::string cross_reference_destination(
    const CrossReferenceTargetIR &target,
    const DocumentMarkdownRendererOptions &options) {
  if (options.resolve_cross_reference) {
    if (const auto resolved = options.resolve_cross_reference(target))
      return *resolved;
  }
  switch (target.kind) {
  case CrossReferenceTargetKindIR::topic:
  case CrossReferenceTargetKindIR::resource:
  case CrossReferenceTargetKindIR::external: return target.value;
  case CrossReferenceTargetKindIR::anchor: return '#' + target.value;
  }
  throw std::logic_error("invalid cross-reference target kind");
}

std::string menu_destination(const CrossReferenceTargetIR &target,
                             const DocumentMarkdownRendererOptions &options) {
  if (options.resolve_cross_reference) {
    if (const auto resolved = options.resolve_cross_reference(target))
      return *resolved;
  }
  return '#' + target.value;
}

std::string footnote_label(const std::string &value) {
  std::ostringstream result;
  for (const auto raw_ch : value) {
    const auto ch = static_cast<unsigned char>(raw_ch);
    if (std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.') {
      result << raw_ch;
    } else {
      result << '%' << std::uppercase << std::hex << std::setw(2)
             << std::setfill('0') << static_cast<unsigned int>(ch) << std::dec;
    }
  }
  return result.str();
}

std::size_t longest_backtick_run(const std::string &value) {
  auto longest = std::size_t{0};
  auto current = std::size_t{0};
  for (const auto ch : value) {
    if (ch == '`') {
      longest = std::max(longest, ++current);
    } else {
      current = 0;
    }
  }
  return longest;
}

std::string normalize_code_span(std::string code, bool table_cell) {
  for (auto &ch : code)
    if (ch == '\r' || ch == '\n')
      ch = ' ';
  if (table_cell) {
    std::string escaped;
    escaped.reserve(code.size());
    for (const auto ch : code) {
      if (ch == '|')
        escaped.push_back('\\');
      escaped.push_back(ch);
    }
    return escaped;
  }
  return code;
}

// A code span split into the delimiters the renderer adds and the projected
// source body between them, so a trace can attribute each half correctly.
struct CodeSpanPartsIR {
  std::string open;
  std::string body;
  std::string close;
};

CodeSpanPartsIR code_span_parts(const std::string &value, bool table_cell) {
  auto code = normalize_code_span(value, table_cell);
  const auto delimiter = std::string(longest_backtick_run(code) + 1, '`');
  const auto all_spaces =
      !code.empty() &&
      std::all_of(code.begin(), code.end(), [](char ch) { return ch == ' '; });
  const auto needs_padding =
      (!code.empty() && (code.front() == '`' || code.back() == '`')) ||
      (!all_spaces && code.size() >= 2 && code.front() == ' ' &&
       code.back() == ' ');
  const auto padding = needs_padding ? std::string(" ") : std::string();
  return {delimiter + padding, std::move(code), padding + delimiter};
}

std::string emphasis_delimiter(EmphasisKindIR kind) {
  switch (kind) {
  case EmphasisKindIR::emphasis:
    return "*";
  case EmphasisKindIR::strong:
    return "**";
  case EmphasisKindIR::strong_emphasis:
    return "***";
  }
  return "*";
}

// Collects the rendered Markdown and, only when a trace was requested, the
// output-range to node-path map beside it.  Every rendered byte passes
// through exactly one `emit`, so the recorded spans cover the output without
// gaps or overlaps by construction.
class RenderSink {
public:
  explicit RenderSink(DocumentRenderTraceIR *trace) noexcept : trace_(trace) {}

  bool tracing() const noexcept { return trace_ != nullptr; }
  const std::string &text() const noexcept { return text_; }
  std::string release() { return std::move(text_); }

  void syntax(const std::string &value, const char *reason,
              const DocumentNodeOriginIR *origin = nullptr) {
    emit(value, DocumentTraceRoleIR::syntax, reason, origin);
  }
  void generated(const std::string &value, const char *reason) {
    emit(value, DocumentTraceRoleIR::generated, reason, nullptr);
  }
  void content(const std::string &value, const char *reason,
               const DocumentNodeOriginIR *origin) {
    emit(value, DocumentTraceRoleIR::content, reason, origin);
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
            const char *reason, const DocumentNodeOriginIR *origin) {
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

// Pushes one structural step for the lifetime of the scope.
class PathScope {
public:
  PathScope(RenderSink &sink, const char *kind, std::size_t index)
      : sink_(sink) {
    sink_.push(kind, index);
  }
  PathScope(const PathScope &) = delete;
  PathScope &operator=(const PathScope &) = delete;
  ~PathScope() { sink_.pop(); }

private:
  RenderSink &sink_;
};

void append_inlines(RenderSink &sink, const InlineSequenceIR &inlines,
                    InlineContext context,
                    const DocumentMarkdownRendererOptions &options) {
  for (std::size_t index = 0; index < inlines.size(); ++index) {
    const PathScope step(sink, "inline", index);
    const auto &in = inlines[index];
    const auto *origin = &in.origin;
    std::visit(
        [&](const auto &node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, TextInlineIR>) {
            sink.content(escape_markdown_text(node.text), "text", origin);
          } else if constexpr (std::is_same_v<T, EmphasisInlineIR>) {
            const auto delimiter = emphasis_delimiter(node.kind);
            sink.syntax(delimiter, "emphasis delimiter", origin);
            sink.content(escape_markdown_text(node.text), "emphasis text",
                         origin);
            sink.syntax(delimiter, "emphasis delimiter", origin);
          } else if constexpr (std::is_same_v<T, CodeInlineIR>) {
            const auto parts = code_span_parts(
                node.code, context == InlineContext::table_cell);
            sink.syntax(parts.open, "code delimiter", origin);
            sink.content(parts.body, "code text", origin);
            sink.syntax(parts.close, "code delimiter", origin);
          } else if constexpr (std::is_same_v<T, CrossReferenceInlineIR>) {
            const auto label =
                node.label.empty() ? node.target.value : node.label;
            sink.syntax("[", "link syntax", origin);
            sink.content(escape_markdown_text(label), "link label", origin);
            sink.syntax("](", "link syntax", origin);
            sink.syntax(markdown_destination(
                            cross_reference_destination(node.target, options)),
                        "link destination", origin);
            sink.syntax(")", "link syntax", origin);
          } else if constexpr (std::is_same_v<T, ImageInlineIR>) {
            sink.syntax("![", "image syntax", origin);
            sink.content(escape_markdown_text(node.alt_text), "image alt text",
                         origin);
            sink.syntax("](", "image syntax", origin);
            sink.syntax(markdown_destination(node.resource),
                        "image destination", origin);
            sink.syntax(")", "image syntax", origin);
          } else if constexpr (std::is_same_v<T, HardBreakInlineIR>) {
            sink.syntax(context == InlineContext::prose ? "  \n" : "<br>",
                        "hard break", origin);
          } else if constexpr (std::is_same_v<T, OpaqueInlineIR>) {
            const auto payload = node.content.empty()
                                     ? node.kind
                                     : node.kind + ": " + node.content;
            const auto parts = code_span_parts(
                payload, context == InlineContext::table_cell);
            sink.syntax(parts.open, "code delimiter", origin);
            sink.content(parts.body, "opaque inline", origin);
            sink.syntax(parts.close, "code delimiter", origin);
          }
        },
        in.node);
  }
}

void append_alt_text(RenderSink &sink, const InlineSequenceIR &inlines) {
  for (std::size_t index = 0; index < inlines.size(); ++index) {
    const PathScope step(sink, "inline", index);
    const auto &in = inlines[index];
    const auto *origin = &in.origin;
    std::visit(
        [&](const auto &node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, TextInlineIR> ||
                        std::is_same_v<T, EmphasisInlineIR>) {
            sink.content(escape_markdown_text(node.text), "figure alt text",
                         origin);
          } else if constexpr (std::is_same_v<T, CodeInlineIR>) {
            sink.content(escape_markdown_text(node.code), "figure alt text",
                         origin);
          } else if constexpr (std::is_same_v<T, CrossReferenceInlineIR>) {
            sink.content(escape_markdown_text(node.label.empty()
                                                  ? node.target.value
                                                  : node.label),
                         "figure alt text", origin);
          } else if constexpr (std::is_same_v<T, ImageInlineIR>) {
            sink.content(escape_markdown_text(node.alt_text.empty()
                                                  ? node.resource
                                                  : node.alt_text),
                         "figure alt text", origin);
          } else if constexpr (std::is_same_v<T, HardBreakInlineIR>) {
            sink.syntax(" ", "figure alt text break", origin);
          } else if constexpr (std::is_same_v<T, OpaqueInlineIR>) {
            sink.content(escape_markdown_text(node.content.empty()
                                                  ? node.kind
                                                  : node.content),
                         "figure alt text", origin);
          }
        },
        in.node);
  }
}

void append_fenced_block(RenderSink &sink,
                         const std::vector<std::string> &lines,
                         const DocumentNodeOriginIR *origin,
                         const char *line_reason,
                         const std::vector<DocumentNodeOriginIR> *line_origins =
                             nullptr) {
  auto longest = std::size_t{0};
  for (const auto &line : lines)
    longest = std::max(longest, longest_backtick_run(line));
  const auto fence = std::string(std::max<std::size_t>(3, longest + 1), '`');
  sink.syntax(fence + '\n', "code fence", origin);
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const PathScope step(sink, "line", index);
    const auto *line_origin =
        line_origins != nullptr && index < line_origins->size()
            ? &(*line_origins)[index]
            : origin;
    sink.content(lines[index], line_reason, line_origin);
    sink.syntax("\n", "code fence line break", line_origin);
  }
  sink.syntax(fence, "code fence", origin);
}

void append_table(RenderSink &sink, const TableBlockIR &table,
                  const DocumentNodeOriginIR *origin,
                  const DocumentMarkdownRendererOptions &options) {
  const auto width = table.rows.front().cells.size();
  // Pipe tables have one header row.  Preserve every declared header by
  // combining same-column header cells with explicit line breaks.
  sink.syntax("|", "table pipe", origin);
  for (std::size_t cell = 0; cell < width; ++cell) {
    sink.syntax(" ", "table cell padding", origin);
    for (std::size_t row = 0; row < table.header_rows; ++row) {
      const PathScope row_step(sink, "row", row);
      const PathScope cell_step(sink, "cell", cell);
      if (row != 0)
        sink.syntax("<br>", "table header join",
                    &table.rows[row].cells[cell].origin);
      append_inlines(sink, table.rows[row].cells[cell].content,
                     InlineContext::table_cell, options);
    }
    sink.syntax(" |", "table pipe", origin);
  }
  // A headerless IR table receives an empty synthetic Markdown header; all
  // source rows remain body rows.  Empty cells are emitted as empty pipe
  // cells.
  sink.syntax("\n|", "table pipe", origin);
  for (std::size_t cell = 0; cell < width; ++cell)
    sink.syntax(" --- |", "table delimiter row", origin);
  for (auto row = table.header_rows; row < table.rows.size(); ++row) {
    const PathScope row_step(sink, "row", row);
    sink.syntax("\n|", "table pipe", &table.rows[row].origin);
    for (std::size_t cell = 0; cell < table.rows[row].cells.size(); ++cell) {
      const PathScope cell_step(sink, "cell", cell);
      sink.syntax(" ", "table cell padding",
                  &table.rows[row].cells[cell].origin);
      append_inlines(sink, table.rows[row].cells[cell].content,
                     InlineContext::table_cell, options);
      sink.syntax(" |", "table pipe", &table.rows[row].cells[cell].origin);
    }
  }
}

void append_block(RenderSink &sink, const BlockIR &block,
                  const DocumentMarkdownRendererOptions &options) {
  const auto *block_origin = &block.origin;
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, HeadingBlockIR>) {
          sink.syntax(std::string(node.level, '#') + " ", "heading marker",
                      block_origin);
          append_inlines(sink, node.content, InlineContext::single_line,
                         options);
        } else if constexpr (std::is_same_v<T, ParagraphBlockIR>) {
          append_inlines(sink, node.content, InlineContext::prose, options);
        } else if constexpr (std::is_same_v<T, AnchorBlockIR>) {
          sink.syntax("<a id=\"" + escape_html_attribute(node.id) + "\"></a>",
                      "anchor", block_origin);
        } else if constexpr (std::is_same_v<T, ListBlockIR>) {
          for (std::size_t index = 0; index < node.items.size(); ++index) {
            const PathScope item_step(sink, "item", index);
            const auto *item_origin = &node.items[index].origin;
            if (index != 0)
              sink.syntax("\n", "list item break", item_origin);
            sink.syntax(
                std::string(
                    static_cast<std::size_t>(node.items[index].depth) * 2, ' '),
                "list indent", item_origin);
            if (node.ordered)
              sink.syntax(std::to_string(
                              node.items[index].source_ordinal.value_or(1)) +
                              ". ",
                          "ordered list marker", item_origin);
            else
              sink.syntax("- ", "list bullet", item_origin);
            append_inlines(sink, node.items[index].content,
                           InlineContext::single_line, options);
          }
        } else if constexpr (std::is_same_v<T, DefinitionListBlockIR>) {
          for (std::size_t index = 0; index < node.entries.size(); ++index) {
            const PathScope entry_step(sink, "entry", index);
            const auto *entry_origin = &node.entries[index].origin;
            if (index != 0)
              sink.syntax("\n", "definition entry break", entry_origin);
            sink.syntax("- **", "definition term syntax", entry_origin);
            {
              const PathScope field(sink, "term", 0);
              append_inlines(sink, node.entries[index].term,
                             InlineContext::single_line, options);
            }
            sink.syntax(":** ", "definition term syntax", entry_origin);
            const PathScope field(sink, "definition", 0);
            append_inlines(sink, node.entries[index].definition,
                           InlineContext::single_line, options);
          }
        } else if constexpr (std::is_same_v<T, TableBlockIR>) {
          append_table(sink, node, block_origin, options);
        } else if constexpr (std::is_same_v<T, PreformattedBlockIR>) {
          append_fenced_block(sink, node.lines, block_origin,
                              "preformatted line", &node.line_origins);
        } else if constexpr (std::is_same_v<T, NoteBlockIR>) {
          sink.syntax("> ", "note marker", block_origin);
          if (!node.label.empty()) {
            sink.syntax("**", "note label syntax", block_origin);
            {
              const PathScope field(sink, "label", 0);
              append_inlines(sink, node.label, InlineContext::single_line,
                             options);
            }
            sink.syntax(":** ", "note label syntax", block_origin);
          }
          const PathScope field(sink, "content", 0);
          append_inlines(sink, node.content, InlineContext::single_line,
                         options);
        } else if constexpr (std::is_same_v<T, PublicationListBlockIR>) {
          for (std::size_t entry = 0; entry < node.entries.size(); ++entry) {
            const PathScope entry_step(sink, "entry", entry);
            const auto *entry_origin = &node.entries[entry].origin;
            if (entry != 0)
              sink.syntax("\n\n", "publication entry break", entry_origin);
            sink.syntax("- **", "publication title syntax", entry_origin);
            {
              const PathScope field(sink, "title", 0);
              append_inlines(sink, node.entries[entry].title,
                             InlineContext::single_line, options);
            }
            sink.syntax("**", "publication title syntax", entry_origin);
            for (std::size_t paragraph = 0;
                 paragraph < node.entries[entry].paragraphs.size();
                 ++paragraph) {
              const PathScope field(sink, "paragraph", paragraph);
              sink.syntax("\n\n  ", "publication paragraph indent",
                          entry_origin);
              append_inlines(sink, node.entries[entry].paragraphs[paragraph],
                             InlineContext::single_line, options);
            }
          }
        } else if constexpr (std::is_same_v<T, FigureBlockIR>) {
          sink.syntax("![", "image syntax", block_origin);
          {
            const PathScope field(sink, "alt", 0);
            append_alt_text(sink, node.caption);
          }
          sink.syntax("](", "image syntax", block_origin);
          sink.syntax(markdown_destination(node.resource),
                      "image destination", block_origin);
          sink.syntax(")", "image syntax", block_origin);
          if (!node.caption.empty()) {
            sink.syntax("\n\n*", "figure caption syntax", block_origin);
            const PathScope field(sink, "caption", 0);
            append_inlines(sink, node.caption, InlineContext::single_line,
                           options);
            sink.syntax("*", "figure caption syntax", block_origin);
          }
        } else if constexpr (std::is_same_v<T, FootnoteBlockIR>) {
          sink.syntax("[^" + footnote_label(node.id) + "]: ",
                      "footnote marker", block_origin);
          append_inlines(sink, node.content, InlineContext::single_line,
                         options);
        } else if constexpr (std::is_same_v<T, IndexGroupBlockIR>) {
          if (!node.heading.empty()) {
            sink.syntax("**", "index heading syntax", block_origin);
            {
              const PathScope field(sink, "heading", 0);
              append_inlines(sink, node.heading, InlineContext::single_line,
                             options);
            }
            sink.syntax("**\n\n", "index heading syntax", block_origin);
          }
          for (std::size_t index = 0; index < node.entries.size(); ++index) {
            const PathScope entry_step(sink, "entry", index);
            const auto *entry_origin = &node.entries[index].origin;
            if (index != 0)
              sink.syntax("\n", "index entry break", entry_origin);
            sink.syntax("- [", "index entry syntax", entry_origin);
            {
              const PathScope field(sink, "term", 0);
              append_inlines(sink, node.entries[index].term,
                             InlineContext::single_line, options);
            }
            sink.syntax("](", "index entry syntax", entry_origin);
            sink.syntax(markdown_destination(node.entries[index].target),
                        "index entry destination", entry_origin);
            sink.syntax(")", "index entry syntax", entry_origin);
          }
        } else if constexpr (std::is_same_v<T, MenuBlockIR>) {
          // BookServer presentation of a generated menu: the `Subtopics:`
          // lead line and the `<topic id> <label>` link text are reader
          // output (hosted FA1PLMM0 5.6, SC33-033 5.3, SC34-425 1.8.5.5,
          // SH12-565 APPENDIX1.9.5, SC31-711 2.1), not source text.  The
          // unresolved destination is the same `#<id>` form the legacy
          // `:li refid` route produces so that boo2git rewrites both alike.
          sink.generated("Subtopics:\n\n", "reader-generated menu lead line");
          for (std::size_t index = 0; index < node.items.size(); ++index) {
            const PathScope item_step(sink, "item", index);
            const auto &item = node.items[index];
            const auto *item_origin = &item.origin;
            if (index != 0)
              sink.syntax("\n", "menu item break", item_origin);
            sink.syntax("- [", "menu item syntax", item_origin);
            // The reader prefixes the visible label with the target topic id;
            // the id is a source-proven CMITEM operand, the separating space
            // is reader presentation.
            sink.content(escape_markdown_text(item.target.value),
                         "menu item target id", item_origin);
            sink.generated(escape_markdown_text(" "),
                           "reader-generated menu label separator");
            sink.content(escape_markdown_text(item.label), "menu item label",
                         item_origin);
            sink.syntax("](", "menu item syntax", item_origin);
            sink.syntax(
                markdown_destination(menu_destination(item.target, options)),
                "menu item destination", item_origin);
            sink.syntax(")", "menu item syntax", item_origin);
          }
        } else if constexpr (std::is_same_v<T, OpaqueBlockIR>) {
          sink.syntax("**Opaque ", "opaque block syntax", block_origin);
          sink.content(escape_markdown_text(node.kind), "opaque block kind",
                       block_origin);
          sink.syntax(" content:**\n\n", "opaque block syntax", block_origin);
          append_fenced_block(sink, std::vector<std::string>{node.content},
                              block_origin, "opaque block content");
        } else {
          throw std::logic_error(
              "legacy region reached the typed Markdown renderer");
        }
      },
      block.node);
}

} // namespace

std::string
format_document_node_path(const std::vector<DocumentNodePathStepIR> &path) {
  std::string result;
  for (const auto &step : path) {
    if (!result.empty())
      result.push_back('/');
    result += step.kind;
    result += '[' + std::to_string(step.index) + ']';
  }
  return result;
}

const DocumentTraceSpanIR *
resolve_document_trace_offset(const DocumentRenderTraceIR &trace,
                              std::size_t offset) {
  const auto found = std::upper_bound(
      trace.spans.begin(), trace.spans.end(), offset,
      [](std::size_t value, const DocumentTraceSpanIR &span) {
        return value < span.output_begin;
      });
  if (found == trace.spans.begin())
    return nullptr;
  const auto &span = *std::prev(found);
  return offset < span.output_end ? &span : nullptr;
}

bool verify_document_render_trace(const DocumentRenderTraceIR &trace,
                                  std::size_t output_size,
                                  std::string *error) {
  const auto fail = [&](const char *message) {
    if (error != nullptr)
      *error = message;
    return false;
  };
  std::size_t cursor = 0;
  for (const auto &span : trace.spans) {
    if (span.output_begin != cursor)
      return fail("render trace spans are not contiguous");
    if (span.output_end <= span.output_begin)
      return fail("render trace span is empty or reversed");
    if (span.reason.empty())
      return fail("render trace span has no reason");
    if (span.role == DocumentTraceRoleIR::content) {
      // Synthesized content is honest about having no source; every other
      // content run must name the bytes it projects.
      if (!span.origin)
        return fail("content render trace span has no node origin");
      if (span.origin->slices.empty() &&
          span.origin->derivation != DocumentDerivationIR::synthesized)
        return fail("content render trace span has no source slices");
    }
    cursor = span.output_end;
  }
  if (cursor != output_size)
    return fail("render trace does not cover the rendered output");
  if (error != nullptr)
    error->clear();
  return true;
}

std::string render_document_markdown(
    const DocumentIR &document,
    const DocumentMarkdownRendererOptions &options,
    DocumentRenderTraceIR *trace) {
  std::string error;
  if (!verify_document_ir(document, &error))
    throw std::invalid_argument("invalid DocumentIR: " + error);
  if (trace != nullptr)
    trace->spans.clear();

  // The legacy adapter remains one indivisible whole-topic call because its
  // state machine carries state across normalized record boundaries.
  if (document.blocks.size() == 1) {
    if (const auto *region =
            std::get_if<LegacyGmlRegionIR>(&document.blocks.front().node)) {
      if (region->state_scope != LegacyRendererStateScopeIR::whole_topic)
        throw std::invalid_argument(
            "DocumentIR Markdown adapter requires one whole-topic legacy "
            "region");
      auto rendered = render_markdown_records(region->normalized_records);
      if (trace != nullptr && !rendered.empty()) {
        DocumentTraceSpanIR span;
        span.output_begin = 0;
        span.output_end = rendered.size();
        span.role = DocumentTraceRoleIR::syntax;
        span.reason = "legacy whole-topic adapter";
        span.path.push_back({"block", 0});
        span.origin = document.blocks.front().origin;
        trace->spans.push_back(std::move(span));
      }
      return rendered;
    }
  }

  RenderSink sink(trace);
  for (std::size_t index = 0; index < document.blocks.size(); ++index) {
    const PathScope block_step(sink, "block", index);
    if (!sink.text().empty())
      sink.syntax("\n\n", "block separator", &document.blocks[index].origin);
    append_block(sink, document.blocks[index], options);
  }
  sink.syntax("\n", "document terminator", nullptr);
  return sink.release();
}

} // namespace geist::detail
