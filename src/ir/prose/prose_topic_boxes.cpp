// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/prose/prose_topic_internal.hpp"

#include "geist/detail/ir/selector_link_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"

#include <algorithm>
#include <optional>

// Drawn box regions embedded in ordinary prose.
//
// A BookManager "screen" or highlighted-note box is not an `SRFIG` figure:
// it is drawn straight into the topic's display lines with the box-drawing
// words the decoder maps to U+2500..U+2518.  Hosted BookServer prints those
// display lines verbatim inside its `<pre width="80">` (verified line for
// line on QSYSNEWG 1.0 DT 19910524085706 and OFCUSEOV 1.10 DT
// 19900805103816), so the region lowers to a preformatted block whose rows
// are the hosted display text.
//
// The geometry is proven per region, never guessed:
//
//   * a top rule row: `U+250C` at column L, `U+2510` at column R > L,
//     nothing but spaces outside them (the interior carries the rule and,
//     when the box is labelled, the label: `___ In a Hurry? ___`);
//   * one or more side rows: `U+2502` (or the `U+251C`/`U+2524` junctions)
//     at exactly L and R; the interior is drawn content and may hold the
//     rails and junctions of an inner grid (SH20-918 3.0 record 140, a
//     syntax diagram);
//   * a bottom rule row: `U+2514` at L, `U+2518` at R, only rules, junctions
//     and spaces between them;
//   * an optional change bar (the ASCII `|` word the reader prints in the
//     left margin, OFCUSEOV record 152 token 2) before column L on any row;
//   * `cfont` control lines may stand between the rows (QSYSNEWG record 18
//     token 171); any other interleaved line ends the candidate.
//
// A candidate without a matching bottom rule is no closed region.  Its rows
// may still form an open drawing (`open_drawing` below): a blank-delimited
// run of display rows, one of which is drawn from box words alone.  Anything
// else keeps whatever disposition the line builder gives it.
namespace geist::detail::prose_internal {

namespace {

constexpr std::uint16_t box_top_left = 0x250C;
constexpr std::uint16_t box_top_right = 0x2510;
constexpr std::uint16_t box_bottom_left = 0x2514;
constexpr std::uint16_t box_bottom_right = 0x2518;
constexpr std::uint16_t box_rail = 0x2502;
constexpr std::uint16_t box_rail_left_tee = 0x251C;
constexpr std::uint16_t box_rail_right_tee = 0x2524;
constexpr std::uint16_t box_rule = 0x2500;

enum class BoxLineKind { none, top, side, bottom };

BoxLineKind classify(const std::vector<std::uint16_t>& columns, std::size_t& left,
                     std::size_t& right) {
  std::size_t start = 0;
  while (start < columns.size() && columns[start] == ' ') ++start;
  // The reader's change bar sits in the left margin, outside the box.
  if (start < columns.size() && columns[start] == '|') {
    ++start;
    while (start < columns.size() && columns[start] == ' ') ++start;
  }
  std::size_t end = columns.size();
  while (end > start && columns[end - 1] == ' ') --end;
  if (start >= end || end - start < 2) return BoxLineKind::none;
  left = start;
  right = end - 1;
  // The interior is drawn content: rules, nested rails and junctions of an
  // inner grid, the box label, and the row's text.  Only the corner and rail
  // columns carry the region's geometry.
  const auto rule_interior = [&]() {
    for (auto column = left + 1; column < right; ++column)
      if (columns[column] != ' ' && columns[column] != box_rule &&
          !box_word(columns[column]))
        return false;
    return true;
  };
  if (columns[left] == box_top_left && columns[right] == box_top_right)
    return BoxLineKind::top;
  if (columns[left] == box_bottom_left && columns[right] == box_bottom_right &&
      rule_interior())
    return BoxLineKind::bottom;
  const auto rail = [](std::uint16_t word, std::uint16_t tee) {
    return word == box_rail || word == tee;
  };
  if (rail(columns[left], box_rail_left_tee) &&
      rail(columns[right], box_rail_right_tee))
    return BoxLineKind::side;
  return BoxLineKind::none;
}

// The columns a row's box geometry is read from.  A revision mark the row
// stores in its left margin (`DisplayLineIR::margin_mark` at the row-margin
// place) stands outside the box like the ASCII change bar `classify` already
// skips, but it may be stored as the box word `U+2502` itself, which would
// otherwise read as the box's left rail.  SH45-5547-00 2.6 record 321 draws
// its `Note` box with the mark `U+2502` at column 1 of the top rule row
// (line 11, ` |  ___ Note ___...`, the one-byte token 0x5D at 0x25ACC, the
// top-left corner at column 3) and of both text rows (lines 13-14,
// ` | | If your installation ...  |`), the rails at columns 3 and 76;
// hosted BookServer (DT 19950203080455) prints the box verbatim with the
// marks in the margin.  The row's cells themselves are unchanged: the mark
// stays drawn in the region's rows.
std::vector<std::uint16_t> geometry_columns(std::vector<std::uint16_t> columns,
                                            const DisplayLineIR& line) {
  if (line.margin_mark &&
      line.margin_mark_place == MarginMarkPlaceIR::row_margin &&
      columns.size() > 1)
    columns[1] = ' ';
  return columns;
}

// Once a strict top rule has established the region's rails, body rows are
// classified at those coordinates rather than by trimming their own visible
// extent.  Native drawings legitimately annotate outside the rails:
// FA1PLMM0 9.3 continues a rounding explanation after its right rail, while
// SC24-5520 1.1.27 puts byte offsets before its left rail.  Those cells are
// part of the framed display row; they do not move the drawing's boundary.
BoxLineKind classify_at(const std::vector<std::uint16_t>& columns,
                        std::size_t left, std::size_t right) {
  if (left >= right || right >= columns.size()) return BoxLineKind::none;
  const auto rail = [](std::uint16_t word, std::uint16_t tee) {
    return word == box_rail || word == tee;
  };
  if (rail(columns[left], box_rail_left_tee) &&
      rail(columns[right], box_rail_right_tee))
    return BoxLineKind::side;
  if (columns[left] != box_bottom_left ||
      columns[right] != box_bottom_right)
    return BoxLineKind::none;
  for (auto column = left + 1; column < right; ++column)
    if (columns[column] != ' ' && columns[column] != box_rule &&
        !box_word(columns[column]))
      return BoxLineKind::none;
  return BoxLineKind::bottom;
}

struct Candidate {
  std::size_t record = 0;
  DisplayLineIR line;
  BoxLineKind kind = BoxLineKind::none;
  std::size_t left = 0;
  std::size_t right = 0;
  bool control_only = false;
  bool index = false;
  bool blank = false;
  std::string text;
  std::vector<std::uint16_t> columns;
};

// A segment the flattened splitter opened on a display line's length byte.
// That byte is the row-control slot, always and only, and position alone
// decides it: whatever control name the book's dictionary spells for the
// byte, the segment opens no control, and the line the byte frames is a row
// of display text.  GG24-4302-00 5.3.2.2 record 446 line 17 opens with the
// length byte `3C` (60) at 0x5DF16, which the dictionary spells
// `cbacklevel`; the splitter read the inline syntax diagram's main path
// after it (`>>__ _/DEQUEUE_ ...><`, `U+252C` junctions at columns 7, 18,
// 53 and 62) as that control's operands, so the diagram rule saw no text
// row.  Hosted DT 19950308184737 prints that row and the `|_/DEQ_____|`
// stack row under it, column for column.
bool length_byte_opcode(const DecodedLogicalRecordSource& record,
                        const ControlSegmentIR& segment) {
  return is_display_line_length_opcode(record, segment);
}

// A display line whose every token belongs to the opcode/operand range of a
// control: the line draws nothing and may stand inside a region.  A control
// that carries display text keeps that text in its payload tokens, which lie
// outside the opcode/operand range and therefore end the candidate.
//
// `CSELECT` is the shape that proves the generalisation: GC23-046 NOTICES
// (DT 19920330095121) draws a closed `U+250C .. U+2518` box whose fourth row
// is the whole line `cselect 43 26 HDRNOTICES`, and hosted BookServer prints
// the box with no line for the control -- the selector styles column 43 of
// the *next* row (`<a href="FRONT_1...">&quot;Notices&quot; in topic
// FRONT_1</a>`).  SC24-5527-02 3.10.4.4 (record 2039) and SC09-2417-00
// NOTICES draw the same shape.
//
// A cross-book `LNK` selector stores its destination as the `<...>`
// alternatives that follow its operands on the same line
// (doc/boo-spec/markup.adoc, "LNK selector alternatives"); they are the
// control's own metadata, never display text, so they draw nothing either.
// SC24-5527-02 1.0 record 59 draws its `Note` box around
// `cselect 47 30 LNK <BOOK> <> <> <SC24-5444> <ANY> <HCPA3>` (line 29), and
// hosted DT 19921218151459 prints the box unbroken with the selector's
// label linked on the next row.
bool control_only_line(const DecodedLogicalRecordSource& record,
                       const DisplayLineIR& line) {
  if (line.token_end <= line.prefix_token + 1) return false;
  for (auto token = line.prefix_token + 1; token < line.token_end; ++token) {
    const auto segment_index = segment_of(record, token);
    if (segment_index >= record.control_segments.size()) return false;
    const auto& segment = record.control_segments[segment_index];
    if (length_byte_opcode(record, segment)) return false;
    const auto operands = operand_tokens(record, segment);
    if (std::binary_search(operands.begin(), operands.end(), token)) continue;
    const auto alternatives =
        selector_link_alternative_tokens(record, segment, nullptr);
    if (std::find(alternatives.begin(), alternatives.end(), token) ==
        alternatives.end())
      return false;
  }
  return true;
}

// A subject-index display line: the line's first displayable token is the `SI`
// keyword.  Hosted BookServer displays no part of such a line -- it carries
// zero `SI` bytes -- so an index entry standing between two rows of a drawn
// box does not interrupt the box any more than a `cfont` control line does.
// The same reading is already the fixed-table block's
// (`fixed_table_block_ir.cpp`, preformatted `SI` lines).
//
// Evidence: QSYSNEWG record 80 draws the `What Are Entry Fields?` box from
// display line 14 (`    ___ What Are Entry Fields? ______ ...`) to its bottom
// rule, and lines 17/18 inside it are `SI field, field keys` and `SI keys,
// field`; hosted DT 19910524085706 serves the box unbroken and prints
// neither entry.  IEAC6MST record 63 line 27 opens the `Performance
// Consideration` box and lines 29/30 are `SI DASD (direct access storage
// device), performance ...` and `SI tape, performance in dump processing`;
// hosted DT 19920124000100 serves the same shape.
bool index_line(const DecodedLogicalRecordSource& record,
                const DisplayLineIR& line) {
  return is_subject_index_display_line(record, line);
}

// A display line made only of instructions the reader draws nothing for: a
// `cfont`/`cselect` control line or a subject-index entry.  These are the
// lines a native example already lets stand between its rows; any other
// control (a structural envelope marker, a layout directive, a title) is
// structure a drawing cannot contain.
bool hidden_drawing_line(const DecodedLogicalRecordSource& record,
                         const Candidate& candidate) {
  if (candidate.index) return true;
  if (!candidate.control_only) return false;
  for (auto token = candidate.line.prefix_token + 1;
       token < candidate.line.token_end; ++token) {
    const auto segment_index = segment_of(record, token);
    if (segment_index >= record.control_segments.size()) return false;
    const auto kind = record.control_segments[segment_index].kind;
    if (kind != BookControlKind::font && kind != BookControlKind::select)
      return false;
  }
  return true;
}

// A display line whose every token is display text: no token is the opcode,
// an operand or the `LNK` destination of a control.
bool display_row(const DecodedLogicalRecordSource& record,
                 const DisplayLineIR& line) {
  for (auto token = line.prefix_token + 1; token < line.token_end; ++token) {
    const auto segment_index = segment_of(record, token);
    if (segment_index >= record.control_segments.size()) continue;
    const auto& segment = record.control_segments[segment_index];
    if (segment.display_text || segment.kind == BookControlKind::text ||
        length_byte_opcode(record, segment))
      continue;
    const auto operands = operand_tokens(record, segment);
    if (std::binary_search(operands.begin(), operands.end(), token))
      return false;
    const auto alternatives =
        selector_link_alternative_tokens(record, segment, nullptr);
    if (std::find(alternatives.begin(), alternatives.end(), token) !=
        alternatives.end())
      return false;
  }
  return true;
}

constexpr std::uint16_t box_drawing_first = 0x2500;
constexpr std::uint16_t box_drawing_last = 0x257F;

// A row drawn from box-drawing words alone.  `joint` reports a corner, rail
// or junction among them; a row of nothing but `U+2500` is the reader's
// horizontal rule instead (prose_topic_lines.cpp, `display_rule_line`).
bool box_only_row(const std::vector<std::uint16_t>& columns, bool& joint) {
  joint = false;
  bool drawn = false;
  for (const auto word : columns) {
    if (word == ' ') continue;
    if (word < box_drawing_first || word > box_drawing_last) return false;
    drawn = true;
    joint = joint || word != box_rule;
  }
  return drawn;
}

// A labelled rule: one display row drawn as a corner, rule text, and the
// matching corner -- `U+250C` .. `U+2510` or `U+2514` .. `U+2518` -- with an
// optional change bar in front of it and no other box word than the rule
// `U+2500` between the corners.  BookMaster draws the `General-Use
// Programming Interface` / `End of ...` and `Change for ...` delimiters
// around a marked passage this way, each alone between empty display
// lines:
//
//   * SC33-033 4.132 record 813 line 23: `   ` + `U+2514` + attach +
//     `---------------` + `End of General-Use Programming Interface` +
//     `---------------` + attach + `U+2518` (DT 19930422134757);
//   * GC23-046 CHANGES.1 record 29 line 10: change bar, `U+250C`, rule text
//     `Change for Root Cause SPE`, `U+2510` (DT 19910711093248), and record
//     31 line 3, the matching `End of Change` row behind the same bar.
//
// Hosted BookServer prints each row column for column in its `<pre>`; the
// row is a drawing of its own, not a sentence of the paragraph around it.
bool labelled_rule_row(const std::vector<std::uint16_t>& columns) {
  std::size_t start = 0;
  while (start < columns.size() && columns[start] == ' ') ++start;
  if (start < columns.size() && columns[start] == '|') {
    ++start;
    while (start < columns.size() && columns[start] == ' ') ++start;
  }
  std::size_t end = columns.size();
  while (end > start && columns[end - 1] == ' ') --end;
  if (end <= start + 2) return false;
  const auto left = columns[start];
  const auto right = columns[end - 1];
  if (!((left == box_top_left && right == box_top_right) ||
        (left == box_bottom_left && right == box_bottom_right)))
    return false;
  bool text = false;
  for (auto column = start + 1; column + 1 < end; ++column) {
    const auto word = columns[column];
    if (word == box_rule || word == ' ') continue;
    if (box_word(word)) return false;
    text = true;
  }
  return text;
}

// An open native drawing: a run of display rows between two blank display
// lines that draws with box words but closes no box.  The closed region
// above proves rails at fixed columns; this shape has none to prove, and the
// display rows themselves are the evidence instead.  At least one row carries
// nothing but box-drawing words, among them a corner, rail or junction: no
// prose row is spelled that way, the row model has no reading for it (it is
// neither a marker slot in front of a row's text nor the rule-only `<hr>`
// line), and hosted BookServer prints the whole run column for column
// inside its `<pre>`.  Byte-level evidence on four books:
//
//   * IEAC6MST 2.1 record 83 lines 3..7, an ISPF panel excerpt with a top
//     rule and rails but no bottom rule.  Line 3 is the length byte (9), a
//     7-cell space run, `U+250C` (byte 0x06), a bare attach token, the
//     rule as `U+2500`x63 and `U+2500`x19 runs, and `U+2510` (DT
//     19920124000100).
//   * OFCUSEOV 1.1 record 47 line 37 to record 48 line 13, the lower half of
//     a panel: rails and a bottom rule, no top rule (DT 19900805103816).
//   * SC24-546 5.3 record 623 lines 16..20, a field template whose column
//     numbers and `FIELDS:` label stand outside the drawn cells; line 20 is
//     a 12-cell origin, `U+2514` and the rule with `U+2534` junctions
//     (DT 19940323131240).
//   * SC33-033 4.77 record 493 lines 26..30, a scale whose tick row is
//     `U+250C`/`U+252C`/`U+2510` over `U+2500` runs, with `cfont` lines
//     between the rows (DT 19930422134757).
//
// The run fails closed -- it is no region, and the line builder keeps its
// own disposition -- when any line in it is a control other than a
// `cfont`/`cselect`/`SI` line (an `SRTBL` envelope nested in a drawn box,
// SC24-5527-02 3.14.1 record 353 line 3, stays declined), a rule-only row,
// or a cell without a proven hosted glyph.
std::optional<BoxRegion> open_drawing(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::vector<Candidate>& candidates, std::size_t first,
    std::size_t last) {
  // Leading and trailing instruction lines address rows outside the run;
  // the line builder applies them as it does around a closed box.
  while (first <= last &&
         hidden_drawing_line(records[candidates[first].record],
                             candidates[first]))
    ++first;
  while (first <= last &&
         hidden_drawing_line(records[candidates[last].record],
                             candidates[last]))
    --last;
  if (first > last) return std::nullopt;
  bool drawn = false;
  std::size_t rows = 0;
  for (auto cursor = first; cursor <= last; ++cursor) {
    const auto& candidate = candidates[cursor];
    const auto& record = records[candidate.record];
    if (hidden_drawing_line(record, candidate)) continue;
    if (!display_row(record, candidate.line)) return std::nullopt;
    // Every cell must have a known hosted spelling.  An unmapped word, or a
    // box word whose hosted glyph is unproven (FA1PLMM0 C.0 record 781 line
    // 16 underlines its column heads with `U+255A` runs), would print the
    // decoder's `?` in the drawing.
    if (std::any_of(candidate.columns.begin(), candidate.columns.end(),
                    [](const auto word) {
                      return word == unmapped_word ||
                             (word != '?' && figure_display_glyph(word) == "?");
                    }))
      return std::nullopt;
    ++rows;
    bool joint = false;
    if (box_only_row(candidate.columns, joint)) {
      if (!joint) return std::nullopt;
      drawn = true;
    }
  }
  // A run that is one labelled rule and nothing else is drawn as well.
  if (!drawn && rows == 1)
    for (auto cursor = first; cursor <= last; ++cursor)
      if (!hidden_drawing_line(records[candidates[cursor].record],
                               candidates[cursor]))
        drawn = labelled_rule_row(candidates[cursor].columns);
  if (!drawn) return std::nullopt;
  BoxRegion region;
  for (auto cursor = first; cursor <= last; ++cursor) {
    const auto& candidate = candidates[cursor];
    if (candidate.index) {
      region.index_lines.push_back({candidate.record, candidate.line,
                                    candidate.text, candidate.columns});
      continue;
    }
    if (candidate.control_only) continue;
    region.lines.push_back(
        {candidate.record, candidate.line, candidate.text, candidate.columns});
  }
  region.begin_record = candidates[first].record;
  region.begin_token = candidates[first].line.prefix_token;
  region.end_record = candidates[last].record;
  region.end_token = candidates[last].line.token_end - 1;
  return region;
}

// The box words whose stroke leaves the cell downward, into the same column
// of the next drawn row, and those whose stroke enters it from above.
bool connects_down(std::uint16_t word) {
  switch (word) {
  case 0x2502: case 0x250C: case 0x2510: case 0x251C: case 0x2524:
  case 0x252C: case 0x253C:
    return true;
  default:
    return false;
  }
}
bool connects_up(std::uint16_t word) {
  switch (word) {
  case 0x2502: case 0x2514: case 0x2518: case 0x251C: case 0x2524:
  case 0x2534: case 0x253C:
    return true;
  default:
    return false;
  }
}

std::vector<std::size_t> stroke_columns(const std::vector<std::uint16_t>& columns,
                                        bool down) {
  std::vector<std::size_t> found;
  for (std::size_t column = 0; column < columns.size(); ++column)
    if (down ? connects_down(columns[column]) : connects_up(columns[column]))
      found.push_back(column);
  return found;
}

// Every cell of a drawn row needs a known hosted spelling (see
// `open_drawing`).
bool proven_glyphs(const std::vector<std::uint16_t>& columns) {
  return std::none_of(columns.begin(), columns.end(), [](const auto word) {
    return word == unmapped_word ||
           (word != '?' && figure_display_glyph(word) == "?");
  });
}

// An inline syntax diagram: a text row that carries a railroad's main path,
// with the stack of optional or alternative items hanging below it.  No row
// of it is drawn from box words alone, so neither the closed-box geometry
// nor `open_drawing` proves it, and the row model reflowed the main path into
// a paragraph while the stack row (which opens with box words) became a
// separate `<pre>`.  The proof is the strokes themselves: the main path
// carries a `U+252C` junction (hosted prints a blank) in every column where
// a branch leaves it, and the row below starts that branch with a `U+2514`,
// `U+251C` or `U+2502` in exactly that column.  SC24-546 3.14 record 238:
//
//   * line 23, `>>__PARSE VALUE LINEIN() WITH__ ___..___ __;__><`, stores
//     `U+252C` as tokens 267 and 271 (byte `0E`), columns 38 and 54;
//   * line 24 is `cfont 40 13 V`, which draws nothing;
//   * line 25, `|_template_list_|`, stores `U+2514 U+2500` (token 287) at
//     columns 38-39 and `U+2500 U+2518` (token 295) at columns 53-54.
//
// Hosted BookServer (DT 19940323131240) prints both rows verbatim, one under
// the other, with the main path's words in `<kbd>` and the item in `<var>`.
// SC09-138 1.1.2 and SC28-1881-05 1.2 draw the same shape (`U+2192` arrows
// for the terminators, stacks two rows deep joined by `U+251C`/`U+2524`).
//
// The run is the main row and every row below it that the strokes reach:
// each next row (only `cfont`/`cselect`/`SI` lines between) must enter from
// above in exactly the columns the row before leaves downward, and the run
// ends at the first row that leaves nothing downward.  The main row itself
// receives no stroke from above.  Anything else -- a blank or other control
// line where a stroke is still open, a stroke that lands on no junction, an
// unproven glyph -- is no diagram.
std::optional<BoxRegion> syntax_diagram(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::vector<Candidate>& candidates,
    const std::vector<bool>& claimed, std::size_t main, std::size_t& last) {
  const auto& head = candidates[main];
  if (claimed[main] || head.blank || head.control_only ||
      !display_row(records[head.record], head.line) ||
      !proven_glyphs(head.columns) || !stroke_columns(head.columns, false).empty())
    return std::nullopt;
  if (std::none_of(head.columns.begin(), head.columns.end(),
                   [](const auto word) { return word == 0x252C; }))
    return std::nullopt;
  // A text row: the main path carries words, not only strokes (a row of box
  // words alone is `open_drawing`'s to prove or decline).
  bool joint = false;
  if (box_only_row(head.columns, joint)) return std::nullopt;
  auto open = stroke_columns(head.columns, true);
  std::vector<std::size_t> rows{main};
  auto cursor = main;
  while (!open.empty()) {
    ++cursor;
    while (cursor < candidates.size() && !claimed[cursor] &&
           hidden_drawing_line(records[candidates[cursor].record],
                               candidates[cursor]))
      ++cursor;
    if (cursor >= candidates.size() || claimed[cursor]) return std::nullopt;
    const auto& row = candidates[cursor];
    if (row.blank || row.control_only ||
        !display_row(records[row.record], row.line) ||
        !proven_glyphs(row.columns) || stroke_columns(row.columns, false) != open)
      return std::nullopt;
    rows.push_back(cursor);
    open = stroke_columns(row.columns, true);
  }
  if (rows.size() < 2) return std::nullopt;
  BoxRegion region;
  for (auto at = main; at <= rows.back(); ++at) {
    const auto& candidate = candidates[at];
    if (candidate.index) {
      region.index_lines.push_back({candidate.record, candidate.line,
                                    candidate.text, candidate.columns});
      continue;
    }
    if (candidate.control_only) continue;
    region.lines.push_back(
        {candidate.record, candidate.line, candidate.text, candidate.columns});
  }
  region.begin_record = head.record;
  region.begin_token = head.line.prefix_token;
  region.end_record = candidates[rows.back()].record;
  region.end_token = candidates[rows.back()].line.token_end - 1;
  last = rows.back();
  return region;
}

constexpr std::uint16_t arrow_up = 0x2191;
constexpr std::uint16_t arrow_down = 0x2193;

// A vertical stroke cell an arrowhead can sit on: the rail `U+2502`, the
// ASCII bar a book may draw the same stroke with, or a box word whose stroke
// runs on toward the arrowhead.
bool vertical_stroke(std::uint16_t word, bool toward_below) {
  if (word == box_rail || word == '|') return true;
  return toward_below ? connects_up(word) : connects_down(word);
}

// An arrow drawing: a blank-delimited run of display rows that draws a
// vertical arrow -- an arrowhead whose shaft is a vertical stroke in the
// same column of the adjacent drawn row.  `U+2191` points up, so its shaft
// runs down into the next row; `U+2193` points down, so its shaft comes from
// the row above.  No row of such a drawing need be made of box words alone
// (open_drawing) or carry a railroad junction (syntax_diagram), yet hosted
// BookServer prints every row of it at its columns inside its `<pre>`, and a
// prose reading runs the rows together.  Evidence (absolute file offsets):
//
//   * DREICMST 1.5.5.4 record 162 lines 29..37, a collating-order listing:
//     each row draws `U+2502` at column 14 and the last, `      GEN     `,
//     ends with the two-byte word `CB 34` (`U+2193`) at 0x1B68D in that
//     column.  Hosted DT 19911219125856 prints the `|` column and ends the
//     `GEN` row with the display byte 0x19.
//   * SC09-138 B.0 record 1894 lines 5..12: `COPT(...)`, a row of six
//     `B0 3B` words (`U+2191`, the first at 0xFB401, column 11), then rows of
//     rails and `U+2514 .. U+2518` brackets; column 11 carries `U+2502` on
//     line 8 and line 10 and the `U+2514` corner on line 12.  Hosted DT
//     19910321130500 prints all eight rows, the arrows as `&quot;`.
//   * SC28-1881-05 1.29 record 701 lines 15..33: `TMARGIN value`, `C8 3A`
//     (`U+2191`) at 0x65A92 in column 41, ASCII `|` rows under it, the text
//     rows, `|` rows and `U+2193` above `BMARGIN value`, a `cfont 41 1 E`
//     line before each arrow and bar row.  Hosted DT 19930326130533 prints
//     every row, the arrow and bar cells in `<samp>`.
//
// The run is read exactly as `open_drawing` reads one -- only `cfont`,
// `cselect` and `SI` lines may stand between its rows, and every cell needs
// a proven hosted glyph -- with the stroked arrowhead as the proof in place
// of a row of box words alone.
std::optional<BoxRegion> arrow_drawing(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::vector<Candidate>& candidates, std::size_t first,
    std::size_t last) {
  while (first <= last &&
         hidden_drawing_line(records[candidates[first].record],
                             candidates[first]))
    ++first;
  while (first <= last &&
         hidden_drawing_line(records[candidates[last].record],
                             candidates[last]))
    --last;
  if (first > last) return std::nullopt;
  std::vector<std::size_t> rows;
  for (auto cursor = first; cursor <= last; ++cursor) {
    const auto& candidate = candidates[cursor];
    const auto& record = records[candidate.record];
    if (hidden_drawing_line(record, candidate)) continue;
    if (candidate.blank || candidate.control_only ||
        !display_row(record, candidate.line) ||
        !proven_glyphs(candidate.columns))
      return std::nullopt;
    rows.push_back(cursor);
  }
  bool stroked = false;
  for (std::size_t row = 0; row < rows.size() && !stroked; ++row) {
    const auto& columns = candidates[rows[row]].columns;
    for (std::size_t column = 0; column < columns.size() && !stroked;
         ++column) {
      const auto word = columns[column];
      if (word != arrow_up && word != arrow_down) continue;
      const bool up = word == arrow_up;
      if (up ? row + 1 >= rows.size() : row == 0) continue;
      const auto& shaft = candidates[rows[up ? row + 1 : row - 1]].columns;
      stroked = column < shaft.size() && vertical_stroke(shaft[column], up);
    }
  }
  if (!stroked) return std::nullopt;
  BoxRegion region;
  for (auto cursor = first; cursor <= last; ++cursor) {
    const auto& candidate = candidates[cursor];
    if (candidate.index) {
      region.index_lines.push_back({candidate.record, candidate.line,
                                    candidate.text, candidate.columns});
      continue;
    }
    if (candidate.control_only) continue;
    region.lines.push_back(
        {candidate.record, candidate.line, candidate.text, candidate.columns});
  }
  region.begin_record = candidates[first].record;
  region.begin_token = candidates[first].line.prefix_token;
  region.end_record = candidates[last].record;
  region.end_token = candidates[last].line.token_end - 1;
  return region;
}

} // namespace

std::vector<BoxRegion> plan_boxes(
    const std::vector<DecodedLogicalRecordSource>& records) {
  std::vector<Candidate> candidates;
  for (std::size_t index = 0; index < records.size(); ++index) {
    const auto& record = records[index];
    const auto parsed = record_display_lines(record);
    if (!parsed) return {};
    for (const auto& line : *parsed) {
      Candidate candidate;
      candidate.record = index;
      candidate.line = line;
      candidate.columns = display_line_columns(record, line);
      candidate.text = display_line_text(record, line);
      candidate.kind = classify(geometry_columns(candidate.columns, line),
                                candidate.left, candidate.right);
      candidate.index = index_line(record, line);
      candidate.control_only =
          control_only_line(record, line) || candidate.index;
      candidate.blank = candidate.columns.empty();
      candidates.push_back(std::move(candidate));
    }
  }
  std::vector<BoxRegion> regions;
  std::vector<bool> claimed(candidates.size(), false);
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    if (candidates[index].kind != BoxLineKind::top) continue;
    const auto left = candidates[index].left;
    const auto right = candidates[index].right;
    std::size_t sides = 0;
    std::size_t end = npos;
    for (auto cursor = index + 1; cursor < candidates.size(); ++cursor) {
      const auto& candidate = candidates[cursor];
      if (candidate.control_only || candidate.blank) continue;
      const auto kind = classify_at(candidate.columns, left, right);
      if (kind == BoxLineKind::side) {
        ++sides;
        continue;
      }
      if (kind == BoxLineKind::bottom) {
        end = cursor;
        break;
      }
      break;
    }
    if (end == npos || sides == 0) continue;
    BoxRegion region;
    for (auto cursor = index; cursor <= end; ++cursor) {
      const auto& candidate = candidates[cursor];
      if (candidate.index) {
        region.index_lines.push_back({candidate.record, candidate.line,
                                      candidate.text, candidate.columns});
        continue;
      }
      if (candidate.control_only) continue;
      region.lines.push_back(
          {candidate.record, candidate.line, candidate.text, candidate.columns});
    }
    region.begin_record = candidates[index].record;
    region.begin_token = candidates[index].line.prefix_token;
    region.end_record = candidates[end].record;
    region.end_token = candidates[end].line.token_end - 1;
    region.closed = true;
    regions.push_back(std::move(region));
    std::fill(claimed.begin() + static_cast<std::ptrdiff_t>(index),
              claimed.begin() + static_cast<std::ptrdiff_t>(end) + 1, true);
    index = end;
  }
  // Open drawings: runs of display lines that no closed region claimed,
  // delimited by blank lines and by list-item closers.  An `SRELIS<owner>`
  // line ends the list item the drawing belongs to, so the drawing cannot
  // run past it: OFCUSEOV 1.8 record 119 line 7 is `SRELISse` directly
  // under the last rail row of the open `Daily Calendar` panel that record
  // 118 line 24 opens, and hosted BookServer (DT 19900805103816) prints the
  // panel verbatim, nothing for the closer, and step 6 after it.  No other
  // control delimits a run; any other control line inside one still makes
  // the run no drawing.
  const auto delimiter = [&](const Candidate& candidate) {
    if (candidate.blank) return true;
    if (!candidate.control_only || candidate.index) return false;
    const auto& record = records[candidate.record];
    for (auto token = candidate.line.prefix_token + 1;
         token < candidate.line.token_end; ++token) {
      const auto segment = segment_of(record, token);
      if (segment >= record.control_segments.size()) return false;
      const auto opcode = ascii_lower(record.control_segments[segment].opcode);
      if (record.control_segments[segment].kind != BookControlKind::structural ||
          opcode.rfind("srelis", 0) != 0 || opcode.size() <= 6)
        return false;
    }
    return true;
  };
  for (std::size_t first = 0; first < candidates.size();) {
    if (claimed[first] || delimiter(candidates[first])) {
      ++first;
      continue;
    }
    auto last = first;
    while (last + 1 < candidates.size() && !claimed[last + 1] &&
           !delimiter(candidates[last + 1]))
      ++last;
    if (auto drawing = open_drawing(records, candidates, first, last)) {
      regions.push_back(std::move(*drawing));
      first = last + 1;
      continue;
    }
    // A run with a row of box words alone is `open_drawing`'s: when that
    // reading declined the run, no narrower reading of it is proven either.
    bool box_only = false;
    for (auto cursor = first; cursor <= last && !box_only; ++cursor) {
      bool joint = false;
      box_only = box_only_row(candidates[cursor].columns, joint) && joint;
    }
    bool diagram_seen = false;
    for (auto main = first; main <= last && !box_only; ++main) {
      std::size_t end = main;
      auto diagram = syntax_diagram(records, candidates, claimed, main, end);
      if (!diagram) continue;
      std::fill(claimed.begin() + static_cast<std::ptrdiff_t>(main),
                claimed.begin() + static_cast<std::ptrdiff_t>(end) + 1, true);
      regions.push_back(std::move(*diagram));
      diagram_seen = true;
      main = end;
    }
    // A run no narrower reading claimed may still draw a vertical arrow.
    if (!box_only && !diagram_seen)
      if (auto drawing = arrow_drawing(records, candidates, first, last))
        regions.push_back(std::move(*drawing));
    first = last + 1;
  }
  std::sort(regions.begin(), regions.end(),
            [](const BoxRegion& a, const BoxRegion& b) {
              return a.begin_record != b.begin_record
                         ? a.begin_record < b.begin_record
                         : a.begin_token < b.begin_token;
            });
  return regions;
}

