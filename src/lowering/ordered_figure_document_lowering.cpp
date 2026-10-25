// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/ordered_figure_document_lowering.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/lowering/cross_reference_target.hpp"
#include "geist/detail/lowering/font_style_inlines.hpp"
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
CrossReferenceTargetKindIR link_target_kind(FigureLinkKindIR kind) {
  switch (kind) {
  case FigureLinkKindIR::in_book:
    return CrossReferenceTargetKindIR::anchor;
  case FigureLinkKindIR::book_contents:
    return CrossReferenceTargetKindIR::book_contents;
  case FigureLinkKindIR::book_heading:
    return CrossReferenceTargetKindIR::book_heading;
  case FigureLinkKindIR::external_url:
    return CrossReferenceTargetKindIR::external;
  }
  return CrossReferenceTargetKindIR::anchor;
}

// The drawn frame of a picture region as one preformatted block: every row
// verbatim, its CFONT runs styled, its selector runs linked, and each
// picture an image standing over the placeholder cells its selector covers
// (`ImageInlineIR::cells`), which is where hosted BookServer draws it.
// `images` are the per-picture templates (anchors, then the image block);
// the anchors come out ahead of the block, the image blocks lend it their
// resource, description and source.
std::optional<std::vector<BlockIR>>
drawn_frame_blocks(const FigureSourceBlockIR &figure,
                   std::vector<std::vector<BlockIR>> &images,
                   std::string *error) {
  std::vector<BlockIR> result;
  std::vector<const FigureBlockIR *> pictures;
  BlockIR frame;
  frame.origin.derivation = DocumentDerivationIR::semantic_lowering;
  frame.origin.detail = "figure block: drawn frame";
  for (auto &image : images) {
    for (auto &block : image) {
      if (const auto *picture = std::get_if<FigureBlockIR>(&block.node)) {
        pictures.push_back(picture);
        frame.origin.slices.insert(frame.origin.slices.end(),
                                   block.origin.slices.begin(),
                                   block.origin.slices.end());
        frame.origin.rows.insert(frame.origin.rows.end(),
                                 block.origin.rows.begin(),
                                 block.origin.rows.end());
        continue;
      }
      result.push_back(std::move(block));
    }
  }
  PreformattedBlockIR body;
  for (const auto &line : figure.drawn_lines) {
    DocumentNodeOriginIR line_origin;
    line_origin.derivation = DocumentDerivationIR::semantic_lowering;
    line_origin.detail = "figure frame row";
    line_origin.slices = line.slices;
    line_origin.rows = line.rows;
    normalize(line_origin);
    std::string text;
    InlineSequenceIR row;
    for (const auto &run : line.runs) {
      text += run.text;
      InlineIR in;
      in.origin = line_origin;
      in.origin.slices = run.slices;
      in.origin.detail = "figure frame run";
      normalize(in.origin);
      if (run.picture) {
        if (*run.picture >= pictures.size())
          return fail(error, "drawn frame names an unknown picture");
        const auto &picture = *pictures[*run.picture];
        const auto placeholder = trim_ascii_whitespace(run.text);
        if (placeholder.empty())
          return fail(error, "drawn frame picture covers no placeholder");
        ImageInlineIR image;
        image.resource = picture.resource;
        // As the figure image: the book's own description when it gave one,
        // else the placeholder the image replaces (hosted's alt text).
        image.alt_text =
            picture.description.empty() ? placeholder : picture.description;
        image.cells = run.text;
        in.origin.detail = "figure frame picture";
        in.node = std::move(image);
      } else if (run.link) {
        CrossReferenceInlineIR reference;
        auto control = line_origin;
        control.detail = "figure frame row: CSELECT target";
        control.slices = {run.link->source};
        control.rows.clear();
        reference.targets.push_back(
            {selector_cross_reference_target(link_target_kind(run.link->kind),
                                             run.link->target,
                                             run.link->alternatives),
             control});
        reference.label = run.text;
        if (run.style != FontStyleIR::unknown) {
          if (font_style_is_code(run.style))
            reference.label_code = true;
          else
            reference.label_emphasis = font_style_emphasis_kind(run.style);
          reference.label_phrase = font_style_phrase_kind(run.style);
        }
        in.node = std::move(reference);
      } else if (run.style == FontStyleIR::unknown) {
        in.node = TextInlineIR{run.text};
      } else if (font_style_is_code(run.style)) {
        in.node =
            CodeInlineIR{run.text, font_style_phrase_kind(run.style)};
      } else {
        in.node = EmphasisInlineIR{run.text,
                                   font_style_emphasis_kind(run.style),
                                   font_style_phrase_kind(run.style)};
      }
      row.push_back(std::move(in));
    }
    frame.origin.slices.insert(frame.origin.slices.end(),
                               line_origin.slices.begin(),
                               line_origin.slices.end());
    frame.origin.rows.insert(frame.origin.rows.end(), line_origin.rows.begin(),
                             line_origin.rows.end());
    body.lines.push_back(std::move(text));
    body.line_origins.push_back(std::move(line_origin));
    body.styled_lines.push_back(std::move(row));
  }
  normalize(frame.origin);
  frame.node = std::move(body);
  result.push_back(std::move(frame));
  return result;
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
  metadata.drawn_lines.clear();
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
  // A drawn frame stands where its first part stood: the region opens with
  // it, since only the parts after the frame are blocks of their own.
  if (!figure.drawn_lines.empty()) {
    if (figure.content.empty() || !figure.content.front().drawn)
      return fail(error, "drawn frame does not open its figure");
    auto frame = drawn_frame_blocks(figure, images, error);
    if (!frame)
      return std::nullopt;
    result.insert(result.end(), std::make_move_iterator(frame->begin()),
                  std::make_move_iterator(frame->end()));
    std::fill(emitted.begin(), emitted.end(), true);
  }
  for (const auto &part : figure.content) {
    if (part.drawn) {
      if (figure.drawn_lines.empty())
        return fail(error, "figure part is drawn without a frame");
      continue;
    }
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
