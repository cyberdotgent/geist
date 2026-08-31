#include "geist/detail/ir/prose/prose_topic_internal.hpp"

#include "geist/detail/layout/display_lines.hpp"

#include <algorithm>

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
// A candidate without a matching bottom rule is not a region and the topic
// keeps whatever disposition it had.
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

struct Candidate {
  std::size_t record = 0;
  DisplayLineIR line;
  BoxLineKind kind = BoxLineKind::none;
  std::size_t left = 0;
  std::size_t right = 0;
  bool control_only = false;
  bool index = false;
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
bool control_only_line(const DecodedLogicalRecordSource& record,
                       const DisplayLineIR& line) {
  if (line.token_end <= line.prefix_token + 1) return false;
  for (auto token = line.prefix_token + 1; token < line.token_end; ++token) {
    const auto segment_index = segment_of(record, token);
    if (segment_index >= record.control_segments.size()) return false;
    const auto& segment = record.control_segments[segment_index];
    const auto operands = operand_tokens(record, segment);
    if (!std::binary_search(operands.begin(), operands.end(), token))
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
  for (auto token = line.prefix_token + 1;
       token < line.token_end && token < record.tokens.size(); ++token) {
    const auto& words = record.tokens[token];
    if (words.empty()) continue;
    if (std::all_of(words.begin(), words.end(), [](const auto word) {
          return word < 4 || word == ' ' || word == 0xA0;
        }))
      continue;
    return ascii_lower(token_words_to_ascii(words)) == "si";
  }
  return false;
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
      candidates.push_back(std::move(candidate));
    }
  }
  std::vector<BoxRegion> regions;
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    if (candidates[index].kind != BoxLineKind::top) continue;
    const auto left = candidates[index].left;
    const auto right = candidates[index].right;
    std::size_t sides = 0;
    std::size_t end = npos;
    for (auto cursor = index + 1; cursor < candidates.size(); ++cursor) {
      const auto& candidate = candidates[cursor];
      if (candidate.kind == BoxLineKind::none) {
        if (candidate.control_only) continue;
        break;
      }
      if (candidate.left != left || candidate.right != right) break;
      if (candidate.kind == BoxLineKind::side) {
        ++sides;
        continue;
      }
      if (candidate.kind == BoxLineKind::bottom) {
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
    regions.push_back(std::move(region));
    index = end;
  }
  return regions;
}

} // namespace geist::detail::prose_internal