std::optional<BoxRegion> implicit_grid_region(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::size_t header_record, std::size_t header_token, std::string* error) {
  const auto decline = [&](std::string message) -> std::optional<BoxRegion> {
    fail(error, "implicit two-column grid: " + std::move(message));
    return std::nullopt;
  };
  if (header_record >= records.size())
    return decline("header control has no source record");
  BoxRegion region;
  region.grid = true;
  bool header_seen = false;
  bool started = false;
  for (auto index = header_record; index < records.size(); ++index) {
    const auto& record = records[index];
    const auto* lines = record_display_lines(record);
    if (lines == nullptr)
      return decline("record " + std::to_string(record.logical_record) +
                     " has no display-line framing");
    for (const auto& line : *lines) {
      if (!header_seen) {
        if (header_token <= line.prefix_token || header_token >= line.token_end)
          continue;
        // The header `cfont` stands alone on its line: SC31-711 record 21
        // line 16 is `cfont 3 9 2 23 4 2 28 2 2 31 5 2` and line 17 the
        // header row `   Directory           Type of Files` it styles.
        if (!control_only_line(record, line))
          return decline("header control shares its display line");
        header_seen = true;
        continue;
      }
      Candidate candidate;
      candidate.record = index;
      candidate.line = line;
      candidate.columns = display_line_columns(record, line);
      candidate.text = display_line_text(record, line);
      candidate.index = index_line(record, line);
      candidate.control_only = control_only_line(record, line) || candidate.index;
      candidate.blank = candidate.columns.empty();
      if (!started) {
        // The region opens on the header row itself.
        if (candidate.control_only || candidate.blank)
          return decline("header control is not followed by its header row");
        started = true;
        region.begin_record = index;
        region.begin_token = line.prefix_token;
      }
      if (candidate.index) {
        region.index_lines.push_back({candidate.record, candidate.line,
                                      candidate.text, candidate.columns});
      } else if (candidate.control_only) {
        if (!hidden_drawing_line(record, candidate))
          return decline("control line '" + candidate.text +
                         "' inside the grid rows");
      } else {
        if (!display_row(record, line))
          return decline("row '" + candidate.text +
                         "' is not wholly display text");
        if (std::any_of(candidate.columns.begin(), candidate.columns.end(),
                        [](const auto word) {
                          return word == unmapped_word ||
                                 (word != '?' &&
                                  figure_display_glyph(word) == "?");
                        }))
          return decline("row '" + candidate.text +
                         "' holds a cell with no proven hosted glyph");
        region.lines.push_back({candidate.record, candidate.line,
                                candidate.text, candidate.columns});
      }
      region.end_record = index;
      region.end_token = line.token_end - 1;
    }
  }
  if (!started) return decline("header row not found");
  // Empty display lines after the last entry close the topic; hosted prints
  // nothing for them.  Their tokens stay inside the region's extent.
  while (!region.lines.empty() && region.lines.back().columns.empty())
    region.lines.pop_back();
  return region;
}

