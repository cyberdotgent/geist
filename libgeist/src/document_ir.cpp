#include "geist/detail/document_ir.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <tuple>
#include <type_traits>

namespace geist::detail {
namespace {

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

bool verify_origin(const DocumentNodeOriginIR& origin, std::string* error) {
  switch (origin.derivation) {
  case DocumentDerivationIR::decoded:
  case DocumentDerivationIR::semantic_lowering:
  case DocumentDerivationIR::synthesized:
  case DocumentDerivationIR::legacy_adapter: break;
  default: return fail(error, "node origin has invalid derivation");
  }
  auto previous_slice = std::tuple<std::uint32_t, std::size_t, std::size_t,
                                   std::size_t, std::uint32_t,
                                   std::uint32_t>{};
  auto first_slice = true;
  for (const auto& slice : origin.slices) {
    if (slice.logical_record == 0)
      return fail(error, "source slice has no logical record");
    if (slice.token_begin > slice.token_end)
      return fail(error, "source slice has reversed token range");
    if (slice.byte_begin > slice.byte_end)
      return fail(error, "source slice has reversed byte range");
    const auto key = std::make_tuple(
        slice.logical_record, slice.segment_index, slice.token_begin,
        slice.token_end, slice.byte_begin, slice.byte_end);
    if (!first_slice && key <= previous_slice)
      return fail(error, "source slices are duplicated or out of order");
    previous_slice = key;
    first_slice = false;
  }
  auto previous_row = std::pair<std::uint64_t, std::size_t>{};
  auto first_row = true;
  for (const auto& row : origin.rows) {
    if (row.display_run == 0)
      return fail(error, "source row has no display run");
    const auto key = std::make_pair(row.display_run, row.row_index);
    if (!first_row && key <= previous_row)
      return fail(error, "source rows are duplicated or out of order");
    previous_row = key;
    first_row = false;
  }
  return true;
}

bool verify_inlines(const InlineSequenceIR& inlines, bool allow_empty,
                    std::string* error) {
  if (!allow_empty && inlines.empty())
    return fail(error, "required inline sequence is empty");
  for (const auto& in : inlines) {
    if (!verify_origin(in.origin, error)) return false;
    const auto valid = std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, TextInlineIR>) {
            return !node.text.empty();
          } else if constexpr (std::is_same_v<T, EmphasisInlineIR>) {
            return !node.text.empty();
          } else if constexpr (std::is_same_v<T, CodeInlineIR>) {
            return !node.code.empty();
          } else if constexpr (std::is_same_v<T, CrossReferenceInlineIR>) {
            switch (node.target.kind) {
            case CrossReferenceTargetKindIR::topic:
            case CrossReferenceTargetKindIR::anchor:
            case CrossReferenceTargetKindIR::resource:
            case CrossReferenceTargetKindIR::external:
              return !node.target.value.empty();
            }
            return false;
          } else if constexpr (std::is_same_v<T, ImageInlineIR>) {
            return !node.resource.empty();
          } else if constexpr (std::is_same_v<T, OpaqueInlineIR>) {
            return !node.kind.empty();
          } else {
            return true;
          }
        },
        in.node);
    if (!valid) return fail(error, "inline node is not canonical");
  }
  return true;
}

