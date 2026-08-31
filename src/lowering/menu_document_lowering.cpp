#include "geist/detail/lowering/menu_document_lowering.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
  return false;
}

bool valid_slice(const DocumentSourceSliceIR &source) {
  return source.logical_record != 0 && source.token_begin < source.token_end &&
         source.byte_begin < source.byte_end;
}

bool cells_belong_to(const std::vector<MenuSourceCellIR> &cells,
                     const DocumentSourceSliceIR &owner) {
  if (cells.empty())
    return false;
  auto previous_output = std::size_t{};
  auto first = true;
  for (const auto &cell : cells) {
    if (cell.logical_record != owner.logical_record ||
        cell.token_index < owner.token_begin ||
        cell.token_index >= owner.token_end ||
        cell.token_bytes.begin < owner.byte_begin ||
        cell.token_bytes.end > owner.byte_end ||
        cell.token_bytes.begin >= cell.token_bytes.end ||
        (!first && cell.output_word_index <= previous_output))
      return false;
    previous_output = cell.output_word_index;
    first = false;
  }
  return true;
}

DocumentNodeOriginIR slice_origin(const DocumentSourceSliceIR &source,
                                  std::string detail) {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::semantic_lowering;
  result.slices.push_back(source);
  result.detail = std::move(detail);
  return result;
}

DocumentNodeOriginIR
cell_origin(const DocumentSourceSliceIR &owner,
            const std::vector<const std::vector<MenuSourceCellIR> *> &groups,
            std::string detail) {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::semantic_lowering;
  result.detail = std::move(detail);
  std::set<std::tuple<std::uint32_t, std::size_t, std::uint32_t, std::uint32_t>>
      slices;
  for (const auto *group : groups)
    for (const auto &cell : *group)
      slices.emplace(cell.logical_record, cell.token_index,
                     cell.token_bytes.begin, cell.token_bytes.end);
  for (const auto &slice : slices)
    result.slices.push_back({std::get<0>(slice), owner.segment_index,
                             std::get<1>(slice), std::get<1>(slice) + 1,
                             std::get<2>(slice), std::get<3>(slice)});
  return result;
}

void add_slice(DocumentNodeOriginIR &origin,
               const DocumentSourceSliceIR &source) {
  origin.slices.push_back(source);
}

void canonicalize_slices(DocumentNodeOriginIR &origin) {
  const auto key = [](const DocumentSourceSliceIR &source) {
    return std::make_tuple(source.logical_record, source.segment_index,
                           source.token_begin, source.token_end,
                           source.byte_begin, source.byte_end);
  };
  std::sort(origin.slices.begin(), origin.slices.end(),
            [&](const auto &left, const auto &right) {
              return key(left) < key(right);
            });
  origin.slices.erase(std::unique(origin.slices.begin(), origin.slices.end(),
                                  [&](const auto &left, const auto &right) {
                                    return key(left) == key(right);
                                  }),
                      origin.slices.end());
}