std::vector<BoxLine> drawn_box_rows(
    const std::vector<DecodedLogicalRecordSource>& records) {
  std::vector<BoxLine> rows;
  for (std::size_t index = 0; index < records.size(); ++index) {
    const auto& record = records[index];
    const auto* lines = record_display_lines(record);
    if (lines == nullptr) continue;
    for (const auto& line : *lines) {
      auto columns = display_line_columns(record, line);
      bool joint = false;
      if (!box_only_row(columns, joint) || !joint ||
          !display_row(record, line))
        continue;
      rows.push_back(
          {index, line, display_line_text(record, line), std::move(columns)});
    }
  }
  return rows;
}

// An `SRTBL<id>` ... `SRETBL` envelope drawn wholly inside a closed box is
// the box's own markup, not a table.  SC24-5527-02 7.4.5 records 675-676
// draw the `Obtaining File Type Abbreviations` note box: top rule record 675
// line 33, bottom rule record 676 line 23, and between them the side rows,
// two `SI` lines, `cfont` lines, `SRTBLTBLUNIQ492` alone on line 6 and
// `SRETBL` alone on line 20.  Hosted BookServer (DT 19921218151459) prints
// the box verbatim and emits no `<table>`: the opener becomes
// `<a name="TBLTBLUNIQ492">` around the first box row after it, and the
// closer prints nothing.  Topics 3.8.4.6, 3.10.4.4, 3.14.1 and 7.4.1 of the
// same book draw the same shape (`TBLTBLUNIQ114`, `TBLTBLUNIQ115`, ...).  The
// same reading already holds for an envelope inside a `cz OFF` verbatim
// region (prose_topic_spans.cpp).
//
// Only the proven shape qualifies: the box is closed (top and bottom rules
// at the same rails), every opener and closer inside it is a control line of
// its own, they alternate opener, closer, starting with an opener and ending
// with a closer, and every row an envelope spans draws nothing but the box's
// own two rails and text between them.  An envelope that draws structure of
// its own -- an inner rail, rule or junction -- is a genuine table inside a
// frame: ITPPIBOK 4.3.5.4 record 158 opens `SRTBLTBLUNIQ34` inside the
// `SRFIGSCHED` frame, and its rows are the `| TASK | START | ...` grid
// (`|_____|_____|` rules between them), which the table pass types.  An open
// drawing has no rails that bound it, so an envelope beside one stays a
// table envelope.
std::set<std::pair<std::size_t, std::size_t>> box_enclosed_envelopes(
    const std::vector<DecodedLogicalRecordSource>& records) {
  std::set<std::pair<std::size_t, std::size_t>> enclosed;
  for (const auto& region : plan_boxes(records)) {
    if (!region.closed) continue;
    std::vector<std::pair<std::size_t, std::size_t>> controls;
    std::vector<bool> opens;
    bool proven = true;
    for (auto record = region.begin_record;
         record <= region.end_record && proven; ++record) {
      const auto& source = records[record];
      for (const auto& segment : source.control_segments) {
        if ((segment.kind != BookControlKind::table_start &&
             segment.kind != BookControlKind::table_end) ||
            segment.source_tokens.empty())
          continue;
        const auto token = segment.source_tokens.front();
        if ((record == region.begin_record && token < region.begin_token) ||
            (record == region.end_record && token > region.end_token))
          continue;
        const auto* line = display_line_of_token(source, token);
        if (line == nullptr || !control_only_line(source, *line)) {
          proven = false;
          break;
        }
        controls.push_back({record, token});
        opens.push_back(segment.kind == BookControlKind::table_start);
      }
    }
    if (!proven || controls.empty() || opens.size() % 2 != 0) continue;
    for (std::size_t index = 0; index < opens.size(); ++index)
      if (opens[index] != (index % 2 == 0)) proven = false;
    if (!proven || region.lines.empty()) continue;
    std::size_t left = 0;
    std::size_t right = 0;
    if (classify(geometry_columns(region.lines.front().columns,
                                  region.lines.front().line),
                 left, right) != BoxLineKind::top)
      continue;
    const auto before = [](const std::pair<std::size_t, std::size_t>& a,
                           const std::pair<std::size_t, std::size_t>& b) {
      return a < b;
    };
    for (std::size_t pair = 0; pair + 1 < controls.size() && proven;
         pair += 2) {
      for (const auto& line : region.lines) {
        const std::pair<std::size_t, std::size_t> at{line.record,
                                                     line.line.prefix_token};
        if (!before(controls[pair], at) || !before(at, controls[pair + 1]) ||
            line.columns.empty())
          continue;
        if (classify_at(line.columns, left, right) != BoxLineKind::side) {
          proven = false;
          break;
        }
        for (auto column = left + 1; column < right; ++column)
          if (line.columns[column] >= box_drawing_first &&
              line.columns[column] <= box_drawing_last)
            proven = false;
      }
    }
    if (!proven) continue;
    enclosed.insert(controls.begin(), controls.end());
  }
  return enclosed;
}

