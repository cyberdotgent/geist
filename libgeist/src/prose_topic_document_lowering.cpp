#include "geist/detail/prose_topic_document_lowering.hpp"

#include "geist/detail/figure_document_lowering.hpp"
#include "geist/detail/fixed_table_document_lowering.hpp"

#include <algorithm>
#include <cctype>
#include <map>
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
  case FontStyleIR::highlight_3:
  // HP8 is the underscored HP3.
  case FontStyleIR::highlight_8: return EmphasisKindIR::strong_emphasis;
  // HP7 is the underscored HP2 (hosted <B><U>); Markdown has no underscore,
  // so the bold half survives.
  case FontStyleIR::highlight_7: return EmphasisKindIR::strong;
  case FontStyleIR::highlight_1:
  // HP5 (plain underscore, hosted <U>) and HP6 (underscored HP1) have no
  // Markdown underscore run and lower to ordinary emphasis.
  case FontStyleIR::highlight_5:
  case FontStyleIR::highlight_6:
  case FontStyleIR::citation:
  case FontStyleIR::variable:
  case FontStyleIR::italic_phrase:
  case FontStyleIR::example_phrase:
  case FontStyleIR::keyword:
  case FontStyleIR::keyword_define:
  case FontStyleIR::highlight_9:
  case FontStyleIR::unknown: break;
  }
  return EmphasisKindIR::emphasis;
}

bool code_style(FontStyleIR style) {
  // HP9 is the underscored HP4, the monospace example phrase.
  return style == FontStyleIR::example_phrase ||
         style == FontStyleIR::keyword ||
         style == FontStyleIR::keyword_define ||
         style == FontStyleIR::highlight_9;
}

// The label of a definition entry or a note is rendered emphasised by the
// document renderer (`- **term:** ...`).  When the source emphasises the whole
// label as well (SC41-485 1.1 `<dt>   <B>List</B>`), keeping the CFONT node
// would double the emphasis, so a fully highlighted label lowers as plain
// text.  A code-styled or partly styled label keeps its nodes.
bool label_is_wholly_highlighted(const ProseBlockIR& block, std::size_t begin,
                                 std::size_t end) {
  if (begin >= end) return false;
  for (auto index = begin; index < end && index < block.inlines.size();
       ++index) {
    const auto& node = block.inlines[index];
    if (node.kind != ProseInlineKindIR::emphasis) return false;
    if (code_style(node.style) || node.style == FontStyleIR::unknown)
      return false;
  }
  return true;
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
        reference.target = {link->target_kind, link->target};
        reference.label = text->text;
        node.node = std::move(reference);
        node.origin.detail =
            link->target_kind == CrossReferenceTargetKindIR::external
                ? "fixed table cell line (CSELECT LNK reference)"
                : "fixed table cell line (CSELECT reference)";
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


bool lower_inlines(const ProseBlockIR& block, std::size_t begin,
                   std::size_t end, InlineSequenceIR& content,
                   std::string* error, bool flatten_emphasis = false) {
  for (auto index = begin; index < end && index < block.inlines.size();
       ++index) {
    const auto& node = block.inlines[index];
    InlineIR lowered;
    if (flatten_emphasis && node.kind == ProseInlineKindIR::emphasis) {
      lowered.node = TextInlineIR{node.text};
      lowered.origin = origin(node.slices, "prose CZ highlighted label");
      if (node.text.empty()) return fail(error, "prose inline has no text");
      content.push_back(std::move(lowered));
      continue;
    }
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
      reference.target = {node.target_kind, node.target};
      reference.label = node.text;
      lowered.node = std::move(reference);
      lowered.origin =
          origin(node.slices,
                 node.target_kind == CrossReferenceTargetKindIR::external
                     ? "prose CSELECT LNK reference"
                     : "prose CSELECT reference");
      break;
    }
    }
    if (node.text.empty()) return fail(error, "prose inline has no text");
    content.push_back(std::move(lowered));
  }
  return !content.empty() || fail(error, "prose block has no inlines");
}

bool lower_inlines(const ProseBlockIR& block, InlineSequenceIR& content,
                   std::string* error) {
  return lower_inlines(block, block.term_inline_count, block.inlines.size(),
                       content, error);
}

std::vector<DocumentSourceSliceIR> inline_slices(const ProseBlockIR& block,
                                                 std::size_t begin,
                                                 std::size_t end) {
  std::vector<DocumentSourceSliceIR> slices;
  for (auto index = begin; index < end && index < block.inlines.size(); ++index)
    slices.insert(slices.end(), block.inlines[index].slices.begin(),
                  block.inlines[index].slices.end());
  return slices;
}

