#include "geist/detail/prose_topic_document_lowering.hpp"

#include "geist/detail/figure_document_lowering.hpp"
#include "geist/detail/fixed_table_document_lowering.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

DocumentNodeOriginIR origin(std::vector<DocumentSourceSliceIR> slices,
                            std::string detail) {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::semantic_lowering;
  result.slices = std::move(slices);
  result.detail = std::move(detail);
  return result;
}

EmphasisKindIR emphasis_kind(FontStyleIR style) {
  switch (style) {
  case FontStyleIR::highlight_2:
  case FontStyleIR::bold_phrase: return EmphasisKindIR::strong;
  case FontStyleIR::highlight_3: return EmphasisKindIR::strong_emphasis;
  case FontStyleIR::highlight_1:
  case FontStyleIR::citation:
  case FontStyleIR::variable:
  case FontStyleIR::italic_phrase:
  case FontStyleIR::example_phrase:
  case FontStyleIR::keyword:
  case FontStyleIR::unknown: break;
  }
  return EmphasisKindIR::emphasis;
}

bool code_style(FontStyleIR style) {
  return style == FontStyleIR::example_phrase || style == FontStyleIR::keyword;
}

// The one selector whose payload holds every source cell of a table cell
// line and whose column range (line-relative, origin cell == 0, like the
// block's border and separator columns) meets the cell's column range, or
// nullptr.  Positioned display columns are not used: the Layout IR can
// split a table line into several physical rows, each with its own column
// base (SC31-711 4.0 row 1).
const ProseTableLinkIR* line_link(const ProseTopicIR& prose, std::size_t span,
                                  const FixedTableBlockIR& block,
                                  std::size_t cell_index,
                                  const FixedTableCellLineIR& line) {
  const auto cell_begin = (cell_index == 0
                               ? block.left_column
                               : block.separator_columns[cell_index - 1]) + 1;
  const auto cell_end = cell_index < block.separator_columns.size()
                            ? block.separator_columns[cell_index]
                            : block.left_column + block.width;
  const ProseTableLinkIR* result = nullptr;
  for (const auto& link : prose.table_links) {
    if (link.span != span) continue;
    if (link.column >= cell_end || link.column + link.length <= cell_begin)
      continue;
    bool covers = !line.source_cells.empty() || !line.unpositioned_cells.empty();
    const auto owned = [&](std::uint32_t record, std::size_t token) {
      return record == link.logical_record &&
             std::binary_search(link.payload_tokens.begin(),
                                link.payload_tokens.end(), token);
    };
    for (const auto& cell : line.source_cells)
      covers = covers && owned(cell.logical_record, cell.token_index);
    for (const auto& cell : line.unpositioned_cells)
      covers = covers && owned(cell.logical_record, cell.token_index);
    if (!covers) continue;
    if (result != nullptr) return nullptr;  // two selectors: ambiguous
    result = &link;
  }
  return result;
}

// Replaces the text of every fully selector-covered cell line of the lowered
// table rows by a cross reference to the selector target.  Rows correspond
// one to one to the block's body rows; the caption is a paragraph and is
// left as text.
bool link_table_cells(const ProseTopicIR& prose, std::size_t span,
                      const FixedTableBlockIR& block,
                      std::vector<BlockIR>& lowered, std::string* error) {
  const auto table = std::find_if(lowered.begin(), lowered.end(),
                                  [](const auto& candidate) {
                                    return std::holds_alternative<TableBlockIR>(
                                        candidate.node);
                                  });
  if (table == lowered.end()) return fail(error, "table span has no table");
  auto& rows = std::get<TableBlockIR>(table->node).rows;
  if (rows.size() != block.body.size())
    return fail(error, "lowered table rows differ from the block rows");
  for (std::size_t row = 0; row < rows.size(); ++row) {
    if (rows[row].cells.size() != block.body[row].cells.size())
      return fail(error, "lowered table cells differ from the block cells");
    for (std::size_t cell = 0; cell < rows[row].cells.size(); ++cell) {
      auto& content = rows[row].cells[cell].content;
      const auto& lines = block.body[row].cells[cell].lines;
      // Lines alternate with hard breaks: text, break, text, ...
      if (content.size() != (lines.empty() ? 0 : lines.size() * 2 - 1))
        return fail(error, "lowered table cell lines differ from the block");
      for (std::size_t line = 0; line < lines.size(); ++line) {
        auto& node = content[line * 2];
        const auto* text = std::get_if<TextInlineIR>(&node.node);
        if (text == nullptr)
          return fail(error, "lowered table cell line is not text");
        const auto* link = line_link(prose, span, block, cell, lines[line]);
        if (link == nullptr) continue;
        CrossReferenceInlineIR reference;
        reference.target = {CrossReferenceTargetKindIR::anchor, link->target};
        reference.label = text->text;
        node.node = std::move(reference);
        node.origin.detail = "fixed table cell line (CSELECT reference)";
        node.origin.slices.push_back(link->source);
        std::sort(node.origin.slices.begin(), node.origin.slices.end(),
                  [](const auto& left, const auto& right) {
                    return std::make_tuple(left.logical_record,
                                           left.segment_index, left.token_begin,
                                           left.token_end) <
                           std::make_tuple(right.logical_record,
                                           right.segment_index,
                                           right.token_begin, right.token_end);
                  });
      }
    }
  }
  return true;
}