// A bare `SR<id>` anchor alone on the display line directly above a closed
// box names the box.  SC24-546 4.3.21 record 375 line 12 is `SRSPTFDATE`
// (`SR` and the id only), line 13 the top rule of the `DATE` syntax-diagram
// box and line 20 its bottom rule.  The anchor's flattened segment runs on
// past its own line into the box's first two rows (lines 13 and 14, up to
// the `cfont` line 15 that opens the next segment).  Hosted BookServer (DT
// 19940323131240) serves `<a name="SPTFDATE">` wrapped around the top rule
// and prints all eight box rows verbatim; 3.14 (`SPTREF35`), 4.3.26
// (`SPTDIGF`), 4.3.32 (`SPTFORMF`), 4.3.34 (`SPTFUZZF`) and 4.3.62 (`SPTREF56`) draw the
// same shape.  Read as the anchor's padding, those rows vanished.
//
// Only the proven shape qualifies: the box is closed, the anchor's line is a
// control line of its own made of one structural control, it ends exactly
// where the top rule begins (no blank or other line between), and every
// payload token of the control lies within the box's rows.
std::set<std::pair<std::size_t, std::size_t>> box_leading_anchors(
    const std::vector<DecodedLogicalRecordSource>& records) {
  std::set<std::pair<std::size_t, std::size_t>> anchors;
  for (const auto& region : plan_boxes(records)) {
    if (!region.closed) continue;
    const auto& record = records[region.begin_record];
    const auto* lines = record_display_lines(record);
    if (lines == nullptr) continue;
    const auto above =
        std::find_if(lines->begin(), lines->end(), [&](const auto& line) {
          return line.token_end == region.begin_token;
        });
    if (above == lines->end() || !control_only_line(record, *above)) continue;
    const auto segment_index = segment_of(record, above->prefix_token + 1);
    if (segment_index >= record.control_segments.size()) continue;
    const auto& segment = record.control_segments[segment_index];
    const auto opcode = ascii_lower(segment.opcode);
    if (segment.kind != BookControlKind::structural || segment.malformed ||
        opcode.rfind("sr", 0) != 0 || opcode.size() <= 2 ||
        segment.source_tokens.empty())
      continue;
    bool proven = true;
    for (auto token = above->prefix_token + 1; token < above->token_end;
         ++token)
      if (segment_of(record, token) != segment_index) proven = false;
    const auto operands = operand_tokens(record, segment);
    for (const auto token : segment.source_tokens) {
      if (std::binary_search(operands.begin(), operands.end(), token))
        continue;
      if (token < region.begin_token ||
          (region.end_record == region.begin_record &&
           token > region.end_token))
        proven = false;
    }
    if (proven)
      anchors.insert({region.begin_record, segment.source_tokens.front()});
  }
  return anchors;
}

