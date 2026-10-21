// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/ordered_figure_document_lowering.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/lowering/prose_topic_document_lowering.hpp"
#include <algorithm>
#include <iterator>
#include <map>
#include <tuple>

namespace geist::detail {
namespace {
std::optional<std::vector<BlockIR>> fail(std::string *error, const char *text) {
  if (error)
    *error = text;
  return std::nullopt;
}
void normalize(DocumentNodeOriginIR &origin) {
  using Key = std::tuple<std::uint32_t, std::size_t, std::size_t, std::size_t,
                         std::uint32_t, std::uint32_t>;
  std::map<Key, DocumentSourceSliceIR> slices;
  for (const auto &slice : origin.slices) {
    const Key key{slice.logical_record, slice.segment_index, slice.token_begin,
                  slice.token_end,      slice.byte_begin,    slice.byte_end};
    // Parents claim whole tokens, matching document-origin normalization;
    // the child inlines retain their precise character slices.
    auto whole = slice;
    whole.character_begin = 0;
    whole.character_end = 0;
    slices.emplace(key, whole);
  }
  origin.slices.clear();
  for (const auto &pair : slices)
    origin.slices.push_back(pair.second);
  std::sort(origin.rows.begin(), origin.rows.end(),
            [](const auto &a, const auto &b) {
              return std::tie(a.display_run, a.row_index) <
                     std::tie(b.display_run, b.row_index);
            });
  origin.rows.erase(std::unique(origin.rows.begin(), origin.rows.end(),
                                [](const auto &a, const auto &b) {
                                  return a.display_run == b.display_run &&
                                         a.row_index == b.row_index;
                                }),
                    origin.rows.end());
}
} // namespace
std::optional<std::vector<BlockIR>>
lower_ordered_figure_document_blocks(const FigureSourceBlockIR &figure,
                                     std::string *error) {
  // Reuse the proven resource/description/anchor lowering, but never its old
  // all-images-then-caption ordering. Each picture template is emitted once
  // where the source content sequence places it.
  auto metadata = figure;
  metadata.content.clear();
  metadata.caption.reset();
  auto templates = lower_figure_block_to_document_blocks(metadata, error);
  if (!templates)
    return std::nullopt;
  std::vector<BlockIR> result;
  std::vector<std::vector<BlockIR>> images;
  std::vector<BlockIR> pending;
  for (auto &block : *templates) {
    const auto *anchor = std::get_if<AnchorBlockIR>(&block.node);
    if (anchor && anchor->role == AnchorRoleIR::figure) {
      result.push_back(std::move(block));
      continue;
    }
    const bool image = std::holds_alternative<FigureBlockIR>(block.node);
    pending.push_back(std::move(block));
    if (image) {
      images.push_back(std::move(pending));
      pending.clear();
    }
  }
  if (!pending.empty())
    return fail(error, "ordered figure leaves unattached anchors");
  std::vector<bool> emitted(images.size());
  for (const auto &part : figure.content) {
    if (part.picture) {
      if (part.picture_index >= images.size() || emitted[part.picture_index])
        return fail(error, "ordered figure has an invalid picture ordinal");
      emitted[part.picture_index] = true;
      auto &image = images[part.picture_index];
      result.insert(result.end(), std::make_move_iterator(image.begin()),
                    std::make_move_iterator(image.end()));
      continue;
    }
    std::vector<ProseInlineIR> text;
    bool styled = false;
    DocumentNodeOriginIR origin;
    origin.derivation = DocumentDerivationIR::semantic_lowering;
    origin.detail =
        part.caption ? "ordered figure caption" : "ordered figure text";
    origin.rows = part.rows;
    for (const auto &run : part.runs) {
      ProseInlineIR node;
      node.text = run.text;
      node.style = run.style;
      node.slices = run.slices;
      styled = styled || run.style != FontStyleIR::unknown;
      node.kind = run.style == FontStyleIR::unknown
                      ? ProseInlineKindIR::text
                      : ProseInlineKindIR::emphasis;
      if (run.link) {
        node.kind = ProseInlineKindIR::cross_reference;
        node.target = run.link->target;
        switch (run.link->kind) {
        case FigureLinkKindIR::in_book:
          node.target_kind = CrossReferenceTargetKindIR::anchor;
          break;
        case FigureLinkKindIR::book_contents:
          node.target_kind = CrossReferenceTargetKindIR::book_contents;
          break;
        case FigureLinkKindIR::book_heading:
          node.target_kind = CrossReferenceTargetKindIR::book_heading;
          break;
        case FigureLinkKindIR::external_url:
          node.target_kind = CrossReferenceTargetKindIR::external;
          break;
        }
        node.targets.push_back({node.target_kind, node.target,
                                run.link->alternatives,
                                run.link->source});
      }
      origin.slices.insert(origin.slices.end(), run.slices.begin(),
                           run.slices.end());
      text.push_back(std::move(node));
    }
    auto content = lower_prose_inline_content(text, error);
    if (!content)
      return std::nullopt;
    normalize(origin);
    // An immediately following, unstyled caption has exactly the old image
    // + caption representation. Other labels keep their position and CFONT
    // presentation as independent paragraphs; they are not guessed into it.
    if (part.caption && !styled && !result.empty()) {
      auto *image = std::get_if<FigureBlockIR>(&result.back().node);
      if (image && image->caption.empty()) {
        image->caption = std::move(*content);
        auto &parent = result.back().origin;
        parent.slices.insert(parent.slices.end(), origin.slices.begin(),
                             origin.slices.end());
        parent.rows.insert(parent.rows.end(), origin.rows.begin(),
                           origin.rows.end());
        normalize(parent);
        continue;
      }
    }
    if (part.caption && !styled)
      for (auto &node : *content)
        if (const auto *plain = std::get_if<TextInlineIR>(&node.node))
          node.node = EmphasisInlineIR{plain->text, EmphasisKindIR::emphasis};
    result.push_back(
        {ParagraphBlockIR{std::move(*content)}, std::move(origin)});
  }
  if (std::find(emitted.begin(), emitted.end(), false) != emitted.end())
    return fail(error, "ordered figure did not emit every picture");
  return result;
}
} // namespace geist::detail
