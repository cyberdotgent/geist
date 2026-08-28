#pragma once

#include "geist/detail/layout_ir.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/provenance_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;

// Half-open range over the physical rows of a LayoutIR flattened in run
// order (row ordinal 0 is the first row of the first run). A topic body is
// partitioned by ordinals so a composer can hand the fixed-table block the
// rows between two prose spans and skip the rows every admitted block claims.
struct LayoutRowRangeIR {
  std::size_t begin = 0;
  std::size_t end = 0;
};

std::size_t count_layout_rows(const LayoutIR &layout);
// The (run, row) identity of one flattened ordinal, or nullopt when the
// ordinal is outside the layout.
std::optional<DocumentSourceRowIR> layout_row_at(const LayoutIR &layout,
                                                 std::size_t ordinal);

// One display line of one table cell. `text` is the visible words of the
// line inside the cell's column span with decoder spacing restored and the
// ends trimmed; `source_cells` are exactly the positioned non-space source
// cells that produced it.
struct FixedTableCellLineIR {
  std::string text;
  std::vector<PositionedRowCellIR> source_cells;
  // Visible words of the line that no physical row positioned (the Layout
  // IR drops a marker-started row whose payload was trimmed to a decoder
  // placeholder run, e.g. the `3` of `topic 4.3` before a box rule). They are
  // claimed from the source ledger instead, where they must be opaque.
  std::vector<OwnedSourceCellIR> unpositioned_cells;
  // Record/segment/token extent of the source cells above.
  DocumentSourceSliceIR slice;
};

struct FixedTableCellIR {
  std::size_t column = 0;
  // Non-blank display lines in source order. Blank lines inside a cell are
  // claimed as structural padding on the owning row.
  std::vector<FixedTableCellLineIR> lines;
};

enum class FixedTableRowKindIR {
  caption,
  header,
  body,
};

struct FixedTableRowIR {
  FixedTableRowKindIR kind = FixedTableRowKindIR::body;
  // Exactly one cell per table column; the caption row has one cell.
  std::vector<FixedTableCellIR> cells;
  // Borders, separators, origins, padding and hidden marker slots of this
  // row's display lines. Every positioned source cell of the row's lines is
  // either a cell source cell or here.
  std::vector<PositionedRowCellIR> structural_cells;
  // Physical rows that contributed any cell of this row, in layout order.
  std::vector<DocumentSourceRowIR> source_rows;
  std::size_t line_count = 0;
};

enum class FixedTableGeometryIR {
  // Box rules and junctions fix the geometry (`separator_columns` are the
  // interior border columns).
  box,
  // Rule-less table: cells are separated by gap columns that stay blank in
  // every display line (`separator_columns` are the start columns of the
  // second and later cells).
  gap,
};

