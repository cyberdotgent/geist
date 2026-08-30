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
  case DocumentDerivationIR::synthesized: break;
  default: return fail(error, "node origin has invalid derivation");
  }
  // `decoded` is the default derivation, so a node that never named its
  // source reaches verification claiming to be decoded from bytes it cannot
  // point at.  A node that really has no source must say so through
  // `synthesized` instead of leaving an unpopulated origin behind.
  if (origin.derivation == DocumentDerivationIR::decoded && origin.slices.empty())
    return fail(error, "decoded node origin names no source slice");
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

// True when one of `outer` covers `inner` whole: same logical record and
// control segment, and a token range that contains the inner one.
bool slice_covers(const std::vector<DocumentSourceSliceIR>& outer,
                  const DocumentSourceSliceIR& inner) {
  for (const auto& candidate : outer)
    if (candidate.logical_record == inner.logical_record &&
        candidate.segment_index == inner.segment_index &&
        candidate.token_begin <= inner.token_begin &&
        inner.token_end <= candidate.token_end)
      return true;
  return false;
}

void collect_inline_origins(const InlineSequenceIR& inlines,
                            std::vector<const DocumentNodeOriginIR*>& out) {
  for (const auto& in : inlines) out.push_back(&in.origin);
}

// Every origin below one block, so a block's own slices can be checked
// against the source its children actually claim.
void collect_child_origins(const BlockNodeIR& node,
                           std::vector<const DocumentNodeOriginIR*>& out) {
  std::visit(
      [&](const auto& block) {
        using T = std::decay_t<decltype(block)>;
        if constexpr (std::is_same_v<T, HeadingBlockIR> ||
                      std::is_same_v<T, ParagraphBlockIR> ||
                      std::is_same_v<T, FootnoteBlockIR>) {
          collect_inline_origins(block.content, out);
        } else if constexpr (std::is_same_v<T, ListBlockIR>) {
          for (const auto& item : block.items) {
            out.push_back(&item.origin);
            collect_inline_origins(item.content, out);
          }
        } else if constexpr (std::is_same_v<T, DefinitionListBlockIR>) {
          for (const auto& entry : block.entries) {
            out.push_back(&entry.origin);
            collect_inline_origins(entry.term, out);
            collect_inline_origins(entry.definition, out);
          }
        } else if constexpr (std::is_same_v<T, TableBlockIR>) {
          for (const auto& row : block.rows) {
            out.push_back(&row.origin);
            for (const auto& cell : row.cells) {
              out.push_back(&cell.origin);
              collect_inline_origins(cell.content, out);
            }
          }
        } else if constexpr (std::is_same_v<T, NoteBlockIR>) {
          collect_inline_origins(block.label, out);
          collect_inline_origins(block.content, out);
        } else if constexpr (std::is_same_v<T, PublicationListBlockIR>) {
          for (const auto& entry : block.entries) {
            out.push_back(&entry.origin);
            collect_inline_origins(entry.title, out);
            for (const auto& paragraph : entry.paragraphs)
              collect_inline_origins(paragraph, out);
          }
        } else if constexpr (std::is_same_v<T, FigureBlockIR>) {
          collect_inline_origins(block.caption, out);
        } else if constexpr (std::is_same_v<T, IndexGroupBlockIR>) {
          collect_inline_origins(block.heading, out);
          for (const auto& entry : block.entries) {
            out.push_back(&entry.origin);
            collect_inline_origins(entry.term, out);
          }
        } else if constexpr (std::is_same_v<T, MenuBlockIR>) {
          for (const auto& item : block.items) out.push_back(&item.origin);
        } else if constexpr (std::is_same_v<T, PreformattedBlockIR>) {
          for (const auto& line : block.line_origins) out.push_back(&line);
        }
      },
      node);
}

// A block that names its own source must name at least the source its
// children name.  A child slice outside the block's slices would let a
// rendered element trace to a block whose byte range does not contain it.
bool verify_block_slices_cover_children(const BlockIR& block,
                                        std::string* error) {
  if (block.origin.slices.empty()) return true;
  std::vector<const DocumentNodeOriginIR*> children;
  collect_child_origins(block.node, children);
  for (const auto* child : children) {
    // A child that states it was synthesized has no source to be covered.
    if (child->derivation == DocumentDerivationIR::synthesized) continue;
    for (const auto& slice : child->slices)
      if (!slice_covers(block.origin.slices, slice))
        return fail(error,
                    "block slices do not cover a child node's source slice");
  }
  return true;
}

