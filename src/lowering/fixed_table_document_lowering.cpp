// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/fixed_table_document_lowering.hpp"
#include "geist/detail/lowering/font_style_inlines.hpp"
#include "geist/detail/lowering/cross_reference_target.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
  return false;
}

DocumentNodeOriginIR origin_for(const char *detail) {
  DocumentNodeOriginIR origin;
  origin.derivation = DocumentDerivationIR::semantic_lowering;
  origin.detail = detail;
  return origin;
}

// Origin rows are an unordered evidence set; keep them unique and sorted so
// the document verifier's ordering rule holds whatever the cell/line order.
void add_row(DocumentNodeOriginIR &origin, const DocumentSourceRowIR &row) {
  for (const auto &existing : origin.rows)
    if (existing.display_run == row.display_run &&
        existing.row_index == row.row_index)
      return;
  origin.rows.push_back(row);
  std::sort(origin.rows.begin(), origin.rows.end(),
            [](const auto &left, const auto &right) {
              return std::make_pair(left.display_run, left.row_index) <
                     std::make_pair(right.display_run, right.row_index);
            });
}

InlineSequenceIR cell_inlines(const FixedTableCellIR &cell,
                              DocumentNodeOriginIR &cell_origin) {
  InlineSequenceIR content;
  for (const auto &line : cell.lines) {
    if (!content.empty()) {
      InlineIR brk;
      brk.node = HardBreakInlineIR{};
      brk.origin = origin_for("fixed table cell line break");
      content.push_back(std::move(brk));
    }
    InlineIR text;
    text.node = TextInlineIR{line.text};
    text.origin = origin_for("fixed table cell line");
    text.origin.slices.push_back(line.slice);
    for (const auto &source : line.source_cells)
      add_row(text.origin, {source.run, source.row_index});
    cell_origin.slices.push_back(line.slice);
    for (const auto &row : text.origin.rows)
      add_row(cell_origin, row);
    if (line.highlights.empty()) {
      content.push_back(std::move(text));
      continue;
    }
    // The runs the line's `CFONT` styles: a code span for a monospace style,
    // emphasis for every other, plain text between them (SC31-6552-00
    // 4.4.69, hosted `<I>macroinstructions:</I>    ASLPLU`).
    std::size_t cursor = 0;
    const auto plain = [&](std::size_t end) {
      if (end <= cursor)
        return;
      InlineIR piece;
      piece.node = TextInlineIR{line.text.substr(cursor, end - cursor)};
      piece.origin = text.origin;
      content.push_back(std::move(piece));
    };
    for (const auto &run : line.highlights) {
      plain(run.begin);
      InlineIR piece;
      auto styled = line.text.substr(run.begin, run.end - run.begin);
      if (font_style_is_code(run.style))
        piece.node =
            CodeInlineIR{std::move(styled), font_style_phrase_kind(run.style)};
      else
        piece.node = EmphasisInlineIR{std::move(styled),
                                      font_style_emphasis_kind(run.style),
                                      font_style_phrase_kind(run.style)};
      piece.origin = text.origin;
      piece.origin.detail = "fixed table cell line: CFONT run";
      content.push_back(std::move(piece));
      cursor = run.end;
    }
    plain(line.text.size());
  }
  return content;
}

