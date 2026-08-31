// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/layout/layout_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/container/provenance_ir.hpp"

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
  // No column structure was proven, but the envelope's display lines were:
  // `preformatted_lines` carries the lines and `body`/`separator_columns`
  // stay empty.  `box` and `gap` blocks also carry `preformatted_lines`,
  // because the rendering is verbatim for every geometry unless the source
  // declared a `:table`; the geometry says only what was proven about the
  // region's columns, never how it is rendered.
  preformatted,
};

// One display line of a preformatted SRTBL region.
struct FixedTablePreformattedLineIR {
  std::uint32_t logical_record = 0;
  std::size_t prefix_token = 0;
  std::size_t token_end = 0;
  // The line as the hosted reader shows it, trailing blanks removed.
  std::string text;
  std::vector<DocumentSourceRowIR> rows;
  // Record/segment/token/byte extent of the line's own tokens, so a rendered
  // preformatted line traces to the BOO bytes it was decoded from rather than
  // only to the enclosing SRTBL opcode.
  DocumentSourceSliceIR slice;
};

// A picture placed inside a fixed-layout region by a `cselect <c> <l>
// PIC<n>` selector.
//
// The compiler wrote the words `PICTURE <n>` into the region's display bytes
// where the picture belongs, and hosted BookServer replaces exactly the
// selector's columns with the image: GG24-395 3.3.8 `TBLUNIQ14`
// (DT 19941215160749) carries `cselect 3 11 PIC69` and the display line
// `    PICTURE 69     SystemView Host Management Facilities/VM ...`, and is
// served as
//
//   <a href="picture-69?mode=zoom"><img src=".../P69.GIF" alt="PICTURE 69">
//   </a>                SystemView Host Management Facilities/VM ...
//
// inside the topic's `<pre width="80">`: three spaces, the image, then the
// line's own text at column 19 -- the same 19 columns of leading whitespace
// the source line has once its placeholder words are removed.  GX27-3999-00
// 1.3 `NOSENVI` (DT 19950730184057) does the same four times inside one
// envelope, one icon per table row.
//
// So the region keeps its art and its picture both: the columns
// `[column, column + length)` of `line` are blanked in the reproduced text
// (which is then exactly hosted's `<pre>` line) and the picture is recorded
// here, to be rendered as an image beside the verbatim block.  Reproducing
// the placeholder words instead would spell `PICTURE 69` where hosted shows
// the image, which is how an earlier attempt lost the picture.
struct FixedTablePictureIR {
  // Resource catalog id, i.e. the digits of `PIC<n>` ("69").
  std::string resource;
  // The display words the region's bytes spell there ("PICTURE 69"), which
  // are also hosted's `alt` text.  Blanked out of the line text.
  std::string placeholder;
  // Index into `preformatted_lines` of the line the picture sits on.
  std::size_t line = 0;
  // Line-relative columns the selector covers, i.e. the blanked span.
  std::size_t column = 0;
  std::size_t length = 0;
  // The CSELECT opcode/operand extent.
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  DocumentSourceSliceIR source;
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
//
// Preformatted geometry is the region's default rendering, and is used
// whenever the envelope carries no `cz OFF TABLE` declaration -- whether or
// not a column model would have proven. The BOO file holds no table
// structure of its own: `SRTBLDBCTL51` (GG24-4302-00 10.2) is an object id
// followed by pre-rasterized character art (a 120-character box-rule run, the
// caption, more rule runs), with no column definitions and no cell
// boundaries. The compiler flattened `:table` markup into a fixed-width grid
// at build time, so the file holds a picture of a table; and many of these
// regions are not data grids at all but captured terminal screens (see
// `cz OFF SCREEN` in `doc/boo-spec/markup.adoc`), where any column inference would
// shred a widget such as OFCUSEOV 1.1's calendar into cells. An SRTBL
// envelope is, whatever it draws, a run of display lines of its logical
// records (`display_lines.hpp`: `<length byte><that many bytes of tokens>`),
// and hosted BookServer serves those lines verbatim inside `<pre>` -- box
// rules included. So an envelope with no provable columns is reproduced line
// for line instead of failing its topic. Evidence (hosted pages fetched
// 2026-08-29):
// - SC24-5527-02 3.6.2 `TBLUNIQ47`/`TBLUNIQ49` (DT=19921218151459): a single
//   command line wrapped in SRTBL, served as `   <B>vmfrec</B> <B>ppf</B>
//   <B>esa</B> <B>vmses</B>` -- one line, no second cell, not a table. The
//   trailing `.` the token reader shows is the spelling of the next line's
//   length byte and is not displayed.
// - SC24-5527-02 3.8.4.2 `TBLUNIQ99`: a VMFBLD2185R message listing, served
//   as 26 plain lines including its blank ones.
// - SC09-138 7.5.2 `TBLUNIQ116` (DT=19910321130500): a two-column box whose
//   caption line follows the bottom rule inside the envelope.
// - SC31-711 2.4.1-2.4.4 (DT=19941010174546): problem-determination forms
//   whose answer cells are ruled off with horizontal box words; hosted keeps
//   the unresolved `&ballot.` macro of 2.4.4 as source text.
// The region is admitted only when every record it touches parses into
// display lines, when SRTBL closes a line and SRETBL opens one (so the
// region is a whole number of lines), when every control lies on a line of
// its own carrying none of its payload, and when a non-blank line remains.
// A CSELECT inside the region that names a picture is admitted and recorded
// in `pictures` (see `FixedTablePictureIR`); any other CSELECT contributes
// its columns as text, because a fenced block carries no link.
// Rule lines, marker slots and blank lines are kept exactly as
// the reader prints them; `body`, `caption` and `separator_columns` stay
// empty and every positioned cell of the envelope's rows is claimed as
// `structural_cells`.
//
// Anything else fails closed and is reported as a decline with its reason.
struct FixedTableBlockIR {
  FixedTableGeometryIR geometry = FixedTableGeometryIR::box;
  // The envelope is delimited by a `cz OFF TABLE` layout directive.  That
  // directive is how the later BookMaster compiler records a source `:table`
  // whose column structure survived the build, and it is the only signal in
  // the file that separates a data grid from character art: hosted BookServer
  // emits an HTML `<table>` for exactly these regions and reproduces every
  // other fixed-layout region verbatim inside `<pre>`.
  //
  // Measured over all 861 corpus topics that produced a Markdown table
  // (2026-08-30, 32 books, hosted pages fetched per book at the DT of the
  // matching document number): `cz OFF TABLE` present and hosted `<table>`
  // 32, absent and no hosted `<table>` 826, present without hosted `<table>`
  // 3 (GX27-3999-00 A.0, SC41-485 1.2.4 and 1.3.4 -- hosted still marks the
  // region `<!-- table -->` and then falls back to `<pre>` on its own page
  // width).  No corpus topic serves an HTML table without the directive.
  bool source_declared_table = false;
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
  // Preformatted geometry only: the region's display lines in source order.
  std::vector<FixedTablePreformattedLineIR> preformatted_lines;
  // Pictures the region's CSELECT selectors place on those lines, in line
  // order.  Their placeholder words are blanked out of the line text.
  std::vector<FixedTablePictureIR> pictures;
  // Subject-index entries the envelope carries: a display line whose first
  // visible word is the `SI` keyword.  Hosted BookServer displays none of it
  // (SC09-138 4.1.4 DT=19910321130500 shows no `SI` byte at all although the
  // `LANG` envelope carries `SI ENGLISH run-time messages`,
  // `SI UENGLISH run-time messages` and `SI KANJI run-time messages`), so
  // the line is neither a table row nor preformatted text; `text` keeps the
  // entry verbatim so the term is not lost.  Admitted only when the line
  // owns no positioned display cell, i.e. it is nowhere in the layout.
  std::vector<FixedTablePreformattedLineIR> index_lines;
  // Rule lines, hidden marker slots between lines, and blank rows. Under
  // preformatted geometry this is every positioned cell of the region.
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