// A fixed table recovered from one SRTBL ... SRETBL envelope, either
// box-drawn or rule-less (gap columns).
//
// Box geometry model (evidence: SC31-711 FRONT_1.1/4.0, GG24-4302-00 10.2,
// SC31-605 2.1/3.5, SC24-5527-02 1.5.1.1, FA1PLMM0 F.1 against the hosted
// BookServer pages):
// - Every display line of the envelope is `[hidden one-byte marker slot]*
//   [exact-space origin] <left border> ... <right border>`. Marker slots are
//   one-byte tokens whose decoded text is arbitrary (spaces, `<`, `)`, and
//   box glyphs such as U+251C/U+253C all occur); they are never displayed.
//   Display columns come from the assembled record's output map, relative
//   to the origin token, so decoder-inserted spaces are honoured.
// - The top rule (U+250C ... U+2510) fixes the box width; the junction
//   columns (U+252C/U+253C/U+2534) of the rules fix the column boundaries;
//   every content line must show U+2502 at exactly those columns. A single
//   spanning caption row directly under a junction-free top rule is the one
//   admitted exception.
// - A line's blank tail (trailing cells and the right border, or a rule's
//   right corner) may be omitted before a following control or a
//   logical-record boundary; a record boundary may also drop the marker
//   slot/origin of the line it opens. Both are restored from the box width
//   and left column. A reflow-off visual `|` marker between origin and
//   border is structural, as is the decoder placeholder glyph U+2666.
// - The first content row is the header when typed CFONT spans set every one
//   of its visible words in a bold face (`highlight_2`, `highlight_3`,
//   `bold_phrase`). Other known styles do not qualify: SC24-5520-00
//   3.8.1.10.2/3.8.1.11 open their CPED allocate-data boxes with an italic
//   row (`<I>VM architected area starts here</I>` on the hosted page), which
//   is emphasis inside a body row, not a column header.
//
// Gap geometry model, used when the envelope draws no box (no U+250C top
// rule). Evidence: SC33-033 PREFACE.6/4.6/4.7, QSYSINFO APPENDIX1.4.1.x,
// SC24-5527-02 3.8.4.2/2.2 against the hosted BookServer pages:
// - A display line is `[hidden one-byte marker slot | fill run |
//   control-only prefix token]* <exact-space origin> <content>`. The origin
//   is the last space run before same-segment content; every column is the
//   assembled output index relative to the origin's first space (a page
//   column). A line ends at the next origin pattern; a one-byte non-space
//   token directly before a control or the envelope end is a hidden
//   terminal slot (`,` before SRETBL in QSYSINFO, `?` before CFONT in
//   SC24-5527-02) and never content.
// - A line boundary is certain when a control lies between the previous
//   content and the origin, when two space runs meet (line fill + origin),
//   when the marker slots are box glyphs, or when a control-only prefix
//   token precedes a glyph marker (that token is a paragraph break: the
//   hosted page shows a blank line). A one-byte dictionary word followed by
//   one space run (`GDDM-` `PGF` 14 spaces `GDDM-PGF`, `table.` 1 space
//   `When`) is ambiguous: it opens a new line only when the run's length is
//   an established cell start column and the in-line reading is not.
// - Cell starts are the columns where some line starts a word and every
//   line leaves the two preceding columns blank; a table needs at least two
//   cells, and every content word must lie inside one cell. A reflow-off
//   revision bar (`|` directly after a one-space origin) is structural.
// - Rows: paragraph breaks separate line groups; inside a group whose first
//   line has first-cell content, every such line starts a row (continuation
//   lines with an empty first cell extend it); a group whose first line has
//   an empty first cell is one vertically centred row (SC24-5527-02
//   `vmfbld` command/explanation rows).
// Anything else fails closed and is reported as a decline with its reason.
struct FixedTableBlockIR {
  FixedTableGeometryIR geometry = FixedTableGeometryIR::box;
  LayoutRowRangeIR rows;
  std::vector<DocumentSourceRowIR> source_rows;
  // SRTBL operand, e.g. `TBLUNIQ1`, and the control's source position.
  std::string object_id;
  DocumentSourceSliceIR object_source;
  std::size_t left_column = 0;
  std::size_t width = 0;
  // Interior separator columns (line-relative), ascending.
  std::vector<std::size_t> separator_columns;
  std::optional<FixedTableRowIR> caption;
  std::size_t header_rows = 0;
  std::vector<FixedTableRowIR> body;
  // Rule lines, hidden marker slots between lines, and blank rows.
  std::vector<PositionedRowCellIR> structural_cells;
};

struct FixedTableDeclineIR {
  LayoutRowRangeIR rows;
  std::string object_id;
  std::string reason;
};

struct FixedTableBlocksIR {
  std::vector<FixedTableBlockIR> blocks;
  std::vector<FixedTableDeclineIR> declined;
};

// Extracts every SRTBL envelope whose physical rows lie inside `range`.
// Envelopes that cross the range boundary, own no rows, or fail the geometry
// are reported in `declined` with the reason; nothing is partially admitted.
FixedTableBlocksIR extract_fixed_table_blocks_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const LayoutRowRangeIR &range);

// Re-extracts the canonical blocks for `range` and checks that `blocks` is
// identical, that every claimed source cell exists in the positioned ledger
// with the same identity, role and column, and that each block claims every
// positioned cell of its rows exactly once.
bool verify_fixed_table_blocks_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const LayoutRowRangeIR &range, const FixedTableBlocksIR &blocks,
    std::string *error = nullptr);

std::string format_fixed_table_blocks_ir(const FixedTableBlocksIR &blocks);

} // namespace geist::detail