bool lower_inlines(const ProseBlockIR& block, InlineSequenceIR& content,
                   std::string* error) {
  for (const auto& node : block.inlines) {
    InlineIR lowered;
    switch (node.kind) {
    case ProseInlineKindIR::text:
      lowered.node = TextInlineIR{node.text};
      lowered.origin = origin(node.slices, "prose text");
      break;
    case ProseInlineKindIR::emphasis:
      if (node.style == FontStyleIR::unknown)
        return fail(error, "prose emphasis has no highlight style");
      if (code_style(node.style)) {
        lowered.node = CodeInlineIR{node.text};
        lowered.origin = origin(node.slices, "prose CFONT example phrase");
      } else {
        lowered.node = EmphasisInlineIR{node.text, emphasis_kind(node.style)};
        lowered.origin = origin(node.slices, "prose CFONT highlight");
      }
      break;
    case ProseInlineKindIR::cross_reference: {
      if (node.target.empty())
        return fail(error, "prose cross-reference has no target");
      CrossReferenceInlineIR reference;
      reference.target = {CrossReferenceTargetKindIR::anchor, node.target};
      reference.label = node.text;
      lowered.node = std::move(reference);
      lowered.origin = origin(node.slices, "prose CSELECT reference");
      break;
    }
    }
    if (node.text.empty()) return fail(error, "prose inline has no text");
    content.push_back(std::move(lowered));
  }
  return !content.empty() || fail(error, "prose block has no inlines");
}

} // namespace

