// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/ir/selector_link_ir.hpp"

#include <algorithm>
#include <cctype>
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

std::string ascii_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

bool valid_cross_reference_target(const CrossReferenceTargetIR& target) {
  const auto no_derived_fields = [&] {
    return target.document_number.empty() && target.document_level.empty() &&
           target.heading_anchor.empty() && target.spot_anchor.empty() &&
           target.url.empty();
  };
  if (target.value.empty()) return false;
  if (target.alternatives.empty()) {
    switch (target.kind) {
    case CrossReferenceTargetKindIR::topic:
    case CrossReferenceTargetKindIR::anchor:
    case CrossReferenceTargetKindIR::resource:
    case CrossReferenceTargetKindIR::external:
      return no_derived_fields();
    case CrossReferenceTargetKindIR::book_contents:
    case CrossReferenceTargetKindIR::book_heading:
    case CrossReferenceTargetKindIR::book_spot:
      return false;
    }
    return false;
  }
  if (target.alternatives.size() != 6 && target.alternatives.size() != 7)
    return false;
  const auto& tuple = target.alternatives;
  if (target.value != tuple[5]) return false;
  const auto kind = ascii_lower(tuple[0]);
  if (target.kind != CrossReferenceTargetKindIR::book_spot &&
      !target.spot_anchor.empty())
    return false;
  switch (target.kind) {
  case CrossReferenceTargetKindIR::book_contents:
    return kind == "book" && !tuple[3].empty() &&
           target.document_number == tuple[3] &&
           target.document_level == tuple[4] &&
           target.heading_anchor.empty() && target.url.empty();
  case CrossReferenceTargetKindIR::book_heading:
    return kind == "hdr" && !tuple[1].empty() && !tuple[3].empty() &&
           target.document_number == tuple[3] &&
           target.document_level == tuple[4] &&
           target.heading_anchor == tuple[1] && target.url.empty();
  case CrossReferenceTargetKindIR::book_spot:
    return kind == "spt" && !tuple[1].empty() && !tuple[3].empty() &&
           target.document_number == tuple[3] &&
           target.document_level == tuple[4] &&
           target.heading_anchor.empty() && target.spot_anchor == tuple[1] &&
           target.url.empty();
  case CrossReferenceTargetKindIR::external:
    return (kind == "other" || kind == "internet" ||
            (kind == "image" && !selector_link_image(tuple).empty())) &&
           !tuple[3].empty() &&
           target.document_number.empty() && target.document_level.empty() &&
           target.heading_anchor.empty() && target.url == tuple[3];
  case CrossReferenceTargetKindIR::topic:
  case CrossReferenceTargetKindIR::anchor:
  case CrossReferenceTargetKindIR::resource:
    return false;
  }
  return false;
}

// A phrase refines the presentation a run already has and never changes it:
// a sample, keyboard or definition phrase is code, a citation or variable
// plain emphasis.
bool phrase_fits(PhraseKindIR phrase, bool code, EmphasisKindIR kind) {
  switch (phrase) {
  case PhraseKindIR::none: return true;
  case PhraseKindIR::sample:
  case PhraseKindIR::keyboard:
  case PhraseKindIR::definition: return code;
  case PhraseKindIR::citation:
  case PhraseKindIR::variable:
    return !code && kind == EmphasisKindIR::emphasis;
  }
  return false;
}