bool verify_block(const BlockIR& block, std::string* error) {
  if (!verify_origin(block.origin, error)) return false;
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, HeadingBlockIR>) {
          if (node.level == 0 || node.level > 6)
            return fail(error, "heading level is outside 1..6");
          return verify_inlines(node.content, false, error);
        } else if constexpr (std::is_same_v<T, ParagraphBlockIR>) {
          return verify_inlines(node.content, false, error);
        } else if constexpr (std::is_same_v<T, AnchorBlockIR>) {
          return !node.id.empty() || fail(error, "anchor id is empty");
        } else if constexpr (std::is_same_v<T, ListBlockIR>) {
          if (node.items.empty()) return fail(error, "list is empty");
          const auto explicit_ordinals = static_cast<std::size_t>(
              std::count_if(node.items.begin(), node.items.end(),
                            [](const auto& item) {
                              return item.source_ordinal.has_value();
                            }));
          if (!node.ordered && explicit_ordinals != 0)
            return fail(error, "unordered list item has a source ordinal");
          if (node.ordered && explicit_ordinals != 0 &&
              explicit_ordinals != node.items.size())
            return fail(error, "ordered list source ordinals are incomplete");
          std::uint64_t previous_ordinal = 0;
          for (const auto& item : node.items) {
            if (!verify_origin(item.origin, error) ||
                !verify_inlines(item.content, false, error))
              return false;
            if (item.source_ordinal) {
              if (*item.source_ordinal == 0)
                return fail(error, "ordered list source ordinal is zero");
              if (*item.source_ordinal <= previous_ordinal)
                return fail(
                    error,
                    "ordered list source ordinals are not strictly increasing");
              previous_ordinal = *item.source_ordinal;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, DefinitionListBlockIR>) {
          if (node.entries.empty())
            return fail(error, "definition list is empty");
          for (const auto& entry : node.entries) {
            if (!verify_origin(entry.origin, error) ||
                !verify_inlines(entry.term, false, error) ||
                !verify_inlines(entry.definition, false, error))
              return false;
          }
          return true;
        } else if constexpr (std::is_same_v<T, TableBlockIR>) {
          if (node.rows.empty()) return fail(error, "table is empty");
          if (node.header_rows > node.rows.size())
            return fail(error, "table header count exceeds row count");
          const auto width = node.rows.front().cells.size();
          if (width == 0) return fail(error, "table has no columns");
          for (const auto& row : node.rows) {
            if (!verify_origin(row.origin, error) || row.cells.size() != width)
              return fail(error, "table row geometry is inconsistent");
            for (const auto& cell : row.cells) {
              if (!verify_origin(cell.origin, error) ||
                  !verify_inlines(cell.content, true, error))
                return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, PreformattedBlockIR>) {
          return !node.lines.empty() ||
                 fail(error, "preformatted block is empty");
        } else if constexpr (std::is_same_v<T, NoteBlockIR>) {
          return verify_inlines(node.label, true, error) &&
                 verify_inlines(node.content, false, error);
        } else if constexpr (std::is_same_v<T, PublicationListBlockIR>) {
          if (node.entries.empty())
            return fail(error, "publication list is empty");
          for (const auto& entry : node.entries) {
            if (!verify_origin(entry.origin, error) ||
                !verify_inlines(entry.title, false, error) ||
                entry.paragraphs.empty())
              return fail(error, "publication entry is incomplete");
            for (const auto& paragraph : entry.paragraphs)
              if (!verify_inlines(paragraph, false, error)) return false;
          }
          return true;
        } else if constexpr (std::is_same_v<T, FigureBlockIR>) {
          if (node.resource.empty())
            return fail(error, "figure resource is empty");
          return verify_inlines(node.caption, true, error);
        } else if constexpr (std::is_same_v<T, FootnoteBlockIR>) {
          if (node.id.empty()) return fail(error, "footnote id is empty");
          return verify_inlines(node.content, false, error);
        } else if constexpr (std::is_same_v<T, IndexGroupBlockIR>) {
          if (!verify_inlines(node.heading, true, error) ||
              node.entries.empty())
            return fail(error, "index group is incomplete");
          for (const auto& entry : node.entries) {
            if (!verify_origin(entry.origin, error) || entry.target.empty() ||
                !verify_inlines(entry.term, false, error))
              return fail(error, "index entry is incomplete");
          }
          return true;
        } else if constexpr (std::is_same_v<T, MenuBlockIR>) {
          if (node.items.empty()) return fail(error, "menu is empty");
          for (const auto& item : node.items) {
            if (!verify_origin(item.origin, error)) return false;
            if (item.target.kind != CrossReferenceTargetKindIR::topic ||
                item.target.value.empty() || item.label.empty())
              return fail(error, "menu item is incomplete");
          }
          return true;
        } else if constexpr (std::is_same_v<T, OpaqueBlockIR>) {
          return !node.kind.empty() ||
                 fail(error, "opaque block kind is empty");
        } else {
          return node.state_scope == LegacyRendererStateScopeIR::whole_topic ||
                 fail(error, "legacy region has invalid renderer state scope");
        }
      },
      block.node);
}

std::string quoted(const std::string& value) {
  std::ostringstream out;
  out << '"';
  for (const auto ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    switch (ch) {
    case '\\': out << "\\\\"; break;
    case '"': out << "\\\""; break;
    case '\n': out << "\\n"; break;
    case '\r': out << "\\r"; break;
    case '\t': out << "\\t"; break;
    default:
      if (byte < 0x20)
        out << "\\x" << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned int>(byte) << std::dec;
      else
        out << ch;
    }
  }
  out << '"';
  return out.str();
}

const char* derivation_name(DocumentDerivationIR value) {
  switch (value) {
  case DocumentDerivationIR::decoded: return "decoded";
  case DocumentDerivationIR::semantic_lowering: return "semantic";
  case DocumentDerivationIR::synthesized: return "synthesized";
  case DocumentDerivationIR::legacy_adapter: return "legacy";
  }
  return "invalid";
}

const char* cross_reference_target_kind_name(
    CrossReferenceTargetKindIR value) {
  switch (value) {
  case CrossReferenceTargetKindIR::topic: return "topic";
  case CrossReferenceTargetKindIR::anchor: return "anchor";
  case CrossReferenceTargetKindIR::resource: return "resource";
  case CrossReferenceTargetKindIR::external: return "external";
  }
  return "invalid";
}

const char* emphasis_kind_name(EmphasisKindIR value) {
  switch (value) {
  case EmphasisKindIR::emphasis: return "emphasis";
  case EmphasisKindIR::strong: return "strong";
  case EmphasisKindIR::strong_emphasis: return "strong_emphasis";
  }
  return "invalid";
}

void format_origin(std::ostringstream& out,
                   const DocumentNodeOriginIR& origin) {
  out << " origin=" << derivation_name(origin.derivation);
  if (!origin.detail.empty()) out << ':' << quoted(origin.detail);
  for (const auto& slice : origin.slices) {
    out << " source=(lr=" << slice.logical_record
        << " seg=" << slice.segment_index << " tok=" << slice.token_begin
        << ':' << slice.token_end << " bytes=" << slice.byte_begin << ':'
        << slice.byte_end << ')';
  }
  for (const auto& row : origin.rows)
    out << " row=" << row.display_run << ':' << row.row_index;
}

void format_inlines(std::ostringstream& out, const InlineSequenceIR& inlines) {
  out << '[';
  bool first = true;
  for (const auto& in : inlines) {
    if (!first) out << ' ';
    first = false;
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, TextInlineIR>)
            out << "text=" << quoted(node.text);
          else if constexpr (std::is_same_v<T, EmphasisInlineIR>)
            out << emphasis_kind_name(node.kind) << '=' << quoted(node.text);
          else if constexpr (std::is_same_v<T, CodeInlineIR>)
            out << "code=" << quoted(node.code);
          else if constexpr (std::is_same_v<T, CrossReferenceInlineIR>)
            out << "xref="
                << cross_reference_target_kind_name(node.target.kind) << ':'
                << quoted(node.target.value) << ':' << quoted(node.label);
          else if constexpr (std::is_same_v<T, ImageInlineIR>)
            out << "image=" << quoted(node.resource) << ':'
                << quoted(node.alt_text);
          else if constexpr (std::is_same_v<T, HardBreakInlineIR>)
            out << "hard_break";
          else
            out << "opaque=" << quoted(node.kind) << ':'
                << quoted(node.content);
        },
        in.node);
    format_origin(out, in.origin);
  }
  out << ']';
}