// One verbatim row with the runs its `CFONT` styles and the links its
// `CSELECT` controls place: plain text between them, a code span for a
// monospace style and emphasis for every other, a cross reference over a
// selector's run (carrying the run's style), so the row flattens byte for
// byte back to its text.  ITPPIBOK 4.3.5.4 serves
// `| <B>TASK</B> ... |      <B>START</B> <B>DATE</B>      |`; ICHA6MST 5.0
// serves `... start on in topic <a href="...#SPTVMSP1I">5.0</a>.`.
InlineSequenceIR styled_row(const FixedTablePreformattedLineIR &line,
                            const DocumentNodeOriginIR &line_origin) {
  InlineSequenceIR row;
  // Every boundary of a highlight or a link cuts the row into runs.
  std::vector<std::size_t> cuts{0, line.text.size()};
  for (const auto &run : line.highlights) {
    cuts.push_back(run.begin);
    cuts.push_back(run.end);
  }
  for (const auto &link : line.links) {
    cuts.push_back(link.begin);
    cuts.push_back(link.end);
  }
  std::sort(cuts.begin(), cuts.end());
  cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
  for (std::size_t at = 0; at + 1 < cuts.size(); ++at) {
    const auto begin = cuts[at];
    const auto end = std::min(cuts[at + 1], line.text.size());
    if (begin >= end)
      continue;
    auto text = line.text.substr(begin, end - begin);
    const FontStyleIR *style = nullptr;
    for (const auto &run : line.highlights)
      if (run.begin <= begin && end <= run.end)
        style = &run.style;
    const FixedTableLineLinkIR *link = nullptr;
    for (const auto &candidate : line.links)
      if (candidate.begin <= begin && end <= candidate.end)
        link = &candidate;
    if (link != nullptr) {
      CrossReferenceInlineIR reference;
      auto control = line_origin;
      control.detail = "fixed table preformatted line: CSELECT target";
      control.slices = {link->source};
      control.rows.clear();
      reference.targets.push_back(
          {selector_cross_reference_target(link->target_kind, link->target,
                                           link->target_alternatives),
           control});
      reference.label = std::move(text);
      if (style != nullptr) {
        if (font_style_is_code(*style))
          reference.label_code = true;
        else
          reference.label_emphasis = font_style_emphasis_kind(*style);
        reference.label_phrase = font_style_phrase_kind(*style);
      }
      row.push_back({std::move(reference), line_origin});
    } else if (style == nullptr) {
      row.push_back({TextInlineIR{std::move(text)}, line_origin});
    } else if (font_style_is_code(*style)) {
      row.push_back(
          {CodeInlineIR{std::move(text), font_style_phrase_kind(*style)},
           line_origin});
    } else {
      row.push_back({EmphasisInlineIR{std::move(text),
                                      font_style_emphasis_kind(*style),
                                      font_style_phrase_kind(*style)},
                     line_origin});
    }
  }
  return row;
}

// The row of a picture line: its text with each picture's cells spelled
// back in (`FixedTablePictureIR::cells`), and those cells an image standing
// in the row, which hosted BookServer draws at the selector's first column
// with the cells blank (GC23-0422-02 2.5.1 `TBLUNIQ5`, GG24-395 3.3.8
// `TBLUNIQ14`).  Empty when a picture's cells do not fall inside one plain
// text run of the row.
std::optional<std::pair<std::string, InlineSequenceIR>>
picture_row(const FixedTablePreformattedLineIR &line,
            const std::vector<const FixedTablePictureIR *> &pictures,
            const DocumentNodeOriginIR &line_origin) {
  auto restored = line;
  for (const auto *picture : pictures) {
    const auto need = picture->column + picture->cells.size();
    if (restored.text.size() < need)
      restored.text.resize(need, ' ');
    if (restored.text.find_first_not_of(' ', picture->column) < need)
      return std::nullopt;
    restored.text.replace(picture->column, picture->cells.size(),
                          picture->cells);
  }
  auto row = styled_row(restored, line_origin);
  for (const auto *picture : pictures) {
    std::size_t at = 0;
    bool placed = false;
    for (std::size_t index = 0; index < row.size() && !placed; ++index) {
      const auto *text = std::get_if<TextInlineIR>(&row[index].node);
      std::size_t width = 0;
      if (text != nullptr)
        width = text->text.size();
      else if (const auto *code = std::get_if<CodeInlineIR>(&row[index].node))
        width = code->code.size();
      else if (const auto *em = std::get_if<EmphasisInlineIR>(&row[index].node))
        width = em->text.size();
      else if (const auto *ref =
                   std::get_if<CrossReferenceInlineIR>(&row[index].node))
        width = ref->label.size();
      else if (const auto *image = std::get_if<ImageInlineIR>(&row[index].node))
        width = image->cells.size();
      const auto begin = picture->column;
      const auto end = begin + picture->cells.size();
      if (end <= at || begin >= at + width) {
        at += width;
        continue;
      }
      if (text == nullptr || begin < at || end > at + width)
        return std::nullopt;
      const auto value = text->text;
      const auto origin = row[index].origin;
      InlineSequenceIR pieces;
      if (begin > at)
        pieces.push_back({TextInlineIR{value.substr(0, begin - at)}, origin});
      ImageInlineIR image;
      image.resource = "resource:" + picture->resource;
      image.alt_text = picture->placeholder;
      image.cells = picture->cells;
      auto image_origin = origin;
      image_origin.detail = "fixed table region: picture";
      pieces.push_back({std::move(image), image_origin});
      if (end < at + width)
        pieces.push_back({TextInlineIR{value.substr(end - at)}, origin});
      row.erase(row.begin() + static_cast<std::ptrdiff_t>(index));
      row.insert(row.begin() + static_cast<std::ptrdiff_t>(index),
                 pieces.begin(), pieces.end());
      placed = true;
    }
    if (!placed)
      return std::nullopt;
  }
  return std::make_pair(std::move(restored.text), std::move(row));
}

