#include "geist/detail/figure_document_lowering.hpp"

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

using SliceKey = std::tuple<std::uint32_t, std::size_t, std::size_t,
                            std::uint32_t, std::uint32_t>;

// One slice per claimed source token, in canonical source order.
template <typename Predicate>
void add_cell_slices(DocumentNodeOriginIR &origin,
                     const std::vector<FigureSourceCellIR> &cells,
                     Predicate keep) {
  std::set<SliceKey> slices;
  for (const auto &cell : cells)
    if (keep(cell))
      slices.emplace(cell.logical_record, cell.segment_index, cell.token_index,
                     cell.token_bytes.begin, cell.token_bytes.end);
  for (const auto &slice : slices)
    origin.slices.push_back({std::get<0>(slice), std::get<1>(slice),
                             std::get<2>(slice), std::get<2>(slice) + 1,
                             std::get<3>(slice), std::get<4>(slice)});
}

void add_rows(DocumentNodeOriginIR &origin,
              const std::vector<DocumentSourceRowIR> &rows) {
  for (const auto &row : rows)
    origin.rows.push_back(row);
}

bool same_origin(const DocumentNodeOriginIR &left,
                 const DocumentNodeOriginIR &right) {
  if (left.derivation != right.derivation || left.detail != right.detail ||
      left.slices.size() != right.slices.size() ||
      left.rows.size() != right.rows.size())
    return false;
  for (std::size_t index = 0; index < left.slices.size(); ++index)
    if (!(left.slices[index] == right.slices[index]))
      return false;
  for (std::size_t index = 0; index < left.rows.size(); ++index)
    if (left.rows[index].display_run != right.rows[index].display_run ||
        left.rows[index].row_index != right.rows[index].row_index)
      return false;
  return true;
}

bool same_inlines(const InlineSequenceIR &left, const InlineSequenceIR &right) {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].node.index() != right[index].node.index() ||
        !same_origin(left[index].origin, right[index].origin))
      return false;
    const auto *left_text = std::get_if<TextInlineIR>(&left[index].node);
    const auto *right_text = std::get_if<TextInlineIR>(&right[index].node);
    const auto *left_emphasis = std::get_if<EmphasisInlineIR>(&left[index].node);
    const auto *right_emphasis = std::get_if<EmphasisInlineIR>(&right[index].node);
    if (left_text != nullptr && right_text != nullptr) {
      if (left_text->text != right_text->text)
        return false;
    } else if (left_emphasis != nullptr && right_emphasis != nullptr) {
      if (left_emphasis->text != right_emphasis->text ||
          left_emphasis->kind != right_emphasis->kind)
        return false;
    } else {
      return false;
    }
  }
  return true;
}

void add_sorted_rows(DocumentNodeOriginIR &origin,
                     std::vector<DocumentSourceRowIR> rows) {
  std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
    return std::make_pair(a.display_run, a.row_index) <
           std::make_pair(b.display_run, b.row_index);
  });
  rows.erase(std::unique(rows.begin(), rows.end(),
                         [](const auto &a, const auto &b) {
                           return a.display_run == b.display_run &&
                                  a.row_index == b.row_index;
                         }),
             rows.end());
  for (const auto &row : rows)
    origin.rows.push_back(row);
}

