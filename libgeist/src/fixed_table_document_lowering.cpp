#include "geist/detail/fixed_table_document_lowering.hpp"

#include <algorithm>
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
    content.push_back(std::move(text));
  }
  return content;
}

TableRowIR lower_row(const FixedTableRowIR &row) {
  TableRowIR lowered;
  lowered.origin = origin_for("fixed table row");
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

} // namespace

std::vector<BlockIR>
lower_fixed_table_block_to_document_ir(const FixedTableBlockIR &block) {
  std::vector<BlockIR> result;

  // Hosted BookServer names the table anchor after the whole SRTBL opcode
  // without its `SR` prefix: SC31-711 4.0 `SRTBLTBLUNIQ6` is served as
  // `<a name="TBLTBLUNIQ6">`, and cross references select the same spelling
  // (GG24-4302-00 10.2 `cselect ... TBLDBCTL51` for `SRTBLDBCTL51`).
  BlockIR anchor;
  anchor.node = AnchorBlockIR{"TBL" + block.object_id};
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
    PreformattedBlockIR body;
    BlockIR body_block;
    body_block.origin = origin_for("fixed table region: verbatim body");
    body_block.origin.slices.push_back(block.object_source);
    for (const auto &line : block.preformatted_lines) {
      body.lines.push_back(line.text);
      for (const auto &row : line.rows)
        add_row(body_block.origin, row);
    }
    body_block.node = std::move(body);
    result.push_back(std::move(body_block));
    return result;
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
  const auto expected = wrap(canonical);
  const auto actual = wrap(lowered);
  std::string verify_error;
  if (!verify_document_ir(expected, &verify_error))
    return fail(error, "fixed table lowering is not a valid document: " +
                           verify_error);
  if (format_document_ir(expected) != format_document_ir(actual))
    return fail(error, "fixed table document nodes differ from canonical "
                       "lowering");
  if (verbatim) {
    const auto body = std::find_if(
        lowered.begin(), lowered.end(), [](const auto &candidate) {
          return std::holds_alternative<PreformattedBlockIR>(candidate.node);
        });
    if (body == lowered.end())
      return fail(error, "preformatted table region has no body block");
    const auto &node = std::get<PreformattedBlockIR>(body->node);
    if (node.lines.size() != block.preformatted_lines.size())
      return fail(error, "preformatted table region lines are not conserved");
    for (std::size_t index = 0; index < node.lines.size(); ++index)
      if (node.lines[index] != block.preformatted_lines[index].text)
        return fail(error, "preformatted table region text is not conserved");
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
