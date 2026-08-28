#include "geist/detail/prose_topic_document_lowering.hpp"

#include <algorithm>
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

  const auto emit_anchors = [&](std::size_t position, bool after_menu = false) {
    for (const auto& anchor : prose.anchors) {
      if (anchor.position != position || anchor.after_menu != after_menu)
        continue;
      document.blocks.push_back(
          {AnchorBlockIR{anchor.id},
           origin({anchor.source}, "prose source anchor")});
    }
  };

  std::size_t index = 0;
  while (index < prose.blocks.size()) {
    emit_anchors(index);
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
