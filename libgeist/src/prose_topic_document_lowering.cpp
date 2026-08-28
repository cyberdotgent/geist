#include "geist/detail/prose_topic_document_lowering.hpp"

#include <algorithm>
#include <cctype>
#include <map>
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

  const auto emit_anchors = [&](std::size_t position, bool after_menu = false) {
    for (const auto& anchor : prose.anchors) {
      if (anchor.position != position || anchor.after_menu != after_menu)
        continue;
      document.blocks.push_back(
          {AnchorBlockIR{anchor.id},
           origin({anchor.source}, "prose source anchor")});
    }
  };

  const auto anchor_at = [&](std::size_t position) {
    return std::any_of(prose.anchors.begin(), prose.anchors.end(),
                       [&](const auto& anchor) {
                         return anchor.position == position;
                       });
  };
  // Items of one CZ list can be interleaved with paragraphs (a `cz FLOW P`
  // inside a list item); each run of items becomes one list block and the
  // ordinal keeps counting across the runs.
  std::map<std::size_t, std::uint64_t> list_positions;
  std::size_t index = 0;
  while (index < prose.blocks.size()) {
    emit_anchors(index);
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
      document.blocks.push_back(
          {PreformattedBlockIR{block.preformatted_lines},
           origin(block.slices, "prose CZ example block")});
      ++index;
      continue;
    }
    case ProseBlockKindIR::footnote: {
      if (block.anchor_id.empty()) {
        fail(error, "prose footnote block has no anchor");
        return std::nullopt;
      }
      document.blocks.push_back(
          {AnchorBlockIR{block.anchor_id},
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
        if (anchor_at(index)) break;
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
      if (anchor_at(index)) break;
    }
    document.blocks.push_back(
        {std::move(list), origin(std::move(list_slices), "prose list")});
  }
  emit_anchors(prose.blocks.size());

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