// `preformatted_row`: the inlines are one styled preformatted row, the only
// place an image may occupy cells of its own (`ImageInlineIR::cells`).
bool verify_inlines(const InlineSequenceIR& inlines, bool allow_empty,
                    std::string* error, bool preformatted_row = false) {
  if (!allow_empty && inlines.empty())
    return fail(error, "required inline sequence is empty");
  for (const auto& in : inlines) {
    if (!verify_origin(in.origin, error)) return false;
    if (const auto* reference = std::get_if<CrossReferenceInlineIR>(&in.node))
      for (const auto& claim : reference->targets)
        if (claim.control_origin &&
            !verify_origin(*claim.control_origin, error))
          return false;
    const auto valid = std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, TextInlineIR>) {
            return !node.text.empty();
          } else if constexpr (std::is_same_v<T, EmphasisInlineIR>) {
            return !node.text.empty() &&
                   (node.kind == EmphasisKindIR::emphasis ||
                    node.kind == EmphasisKindIR::strong ||
                    node.kind == EmphasisKindIR::strong_emphasis) &&
                   phrase_fits(node.phrase, false, node.kind);
          } else if constexpr (std::is_same_v<T, UnderlineInlineIR>) {
            return !node.text.empty();
          } else if constexpr (std::is_same_v<T, CodeInlineIR>) {
            return !node.code.empty() &&
                   phrase_fits(node.phrase, true, EmphasisKindIR::emphasis);
          } else if constexpr (std::is_same_v<T, CrossReferenceInlineIR>) {
            if (node.targets.empty() ||
                (node.label_emphasis && node.label_code) ||
                (node.label_phrase != PhraseKindIR::none &&
                 !node.label_code && !node.label_emphasis) ||
                !phrase_fits(node.label_phrase, node.label_code,
                             node.label_emphasis.value_or(
                                 EmphasisKindIR::emphasis)) ||
                (node.label_emphasis &&
                 *node.label_emphasis != EmphasisKindIR::emphasis &&
                 *node.label_emphasis != EmphasisKindIR::strong &&
                 *node.label_emphasis != EmphasisKindIR::strong_emphasis) ||
                (node.label_image &&
                 (node.targets.size() != 1 ||
                  node.label_image->resource.empty() ||
                  node.label_image->resource !=
                      selector_link_image(
                          node.targets.front().target.alternatives))))
              return false;
            return std::all_of(node.targets.begin(), node.targets.end(),
                               [](const auto& claim) {
              return valid_cross_reference_target(claim.target);
            });
          } else if constexpr (std::is_same_v<T, ImageInlineIR>) {
            return !node.resource.empty() &&
                   node.cells.empty() != preformatted_row &&
                   node.cells.find_first_of("\r\n") == std::string::npos;
          } else if constexpr (std::is_same_v<T, OpaqueInlineIR>) {
            return !node.kind.empty();
          } else if constexpr (std::is_same_v<T, RevisedInlineIR>) {
            // One level: a revised run holds ordinary inlines only.
            return valid_revision_mark(node.mark) && !node.children.empty() &&
                   std::none_of(node.children.begin(), node.children.end(),
                                [](const InlineIR& child) {
                                  return std::holds_alternative<
                                      RevisedInlineIR>(child.node);
                                });
          } else {
            return true;
          }
        },
        in.node);
    if (!valid) return fail(error, "inline node is not canonical");
    if (const auto* revised = std::get_if<RevisedInlineIR>(&in.node))
      if (!verify_inlines(revised->children, false, error)) return false;
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
  for (const auto& in : inlines) {
    out.push_back(&in.origin);
    if (const auto* reference = std::get_if<CrossReferenceInlineIR>(&in.node))
      for (const auto& claim : reference->targets)
        if (claim.control_origin) out.push_back(&*claim.control_origin);
    if (const auto* revised = std::get_if<RevisedInlineIR>(&in.node))
      collect_inline_origins(revised->children, out);
  }
}