void format_entry_origin(std::ostringstream& out,
                         const DocumentNodeOriginIR& origin) {
  out << '{';
  format_origin(out, origin);
  out << '}';
}

} // namespace

bool verify_document_ir(const DocumentIR& document, std::string* error) {
  const auto is_identity_free_legacy_adapter =
      document.topic.id.empty() && document.topic.title.empty() &&
      document.blocks.size() == 1 &&
      std::holds_alternative<LegacyGmlRegionIR>(document.blocks.front().node);
  if ((document.topic.id.empty() || document.topic.title.empty()) &&
      !is_identity_free_legacy_adapter)
    return fail(error, "document topic identity is incomplete");
  if (document.topic.end_logical_record != 0 &&
      document.topic.start_logical_record > document.topic.end_logical_record)
    return fail(error, "topic logical-record range is reversed");
  if (document.blocks.empty()) return fail(error, "document has no blocks");
  auto legacy_regions = std::size_t{0};
  for (const auto& block : document.blocks)
    if (!verify_block(block, error)) {
      return false;
    } else if (const auto* legacy =
                   std::get_if<LegacyGmlRegionIR>(&block.node)) {
      ++legacy_regions;
      if (legacy->normalized_records.empty())
        return fail(error, "legacy region has no normalized records");
      if (block.origin.derivation != DocumentDerivationIR::legacy_adapter)
        return fail(error, "legacy region has a nonlegacy origin");
    }
  if (legacy_regions != 0 &&
      (legacy_regions != 1 || document.blocks.size() != 1))
    return fail(error, "whole-topic legacy region is mixed or duplicated");
  return true;
}

