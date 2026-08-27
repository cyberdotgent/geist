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

std::string code_span(const std::string &value, bool table_cell = false) {
  const auto code = normalize_code_span(value, table_cell);
  const auto delimiter = std::string(longest_backtick_run(code) + 1, '`');
  const auto all_spaces =
      !code.empty() &&
      std::all_of(code.begin(), code.end(), [](char ch) { return ch == ' '; });
  const auto needs_padding =
      (!code.empty() && (code.front() == '`' || code.back() == '`')) ||
      (!all_spaces && code.size() >= 2 && code.front() == ' ' &&
       code.back() == ' ');
  return delimiter + (needs_padding ? " " : "") + code +
         (needs_padding ? " " : "") + delimiter;
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

std::string render_inlines(const InlineSequenceIR &inlines,
                           InlineContext context,
                           const DocumentMarkdownRendererOptions &options) {
  std::string result;
  for (const auto &in : inlines) {
    std::visit(
        [&](const auto &node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, TextInlineIR>) {
            result += escape_markdown_text(node.text);
          } else if constexpr (std::is_same_v<T, EmphasisInlineIR>) {
            const auto delimiter = emphasis_delimiter(node.kind);
            result += delimiter + escape_markdown_text(node.text) + delimiter;
          } else if constexpr (std::is_same_v<T, CodeInlineIR>) {
            result +=
                code_span(node.code, context == InlineContext::table_cell);
          } else if constexpr (std::is_same_v<T, CrossReferenceInlineIR>) {
            const auto label =
                node.label.empty() ? node.target.value : node.label;
            result += '[' + escape_markdown_text(label) + "](" +
                      markdown_destination(
                          cross_reference_destination(node.target, options)) +
                      ')';
          } else if constexpr (std::is_same_v<T, ImageInlineIR>) {
            result += "![" + escape_markdown_text(node.alt_text) + "](" +
                      markdown_destination(node.resource) + ')';
          } else if constexpr (std::is_same_v<T, HardBreakInlineIR>) {
            result += context == InlineContext::prose ? "  \n" : "<br>";
          } else if constexpr (std::is_same_v<T, OpaqueInlineIR>) {
            const auto payload = node.content.empty()
                                     ? node.kind
                                     : node.kind + ": " + node.content;
            result += code_span(payload, context == InlineContext::table_cell);
          }
        },
        in.node);
  }
  return result;
}

std::string render_alt_text(const InlineSequenceIR &inlines) {
  std::string result;
  for (const auto &in : inlines) {
    std::visit(
        [&](const auto &node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, TextInlineIR> ||
                        std::is_same_v<T, EmphasisInlineIR>) {
            result += escape_markdown_text(node.text);
          } else if constexpr (std::is_same_v<T, CodeInlineIR>) {
            result += escape_markdown_text(node.code);
          } else if constexpr (std::is_same_v<T, CrossReferenceInlineIR>) {
            result += escape_markdown_text(
                node.label.empty() ? node.target.value : node.label);
          } else if constexpr (std::is_same_v<T, ImageInlineIR>) {
            result += escape_markdown_text(
                node.alt_text.empty() ? node.resource : node.alt_text);
          } else if constexpr (std::is_same_v<T, HardBreakInlineIR>) {
            result.push_back(' ');
          } else if constexpr (std::is_same_v<T, OpaqueInlineIR>) {
            result += escape_markdown_text(node.content.empty() ? node.kind
                                                                : node.content);
          }
        },
        in.node);
  }
  return result;
}

std::string fenced_block(const std::vector<std::string> &lines) {
  auto longest = std::size_t{0};
  for (const auto &line : lines)
    longest = std::max(longest, longest_backtick_run(line));
  const auto fence = std::string(std::max<std::size_t>(3, longest + 1), '`');
  std::string result = fence + '\n';
  for (std::size_t index = 0; index < lines.size(); ++index) {
    result += lines[index];
    result.push_back('\n');
  }
  result += fence;
  return result;
}