// The explicit ordinal label of a CZ ordered-list row, or the item's position
// in its list when the row has no numeric label.
std::optional<std::uint64_t> item_ordinal(const ProseBlockIR& block,
                                          std::uint64_t position) {
  if (!block.ordered) return std::nullopt;
  const auto& label = block.ordinal;
  if (label.size() >= 2 && label.back() == '.' &&
      std::all_of(label.begin(), label.end() - 1, [](unsigned char ch) {
        return std::isdigit(ch) != 0;
      })) {
    std::uint64_t value = 0;
    for (auto it = label.begin(); it + 1 != label.end(); ++it)
      value = value * 10 + static_cast<std::uint64_t>(*it - '0');
    if (value != 0) return value;
  }
  return position;
}

} // namespace

std::optional<DocumentIR> lower_prose_topic_to_document_ir(
    TopicIdentityIR identity, const ProseTopicIR& prose, std::string* error) {
  if (prose.heading_level.size() != 2 || prose.heading_level.front() != 'h' ||
      prose.heading_level.back() < '1' || prose.heading_level.back() > '6') {
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
  // An `ST` control with an empty payload leaves the heading to the topic
  // identity prefix alone, which is what hosted BookServer serves
  // (`<H3> 8.1.1.1 </H3>`, SC09-138 DT 19910321130500).
  std::vector<InlineIR> heading_content;
  if (prose.title.empty()) {
    InlineIR identity_only;
    identity_only.node = TextInlineIR{document.topic.id};
    identity_only.origin.derivation = DocumentDerivationIR::synthesized;
    identity_only.origin.detail = "public topic identity prefix";
    heading_content.push_back(std::move(identity_only));
  } else {
    heading_content.push_back(
        InlineIR{TextInlineIR{prose.title}, heading_origin});
  }
  document.blocks.push_back(
      {HeadingBlockIR{level, std::move(heading_content)}, heading_origin});

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
      // A preformatted region has no cells for a CSELECT to attach to; the
      // table block declines such an envelope, so there is nothing to link.
      if (table.geometry != FixedTableGeometryIR::preformatted &&
          !link_table_cells(prose, span_index, table, blocks, &span_error))
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

  const auto anchor_at = [&](std::size_t position) {
    return std::any_of(prose.anchors.begin(), prose.anchors.end(),
                       [&](const auto& anchor) {
                         return anchor.position == position;
                       });
  };
  // A table or figure span placed between two items of one list splits the
  // list, exactly as an anchor does: the run below only visits the position
  // it stops at, so a span inside an unbroken run would never be emitted
  // (SC09-2417-00 6.2.3, where `cz OFF TABLE` stands between two
  // `cz FLOW LI` items of the same `cz FLOW UL`).
  const auto span_at = [&](std::size_t position) {
    return std::any_of(prose.spans.begin(), prose.spans.end(),
                       [&](const auto& span) {
                         return span.position == position;
                       });
  };
  // Items of one CZ list can be interleaved with paragraphs (a `cz FLOW P`
  // inside a list item); each run of items becomes one list block and the
  // ordinal keeps counting across the runs.
  std::map<std::size_t, std::uint64_t> list_positions;
  std::size_t index = 0;
  while (index < prose.blocks.size()) {
    if (!emit_anchors(index)) {
      fail(error, "prose span rejected: " + span_error);
      return std::nullopt;
    }
    const auto& block = prose.blocks[index];
    switch (block.kind) {
    case ProseBlockKindIR::paragraph: {
      ParagraphBlockIR paragraph;
      if (!lower_inlines(block, paragraph.content, error)) return std::nullopt;
      document.blocks.push_back(
          {std::move(paragraph), origin(block.slices, "prose paragraph")});
      ++index;
      continue;
    }
    case ProseBlockKindIR::heading: {
      HeadingBlockIR heading;
      heading.level = static_cast<std::uint32_t>(block.heading_level);
      if (heading.level < 2 || heading.level > 6) {
        fail(error, "prose heading block level is outside 2..6");
        return std::nullopt;
      }
      if (!lower_inlines(block, heading.content, error)) return std::nullopt;
      document.blocks.push_back(
          {std::move(heading), origin(block.slices, "prose CZ heading")});
      ++index;
      continue;
    }
    case ProseBlockKindIR::note: {
      // The label is the source word before the colon (`Note:`); the
      // renderer supplies the emphasis and the colon.
      NoteBlockIR note;
      std::string label;
      for (std::size_t inline_index = 0; inline_index < block.term_inline_count;
           ++inline_index)
        label += block.inlines[inline_index].text;
      while (!label.empty() && (label.back() == ':' || label.back() == ' '))
        label.pop_back();
      if (label.empty()) {
        fail(error, "prose note block has no label");
        return std::nullopt;
      }
      note.label.push_back(
          {TextInlineIR{label},
           origin(inline_slices(block, 0, block.term_inline_count),
                  "prose CZ note label")});
      if (!lower_inlines(block, note.content, error)) return std::nullopt;
      document.blocks.push_back(
          {std::move(note), origin(block.slices, "prose CZ note")});
      ++index;
      continue;
    }
    case ProseBlockKindIR::preformatted: {
      if (block.preformatted_lines.empty()) {
        fail(error, "prose preformatted block has no rows");
        return std::nullopt;
      }
      auto preformatted_origin =
          origin(block.slices, "prose CZ example block");
      if (!block.degradation_code.empty()) {
        preformatted_origin.detail = "prose drawn box region: verbatim rows";
        preformatted_origin.fidelity = DocumentFidelityIR::degraded;
        preformatted_origin.degradation_code = block.degradation_code;
        preformatted_origin.degradation_detail =
            "drawn box region has no proven structure; display rows kept "
            "verbatim";
      }
      document.blocks.push_back({PreformattedBlockIR{block.preformatted_lines},
                                 std::move(preformatted_origin)});
      ++index;
      continue;
    }
    case ProseBlockKindIR::footnote: {
      if (block.anchor_id.empty()) {
        fail(error, "prose footnote block has no anchor");
        return std::nullopt;
      }
      document.blocks.push_back(
          {AnchorBlockIR{block.anchor_id, AnchorRoleIR::local},
           origin(block.slices, "prose CZ footnote anchor")});
      ParagraphBlockIR paragraph;
      if (!lower_inlines(block, paragraph.content, error)) return std::nullopt;
      document.blocks.push_back(
          {std::move(paragraph), origin(block.slices, "prose CZ footnote")});
      ++index;
      continue;
    }
    case ProseBlockKindIR::definition_entry: {
      DefinitionListBlockIR list;
      std::vector<DocumentSourceSliceIR> list_slices;
      const auto ordinal = block.list_ordinal;
      while (index < prose.blocks.size() &&
             prose.blocks[index].kind == ProseBlockKindIR::definition_entry &&
             prose.blocks[index].list_ordinal == ordinal) {
        const auto& entry = prose.blocks[index];
        DefinitionEntryIR lowered;
        if (!lower_inlines(entry, 0, entry.term_inline_count, lowered.term,
                           error,
                           label_is_wholly_highlighted(
                               entry, 0, entry.term_inline_count)) ||
            !lower_inlines(entry, lowered.definition, error))
          return std::nullopt;
        lowered.origin = origin(entry.slices, "prose CZ definition entry");
        list_slices.insert(list_slices.end(), entry.slices.begin(),
                           entry.slices.end());
        list.entries.push_back(std::move(lowered));
        ++index;
        if (anchor_at(index) || span_at(index)) break;
      }
      document.blocks.push_back(
          {std::move(list),
           origin(std::move(list_slices), "prose CZ definition list")});
      continue;
    }
    case ProseBlockKindIR::list_item: break;
    }
    ListBlockIR list;
    list.ordered = block.ordered;
    std::vector<DocumentSourceSliceIR> list_slices;
    const auto ordinal = block.list_ordinal;
    while (index < prose.blocks.size() &&
           prose.blocks[index].kind == ProseBlockKindIR::list_item &&
           prose.blocks[index].list_ordinal == ordinal) {
      const auto& item = prose.blocks[index];
      if (item.ordered != list.ordered) {
        fail(error, "prose list mixes ordered and unordered items");
        return std::nullopt;
      }
      ListItemIR lowered;
      if (!lower_inlines(item, lowered.content, error)) return std::nullopt;
      lowered.origin = origin(item.slices, "prose list item");
      lowered.source_ordinal = item_ordinal(item, ++list_positions[ordinal]);
      list_slices.insert(list_slices.end(), item.slices.begin(),
                         item.slices.end());
      list.items.push_back(std::move(lowered));
      ++index;
      // An anchor inside a list splits it.
      if (anchor_at(index) || span_at(index)) break;
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

  // Every container names at least the BOO bytes its own content names
  // before the document is verified.
  normalize_document_origin_slices(document);
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