TableRowIR lower_row(const FixedTableRowIR &row) {
  TableRowIR lowered;
  lowered.origin = origin_for("fixed table row");
  lowered.revision_mark = row.revision_mark;
  for (const auto &cell : row.cells) {
    TableCellIR lowered_cell;
    lowered_cell.origin = origin_for("fixed table cell");
    lowered_cell.content = cell_inlines(cell, lowered_cell.origin);
    lowered.cells.push_back(std::move(lowered_cell));
  }
  for (const auto &source : row.source_rows)
    add_row(lowered.origin, source);
  return lowered;
}

// Where a spot anchor that stands between the rows of a declared table
// opens: the body row its display line belongs to, and the line of that
// row's first cell it stands in front of (the number of the cell's lines
// above it).  Hosted BookServer opens such an anchor in the first cell of its
// row, at its display line: SC31-8569-00 10.2 serves `SRSPTNEWRUS` as
// `<td ...><a name="SPTNEWRUS"> API INQPER </a></td>`, SC26-8219-00 8.5.1
// `SRSPTEDTITP` (second display line of its row) after the first cell's
// first `<br>`.  The display line is matched to its row by the source
// tokens of the row's cells; anything that does not prove it -- a line no
// cell draws from, a line two rows share, a first cell line without source
// cells -- answers nothing.
std::optional<std::pair<std::size_t, std::size_t>>
spot_anchor_cell(const FixedTableBlockIR &block,
                 const FixedTableSpotAnchorIR &spot) {
  if (spot.line >= block.preformatted_lines.size())
    return std::nullopt;
  // A blank padding line (only rails and blanks) opens the row below it:
  // SD35-0238-00 1.1.3 `SRSPTSDLC1` stands before two such lines, and hosted
  // opens it in the first cell of the row they pad.
  auto drawn = spot.line;
  const auto padding = [](const std::string &text) {
    return text.find_first_not_of(" |") == std::string::npos;
  };
  while (drawn + 1 < block.preformatted_lines.size() &&
         padding(block.preformatted_lines[drawn].text))
    ++drawn;
  const auto &line = block.preformatted_lines[drawn];
  const auto &opened = block.preformatted_lines[spot.line];
  // A cell line whose words come from the display line's own tokens.
  const auto on_line = [&](const FixedTableCellLineIR &cell_line) {
    const auto &slice = cell_line.slice;
    return slice.token_begin < slice.token_end &&
           slice.logical_record == line.logical_record &&
           slice.token_begin > line.prefix_token &&
           slice.token_begin < line.token_end;
  };
  const auto holds = [&](const FixedTableRowIR &row) {
    for (const auto &cell : row.cells)
      if (std::any_of(cell.lines.begin(), cell.lines.end(), on_line))
        return true;
    return false;
  };
  if (block.caption && holds(*block.caption))
    return std::nullopt;
  std::optional<std::size_t> found;
  for (std::size_t index = 0; index < block.body.size(); ++index) {
    if (!holds(block.body[index]))
      continue;
    if (found)
      return std::nullopt;
    found = index;
  }
  if (!found || block.body[*found].cells.empty())
    return std::nullopt;
  const auto key = std::make_pair(opened.logical_record, opened.prefix_token);
  std::size_t above = 0;
  for (const auto &cell_line : block.body[*found].cells.front().lines) {
    const auto &slice = cell_line.slice;
    if (slice.token_begin >= slice.token_end)
      return std::nullopt;
    if (std::make_pair(slice.logical_record, slice.token_begin) < key)
      ++above;
  }
  return std::make_pair(*found, above);
}

} // namespace