std::string table_row(const std::vector<std::string> &cells) {
  std::string result = "|";
  for (const auto &cell : cells)
    result += " " + cell + " |";
  return result;
}

std::string render_table(const TableBlockIR &table,
                         const DocumentMarkdownRendererOptions &options) {
  const auto width = table.rows.front().cells.size();
  std::vector<std::string> header(width);
  auto body_begin = std::size_t{0};
  if (table.header_rows != 0) {
    // Pipe tables have one header row.  Preserve every declared header by
    // combining same-column header cells with explicit line breaks.
    for (std::size_t row = 0; row < table.header_rows; ++row)
      for (std::size_t cell = 0; cell < width; ++cell) {
        if (row != 0)
          header[cell] += "<br>";
        header[cell] += render_inlines(table.rows[row].cells[cell].content,
                                       InlineContext::table_cell, options);
      }
    body_begin = table.header_rows;
  }
  // A headerless IR table receives an empty synthetic Markdown header; all
  // source rows remain body rows.  Empty cells are emitted as empty pipe cells.
  std::string result = table_row(header) + '\n';
  result += table_row(std::vector<std::string>(width, "---"));
  for (auto row = body_begin; row < table.rows.size(); ++row) {
    std::vector<std::string> cells;
    cells.reserve(width);
    for (const auto &cell : table.rows[row].cells)
      cells.push_back(
          render_inlines(cell.content, InlineContext::table_cell, options));
    result += '\n' + table_row(cells);
  }
  return result;
}