void collect_block_origins(const BlockSequenceIR& blocks,
                           std::vector<const DocumentNodeOriginIR*>& out) {
  for (const auto& block : blocks) out.push_back(&block.origin);
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
            collect_inline_origins(item.label, out);
            collect_inline_origins(item.content, out);
            collect_block_origins(item.blocks, out);
          }
        } else if constexpr (std::is_same_v<T, DefinitionListBlockIR>) {
          for (const auto& entry : block.entries) {
            out.push_back(&entry.origin);
            collect_inline_origins(entry.term, out);
            collect_inline_origins(entry.definition, out);
            collect_block_origins(entry.definition_blocks, out);
          }
        } else if constexpr (std::is_same_v<T, TableBlockIR>) {
          for (const auto& row : block.rows) {
            out.push_back(&row.origin);
            for (const auto& cell : row.cells) {
              out.push_back(&cell.origin);
              collect_inline_origins(cell.content, out);
              collect_block_origins(cell.blocks, out);
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
          for (const auto& anchor : block.line_anchors)
            out.push_back(&anchor.origin);
          for (const auto& line : block.styled_lines)
            collect_inline_origins(line, out);
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

bool verify_block(const BlockIR& block, bool nested, std::string* error) {
  if (!verify_origin(block.origin, error)) return false;
  if (!verify_block_slices_cover_children(block, error)) return false;
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, HeadingBlockIR>) {
          if (nested) return fail(error, "nested heading is not allowed");
          if (node.level == 0 || node.level > 6)
            return fail(error, "heading level is outside 1..6");
          if (node.revision_mark && !valid_revision_mark(*node.revision_mark))
            return fail(error, "heading revision mark is not a drawn glyph");
          return verify_inlines(node.content, false, error);
        } else if constexpr (std::is_same_v<T, ParagraphBlockIR>) {
          return verify_inlines(node.content, false, error);
        } else if constexpr (std::is_same_v<T, AnchorBlockIR>) {
          return !node.id.empty() || fail(error, "anchor id is empty");
        } else if constexpr (std::is_same_v<T, ThematicBreakBlockIR>) {
          return true;
        } else if constexpr (std::is_same_v<T, ListBlockIR>) {
          if (node.items.empty()) return fail(error, "list is empty");
          if (!node.ordered && node.marker != OrderedListMarkerIR::decimal)
            return fail(error, "unordered list has an ordered marker style");
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
            // An item may carry no inlines only where it says the source wrote
            // it empty; declaring that and then carrying content is a lowering
            // bug, not a source shape.
            if (!item.content.empty() && !item.blocks.empty())
              return fail(error, "list item has both inline and block content");
            if (item.empty_content &&
                (!item.content.empty() || !item.blocks.empty()))
              return fail(error, "empty list item carries content");
            if (!verify_origin(item.origin, error)) return false;
            if (item.revision_mark && !valid_revision_mark(*item.revision_mark))
              return fail(error, "list item revision mark is not a drawn glyph");
            // A label stands in front of the item's own text, so an item
            // that declares itself empty cannot carry one.
            if (!item.label.empty() &&
                (item.empty_content ||
                 !verify_inlines(item.label, false, error)))
              return fail(error, "list item label is invalid");
            if (item.blocks.empty()) {
              if (!verify_inlines(item.content, item.empty_content, error))
                return false;
            } else {
              for (const auto& child : item.blocks)
                if (!verify_block(child, true, error)) return false;
            }
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
              if (node.marker != OrderedListMarkerIR::decimal &&
                  *item.source_ordinal > 26)
                return fail(error, "alphabetic list ordinal exceeds Z");
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, DefinitionListBlockIR>) {
          if (node.entries.empty())
            return fail(error, "definition list is empty");
          for (const auto& entry : node.entries) {
            if (entry.empty_term != entry.term.empty())
              return fail(error,
                          "definition empty-term disposition disagrees with "
                          "its content");
            if (entry.empty_term && entry.definition_blocks.empty())
              return fail(error,
                          "empty definition term has no compound body");
            if (!entry.definition.empty() &&
                !entry.definition_blocks.empty())
              return fail(
                  error,
                  "definition has both inline and block content");
            if (!verify_origin(entry.origin, error) ||
                !verify_inlines(entry.term, entry.empty_term, error))
              return false;
            if (entry.definition_blocks.empty()) {
              // A term alone (a CZ `PT`/`DT` whose body never comes) has no
              // definition; an empty term always has one.
              if (!verify_inlines(entry.definition, !entry.empty_term, error))
                return false;
            } else {
              for (const auto& child : entry.definition_blocks)
                if (!verify_block(child, true, error)) return false;
            }
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
            if (row.revision_mark && !valid_revision_mark(*row.revision_mark))
              return fail(error, "table row revision mark is not a drawn glyph");
            for (const auto& cell : row.cells) {
              if (!cell.content.empty() && !cell.blocks.empty())
                return fail(error,
                            "table cell has both inline and block content");
              if (!verify_origin(cell.origin, error)) return false;
              if (cell.blocks.empty()) {
                if (!verify_inlines(cell.content, true, error)) return false;
              } else {
                for (const auto& child : cell.blocks)
                  if (!verify_block(child, true, error)) return false;
              }
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
          if (!node.line_links.empty() &&
              node.line_links.size() != node.lines.size())
            return fail(error,
                        "preformatted line links do not match its lines");
          for (const auto& line : node.line_origins)
            if (!verify_origin(line, error)) return false;
          for (std::size_t line = 0; line < node.line_links.size(); ++line) {
            std::size_t previous_end = 0;
            for (const auto& link : node.line_links[line]) {
              if (link.begin >= link.end || link.end > node.lines[line].size())
                return fail(error,
                            "preformatted link range is outside its line");
              if (link.begin < previous_end)
                return fail(error,
                            "preformatted link ranges overlap or are unordered");
              previous_end = link.end;
              switch (link.kind) {
              case VerbatimLinkKindIR::in_book:
                if (link.target.empty())
                  return fail(error, "in-book preformatted link has no target");
                break;
              case VerbatimLinkKindIR::book_contents:
                if (link.document_number.empty() || link.target.empty())
                  return fail(error,
                              "cross-book contents link is incomplete");
                break;
              case VerbatimLinkKindIR::book_heading:
                if (link.document_number.empty() || link.target.empty() ||
                    link.heading_anchor.empty())
                  return fail(error, "cross-book heading link is incomplete");
                break;
              case VerbatimLinkKindIR::external_url:
                if (link.url.empty())
                  return fail(error, "external preformatted link has no URL");
                break;
              }
            }
          }
          std::size_t previous_line = 0;
          bool first_anchor = true;
          for (const auto& anchor : node.line_anchors) {
            if (anchor.line >= node.lines.size())
              return fail(error,
                          "preformatted anchor line is outside its block");
            if (!first_anchor && anchor.line < previous_line)
              return fail(error,
                          "preformatted anchors are not ordered by line");
            if (anchor.anchor.id.empty())
              return fail(error, "preformatted anchor has no id");
            if (!verify_origin(anchor.origin, error)) return false;
            previous_line = anchor.line;
            first_anchor = false;
          }
          if (!node.styled_lines.empty()) {
            if (node.styled_lines.size() != node.lines.size() ||
                node.line_origins.size() != node.lines.size())
              return fail(error, "styled preformatted rows lack matching lines/origins");
            if (!node.line_links.empty())
              return fail(error, "styled preformatted rows have competing link ownership");
            for (std::size_t line = 0; line < node.lines.size(); ++line) {
              if (!verify_inlines(node.styled_lines[line], true, error, true))
                return false;
              std::string flattened;
              for (const auto& in : node.styled_lines[line]) {
                const auto valid = std::visit([&](const auto& value) {
                  using Inline = std::decay_t<decltype(value)>;
                  if constexpr (std::is_same_v<Inline, TextInlineIR> ||
                                std::is_same_v<Inline, EmphasisInlineIR> ||
                                std::is_same_v<Inline, UnderlineInlineIR>)
                    flattened += value.text;
                  else if constexpr (std::is_same_v<Inline, CodeInlineIR>)
                    flattened += value.code;
                  else if constexpr (std::is_same_v<Inline, CrossReferenceInlineIR>)
                    flattened += value.label.empty()
                                     ? value.targets.front().target.value
                                     : value.label;
                  // An image occupies the cells it names: the row's own
                  // placeholder columns, so the row keeps its geometry.
                  else if constexpr (std::is_same_v<Inline, ImageInlineIR>)
                    flattened += value.cells;
                  else return false;
                  return true;
                }, in.node);
                if (!valid)
                  return fail(error, "styled preformatted inline changes row geometry");
                for (const auto& slice : in.origin.slices)
                  if (!slice_covers(node.line_origins[line].slices, slice))
                    return fail(error, "styled row origin does not cover inline source");
              }
              if (flattened != node.lines[line] ||
                  flattened.find_first_of("\r\n") != std::string::npos)
                return fail(error, "styled preformatted text differs from literal source row");
            }
          }
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
  case CrossReferenceTargetKindIR::book_contents: return "book_contents";
  case CrossReferenceTargetKindIR::book_heading: return "book_heading";
  case CrossReferenceTargetKindIR::book_spot: return "book_spot";
  }
  return "invalid";
}

const char* phrase_kind_name(PhraseKindIR value) {
  switch (value) {
  case PhraseKindIR::none: return "none";
  case PhraseKindIR::sample: return "sample";
  case PhraseKindIR::keyboard: return "keyboard";
  case PhraseKindIR::citation: return "citation";
  case PhraseKindIR::variable: return "variable";
  case PhraseKindIR::definition: return "definition";
  }
  return "invalid";
}

// A suffix only for a run that names a phrase, so every other dump keeps
// the spelling it always had.
std::string phrase_suffix(PhraseKindIR value) {
  if (value == PhraseKindIR::none) return {};
  return std::string("/") + phrase_kind_name(value);
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
        << slice.byte_end;
    if (slice.character_begin != 0 || slice.character_end != 0)
      out << " chars=" << slice.character_begin << ':' << slice.character_end;
    out << ')';
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
            out << emphasis_kind_name(node.kind) << phrase_suffix(node.phrase)
                << '=' << quoted(node.text);
          else if constexpr (std::is_same_v<T, UnderlineInlineIR>)
            out << "underline=" << quoted(node.text);
          else if constexpr (std::is_same_v<T, CodeInlineIR>)
            out << "code" << phrase_suffix(node.phrase) << '='
                << quoted(node.code);
          else if constexpr (std::is_same_v<T, CrossReferenceInlineIR>) {
            out << "xref=" << quoted(node.label);
            if (node.label_code)
              out << ":code";
            else if (node.label_emphasis)
              out << ':' << emphasis_kind_name(*node.label_emphasis);
            out << phrase_suffix(node.label_phrase);
            if (node.label_image)
              out << " image=" << quoted(node.label_image->resource) << ':'
                  << quoted(node.label_image->alt_text);
            for (const auto& claim : node.targets) {
              out << " claim={kind="
                  << cross_reference_target_kind_name(claim.target.kind)
                  << " value=" << quoted(claim.target.value);
              if (!claim.target.alternatives.empty()) {
                out << " alternatives=[";
                for (const auto& alternative : claim.target.alternatives)
                  out << quoted(alternative);
                out << ']';
              }
              if (!claim.target.document_number.empty())
                out << " document_number="
                    << quoted(claim.target.document_number);
              if (!claim.target.document_level.empty())
                out << " document_level=" << quoted(claim.target.document_level);
              if (!claim.target.heading_anchor.empty())
                out << " heading_anchor=" << quoted(claim.target.heading_anchor);
              if (!claim.target.spot_anchor.empty())
                out << " spot_anchor=" << quoted(claim.target.spot_anchor);
              if (!claim.target.url.empty())
                out << " url=" << quoted(claim.target.url);
              if (claim.control_origin) {
                out << " control_origin={";
                format_origin(out, *claim.control_origin);
                out << '}';
              }
              out << '}';
            }
          }
          else if constexpr (std::is_same_v<T, ImageInlineIR>)
          {
            out << "image=" << quoted(node.resource) << ':'
                << quoted(node.alt_text);
            if (!node.cells.empty())
              out << " cells=" << quoted(node.cells);
          }
          else if constexpr (std::is_same_v<T, HardBreakInlineIR>)
            out << "hard_break";
          else if constexpr (std::is_same_v<T, RevisedInlineIR>) {
            out << "revised=" << quoted(revision_mark_text(node.mark))
                << (node.mark == 0x2502 ? ":U+2502" : "");
            format_inlines(out, node.children);
          } else
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
          for (auto& item : node.items) {
            for (auto& child : item.blocks) normalize_block(child);
            lift_inlines(item.origin, item.label);
            lift_inlines(item.origin, item.content);
            std::vector<const DocumentNodeOriginIR*> children;
            collect_block_origins(item.blocks, children);
            lift_into(item.origin, children);
          }
        } else if constexpr (std::is_same_v<T, DefinitionListBlockIR>) {
          for (auto& entry : node.entries) {
            for (auto& child : entry.definition_blocks) normalize_block(child);
            lift_inlines(entry.origin, entry.term);
            lift_inlines(entry.origin, entry.definition);
            std::vector<const DocumentNodeOriginIR*> children;
            collect_block_origins(entry.definition_blocks, children);
            lift_into(entry.origin, children);
          }
        } else if constexpr (std::is_same_v<T, TableBlockIR>) {
          for (auto& row : node.rows) {
            for (auto& cell : row.cells) {
              for (auto& child : cell.blocks) normalize_block(child);
              lift_inlines(cell.origin, cell.content);
              std::vector<const DocumentNodeOriginIR*> children;
              collect_block_origins(cell.blocks, children);
              lift_into(cell.origin, children);
            }
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
        } else if constexpr (std::is_same_v<T, PreformattedBlockIR>) {
          for (std::size_t line = 0; line < node.styled_lines.size() &&
                                     line < node.line_origins.size(); ++line)
            lift_inlines(node.line_origins[line], node.styled_lines[line]);
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
  // The source may explicitly publish an empty title (SC09-138 FRONT). The
  // stable topic id still supplies complete identity; fabricating a title is
  // less faithful than retaining the empty field.
  if (document.topic.id.empty())
    return fail(error, "document topic identity is incomplete");
  if (document.topic.end_logical_record != 0 &&
      document.topic.start_logical_record > document.topic.end_logical_record)
    return fail(error, "topic logical-record range is reversed");
  if (document.blocks.empty()) return fail(error, "document has no blocks");
  for (const auto& block : document.blocks)
    if (!verify_block(block, false, error))
      return false;
  return true;
}

std::string format_document_ir(const DocumentIR& document) {
  std::ostringstream out;
  const auto format_blocks = [&](const BlockSequenceIR& blocks) {
    out << '[';
    for (std::size_t index = 0; index < blocks.size(); ++index) {
      if (index != 0) out << ' ';
      DocumentIR nested;
      nested.topic.id = "<nested>";
      nested.blocks.push_back(blocks[index]);
      out << quoted(format_document_ir(nested));
    }
    out << ']';
  };
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
            // Printed only when present, so every document without a mark
            // formats exactly as it did before.
            if (node.revision_mark)
              out << "revision_mark=" << quoted(revision_mark_text(
                                             *node.revision_mark))
                  << ' ';
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
          } else if constexpr (std::is_same_v<T, ThematicBreakBlockIR>) {
            out << " thematic_break";
          } else if constexpr (std::is_same_v<T, ListBlockIR>) {
            out << " list ordered=" << (node.ordered ? 1 : 0);
            if (node.marker != OrderedListMarkerIR::decimal)
              out << " marker="
                  << (node.marker == OrderedListMarkerIR::lower_alpha
                          ? "lower-alpha"
                          : "upper-alpha");
            out << " items=[";
            for (std::size_t item = 0; item < node.items.size(); ++item) {
              if (item != 0) out << ' ';
              out << "item=";
              if (node.items[item].depth != 0)
                out << "depth=" << node.items[item].depth << ' ';
              if (node.items[item].source_ordinal)
                out << "ordinal=" << *node.items[item].source_ordinal << ' ';
              if (node.items[item].revision_mark)
                out << "revision_mark="
                    << quoted(revision_mark_text(*node.items[item].revision_mark))
                    << ' ';
              format_inlines(out, node.items[item].content);
              if (!node.items[item].blocks.empty()) {
                out << " blocks=";
                format_blocks(node.items[item].blocks);
              }
              format_entry_origin(out, node.items[item].origin);
            }
            out << ']';
          } else if constexpr (std::is_same_v<T, DefinitionListBlockIR>) {
            out << " definition_list entries=[";
            for (std::size_t entry = 0; entry < node.entries.size(); ++entry) {
              if (entry != 0) out << ' ';
              out << "term=";
              format_inlines(out, node.entries[entry].term);
              if (node.entries[entry].empty_term) out << " empty_term=1";
              out << " definition=";
              format_inlines(out, node.entries[entry].definition);
              if (!node.entries[entry].definition_blocks.empty()) {
                out << " blocks=";
                format_blocks(node.entries[entry].definition_blocks);
              }
              format_entry_origin(out, node.entries[entry].origin);
            }
            out << ']';
          } else if constexpr (std::is_same_v<T, TableBlockIR>) {
            out << " table header_rows=" << node.header_rows << " rows=[";
            for (std::size_t row = 0; row < node.rows.size(); ++row) {
              if (row != 0) out << ' ';
              out << "row=";
              if (node.rows[row].revision_mark)
                out << "revision_mark="
                    << quoted(revision_mark_text(*node.rows[row].revision_mark));
              out << '[';
              for (std::size_t cell = 0; cell < node.rows[row].cells.size();
                   ++cell) {
                if (cell != 0) out << ' ';
                out << "cell=";
                format_inlines(out, node.rows[row].cells[cell].content);
                if (!node.rows[row].cells[cell].blocks.empty()) {
                  out << " blocks=";
                  format_blocks(node.rows[row].cells[cell].blocks);
                }
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
              if (line < node.styled_lines.size()) {
                out << " styled=";
                format_inlines(out, node.styled_lines[line]);
              }
              for (const auto& anchor : node.line_anchors)
                if (anchor.line == line) {
                  out << " anchor='" << anchor.anchor.id << "'";
                  format_entry_origin(out, anchor.origin);
                }
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

bool has_revised_inline(const InlineSequenceIR& inlines) {
  return std::any_of(inlines.begin(), inlines.end(), [](const InlineIR& in) {
    return std::holds_alternative<RevisedInlineIR>(in.node);
  });
}

std::string blank_image_cells(const ImageInlineIR& image) {
  std::size_t columns = 0;
  for (const auto ch : image.cells)
    if ((static_cast<unsigned char>(ch) & 0xc0) != 0x80) ++columns;
  return std::string(columns, ' ');
}

InlineSequenceIR flatten_revised_inlines(const InlineSequenceIR& inlines) {
  InlineSequenceIR flat;
  for (const auto& in : inlines) {
    if (const auto* revised = std::get_if<RevisedInlineIR>(&in.node))
      flat.insert(flat.end(), revised->children.begin(),
                  revised->children.end());
    else
      flat.push_back(in);
  }
  return flat;
}

bool valid_revision_mark(const char32_t mark) {
  return mark > U' ' && mark != 0x7f && mark != 0xffff && mark <= 0x10ffff &&
         !(mark >= 0x80 && mark < 0xa0);
}

std::string revision_mark_text(const char32_t mark) {
  if (mark == 0x2502) return "|";
  std::string out;
  if (mark < 0x80) {
    out.push_back(static_cast<char>(mark));
  } else if (mark < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (mark >> 6)));
    out.push_back(static_cast<char>(0x80 | (mark & 0x3F)));
  } else if (mark < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (mark >> 12)));
    out.push_back(static_cast<char>(0x80 | ((mark >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (mark & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (mark >> 18)));
    out.push_back(static_cast<char>(0x80 | ((mark >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((mark >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (mark & 0x3F)));
  }
  return out;
}

} // namespace geist::detail