std::string format_document_ir(const DocumentIR& document) {
  std::ostringstream out;
  out << "document id=" << quoted(document.topic.id)
      << " title=" << quoted(document.topic.title)
      << " heading_level=" << quoted(document.topic.heading_level)
      << " number=" << document.topic.topic_number << " lr="
      << document.topic.start_logical_record << ':'
      << document.topic.end_logical_record << '\n';
  for (std::size_t index = 0; index < document.blocks.size(); ++index) {
    const auto& block = document.blocks[index];
    out << "block " << index;
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, HeadingBlockIR>) {
            out << " heading level=" << node.level << ' ';
            format_inlines(out, node.content);
          } else if constexpr (std::is_same_v<T, ParagraphBlockIR>) {
            out << " paragraph ";
            format_inlines(out, node.content);
          } else if constexpr (std::is_same_v<T, AnchorBlockIR>) {
            out << " anchor=" << quoted(node.id);
          } else if constexpr (std::is_same_v<T, ListBlockIR>) {
            out << " list ordered=" << (node.ordered ? 1 : 0)
                << " items=[";
            for (std::size_t item = 0; item < node.items.size(); ++item) {
              if (item != 0) out << ' ';
              out << "item=";
              if (node.items[item].source_ordinal)
                out << "ordinal=" << *node.items[item].source_ordinal << ' ';
              format_inlines(out, node.items[item].content);
              format_entry_origin(out, node.items[item].origin);
            }
            out << ']';
          } else if constexpr (std::is_same_v<T, DefinitionListBlockIR>) {
            out << " definition_list entries=[";
            for (std::size_t entry = 0; entry < node.entries.size(); ++entry) {
              if (entry != 0) out << ' ';
              out << "term=";
              format_inlines(out, node.entries[entry].term);
              out << " definition=";
              format_inlines(out, node.entries[entry].definition);
              format_entry_origin(out, node.entries[entry].origin);
            }
            out << ']';
          } else if constexpr (std::is_same_v<T, TableBlockIR>) {
            out << " table header_rows=" << node.header_rows << " rows=[";
            for (std::size_t row = 0; row < node.rows.size(); ++row) {
              if (row != 0) out << ' ';
              out << "row=[";
              for (std::size_t cell = 0; cell < node.rows[row].cells.size();
                   ++cell) {
                if (cell != 0) out << ' ';
                out << "cell=";
                format_inlines(out, node.rows[row].cells[cell].content);
                format_entry_origin(out,
                                    node.rows[row].cells[cell].origin);
              }
              out << ']';
              format_entry_origin(out, node.rows[row].origin);
            }
            out << ']';
          } else if constexpr (std::is_same_v<T, PreformattedBlockIR>) {
            out << " preformatted lines=[";
            for (std::size_t line = 0; line < node.lines.size(); ++line) {
              if (line != 0) out << ' ';
              out << quoted(node.lines[line]);
            }
            out << ']';
          } else if constexpr (std::is_same_v<T, NoteBlockIR>) {
            out << " note label=";
            format_inlines(out, node.label);
            out << " content=";
            format_inlines(out, node.content);
          } else if constexpr (std::is_same_v<T, PublicationListBlockIR>) {
            out << " publication_list entries=[";
            for (std::size_t entry = 0; entry < node.entries.size(); ++entry) {
              if (entry != 0) out << ' ';
              out << "title=";
              format_inlines(out, node.entries[entry].title);
              out << " paragraphs=[";
              for (std::size_t paragraph = 0;
                   paragraph < node.entries[entry].paragraphs.size();
                   ++paragraph) {
                if (paragraph != 0) out << ' ';
                format_inlines(out,
                               node.entries[entry].paragraphs[paragraph]);
              }
              out << ']';
              format_entry_origin(out, node.entries[entry].origin);
            }
            out << ']';
          } else if constexpr (std::is_same_v<T, FigureBlockIR>) {
            out << " figure resource=" << quoted(node.resource);
            out << " caption=";
            format_inlines(out, node.caption);
          } else if constexpr (std::is_same_v<T, FootnoteBlockIR>) {
            out << " footnote id=" << quoted(node.id);
            out << " content=";
            format_inlines(out, node.content);
          } else if constexpr (std::is_same_v<T, IndexGroupBlockIR>) {
            out << " index_group heading=";
            format_inlines(out, node.heading);
            out << " entries=[";
            for (std::size_t entry = 0; entry < node.entries.size(); ++entry) {
              if (entry != 0) out << ' ';
              out << "term=";
              format_inlines(out, node.entries[entry].term);
              out << " target=" << quoted(node.entries[entry].target);
              format_entry_origin(out, node.entries[entry].origin);
            }
            out << ']';
          } else if constexpr (std::is_same_v<T, MenuBlockIR>) {
            out << " menu items=[";
            for (std::size_t item = 0; item < node.items.size(); ++item) {
              if (item != 0) out << ' ';
              out << "target=" << quoted(node.items[item].target.value)
                  << " label=" << quoted(node.items[item].label);
              format_entry_origin(out, node.items[item].origin);
            }
            out << ']';
          } else if constexpr (std::is_same_v<T, OpaqueBlockIR>) {
            out << " opaque kind=" << quoted(node.kind)
                << " content=" << quoted(node.content);
          } else {
            out << " legacy_gml scope=whole_topic records="
                << node.normalized_records.size();
            for (const auto& record : node.normalized_records)
              out << " record=" << quoted(record);
          }
        },
        block.node);
    format_origin(out, block.origin);
    out << '\n';
  }
  return out.str();
}

} // namespace geist::detail