// Anchor + preformatted body + emphasised caption paragraph for a drawn
// figure.  The body lines are the hosted display lines; the caption keeps
// the presentation of a picture figure's caption.
std::optional<std::vector<BlockIR>>
lower_preformatted_figure(const FigureSourceBlockIR &figure,
                          std::string *error) {
  if (figure.lines.empty()) {
    fail(error, "preformatted figure has no body lines");
    return std::nullopt;
  }
  std::vector<BlockIR> blocks;
  BlockIR anchor;
  anchor.node = AnchorBlockIR{figure.anchor};
  anchor.origin.derivation = DocumentDerivationIR::semantic_lowering;
  anchor.origin.detail = "figure anchor";
  add_cell_slices(anchor.origin, figure.cells, [&](const auto &cell) {
    return cell.role == FigureCellRoleIR::control &&
           cell.logical_record == figure.span.begin.logical_record &&
           cell.segment_index == figure.span.begin.segment_index;
  });
  if (anchor.origin.slices.empty()) {
    fail(error, "figure anchor has no source slice");
    return std::nullopt;
  }
  blocks.push_back(std::move(anchor));

  PreformattedBlockIR body;
  std::vector<DocumentSourceRowIR> body_rows;
  for (const auto &line : figure.lines) {
    body.lines.push_back(line.text);
    body_rows.insert(body_rows.end(), line.rows.begin(), line.rows.end());
  }
  BlockIR block;
  block.node = std::move(body);
  block.origin.derivation = DocumentDerivationIR::semantic_lowering;
  block.origin.detail = "figure block: verbatim body";
  // Clean, not degraded: an ASCII/CFONT-drawn figure *is* character art the
  // compiler rasterized at build time, and hosted BookServer reproduces it
  // line for line inside `<pre>`. Keeping the display rows verbatim equals
  // the reference renderer, so nothing about the source is lost and no
  // structure is being claimed. Degradation is reserved for real loss.
  add_cell_slices(block.origin, figure.cells, [&](const auto &cell) {
    if (cell.role == FigureCellRoleIR::caption_content ||
        cell.role == FigureCellRoleIR::caption_layout)
      return false;
    if (cell.role == FigureCellRoleIR::control &&
        cell.logical_record == figure.span.begin.logical_record &&
        cell.segment_index == figure.span.begin.segment_index)
      return false;
    return true;
  });
  add_sorted_rows(block.origin, std::move(body_rows));
  blocks.push_back(std::move(block));

  if (figure.caption) {
    if (figure.caption->text.empty()) {
      fail(error, "figure caption is incomplete");
      return std::nullopt;
    }
    InlineIR caption;
    caption.node = EmphasisInlineIR{figure.caption->text,
                                    EmphasisKindIR::emphasis};
    caption.origin.derivation = DocumentDerivationIR::semantic_lowering;
    caption.origin.detail = "figure caption";
    add_cell_slices(caption.origin, figure.cells, [](const auto &cell) {
      return cell.role == FigureCellRoleIR::caption_content;
    });
    add_sorted_rows(caption.origin, figure.caption->rows);
    if (caption.origin.slices.empty()) {
      fail(error, "figure caption has no source slice");
      return std::nullopt;
    }
    BlockIR paragraph;
    ParagraphBlockIR node;
    node.content.push_back(std::move(caption));
    paragraph.node = std::move(node);
    paragraph.origin.derivation = DocumentDerivationIR::semantic_lowering;
    paragraph.origin.detail = "figure caption";
    add_cell_slices(paragraph.origin, figure.cells, [](const auto &cell) {
      return cell.role == FigureCellRoleIR::caption_content ||
             cell.role == FigureCellRoleIR::caption_layout;
    });
    add_sorted_rows(paragraph.origin, figure.caption->rows);
    blocks.push_back(std::move(paragraph));
  }
  if (error != nullptr)
    error->clear();
  return blocks;
}

} // namespace