std::string render_block(const BlockNodeIR &block,
                         const DocumentMarkdownRendererOptions &options) {
  return std::visit(
      [&](const auto &node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, HeadingBlockIR>) {
          return std::string(node.level, '#') + " " +
                 render_inlines(node.content, InlineContext::single_line,
                                options);
        } else if constexpr (std::is_same_v<T, ParagraphBlockIR>) {
          return render_inlines(node.content, InlineContext::prose, options);
        } else if constexpr (std::is_same_v<T, AnchorBlockIR>) {
          return "<a id=\"" + escape_html_attribute(node.id) + "\"></a>";
        } else if constexpr (std::is_same_v<T, ListBlockIR>) {
          std::string result;
          for (std::size_t index = 0; index < node.items.size(); ++index) {
            if (index != 0)
              result.push_back('\n');
            if (node.ordered) {
              result += std::to_string(
                            node.items[index].source_ordinal.value_or(1)) +
                        ". ";
            } else {
              result += "- ";
            }
            result += render_inlines(node.items[index].content,
                                     InlineContext::single_line, options);
          }
          return result;
        } else if constexpr (std::is_same_v<T, DefinitionListBlockIR>) {
          std::string result;
          for (std::size_t index = 0; index < node.entries.size(); ++index) {
            if (index != 0)
              result.push_back('\n');
            result += "- **" +
                      render_inlines(node.entries[index].term,
                                     InlineContext::single_line, options) +
                      ":** " +
                      render_inlines(node.entries[index].definition,
                                     InlineContext::single_line, options);
          }
          return result;
        } else if constexpr (std::is_same_v<T, TableBlockIR>) {
          return render_table(node, options);
        } else if constexpr (std::is_same_v<T, PreformattedBlockIR>) {
          return fenced_block(node.lines);
        } else if constexpr (std::is_same_v<T, NoteBlockIR>) {
          std::string result = "> ";
          if (!node.label.empty())
            result += "**" +
                      render_inlines(node.label, InlineContext::single_line,
                                     options) +
                      ":** ";
          result +=
              render_inlines(node.content, InlineContext::single_line, options);
          return result;
        } else if constexpr (std::is_same_v<T, PublicationListBlockIR>) {
          std::string result;
          for (std::size_t entry = 0; entry < node.entries.size(); ++entry) {
            if (entry != 0)
              result += "\n\n";
            result += "- **" +
                      render_inlines(node.entries[entry].title,
                                     InlineContext::single_line, options) +
                      "**";
            for (const auto &paragraph : node.entries[entry].paragraphs)
              result += "\n\n  " +
                        render_inlines(paragraph, InlineContext::single_line,
                                       options);
          }
          return result;
        } else if constexpr (std::is_same_v<T, FigureBlockIR>) {
          const auto alt = render_alt_text(node.caption);
          auto result =
              "![" + alt + "](" + markdown_destination(node.resource) + ')';
          if (!node.caption.empty())
            result += "\n\n*" +
                      render_inlines(node.caption, InlineContext::single_line,
                                     options) +
                      '*';
          return result;
        } else if constexpr (std::is_same_v<T, FootnoteBlockIR>) {
          return "[^" + footnote_label(node.id) + "]: " +
                 render_inlines(node.content, InlineContext::single_line,
                                options);
        } else if constexpr (std::is_same_v<T, IndexGroupBlockIR>) {
          std::string result;
          if (!node.heading.empty())
            result = "**" +
                     render_inlines(node.heading, InlineContext::single_line,
                                    options) +
                     "**\n\n";
          for (std::size_t index = 0; index < node.entries.size(); ++index) {
            if (index != 0)
              result.push_back('\n');
            result += "- [" +
                      render_inlines(node.entries[index].term,
                                     InlineContext::single_line, options) +
                      "](" + markdown_destination(node.entries[index].target) +
                      ')';
          }
          return result;
        } else if constexpr (std::is_same_v<T, MenuBlockIR>) {
          // BookServer presentation of a generated menu: the `Subtopics:`
          // lead line and the `<topic id> <label>` link text are reader
          // output (hosted FA1PLMM0 5.6, SC33-033 5.3, SC34-425 1.8.5.5,
          // SH12-565 APPENDIX1.9.5, SC31-711 2.1), not source text.  The
          // unresolved destination is the same `#<id>` form the legacy
          // `:li refid` route produces so that boo2git rewrites both alike.
          std::string result = "Subtopics:\n\n";
          for (std::size_t index = 0; index < node.items.size(); ++index) {
            if (index != 0)
              result.push_back('\n');
            const auto &item = node.items[index];
            result += "- [" +
                      escape_markdown_text(item.target.value + ' ' +
                                           item.label) +
                      "](" +
                      markdown_destination(menu_destination(item.target,
                                                            options)) +
                      ')';
          }
          return result;
        } else if constexpr (std::is_same_v<T, OpaqueBlockIR>) {
          return "**Opaque " + escape_markdown_text(node.kind) +
                 " content:**\n\n" +
                 fenced_block(std::vector<std::string>{node.content});
        } else {
          throw std::logic_error(
              "legacy region reached the typed Markdown renderer");
        }
      },
      block);
}

} // namespace

std::string render_document_markdown(
    const DocumentIR &document,
    const DocumentMarkdownRendererOptions &options) {
  std::string error;
  if (!verify_document_ir(document, &error))
    throw std::invalid_argument("invalid DocumentIR: " + error);

  // The legacy adapter remains one indivisible whole-topic call because its
  // state machine carries state across normalized record boundaries.
  if (document.blocks.size() == 1) {
    if (const auto *region =
            std::get_if<LegacyGmlRegionIR>(&document.blocks.front().node)) {
      if (region->state_scope != LegacyRendererStateScopeIR::whole_topic)
        throw std::invalid_argument(
            "DocumentIR Markdown adapter requires one whole-topic legacy "
            "region");
      return render_markdown_records(region->normalized_records);
    }
  }

  std::string result;
  for (const auto &block : document.blocks) {
    if (!result.empty())
      result += "\n\n";
    result += render_block(block.node, options);
  }
  result.push_back('\n');
  return result;
}

} // namespace geist::detail