std::optional<DocumentIR> lower_prose_topic_to_document_ir(
    TopicIdentityIR identity, const ProseTopicIR& prose, std::string* error) {
  if (prose.heading_level.size() != 2 || prose.heading_level.front() != 'h' ||
      prose.heading_level.back() < '1' || prose.heading_level.back() > '6' ||
      prose.title.empty()) {
    fail(error, "prose topic heading is incomplete");
    return std::nullopt;
  }
  // The source-proven CHDLEVEL is authoritative over compatibility metadata.
  identity.heading_level = prose.heading_level;

  DocumentIR document;
  document.topic = std::move(identity);
  const auto level =
      static_cast<std::uint32_t>(prose.heading_level.back() - '0');
  auto heading_origin = origin({prose.title_source}, "prose heading");
  InlineIR heading_text{TextInlineIR{prose.title}, heading_origin};
  document.blocks.push_back(
      {HeadingBlockIR{level, {std::move(heading_text)}}, heading_origin});

  // Table and figure spans lower through their own typed blocks; the prose
  // family only places them.
  std::size_t emitted_spans = 0;
  std::string span_error;
  const auto emit_span = [&](const ProseSpanIR& span) -> bool {
    if (span.kind == ProseSpanKindIR::table) {
      if (span.index >= prose.tables.blocks.size())
        return fail(&span_error, "table span addresses no table block");
      const auto& table = prose.tables.blocks[span.index];
      auto blocks = lower_fixed_table_block_to_document_ir(table);
      if (blocks.empty()) return fail(&span_error, "table span lowered to nothing");
      const auto span_index =
          static_cast<std::size_t>(&span - prose.spans.data());
      if (!link_table_cells(prose, span_index, table, blocks, &span_error))
        return false;
      for (auto& block : blocks) document.blocks.push_back(std::move(block));
    } else {
      if (span.index >= prose.figures.blocks.size())
        return fail(&span_error, "figure span addresses no figure block");
      auto blocks = lower_figure_block_to_document_blocks(
          prose.figures.blocks[span.index], &span_error);
      if (!blocks) return false;
      for (auto& block : *blocks) document.blocks.push_back(std::move(block));
    }
    ++emitted_spans;
    return true;
  };
  // Anchors and spans placed at one position keep their source order: a span
  // follows the first `anchors_before` anchors of that position.
  const auto emit_anchors = [&](std::size_t position,
                                bool after_menu = false) -> bool {
    std::size_t ordinal = 0;
    const auto flush_spans = [&]() -> bool {
      for (const auto& span : prose.spans)
        if (span.position == position && span.anchors_before == ordinal &&
            !emit_span(span))
          return false;
      return true;
    };
    if (!after_menu && !flush_spans()) return false;
    for (const auto& anchor : prose.anchors) {
      if (anchor.position != position || anchor.after_menu != after_menu)
        continue;
      document.blocks.push_back(
          {AnchorBlockIR{anchor.id},
           origin({anchor.source}, "prose source anchor")});
      if (after_menu) continue;
      ++ordinal;
      if (!flush_spans()) return false;
    }
    return true;
  };

  std::size_t index = 0;
  while (index < prose.blocks.size()) {
    if (!emit_anchors(index)) {
      fail(error, "prose span rejected: " + span_error);
      return std::nullopt;
    }
    const auto& block = prose.blocks[index];
    if (block.kind == ProseBlockKindIR::paragraph) {
      ParagraphBlockIR paragraph;
      if (!lower_inlines(block, paragraph.content, error)) return std::nullopt;
      document.blocks.push_back(
          {std::move(paragraph), origin(block.slices, "prose paragraph")});
      ++index;
      continue;
    }
    ListBlockIR list;
    std::vector<DocumentSourceSliceIR> list_slices;
    const auto ordinal = block.list_ordinal;
    while (index < prose.blocks.size() &&
           prose.blocks[index].kind == ProseBlockKindIR::list_item &&
           prose.blocks[index].list_ordinal == ordinal) {
      const auto& item = prose.blocks[index];
      ListItemIR lowered;
      if (!lower_inlines(item, lowered.content, error)) return std::nullopt;
      lowered.origin = origin(item.slices, "prose list item");
      list_slices.insert(list_slices.end(), item.slices.begin(),
                         item.slices.end());
      list.items.push_back(std::move(lowered));
      ++index;
      // An anchor inside a list splits it.
      if (std::any_of(prose.anchors.begin(), prose.anchors.end(),
                      [&](const auto& anchor) {
                        return anchor.position == index;
                      }))
        break;
    }
    document.blocks.push_back(
        {std::move(list), origin(std::move(list_slices), "prose list")});
  }
  if (!emit_anchors(prose.blocks.size())) {
    fail(error, "prose span rejected: " + span_error);
    return std::nullopt;
  }
  if (emitted_spans != prose.spans.size()) {
    fail(error, "prose span was never placed");
    return std::nullopt;
  }

  if (!prose.menu_items.empty()) {
    MenuBlockIR menu;
    std::vector<DocumentSourceSliceIR> menu_slices;
    for (const auto& item : prose.menu_items) {
      if (item.target.empty() || item.label.empty()) {
        fail(error, "prose menu item is incomplete");
        return std::nullopt;
      }
      menu.items.push_back(
          {{CrossReferenceTargetKindIR::topic, item.target},
           item.label,
           origin({item.source}, "prose trailing menu item")});
      menu_slices.push_back(item.source);
    }
    document.blocks.push_back(
        {std::move(menu), origin(std::move(menu_slices), "prose trailing menu")});
  }
  emit_anchors(prose.blocks.size(), true);

  std::string document_error;
  if (!verify_document_ir(document, &document_error)) {
    fail(error, "invalid prose DocumentIR: " + document_error);
    return std::nullopt;
  }
  if (error != nullptr) error->clear();
  return document;
}

bool verify_prose_topic_document_ir(const ProseTopicIR& prose,
                                    const DocumentIR& document,
                                    std::string* error) {
  auto expected =
      lower_prose_topic_to_document_ir(document.topic, prose, error);
  if (!expected) return false;
  if (format_document_ir(document) != format_document_ir(*expected))
    return fail(error, "prose DocumentIR differs from canonical lowering");
  if (error != nullptr) error->clear();
  return true;
}

} // namespace geist::detail