std::optional<std::vector<BlockIR>>
lower_figure_block_to_document_blocks(const FigureSourceBlockIR &figure,
                                      std::string *error) {
  if (figure.body_kind == FigureBodyKindIR::preformatted) {
    if (!figure.target.empty() || figure.cells.empty() ||
        figure.span.anchored == figure.anchor.empty()) {
      fail(error, "preformatted figure block is inconsistent");
      return std::nullopt;
    }
    return lower_preformatted_figure(figure, error);
  }
  if (figure.target.empty()) {
    fail(error, "figure block has no target");
    return std::nullopt;
  }
  if (figure.cells.empty()) {
    fail(error, "figure block owns no source cells");
    return std::nullopt;
  }
  if (figure.span.anchored == figure.anchor.empty()) {
    fail(error, "figure anchor does not match its span kind");
    return std::nullopt;
  }
  std::vector<BlockIR> blocks;

  if (figure.span.anchored) {
    BlockIR anchor;
    anchor.node = AnchorBlockIR{figure.anchor};
    anchor.origin.derivation = DocumentDerivationIR::semantic_lowering;
    anchor.origin.detail = "figure anchor";
    add_cell_slices(anchor.origin, figure.cells, [&](const auto &cell) {
      return cell.role == FigureCellRoleIR::control &&
             cell.logical_record == figure.span.begin.logical_record &&
             cell.segment_index == figure.span.begin.segment_index;
    });
    if (anchor.origin.slices.empty()) {
      fail(error, "figure anchor has no source slice");
      return std::nullopt;
    }
    blocks.push_back(std::move(anchor));
  }

  FigureBlockIR node;
  node.resource = figure.target_kind == FigureTargetKindIR::book_resource
                      ? "resource:" + figure.target
                      : figure.target;
  if (figure.caption) {
    if (figure.caption->text.empty()) {
      fail(error, "figure caption is incomplete");
      return std::nullopt;
    }
    InlineIR caption;
    caption.node = TextInlineIR{figure.caption->text};
    caption.origin.derivation = DocumentDerivationIR::semantic_lowering;
    caption.origin.detail = "figure caption";
    add_cell_slices(caption.origin, figure.cells, [](const auto &cell) {
      return cell.role == FigureCellRoleIR::caption_content;
    });
    add_rows(caption.origin, figure.caption->rows);
    if (caption.origin.slices.empty()) {
      fail(error, "figure caption has no source slice");
      return std::nullopt;
    }
    node.caption.push_back(std::move(caption));
  }

  BlockIR block;
  block.node = std::move(node);
  block.origin.derivation = DocumentDerivationIR::semantic_lowering;
  block.origin.detail = figure.target_kind == FigureTargetKindIR::book_resource
                            ? "figure block: book resource"
                            : "figure block: external image";
  add_cell_slices(block.origin, figure.cells, [&](const auto &cell) {
    if (figure.span.anchored && cell.role == FigureCellRoleIR::control &&
        cell.logical_record == figure.span.begin.logical_record &&
        cell.segment_index == figure.span.begin.segment_index)
      return false;
    return true;
  });
  add_rows(block.origin, figure.suppressed_rows);
  if (figure.caption)
    add_rows(block.origin, figure.caption->rows);
  blocks.push_back(std::move(block));

  if (error != nullptr)
    error->clear();
  return blocks;
}

bool verify_figure_document_blocks(const FigureSourceBlockIR &figure,
                                   const std::vector<BlockIR> &blocks,
                                   std::string *error) {
  std::string lowering_error;
  const auto canonical =
      lower_figure_block_to_document_blocks(figure, &lowering_error);
  if (!canonical)
    return fail(error, lowering_error);
  if (canonical->size() != blocks.size())
    return fail(error, "figure document block count differs from canonical");
  for (std::size_t index = 0; index < blocks.size(); ++index) {
    const auto &expected = (*canonical)[index];
    const auto &actual = blocks[index];
    if (expected.node.index() != actual.node.index())
      return fail(error, "figure document block kind differs from canonical");
    if (!same_origin(expected.origin, actual.origin))
      return fail(error, "figure document block origin differs from "
                         "canonical");
    if (const auto *anchor = std::get_if<AnchorBlockIR>(&expected.node)) {
      if (anchor->id != std::get<AnchorBlockIR>(actual.node).id)
        return fail(error, "figure anchor id differs from canonical");
    } else if (const auto *node = std::get_if<FigureBlockIR>(&expected.node)) {
      const auto &actual_node = std::get<FigureBlockIR>(actual.node);
      if (node->resource != actual_node.resource)
        return fail(error, "figure resource differs from canonical");
      if (!same_inlines(node->caption, actual_node.caption))
        return fail(error, "figure caption differs from canonical");
    } else if (const auto *node =
                   std::get_if<PreformattedBlockIR>(&expected.node)) {
      if (node->lines != std::get<PreformattedBlockIR>(actual.node).lines)
        return fail(error, "figure body lines differ from canonical");
    } else if (const auto *node =
                   std::get_if<ParagraphBlockIR>(&expected.node)) {
      if (!same_inlines(node->content,
                        std::get<ParagraphBlockIR>(actual.node).content))
        return fail(error, "figure caption differs from canonical");
    } else {
      return fail(error, "figure lowering produced an unexpected block");
    }
  }
  if (error != nullptr)
    error->clear();
  return true;
}

} // namespace geist::detail
