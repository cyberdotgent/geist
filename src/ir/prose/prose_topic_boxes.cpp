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
    if (segment.display_text || segment.kind == BookControlKind::text)
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
    bool joint = false;
    if (box_only_row(candidate.columns, joint)) {
      if (!joint) return std::nullopt;
      drawn = true;
    }
  }
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
      candidate.kind =
          classify(candidate.columns, candidate.left, candidate.right);
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
    if (auto drawing = open_drawing(records, candidates, first, last))
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
    if (classify(region.lines.front().columns, left, right) != BoxLineKind::top)
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

} // namespace geist::detail::prose_internal