bool verify_block(const BlockIR& block, std::string* error) {
  if (!verify_origin(block.origin, error)) return false;
  if (!verify_block_slices_cover_children(block, error)) return false;
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
          std::uint32_t previous_depth = 0;
          bool first_item = true;
          for (const auto& item : node.items) {
            if (!verify_origin(item.origin, error) ||
                !verify_inlines(item.content, false, error))
              return false;
            // A list may only descend one level at a time, and its first item
            // is its shallowest.
            if (first_item ? item.depth != 0 : item.depth > previous_depth + 1)
              return fail(error, "list item depth skips a level");
            previous_depth = item.depth;
            first_item = false;
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
          if (node.lines.empty())
            return fail(error, "preformatted block is empty");
          if (!node.line_origins.empty() &&
              node.line_origins.size() != node.lines.size())
            return fail(error,
                        "preformatted line origins do not match its lines");
          for (const auto& line : node.line_origins)
            if (!verify_origin(line, error)) return false;
          return true;
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
        } else {
          static_assert(std::is_same_v<T, OpaqueBlockIR>);
          return !node.kind.empty() ||
                 fail(error, "opaque block kind is empty");
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

namespace {

using SliceKey = std::tuple<std::uint32_t, std::size_t, std::size_t,
                            std::size_t, std::uint32_t, std::uint32_t>;

SliceKey slice_key(const DocumentSourceSliceIR& slice) {
  return {slice.logical_record, slice.segment_index, slice.token_begin,
          slice.token_end,      slice.byte_begin,    slice.byte_end};
}

// Orders a container's slices and drops exact duplicates.  Adjacent ranges
// are deliberately not fused: a lowering that states one slice per source
// token is naming real, separately owned tokens, and fusing them would hide
// the gaps between them.
void merge_slices(std::vector<DocumentSourceSliceIR>& slices) {
  const auto full_key = [](const DocumentSourceSliceIR& slice) {
    return std::tuple_cat(slice_key(slice),
                          std::make_tuple(slice.character_begin,
                                          slice.character_end));
  };
  std::sort(slices.begin(), slices.end(),
            [&](const auto& left, const auto& right) {
              return full_key(left) < full_key(right);
            });
  slices.erase(std::unique(slices.begin(), slices.end(),
                           [&](const auto& left, const auto& right) {
                             return full_key(left) == full_key(right);
                           }),
               slices.end());
}

// A slice lifted into a container covers whole tokens: the container owns the
// token, the child owns the characters inside it.
DocumentSourceSliceIR widened(DocumentSourceSliceIR slice) {
  slice.character_begin = 0;
  slice.character_end = 0;
  return slice;
}

void lift_into(DocumentNodeOriginIR& container,
               const std::vector<const DocumentNodeOriginIR*>& children) {
  if (container.derivation == DocumentDerivationIR::synthesized)
    return;
  const auto had_slices = !container.slices.empty();
  for (const auto* child : children) {
    if (child->derivation == DocumentDerivationIR::synthesized) continue;
    for (const auto& slice : child->slices)
      container.slices.push_back(widened(slice));
  }
  if (container.slices.empty()) return;
  merge_slices(container.slices);
  if (!had_slices && container.derivation == DocumentDerivationIR::decoded) {
    container.derivation = DocumentDerivationIR::semantic_lowering;
    if (container.detail.empty()) container.detail = "source of its content";
  }
}

void lift_inlines(DocumentNodeOriginIR& container,
                  const InlineSequenceIR& inlines) {
  std::vector<const DocumentNodeOriginIR*> children;
  collect_inline_origins(inlines, children);
  lift_into(container, children);
}

void normalize_block(BlockIR& block) {
  std::visit(
      [&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ListBlockIR>) {
          for (auto& item : node.items) lift_inlines(item.origin, item.content);
        } else if constexpr (std::is_same_v<T, DefinitionListBlockIR>) {
          for (auto& entry : node.entries) {
            lift_inlines(entry.origin, entry.term);
            lift_inlines(entry.origin, entry.definition);
          }
        } else if constexpr (std::is_same_v<T, TableBlockIR>) {
          for (auto& row : node.rows) {
            for (auto& cell : row.cells) lift_inlines(cell.origin, cell.content);
            std::vector<const DocumentNodeOriginIR*> cells;
            for (const auto& cell : row.cells) cells.push_back(&cell.origin);
            lift_into(row.origin, cells);
          }
        } else if constexpr (std::is_same_v<T, PublicationListBlockIR>) {
          for (auto& entry : node.entries) {
            lift_inlines(entry.origin, entry.title);
            for (const auto& paragraph : entry.paragraphs)
              lift_inlines(entry.origin, paragraph);
          }
        } else if constexpr (std::is_same_v<T, IndexGroupBlockIR>) {
          for (auto& entry : node.entries)
            lift_inlines(entry.origin, entry.term);
        }
      },
      block.node);
  std::vector<const DocumentNodeOriginIR*> children;
  collect_child_origins(block.node, children);
  lift_into(block.origin, children);
}

} // namespace

void normalize_document_origin_slices(DocumentIR& document) {
  for (auto& block : document.blocks) normalize_block(block);
}

bool verify_document_ir(const DocumentIR& document, std::string* error) {
  if (document.topic.id.empty() || document.topic.title.empty())
    return fail(error, "document topic identity is incomplete");
  if (document.topic.end_logical_record != 0 &&
      document.topic.start_logical_record > document.topic.end_logical_record)
    return fail(error, "topic logical-record range is reversed");
  if (document.blocks.empty()) return fail(error, "document has no blocks");
  for (const auto& block : document.blocks)
    if (!verify_block(block, error))
      return false;
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
            // Printed only when it is not the default, so every document that
            // predates anchor roles formats exactly as it did before.
            if (node.role != AnchorRoleIR::cross_reference)
              out << " role="
                  << (node.role == AnchorRoleIR::figure
                          ? "figure"
                          : node.role == AnchorRoleIR::table ? "table"
                                                             : "local");
          } else if constexpr (std::is_same_v<T, ListBlockIR>) {
            out << " list ordered=" << (node.ordered ? 1 : 0)
                << " items=[";
            for (std::size_t item = 0; item < node.items.size(); ++item) {
              if (item != 0) out << ' ';
              out << "item=";
              if (node.items[item].depth != 0)
                out << "depth=" << node.items[item].depth << ' ';
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
              if (line < node.line_origins.size())
                format_entry_origin(out, node.line_origins[line]);
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
          } else {
            static_assert(std::is_same_v<T, OpaqueBlockIR>);
            out << " opaque kind=" << quoted(node.kind)
                << " content=" << quoted(node.content);
          }
        },
        block.node);
    format_origin(out, block.origin);
    out << '\n';
  }
  return out.str();
}

} // namespace geist::detail