std::vector<BlockIR>
lower_fixed_table_block_to_document_ir(const FixedTableBlockIR &block) {
  std::vector<BlockIR> result;

  // Hosted BookServer names the table anchor after the whole SRTBL opcode
  // without its `SR` prefix: SC31-711 4.0 `SRTBLTBLUNIQ6` is served as
  // `<a name="TBLTBLUNIQ6">`, and cross references select the same spelling
  // (GG24-4302-00 10.2 `cselect ... TBLDBCTL51` for `SRTBLDBCTL51`).
  BlockIR anchor;
  anchor.node = AnchorBlockIR{"TBL" + block.object_id, AnchorRoleIR::table};
  anchor.origin = origin_for("fixed table object");
  anchor.origin.slices.push_back(block.object_source);
  result.push_back(std::move(anchor));

  // The region lowers to its display lines, which is what the hosted reader
  // serves inside `<pre>`.  This is the faithful rendering, not a fallback:
  // the BOO file stores character art, not a grid, and reproducing it line
  // for line equals the reference renderer, so the block is clean even when
  // a column model did prove (`geometry` is then `box` or `gap` and the
  // recovered rows stay in the IR for consumers and provenance).  The one
  // exception is an envelope the source itself declared a `:table` with
  // `cz OFF TABLE`, which is the only region hosted serves as an HTML
  // `<table>`.
  const auto verbatim =
      !block.source_declared_table && !block.preformatted_lines.empty();
  if (verbatim || block.geometry == FixedTableGeometryIR::preformatted) {
    // A picture the region places on one of those lines is served by hosted
    // BookServer as an `<img>` over the selector's columns, inside the same
    // `<pre>`: the row keeps it as an image standing over those columns
    // (`picture_row`).  Should a picture's columns not fall inside one plain
    // run of its row, every picture of the region is emitted as its own
    // block, in line order, ahead of the art whose blanked columns are the
    // slot it belongs in.
    std::vector<std::vector<const FixedTablePictureIR *>> line_pictures(
        block.preformatted_lines.size());
    for (const auto &picture : block.pictures)
      if (picture.line < line_pictures.size())
        line_pictures[picture.line].push_back(&picture);
    std::vector<std::optional<std::pair<std::string, InlineSequenceIR>>>
        placed_rows(block.preformatted_lines.size());
    bool inline_pictures = true;
    for (std::size_t index = 0; index < line_pictures.size(); ++index) {
      if (line_pictures[index].empty())
        continue;
      auto line_origin = origin_for("fixed table preformatted line");
      const auto &line = block.preformatted_lines[index];
      if (line.slice.byte_begin < line.slice.byte_end)
        line_origin.slices.push_back(line.slice);
      for (const auto &row : line.rows)
        add_row(line_origin, row);
      placed_rows[index] =
          picture_row(line, line_pictures[index], line_origin);
      inline_pictures = inline_pictures && placed_rows[index].has_value();
    }
    if (!inline_pictures)
      for (auto &placed : placed_rows)
        placed.reset();
    for (const auto &picture : block.pictures) {
      if (inline_pictures)
        break;
      BlockIR image;
      image.origin = origin_for("fixed table region: picture");
      image.origin.slices.push_back(picture.source);
      if (picture.line < block.preformatted_lines.size())
        for (const auto &row : block.preformatted_lines[picture.line].rows)
          add_row(image.origin, row);
      image.node = FigureBlockIR{"resource:" + picture.resource, {}, {}};
      result.push_back(std::move(image));
    }
    const auto append_body = [&](std::size_t begin, std::size_t end) {
      if (begin == end)
        return;
      PreformattedBlockIR body;
      BlockIR body_block;
      body_block.origin = origin_for("fixed table region: verbatim body");
      // A block whose rows carry any style or picture keeps every row as
      // inlines (an unstyled row is one text run, an empty row none).
      bool styled = false;
      for (auto index = begin; index < end; ++index) {
        const auto &line = block.preformatted_lines[index];
        styled = styled || !line.highlights.empty() || !line.links.empty() ||
                 placed_rows[index].has_value();
      }
      for (auto index = begin; index < end; ++index) {
        const auto &line = block.preformatted_lines[index];
        const auto &placed = placed_rows[index];
        body.lines.push_back(placed ? placed->first : line.text);
        auto line_origin = origin_for("fixed table preformatted line");
        if (line.slice.byte_begin < line.slice.byte_end)
          line_origin.slices.push_back(line.slice);
        for (const auto &row : line.rows) {
          add_row(body_block.origin, row);
          add_row(line_origin, row);
        }
        if (placed)
          for (const auto *picture : line_pictures[index])
            body_block.origin.slices.push_back(picture->source);
        if (placed)
          body.styled_lines.push_back(placed->second);
        else if (styled)
          body.styled_lines.push_back(styled_row(line, line_origin));
        body.line_origins.push_back(std::move(line_origin));
        // An `SRLIS<owner>` inside the region names this row, inside the
        // same `<pre>` (`FixedTableBlockIR::list_anchors`).
        for (const auto &list : block.list_anchors) {
          if (list.line != index)
            continue;
          PreformattedAnchorIR anchor;
          anchor.line = index - begin;
          anchor.anchor = AnchorBlockIR{list.id};
          anchor.origin = origin_for("fixed table region: list anchor");
          anchor.origin.slices.push_back(list.source);
          body.line_anchors.push_back(std::move(anchor));
        }
      }
      body_block.node = std::move(body);
      result.push_back(std::move(body_block));
    };
    std::size_t line = 0;
    for (const auto &spot : block.spot_anchors) {
      append_body(line, spot.line);
      BlockIR anchor_block;
      anchor_block.node = AnchorBlockIR{spot.id};
      anchor_block.origin = origin_for("fixed table region: spot anchor");
      anchor_block.origin.slices.push_back(spot.source);
      result.push_back(std::move(anchor_block));
      line = spot.line;
    }
    append_body(line, block.preformatted_lines.size());
    return result;
  }

  // A bare `SR<id>` anchor inside a declared table is a second name of the
  // table, which hosted BookServer opens right behind the table's own
  // anchor, ahead of the `<table>`: SH24-5221-02 6.1.1 `SRTBLQCOL1`,
  // `SRSPTTQCOL1` is served as `<a name="TBLQCOL1"></a><a
  // name="SPTTQCOL1"></a>` (dfhla313 1.1.3.1 `SRSPTXAKUSER` has the same
  // shape).  One that stands between the rows opens in the first cell of
  // its row (`spot_anchor_cell`); one whose row the display lines do not
  // prove is placed ahead of the table too, and says so.
  std::vector<std::pair<const FixedTableSpotAnchorIR *,
                        std::pair<std::size_t, std::size_t>>>
      in_cells;
  for (const auto &spot : block.spot_anchors) {
    if (spot.line != 0)
      if (const auto cell = spot_anchor_cell(block, spot)) {
        in_cells.push_back({&spot, *cell});
        continue;
      }
    BlockIR anchor_block;
    anchor_block.node = AnchorBlockIR{spot.id};
    anchor_block.origin = origin_for("fixed table: spot anchor");
    anchor_block.origin.slices.push_back(spot.source);
    if (spot.line != 0) {
      anchor_block.origin.fidelity = DocumentFidelityIR::degraded;
      anchor_block.origin.degradation_code = "table-spot-anchor-position";
      anchor_block.origin.degradation_detail =
          "anchor '" + spot.id + "' opens display line " +
          std::to_string(spot.line) +
          " of a declared table; placed ahead of the table";
    }
    result.push_back(std::move(anchor_block));
  }

  if (block.caption && !block.caption->cells.empty()) {
    BlockIR caption;
    ParagraphBlockIR paragraph;
    caption.origin = origin_for("fixed table caption");
    paragraph.content = cell_inlines(block.caption->cells.front(), caption.origin);
    for (const auto &source : block.caption->source_rows)
      add_row(caption.origin, source);
    if (!paragraph.content.empty()) {
      caption.node = std::move(paragraph);
      result.push_back(std::move(caption));
    }
  }

  BlockIR table_block;
  TableBlockIR table;
  table.header_rows = block.header_rows;
  for (const auto &row : block.body)
    table.rows.push_back(lower_row(row));
  for (const auto &[spot, cell] : in_cells) {
    TableCellAnchorIR placed;
    placed.line = cell.second;
    placed.anchor = AnchorBlockIR{spot->id};
    placed.origin = origin_for("fixed table cell: spot anchor");
    placed.origin.slices.push_back(spot->source);
    auto &anchors = table.rows[cell.first].cells.front().line_anchors;
    anchors.insert(std::upper_bound(anchors.begin(), anchors.end(), placed,
                                    [](const auto &left, const auto &right) {
                                      return left.line < right.line;
                                    }),
                   std::move(placed));
  }
  table_block.origin = origin_for("fixed table");
  // Reached only when the source declared the region a `:table`, or when the
  // record's display-line model did not parse so no verbatim text exists.
  // The second case asserts columns the file does not carry, so it is named
  // in the render diagnostic instead of passing silently.
  if (!block.source_declared_table) {
    table_block.origin.fidelity = DocumentFidelityIR::degraded;
    table_block.origin.degradation_code = "fixed-table-columns-inferred";
    table_block.origin.degradation_detail =
        "SRTBL region has no parseable display lines; recovered columns are "
        "rendered as a table although the source declares no :table";
  }
  table_block.origin.slices.push_back(block.object_source);
  for (const auto &source : block.source_rows)
    add_row(table_block.origin, source);
  table_block.node = std::move(table);
  result.push_back(std::move(table_block));
  return result;
}