// An `SRFIG<id>` alone on the line directly above a closed box, with no
// `SREFIG` anywhere after it in the topic.  SC09-138 3.1.9 record 376 line
// 18 is `SRFIGFIGUNIQ30` (`02` at 0x837F6, then `DD A5`), lines 19-26 the
// closed RENAME syntax-diagram box, and no `SREFIG` follows before the topic
// ends.  Hosted BookServer (DT 19910321130500) wraps the box's top rule in
// `<a name="FIGFIGUNIQ30">`, prints the eight rows verbatim and continues
// with the prose after them: the opener names the box and opens no figure.
std::set<std::pair<std::size_t, std::size_t>> box_leading_figure_openers(
    const std::vector<DecodedLogicalRecordSource>& records) {
  std::set<std::pair<std::size_t, std::size_t>> openers;
  for (const auto& [record_index, token] : box_leading_anchors(records)) {
    const auto& record = records[record_index];
    const auto segment_index = segment_of(record, token);
    if (segment_index >= record.control_segments.size()) continue;
    const auto opcode =
        ascii_lower(record.control_segments[segment_index].opcode);
    if (opcode.rfind("srfig", 0) != 0 || opcode.size() <= 5) continue;
    // `SREFIG` may decode as a structural control or as the first word of
    // a text segment, so any later spelling of it in the topic's decoded
    // text counts as a terminator.
    bool terminated = false;
    for (auto scan = record_index; scan < records.size() && !terminated;
         ++scan) {
      const auto text =
          ascii_lower(token_words_to_ascii(records[scan].assembled.words));
      const auto from =
          scan == record_index
              ? std::min<std::size_t>(
                    record.control_segments[segment_index].opcode_range.end,
                    text.size())
              : std::size_t{0};
      terminated = text.find("srefig", from) != std::string::npos;
    }
    if (!terminated) openers.insert({record_index, token});
  }
  return openers;
}

} // namespace geist::detail::prose_internal