std::optional<DocumentIR> canonical_document(TopicIdentityIR identity,
                                             const MenuTopicIR &menu,
                                             std::string *error) {
  if (menu.heading_level.size() != 2 || menu.heading_level.front() != 'h' ||
      menu.heading_level.back() < '1' || menu.heading_level.back() > '6' ||
      menu.title.empty() || menu.items.empty() ||
      !valid_slice(menu.title_source) ||
      !cells_belong_to(menu.title_cells, menu.title_source)) {
    fail(error, "menu semantics are incomplete");
    return std::nullopt;
  }
  if (menu.anchor &&
      (menu.anchor->id.empty() || !valid_slice(menu.anchor->source))) {
    fail(error, "menu anchor semantics are incomplete");
    return std::nullopt;
  }

  identity.heading_level = menu.heading_level;
  DocumentIR document;
  document.topic = std::move(identity);
  const auto heading_level =
      static_cast<std::uint32_t>(menu.heading_level.back() - '0');

  if (menu.anchor) {
    auto anchor_origin =
        slice_origin(menu.anchor->source, "menu topic source anchor");
    document.blocks.push_back(
        {AnchorBlockIR{menu.anchor->id}, std::move(anchor_origin)});
  }

  auto heading_origin = slice_origin(menu.title_source, "menu topic heading");
  auto heading_text_origin = cell_origin(menu.title_source, {&menu.title_cells},
                                         "menu topic heading text");
  document.blocks.push_back(
      {HeadingBlockIR{heading_level,
                      {{TextInlineIR{menu.title}, heading_text_origin}}},
       heading_origin});

  for (const auto &paragraph : menu.introductions) {
    if (paragraph.text.empty() || !valid_slice(paragraph.source) ||
        !cells_belong_to(paragraph.cells, paragraph.source)) {
      fail(error, "menu introduction semantics are incomplete");
      return std::nullopt;
    }
    auto paragraph_origin =
        slice_origin(paragraph.source, "menu topic introduction");
    auto text_origin = cell_origin(paragraph.source, {&paragraph.cells},
                                   "menu topic introduction text");
    document.blocks.push_back(
        {ParagraphBlockIR{{{TextInlineIR{paragraph.text},
                            std::move(text_origin)}}},
         std::move(paragraph_origin)});
  }

  // The menu lowers to a typed MenuBlockIR: `Subtopics:` and the `<id> `
  // label prefix are BookServer render-time output (see document_ir.hpp), so
  // they are not materialized here; only source-proven items are carried.
  MenuBlockIR menu_block;
  DocumentNodeOriginIR menu_origin;
  menu_origin.derivation = DocumentDerivationIR::semantic_lowering;
  menu_origin.detail = "menu topic subtopic menu";
  std::set<std::pair<std::uint32_t, std::size_t>> owned_cells;
  for (const auto &item : menu.items) {
    if (item.target.kind != CrossReferenceTargetKindIR::topic ||
        item.target.value.empty() || item.label.empty() ||
        !valid_slice(item.source) ||
        !cells_belong_to(item.target_cells, item.source) ||
        !cells_belong_to(item.label_cells, item.source) ||
        (!item.marker_cells.empty() &&
         !cells_belong_to(item.marker_cells, item.source))) {
      fail(error, "menu item semantics are incomplete");
      return std::nullopt;
    }
    for (const auto *cells :
         {&item.target_cells, &item.label_cells, &item.marker_cells})
      for (const auto &cell : *cells)
        if (!owned_cells.emplace(cell.logical_record, cell.output_word_index)
                 .second) {
          fail(error, "menu item visible source cells overlap");
          return std::nullopt;
        }

    auto item_origin =
        cell_origin(item.source, {&item.target_cells, &item.label_cells},
                    "menu topic item target and label");
    add_slice(item_origin, item.source);
    canonicalize_slices(item_origin);
    menu_block.items.push_back({item.target, item.label, std::move(item_origin)});
    add_slice(menu_origin, item.source);
  }
  canonicalize_slices(menu_origin);
  document.blocks.push_back({std::move(menu_block), std::move(menu_origin)});

  // Every container names at least the BOO bytes its own content names
  // before the document is verified.
  normalize_document_origin_slices(document);
  std::string document_error;
  if (!verify_document_ir(document, &document_error)) {
    fail(error, "invalid menu DocumentIR: " + document_error);
    return std::nullopt;
  }
  if (error != nullptr)
    error->clear();
  return document;
}

} // namespace

std::optional<DocumentIR>
lower_menu_topic_to_document_ir(TopicIdentityIR identity,
                                const MenuTopicIR &menu, std::string *error) {
  return canonical_document(std::move(identity), menu, error);
}

bool verify_menu_topic_document_ir(const MenuTopicIR &menu,
                                   const DocumentIR &document,
                                   std::string *error) {
  const auto expected = canonical_document(document.topic, menu, error);
  if (!expected)
    return false;
  if (format_document_ir(*expected) != format_document_ir(document))
    return fail(error, "menu DocumentIR differs from canonical lowering");
  if (error != nullptr)
    error->clear();
  return true;
}

} // namespace geist::detail