bool verify_fixed_table_document_ir(const FixedTableBlockIR &block,
                                    const std::vector<BlockIR> &lowered,
                                    std::string *error) {
  // The block renders verbatim unless the source declared a `:table`; a
  // declared region with no proven columns still renders verbatim.
  const auto verbatim =
      block.geometry == FixedTableGeometryIR::preformatted ||
      (!block.source_declared_table && !block.preformatted_lines.empty());
  if (!verbatim && block.body.empty())
    return fail(error, "fixed table block has no body rows");
  if (verbatim && block.preformatted_lines.empty())
    return fail(error, "verbatim table region has no display lines");
  const auto canonical = lower_fixed_table_block_to_document_ir(block);
  const auto wrap = [](const std::vector<BlockIR> &blocks) {
    DocumentIR document;
    document.topic.id = "fixed-table";
    document.topic.title = "fixed-table";
    document.blocks = blocks;
    return document;
  };
  auto expected = wrap(canonical);
  auto actual = wrap(lowered);
  // The prose document these blocks are placed in normalizes every container
  // origin before verification; the standalone comparison has to do the same
  // work so the two sides describe the same provenance.
  normalize_document_origin_slices(expected);
  normalize_document_origin_slices(actual);
  std::string verify_error;
  if (!verify_document_ir(expected, &verify_error))
    return fail(error, "fixed table lowering is not a valid document: " +
                           verify_error);
  if (format_document_ir(expected) != format_document_ir(actual))
    return fail(error, "fixed table document nodes differ from canonical "
                       "lowering");
  if (verbatim) {
    std::vector<std::string> lines;
    for (const auto &candidate : lowered)
      if (const auto *body = std::get_if<PreformattedBlockIR>(&candidate.node))
        lines.insert(lines.end(), body->lines.begin(), body->lines.end());
    if (lines.empty())
      return fail(error, "preformatted table region has no body block");
    if (lines.size() != block.preformatted_lines.size())
      return fail(error, "preformatted table region lines are not conserved");
    for (std::size_t index = 0; index < lines.size(); ++index)
      if (lines[index] != block.preformatted_lines[index].text)
        return fail(error, "preformatted table region text is not conserved");
    // No picture may be lost: the region's images are the only place the
    // blanked placeholder words survive.
    std::size_t images = 0;
    for (const auto &candidate : lowered)
      if (const auto *figure = std::get_if<FigureBlockIR>(&candidate.node)) {
        if (images >= block.pictures.size() ||
            figure->resource != "resource:" + block.pictures[images].resource)
          return fail(error, "fixed table picture is not conserved");
        ++images;
      }
    if (images != block.pictures.size())
      return fail(error, "fixed table region loses a picture");
    if (error != nullptr)
      error->clear();
    return true;
  }
  const auto table = std::find_if(
      lowered.begin(), lowered.end(), [](const auto &candidate) {
        return std::holds_alternative<TableBlockIR>(candidate.node);
      });
  if (table == lowered.end())
    return fail(error, "fixed table lowering has no table block");
  const auto &node = std::get<TableBlockIR>(table->node);
  if (node.rows.size() != block.body.size() ||
      node.header_rows != block.header_rows)
    return fail(error, "fixed table document rows differ from the block");
  const auto width = block.separator_columns.size() + 1;
  for (std::size_t index = 0; index < node.rows.size(); ++index) {
    const auto &row = node.rows[index];
    const auto &source = block.body[index];
    if (row.cells.size() != width || source.cells.size() != width)
      return fail(error, "fixed table document row width differs");
    for (std::size_t cell = 0; cell < width; ++cell) {
      std::size_t lines = 0;
      for (const auto &in : row.cells[cell].content)
        if (const auto *text = std::get_if<TextInlineIR>(&in.node)) {
          if (lines >= source.cells[cell].lines.size() ||
              text->text != source.cells[cell].lines[lines].text)
            return fail(error, "fixed table cell text is not conserved");
          ++lines;
        }
      if (lines != source.cells[cell].lines.size())
        return fail(error, "fixed table cell lines are not conserved");
    }
  }
  if (error != nullptr)
    error->clear();
  return true;
}

} // namespace geist::detail
