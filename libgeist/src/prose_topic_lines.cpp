#include "geist/detail/prose_topic_internal.hpp"

#include "geist/detail/display_lines.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace geist::detail::prose_internal {

struct LineBuilder {
  LineBuilder(const std::vector<DecodedLogicalRecordSource>& sources,
              const std::vector<Item>& stream, Ledger& owner, LineBuild& build,
              std::string* message)
      : records(sources), items(stream), ledger(owner), out(build),
        error(message) {}

  const std::vector<DecodedLogicalRecordSource>& records;
  const std::vector<Item>& items;
  Ledger& ledger;
  LineBuild& out;
  std::string* error;

  bool in_title = false;
  // True from the first word of the `ST` payload until the accumulated run is
  // long enough to decide any catalog title against it; the title itself stops
  // earlier, at the first display-row break.
  bool in_title_segment = false;
  // One marker slot at most stands between `ST` and the first title word.
  bool title_marker_seen = false;
  std::size_t title_run_rows = 0;
  bool title_done = false;
  bool in_index = false;
  // A structured subject-index entry occupies exactly one display line of its
  // record (`SI ??3HI1?0?Physical Planning Guide`, QSYSINFO 2.1.1 record 72;
  // `SI ??4XMP@?0?AGGREGATE?  compile-time option`, SC09-138 2.1.1.2 record
  // 132).  Hosted BookServer displays no part of such a line, exactly as it
  // displays no part of a plain `SI term` line, and the visible body text
  // that the flattened decoded string glues after the separator is a
  // separate display line (QSYSNEWG 2.1 record 40: `SI display station` is
  // one line, `| If your display station ...` the next).  Inside the index
  // line the decoder placeholders are the entry's field separators; the
  // fields themselves stay opaque.
  std::size_t index_record = npos;
  std::size_t index_line_end = npos;  // exclusive token end of the SI line
  ProseIndexTermIR current_term;
  std::vector<std::pair<std::size_t, std::size_t>> term_refs;
  bool pending_space = false;
  std::size_t trailing_bare = 0;
  bool anchor_pending = false;
  std::size_t pending_anchor_index = npos;
  std::vector<std::size_t> pending_controls;
  bool line_open = false;
  std::size_t line_visible_cells = 0;
  // Drawn box regions of the topic (prose_topic_boxes.cpp), admitted only
  // where no table/figure span already owns their tokens.
  std::vector<BoxRegion> boxes;
  // CZ dialect: the stream carries layout items; every display line belongs
  // to the most recent directive.
  bool cz_mode = false;
  // Inside `cz OFF XMP` .. `cz OFF EXMP`: the rows are verbatim example
  // text, not reflowed prose, so no visible token is a row-control slot.
  bool xmp_mode = false;
  // Inside `cz OFF COVER`/`cz OFF TIPAGE` .. their closers: the generated
  // title-page projection, whose rows the reader re-flows.  Its wide rows
  // carry the two-run leading whitespace the change-bar margin rule reads,
  // and their `CFONT` operands corroborate the column, so the rule applies
  // there even though it is otherwise off in the CZ dialect.
  bool title_page_mode = false;
  std::size_t current_directive = npos;

  Line& line() { return out.lines.back(); }

  bool is_token(std::size_t index) const {
    return index < items.size() && items[index].kind == ItemKind::token;
  }
  // Next token item after `index`, skipping controls, segment ends and bare
  // spacing tokens.
  std::size_t next_token(std::size_t index) const {
    for (auto cursor = index + 1; cursor < items.size(); ++cursor) {
      // A table/figure span is a hard boundary: no row geometry crosses it.
      if (items[cursor].kind == ItemKind::span) return npos;
      if (items[cursor].kind != ItemKind::token) continue;
      if (is_bare(items[cursor].token)) continue;
      return cursor;
    }
    return npos;
  }
  // True when a control-segment boundary separates the two item positions.
  bool segment_end_between(std::size_t from, std::size_t to) const {
    if (to == npos) to = items.size();
    for (auto cursor = from + 1; cursor < to && cursor < items.size(); ++cursor)
      if (items[cursor].kind == ItemKind::segment_end) return true;
    return false;
  }
  bool space_at(std::size_t index) const {
    return index != npos && is_token(index) && is_space_run(items[index].token);
  }
  bool visible_at(std::size_t index) const {
    return index != npos && is_token(index) && is_visible(items[index].token);
  }
  std::size_t run_length(std::size_t index) const {
    return items[index].token.body.size();
  }
  bool assign(const TokenView& view, ProseTokenRoleIR role) {
    return ledger.assign(view.record, view.token, role, error);
  }

  // The record decoder's stored display-line framing
  // (Format/logical-controls.md, "Display Lines Inside A Record Payload").
  const std::vector<DisplayLineIR>* display_lines_of(std::size_t record) const {
    return record_display_lines(records[record]);
  }
  // Exclusive token end of the display line whose first token is `token`, or
  // npos when the record's lines do not parse or `token` opens no line.
  std::size_t display_line_end_at(std::size_t record, std::size_t token) {
    const auto* lines = display_lines_of(record);
    if (lines == nullptr) return npos;
    for (const auto& line : *lines)
      if (line.prefix_token + 1 == token && line.token_end > token)
        return line.token_end;
    return npos;
  }

  // True when `view` is the length byte that opens a display line of its
  // record (Format/logical-controls.md, "Display Lines Inside A Record
  // Payload").  The byte's value is the line's byte count; the dictionary
  // word a token reader resolves it to is incidental, so a box-drawing or
  // geometric-shape spelling is not a drawn glyph.
  bool opens_display_line(const TokenView& view) const {
    return is_display_line_length_token(records[view.record], view.token);
  }

  // A bare token is a paragraph break only when it is the length byte of a
  // display line that draws nothing: an empty line is how the record spells
  // the break, and hosted BookServer answers it with `<p>`.  Bare spacing
  // markers *inside* a line are not breaks, and they are the only bare
  // tokens a hidden line contributes -- FA1PLMM0 17.2.3.1 record 713 places
  // four `SI` entries between the two display lines of one paragraph
  // (`... available to a` / `user; CEOS a subset ...`), and hosted DT
  // 19910927114801 serves that paragraph unbroken.
  bool blank_display_line(const TokenView& view) {
    const auto* lines = display_lines_of(view.record);
    if (lines == nullptr) return true;  // unparsed record: keep the old count
    for (const auto& line : *lines) {
      if (line.prefix_token != view.token) continue;
      for (auto token = line.prefix_token + 1; token < line.token_end; ++token)
        if (is_visible(view_token(records, view.record, token))) return false;
      return true;
    }
    return false;
  }

  // The display line that covers `token`, or nullptr.
  const DisplayLineIR* display_line_of(std::size_t record, std::size_t token) {
    const auto* lines = display_lines_of(record);
    if (lines == nullptr) return nullptr;
    for (const auto& line : *lines)
      if (token >= line.prefix_token && token < line.token_end) return &line;
    return nullptr;
  }

  // A displayed word already stands in front of `view` on its own display
  // line, so `view` is inside a drawn row rather than in front of one.  This
  // is the same corroboration `demote_display_line_owned_controls` applies to
  // a control-shaped word (Format/logical-controls.md, "A Control-Shaped Word
  // Inside A Row Is Display Text"), read here for box-drawing runs.
  bool display_word_precedes_in_line(const TokenView& view) {
    const auto* line = display_line_of(view.record, view.token);
    if (line == nullptr || view.token == line->prefix_token) return false;
    for (auto token = line->prefix_token + 1; token < view.token; ++token)
      if (is_visible(view_token(records, view.record, token))) return true;
    return false;
  }

  // A display line whose whole visible content is one `c.<xx>` body-control
  // opcode standing at the line origin, with at most one operand word after
  // it, is a body control line and draws nothing.  It reaches the line
  // builder as text only when the decoder lost the control boundary, and the
  // display-line structure is what restores it.
  //
  // Hosted BookServer prints no such line: SC33-033 PREFACE.1 (DT
  // 19930422134757) stores `c.cc 4` between the paragraph that ends
  // `... the following marking:` and the fence line, and the hosted page
  // shows only those two; GG24-4302-00 2.2.3 (DT 19950308184737) stores
  // `c.cc 12` and DREICMST 1.5.6.3 (DT 19911219125856) a bare `c.cp`, and
  // neither word appears in either hosted topic.
  bool body_control_line(std::size_t index) {
    const auto& view = items[index].token;
    const auto* line = display_line_of(view.record, view.token);
    if (line == nullptr || view.token != line->prefix_token + 1) return false;
    const auto text = ascii_lower(body_text(view));
    if (text.size() < 4 || text.compare(0, 2, "c.") != 0) return false;
    for (std::size_t at = 2; at < text.size(); ++at)
      if (std::islower(static_cast<unsigned char>(text[at])) == 0) return false;
    std::size_t visible = 0;
    std::size_t last = index;
    for (auto cursor = index + 1; cursor < items.size(); ++cursor) {
      if (items[cursor].kind == ItemKind::segment_end) continue;
      if (items[cursor].kind != ItemKind::token) return false;
      const auto& next = items[cursor].token;
      if (next.record != view.record || next.token >= line->token_end) break;
      if (is_visible(next) && ++visible > 1) return false;
      last = cursor;
    }
    for (auto cursor = index; cursor <= last; ++cursor) {
      if (items[cursor].kind != ItemKind::token) continue;
      const auto& owned = items[cursor].token;
      // Bare spacing tokens keep their `spacing` role from the skip walk.
      if (is_bare(owned)) continue;
      if (!assign(owned, is_visible(owned) ? ProseTokenRoleIR::control
                                           : ProseTokenRoleIR::padding))
        return false;
    }
    skip_until = last;
    return true;
  }

  // A display line whose whole visible content is box-rule words (`U+2500`)
  // is the reader's horizontal rule, not prose: hosted BookServer serves it
  // as `<hr>` and prints no character of it (ACPZMST1 COVER DT
  // 19920319123146 and DREICMST COVER DT 19911219125856 both draw the cover
  // frame as two such lines and both hosted pages carry `<hr>` in their
  // place).  The row draws no word, so the line builder emits no row and the
  // tokens stay structural.  Residual: the Document IR has no thematic-break
  // node, so the rule is dropped rather than lowered; the legacy renderer
  // drops it too, so no word and no rule is lost against it.
  bool display_rule_line(std::size_t index) {
    const auto& view = items[index].token;
    const auto* line = display_line_of(view.record, view.token);
    if (line == nullptr) return false;
    bool rule = false;
    for (auto token = line->prefix_token + 1; token < line->token_end;
         ++token) {
      const auto cell = view_token(records, view.record, token);
      if (is_bare(cell) || is_space_run(cell)) continue;
      if (!std::all_of(cell.body.begin(), cell.body.end(),
                       [](const auto word) { return word == 0x2500; }))
        return false;
      rule = true;
    }
    if (!rule) return false;
    if (!finish_title() || !finish_index()) return false;
    std::size_t last = index;
    for (auto cursor = index; cursor < items.size(); ++cursor) {
      if (items[cursor].kind == ItemKind::segment_end) continue;
      if (items[cursor].kind != ItemKind::token) break;
      const auto& owned = items[cursor].token;
      if (owned.record != view.record || owned.token >= line->token_end) break;
      // Bare spacing tokens keep their `spacing` role from the skip walk.
      if (!is_bare(owned) &&
          !assign(owned, is_space_run(owned) ? ProseTokenRoleIR::fill
                                             : ProseTokenRoleIR::marker))
        return false;
      last = cursor;
    }
    skip_until = last;
    line_open = false;
    pending_space = false;
    return true;
  }

  // The row's visual marker glyph stands directly before the row's text, or
  // before one gap run and then the text: GC23-046 2.3.2 record 48 draws the
  // change-bar row ` |     Â°   For SMP/E Reference:` as `|`, a four-cell gap
  // and the bullet, and hosted (DT 19920330095121) prints the bar in the
  // margin and the bullet in its own column.
  bool marker_precedes_row_text(std::size_t index) const {
    if (index + 1 >= items.size() ||
        items[index + 1].kind != ItemKind::token)
      return false;
    auto next = index + 1;
    if (is_space_run(items[next].token)) {
      const auto after = next_token(next);
      if (after == npos || !is_token(after)) return false;
      next = after;
    }
    return is_visible(items[next].token) &&
           !is_placeholder_run(items[next].token);
  }

  // Consumes a display line's length byte as the row-control slot of the row
  // it opens: it ends the row before it and, when a single space run follows
  // in front of the row's text, that run is the new row's origin.
  //
  // Hosted BookServer prints nothing for the byte: SC33-033 PREFACE.1 (DT
  // 19930422134757) record 18 token 219 spells `U+2666` in front of the
  // `c.cc 4` line and the hosted page runs `... by the following marking:`
  // straight into the next paragraph with no bullet, and FA1PLMM0 11.3.1
  // (DT 19910927114801) spells the same slot with a `U+2500` run.

  // The revision margin of one display line: the columns the line spends in
  // front of its first word when a change bar stands in that whitespace.
  struct Margin {
    std::size_t origin_item = npos;  // the space run that ends the margin
    std::size_t last_item = npos;    // last token of the margin
    std::size_t origin_cells = 0;    // display cells of the origin run
    std::size_t margin_cells = 0;    // columns before the origin run
  };

  // A change bar (`U+2502` or ASCII `|`) standing in the leading whitespace
  // of a display line is the reader's revision margin, not the row's first
  // word.  Where a record's display lines parse, that margin is exactly the
  // columns the line spends before its first word, so it is measured from
  // the line's own cells instead of guessed.
  //
  // Byte-level evidence.  OFCUSEOV record 839 opens display line 0 with the
  // length byte (value 22), a one-cell space (value 10), the ASCII bar
  // (value 135), a four-cell space run and the `U+2666` bullet; the line's
  // cells are ` `, `|`, the assembler's space, four origin cells, the bullet
  // at column 7, and the text at column 11.  Hosted 6.4.3 (DT
  // 19900805103816) serves ` |     °   Leave the prompt blank ...` -- the
  // same columns its unrevised sibling items carry, which is why the list
  // was rejected as misaligned while the bar column was lost.  ACPZMST1
  // record 459 line 11 stores the identical shape with a `U+2502` bar
  // (tokens 114..118: space, bar, four-cell origin, `XC`) and hosted 8.14.1
  // (DT 19920319123146) serves ` |     XC_NOTIFY_MSG, ...` for
  // `cfont 7 13 4`, the operand counting from column 0 of the margin.
  // SC24-546 6.2.11 (`cfont 4 6 9` on ` |     is 9, VM supports ...`) and
  // SC26-457 2.1/3.9.1.1/3.14.1.2 repeat it.
  //
  // The same measurement also settles a line with no bar at all but with
  // *two or more* leading space runs, which the row model would otherwise
  // read as the fill/origin pair that opens the next row; see the guard at
  // the end of the function for its conditions and title-page evidence.
  bool change_bar_margin_line(std::size_t index, Margin& margin) {
    const auto& view = items[index].token;
    const auto* lines = display_lines_of(view.record);
    if (lines == nullptr) return false;
    const DisplayLineIR* line = nullptr;
    for (const auto& candidate : *lines)
      if (candidate.prefix_token == view.token) line = &candidate;
    if (line == nullptr) return false;
    std::size_t bars = 0;
    std::size_t runs = 0;
    std::size_t text_token = npos;
    for (auto cursor = next_token(index); cursor != npos && is_token(cursor);
         cursor = next_token(cursor)) {
      const auto& cell = items[cursor].token;
      if (cell.record != view.record || cell.token >= line->token_end) break;
      if (is_space_run(cell)) {
        ++runs;
        margin.origin_item = cursor;
        margin.last_item = cursor;
        continue;
      }
      if (change_bar_slot(cell)) {
        if (++bars > 1) return false;
        margin.origin_item = npos;
        margin.last_item = cursor;
        continue;
      }
      // The first word of the line: a visible token the row displays.  A
      // placeholder run carries no character, so it never proves the margin.
      if (!is_visible(cell) || is_placeholder_run(cell)) return false;
      text_token = cell.token;
      break;
    }
    if (margin.last_item == npos || text_token == npos) return false;
    const auto cells = display_line_cells(records[view.record], *line);
    std::size_t text_column = npos;
    for (std::size_t column = 0; column < cells.size(); ++column)
      if (cells[column].token == text_token) {
        text_column = column;
        break;
      }
    if (text_column == npos || text_column == 0) return false;
    // Without a change bar the line's leading whitespace is a proven margin
    // only under three conditions together, because the row model already has
    // a rule for the ordinary shape and that rule must not move.
    //
    //  * The line spends **two or more** space runs before its first word.
    //    One run is the shape `row_control_length_byte` already reads below
    //    (`<length byte> <space run> <word>`) and reaches the same columns;
    //    two runs are what the model has no rule for, so they fall through to
    //    the fill/origin pair and the row loses everything the first run
    //    spans -- SC24-546 `TITLE`'s `cfont 66 7 2 74 3 2` then lands on a
    //    14-cell row instead of a 77-column one.
    //  * A control still waiting for its display text opens a span at that
    //    very column.  A `CFONT`/`CSELECT` operand addresses the display
    //    columns of exactly one display row, so a triple starting on the
    //    line's first word is the operand agreeing with the line.
    //  * The flattened dialect, **or** a CZ generated title-page projection.
    //    CZ rows otherwise carry their margins explicitly, and reading them
    //    off the line instead re-indents the verbatim rows of a `cz OFF XMP`
    //    listing (measured on SC09-2417-00 `4.1.9.4`, whose COBOL sample is
    //    one such region: its whole listing moved 10 columns left).  A
    //    `cz OFF COVER`/`cz OFF TIPAGE` region is not stored prose at all: it
    //    is the same generated title page the three flattened books below
    //    store, laid out one wide row per line, and it carries the identical
    //    two-run shape.  packet record 3 line 16 is the length byte (token
    //    77), a 63-cell run (78), a 3-cell run (79) and `Evie` (80) under
    //    `cfont 66 4 2 71 6 2`; without the rule the operand lands on a
    //    14-cell row.
    //
    // Byte-level, three books, each a title page stored as one wide row.
    // SC24-546 record 3 display line 17 is the length byte (token 83), a
    // 63-cell space run (84), a 3-cell space run (85) and `Release` (86), and
    // the `cfont 66 7 2 74 3 2` in front of it names column 66 -- exactly
    // where the line's own cells put `Release`.  N2AH1MST record 2 line 10 is
    // token 33, a 63-cell run (34), a 7-cell run (35) and `MVS` (36) under
    // `cfont 70 7 2`.  IBMMMSTR record 2 line 12 is token 57, a 63-cell run
    // (58), a 2-cell run (59) and `Programming` (60) under `cfont 65 12 2`.
    // Hosted serves all three rows at those columns (DTs 19940323131240,
    // 19910329000100, 19911004151140).
    if (bars != 1 && ((cz_mode && !title_page_mode) || runs < 2 ||
                      !pending_span_opens_at(text_column)))
      return false;
    margin.origin_cells = margin.origin_item == npos
                              ? 0
                              : items[margin.origin_item].token.body.size();
    if (margin.origin_cells > text_column) return false;
    margin.margin_cells = text_column - margin.origin_cells;
    return true;
  }
  bool row_control_length_byte(std::size_t index) {
    const auto& view = items[index].token;
    if (!finish_title() || !finish_index()) return false;
    if (!assign(view, ProseTokenRoleIR::marker)) return false;
    line_open = false;
    pending_space = false;
    Margin margin;
    if (change_bar_margin_line(index, margin)) {
      for (auto cursor = next_token(index);
           cursor != npos && cursor <= margin.last_item;
           cursor = next_token(cursor)) {
        const auto& cell = items[cursor].token;
        if (cursor == margin.origin_item) {
          if (!assign(cell, ProseTokenRoleIR::origin)) return false;
        } else if (!assign(cell, is_space_run(cell)
                                     ? ProseTokenRoleIR::fill
                                     : ProseTokenRoleIR::marker)) {
          return false;
        }
      }
      open_line(margin.origin_cells,
                margin.origin_item == npos ? nullptr
                                           : &items[margin.origin_item].token,
                margin.margin_cells);
      skip_until = margin.last_item;
      return true;
    }
    const auto next = next_token(index);
    if (space_at(next) && !space_at(next_token(next)) &&
        visible_at(next_token(next))) {
      const auto& origin = items[next].token;
      if (!assign(origin, ProseTokenRoleIR::origin)) return false;
      open_line(origin.body.size(), &origin);
      skip_until = next;
    }
    return true;
  }

  // True when the record's payload parses into length-prefixed display lines
  // and `token` is one line's length byte (Format/logical-controls.md,
  // "Display Lines Inside A Record Payload").  That byte is a real row
  // boundary whatever the reflow heuristics say.
  bool display_line_prefix_at(std::size_t record, std::size_t token) {
    return is_display_line_length_token(records[record], token);
  }
  // True when the record's display lines parse and `token` opens none of
  // them, so the token is inside a display line whatever the reflow
  // heuristics would make of it.  A record whose lines do not parse answers
  // false: without that structure there is no proof either way.
  bool inside_display_line(std::size_t record, std::size_t token) {
    if (display_lines_of(record) == nullptr) return false;
    return !display_line_prefix_at(record, token);
  }

  // The drawn box region covering `record`/`token`, or nullptr.
  const BoxRegion* box_at(std::size_t record, std::size_t token) const {
    for (const auto& region : boxes) {
      if (record < region.begin_record || record > region.end_record) continue;
      if (record == region.begin_record && token < region.begin_token) continue;
      if (record == region.end_record && token > region.end_token) continue;
      return &region;
    }
    return nullptr;
  }

  const BoxRegion* box_at_logical(std::uint32_t logical_record,
                                  std::size_t token) const {
    for (std::size_t record = 0; record < records.size(); ++record)
      if (records[record].logical_record == logical_record)
        return box_at(record, token);
    return nullptr;
  }

  // Emits one preformatted row per display line of the region and gives every
  // source token of the region its ledger role.
  bool emit_box(const BoxRegion& region) {
    if (!finish_title() || !finish_index()) return false;
    pending_controls.clear();
    line_open = false;
    pending_space = false;
    last_visible.clear();
    bool first = true;
    for (const auto& box_line : region.lines) {
      const auto& record = records[box_line.record];
      Line row;
      row.box = true;
      row.origin = 0;
      row.breaks_before = first ? trailing_bare : 0;
      row.directive = current_directive;
      if (first && anchor_pending) {
        row.anchor_before = true;
        row.anchor_index = pending_anchor_index;
        anchor_pending = false;
      }
      first = false;
      for (const auto& cell : display_line_cells(record, box_line.line)) {
        row.cells.push_back({cell.token == npos ? npos : box_line.record,
                             cell.token == npos ? 0 : cell.token,
                             figure_display_glyph(cell.word),
                             cell.word == ' '});
      }
      row.text_begin = 0;
      out.lines.push_back(std::move(row));
    }
    trailing_bare = 0;
    // A subject-index line inside the region draws nothing (hosted carries no
    // `SI` bytes), so it contributes no row -- but its words are an index
    // term, exactly as an `SI` line outside a box is.  Its tokens take the
    // index roles rather than `text`, which keeps the block-conservation
    // check honest: an unprinted word must not be claimed as prose.
    for (const auto& index_line : region.index_lines) {
      const auto& record = records[index_line.record];
      ProseIndexTermIR term;
      std::vector<std::pair<std::size_t, std::size_t>> refs;
      bool keyword = true;
      for (auto token = index_line.line.prefix_token + 1;
           token < index_line.line.token_end; ++token) {
        const auto view = view_token(records, index_line.record, token);
        if (is_bare(view)) {
          if (!assign(view, ProseTokenRoleIR::spacing)) return false;
          continue;
        }
        if (is_space_run(view)) {
          if (!assign(view, ProseTokenRoleIR::fill)) return false;
          continue;
        }
        if (keyword) {
          keyword = false;
          if (ascii_lower(body_text(view)) != "si")
            return fail(error, "box index line does not open with SI");
          if (!assign(view, ProseTokenRoleIR::index_keyword)) return false;
          continue;
        }
        if (!term.term.empty() && view.prefix != 0 && view.prefix != 1)
          term.term.push_back(' ');
        term.term += body_text(view);
        refs.push_back({index_line.record, token});
        if (!assign(view, ProseTokenRoleIR::index_term)) return false;
      }
      (void)record;
      term.term = collapse_ascii_whitespace(term.term);
      if (term.term.empty())
        return fail(error, "box index line has an empty index term");
      term.slices = slices_for(records, refs);
      out.index_terms.push_back(std::move(term));
    }
    // Every token from the top rule's length byte to the bottom rule's last
    // token belongs to the region.
    for (auto record = region.begin_record; record <= region.end_record;
         ++record) {
      const auto begin = record == region.begin_record ? region.begin_token : 0;
      const auto end = record == region.end_record
                           ? region.end_token
                           : records[record].ir.tokens.size() - 1;
      for (auto token = begin; token <= end; ++token) {
        if (ledger.at(record, token).role != ProseTokenRoleIR::unassigned)
          continue;
        const auto view = view_token(records, record, token);
        auto role = ProseTokenRoleIR::text;
        // Whether a token is a display line's length byte is the decoder's
        // decision, carried on the token; asking the box region's own line
        // list instead answers only for the lines the box drew.  A region
        // spans whole records, so it also covers the length bytes of the
        // lines *around* the box -- QSYSNEWG record 232 token 0 is the byte
        // 51 that opens the `cfont 12 4 2 ...` line above the drawn frame,
        // and its dictionary spelling is the ordinary word `any`.  Read as
        // text it becomes a word the reader never displays; the framing says
        // it is the row-control slot.
        if (display_line_prefix_at(record, token) || is_placeholder_run(view))
          role = ProseTokenRoleIR::marker;
        else if (is_bare(view))
          role = ProseTokenRoleIR::spacing;
        else if (is_space_run(view))
          role = ProseTokenRoleIR::fill;
        if (!ledger.assign(record, token, role, error)) return false;
      }
    }
    return true;
  }

  // The revision change bar (`U+2502` or ASCII `|`) that stands in a row's
  // marker slot, before the row's origin run.  BookServer prints it in the
  // three-column left margin it puts in front of every reflowed prose row
  // (` | ` for a revised row, `   ` otherwise), so the row's stored origin
  // run measures the indent *after* that margin and the columns a
  // CFONT/CSELECT operand names include it (Format/markup.md, "Spans And
  // The Display Row").
  static constexpr std::size_t change_bar_margin_cells = 3;
  static bool change_bar_slot(const TokenView& view) {
    return view.width == 1 && view.body.size() == 1 &&
           (view.body[0] == 0x2502 || view.body[0] == '|');
  }

  void open_line(std::size_t origin_cells, const TokenView* origin,
                 std::size_t margin_cells = 0) {
    Line fresh;
    fresh.origin = origin_cells + margin_cells;
    for (std::size_t cell = 0; cell < margin_cells; ++cell)
      fresh.cells.push_back({npos, 0, " ", true});
    fresh.breaks_before = trailing_bare;
    fresh.directive = current_directive;
    trailing_bare = 0;
    if (anchor_pending) {
      fresh.anchor_before = true;
      fresh.anchor_index = pending_anchor_index;
      anchor_pending = false;
    }
    if (origin != nullptr) {
      for (std::size_t word = 0; word < origin->body.size(); ++word)
        fresh.cells.push_back({origin->record, origin->token, " ", true});
      implied_origin = origin->body.size() + margin_cells;
    }
    fresh.text_begin = fresh.cells.size();
    out.lines.push_back(std::move(fresh));
    line_open = true;
    line_visible_cells = 0;
    pending_space = false;
  }

  bool ensure_line() {
    if (!line_open) open_line(0, nullptr);
    return true;
  }

  // A CFONT/CSELECT operand addresses display columns of one display row, so
  // a row that already carries a span reaching past its current cell count
  // has not ended: the wide space run in front of the next word is in-row
  // spacing, not a markerless row break.  Two-column definition rows are the
  // shape that proves it: ACPZMST1 3.6 stores `cselect 43 3 SPTUSERID` and
  // `cfont 3 6 2` for the single hosted row
  // `   Userid                   User ID (topic 4.3)`, and DREICMST 2.8.1
  // stores `cfont 3 3 2 7 4 2 17 3 2 21 4 2` for the single hosted row
  // `   RFT Name      Log Type`.
  bool span_continues_row() const {
    if (!line_open || out.lines.empty()) return false;
    const auto& current = out.lines.back();
    const auto reaches = [&](const std::vector<Span>& spans) {
      for (const auto& span : spans)
        if (span.end > current.cells.size()) return true;
      return false;
    };
    if (reaches(current.fonts) || reaches(current.links)) return true;
    // A control the row has already met but whose display text has not
    // arrived yet can address the same row: the second `cfont` of SC24-546
    // 4.3.1 is stored between the example and the `->` that follows it.  Only
    // a span that starts past the cells written so far proves that, because a
    // control standing at a row boundary introduces the *next* row and its
    // columns start again at that row's left margin.
    if (line_visible_cells == 0) return false;
    for (const auto control : pending_controls) {
      const auto& item = items[control];
      if (item.kind == ItemKind::font) {
        for (const auto& span : item.spans)
          if (span.column >= current.cells.size() && span.length != 0)
            return true;
      } else if (item.column >= current.cells.size() && item.length != 0) {
        return true;
      }
    }
    return false;
  }

  // True when a control that is still waiting for its display text covers
  // exactly the `cells` display columns starting at `column`.  An exact
  // match is what proves a glyph is styled display text; a span that merely
  // starts there could still be a span over the row's first word.
  // True when a control still waiting for its display text opens a span at
  // exactly `column`.
  bool pending_span_opens_at(std::size_t column) const {
    for (const auto control : pending_controls) {
      const auto& item = items[control];
      if (item.kind == ItemKind::font) {
        for (const auto& span : item.spans)
          if (span.column == column && span.length != 0) return true;
      } else if (item.column == column && item.length != 0) {
        return true;
      }
    }
    return false;
  }

  bool pending_span_covers(std::size_t column, std::size_t cells) const {
    for (const auto control : pending_controls) {
      const auto& item = items[control];
      if (item.kind == ItemKind::font) {
        for (const auto& span : item.spans)
          if (span.column == column && span.length == cells) return true;
      } else if (item.column == column && item.length == cells) {
        return true;
      }
    }
    return false;
  }

  // Finishes the ST title at the first structural boundary.
  bool finish_title() {
    if (!in_title) return true;
    in_title = false;
    title_done = true;
    out.title = collapse_ascii_whitespace(out.title);
    if (out.title.empty()) return fail(error, "ST title is empty");
    return true;
  }

  // True when the item after `index` is another token of the display line the
  // open `SI` keyword started.
  //
  // A subject-index entry occupies exactly one display line and draws
  // nothing, so the record's own framing bounds its term.  The decoded-string
  // splitter, which knows nothing of that framing, opens a new segment
  // wherever a term word is spelled like a control, and so cuts the term away
  // from the keyword that owns it: SH12-565 4.7.5.1 record 374 display line
  // 15 is `SI SRVMODE, server initialization parameter` over tokens 90..95
  // and is cut before `SRVMODE` at token 91 (lines 14, 16 and 17 are the same
  // shape), and APPENDIX1.5.9.2 record 700 display line 15 is `SI SRCVPAC`
  // over tokens 111..112, cut before `SRCVPAC`.  Hosted (SH12-5657-04 DT
  // 19941206115523) displays no part of any of those lines, so no word of
  // them is body text.  Such a boundary is the splitter's, not the encoder's,
  // and it may not end the term; the line end does that, in `token()`.
  //
  // Fail closed: only a *token* may continue the term.  A font or selector
  // control, an anchor, a span or a layout directive inside the line is real
  // structure the index model does not claim, so it still ends the term and
  // the topic still declines when the term is then empty.
  bool index_line_open_after(std::size_t index) const {
    if (!in_index || index_record == npos || index_line_end == npos)
      return false;
    for (auto next = index + 1; next < items.size(); ++next) {
      if (items[next].kind == ItemKind::segment_end) continue;
      if (items[next].kind != ItemKind::token) return false;
      const auto& view = items[next].token;
      return view.record == index_record && view.token < index_line_end;
    }
    return false;
  }

  bool finish_index() {
    if (!in_index) return true;
    in_index = false;
    index_record = npos;
    index_line_end = npos;
    current_term.term = collapse_ascii_whitespace(current_term.term);
    current_term.slices = slices_for(records, term_refs);
    if (current_term.term.empty())
      return fail(error, "SI control has an empty index term");
    out.index_terms.push_back(std::move(current_term));
    current_term = {};
    term_refs.clear();
    return true;
  }

  void append_space_cells(const TokenView& view, bool literal_gap) {
    (void)literal_gap;
    for (std::size_t word = 0; word < view.body.size(); ++word)
      line().cells.push_back({view.record, view.token, " ", true});
  }

  // Accumulates the `ST` payload's visible word run (see LineBuild::title_run).
  void append_title_run(const TokenView& view) {
    if (!in_title_segment) return;
    // Words on different display rows are separate words; inside one row the
    // token's own attach prefix decides, exactly as for the row cells.
    const auto row_changed = out.lines.size() != title_run_rows;
    title_run_rows = out.lines.size();
    if (!out.title_run.empty() &&
        (row_changed ||
         (pending_space && view.prefix != 0 && view.prefix != 1)))
      out.title_run.push_back(' ');
    out.title_run += body_text(view);
    // The run only has to outlast the longest catalog title in the corpus;
    // no catalog title reaches this length.
    if (out.title_run.size() > 512) in_title_segment = false;
  }

  bool append_visible(const TokenView& view, ProseTokenRoleIR role) {
    append_title_run(view);
    if (!ensure_line()) return false;
    if (pending_space && view.prefix != 0 && view.prefix != 1)
      line().cells.push_back({npos, 0, " ", true});
    for (const auto word : view.body)
      // Inside a verbatim region every column is content, and a token there
      // may mix drawn box words with ordinary ones: SC09-2417-00 `3.1.1.2`
      // record 348 token 129 is `U+250C U+2500 *`, the corner and rule that
      // open the `_*LIBL/________` branch of a railroad diagram.  The
      // one-byte ASCII projection spells a box word `?`, which is a fallback
      // for text the reader has no character for -- but the display line
      // has a character for it, and it is the character hosted serves
      // (`&gt;&gt;__<kbd>STATEMENT</kbd>__`).  Draw it.
      line().cells.push_back(
          {view.record, view.token,
           xmp_mode && box_word(word) ? figure_display_glyph(word)
                                      : word_text(word),
           word == ' '});
    ++line_visible_cells;
    last_visible = body_text(view);
    pending_space = view.prefix != 2;
    if (!assign(view, role)) return false;
    if (role == ProseTokenRoleIR::text && !pending_controls.empty()) {
      for (const auto control : pending_controls) {
        const auto& item = items[control];
        if (item.kind == ItemKind::font) {
          for (const auto& span : item.spans)
            line().fonts.push_back(
                {span.column, span.column + span.length, span.style, {}});
        } else {
          line().links.push_back({item.column, item.column + item.length,
                                  FontStyleIR::unknown, item.target,
                                  item.target_kind, item.picture});
        }
      }
      pending_controls.clear();
    }
    return true;
  }

  // Classifies the token at `index` as a one-byte marker slot when it is
  // followed by exactly one origin run of three or more cells and then the
  // next line's first visible token.  A width-one word followed by a fill
  // run and another marker is text (the row's last word).
  // A standalone layout glyph (`(`, `)`, `-`, `<`, `>`, `/`, `=`, `"`...)
  // before any line break is a marker in both shapes; attached punctuation
  // (`conditions:`, `useful.`) and words are visible when a fill run
  // separates them from the origin.
  static bool ballot_token(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(),
                                        [](char ch) { return ch == '_'; });
  }

  static bool alpha_word(const TokenView& view) {
    return !view.body.empty() &&
           std::all_of(view.body.begin(), view.body.end(), [](const auto word) {
             return word < 0x80 && std::isalpha(static_cast<int>(word)) != 0;
           });
  }

  static bool alnum_word(const TokenView& view) {
    return !view.body.empty() &&
           std::all_of(view.body.begin(), view.body.end(), [](const auto word) {
             return word < 0x80 && std::isalnum(static_cast<int>(word)) != 0;
           });
  }

  std::string last_visible;
  // Origin of the most recent row opened by an explicit origin run; an
  // implied row break (word + lone run + word) reuses it.
  std::size_t implied_origin = 3;

  std::string where(const TokenView& view) const {
    return "record " +
           std::to_string(records[view.record].logical_record) + " token " +
           std::to_string(view.token);
  }

  static bool punctuation_glyph(const TokenView& view) {
    return !view.body.empty() &&
           std::all_of(view.body.begin(), view.body.end(), [](const auto word) {
             return word < 0x80 && std::ispunct(static_cast<int>(word)) != 0;
           });
  }

  // Display column the next visible token would occupy on the open row.
  std::size_t next_column(const TokenView& view) const {
    if (!line_open || out.lines.empty()) return npos;
    const auto extra =
        pending_space && view.prefix != 0 && view.prefix != 1 ? 1u : 0u;
    return out.lines.back().cells.size() + extra;
  }

  // True when the open row already carries a CFONT span over the column the
  // token would occupy.
  bool covered_by_font_span(const TokenView& view) const {
    const auto column = next_column(view);
    if (column == npos) return false;
    for (const auto& span : out.lines.back().fonts)
      if (column >= span.begin && column < span.end) return true;
    return false;
  }

  // Widest display row completed so far.  CZ rows are justified to one
  // width, so the widest finished row is the row width of the topic.
  std::size_t widest_row() const {
    std::size_t widest = 0;
    for (std::size_t seen = 0; seen + 1 < out.lines.size(); ++seen)
      widest = std::max(widest, out.lines[seen].cells.size());
    return widest;
  }

  // True when the space run at `space_index`, preceded by `extra_cells` of
  // visible text still to be appended to the open row, keeps the following
  // word inside the topic's row width.  Such a run is an in-row
  // justification gap rather than an implied row break: packet 1.1 keeps
  // `PRNET,   and   SATNET   (a   satellite ...` on one 77-cell row.
  bool run_fits_row(std::size_t extra_cells, std::size_t space_index) const {
    if (!cz_mode || !line_open || out.lines.empty()) return false;
    const auto widest = widest_row();
    if (widest < 40) return false;
    const auto after = next_token(space_index);
    if (!visible_at(after)) return false;
    const auto width = out.lines.back().cells.size() +
                       (pending_space ? 1u : 0u) + extra_cells +
                       items[space_index].token.body.size() +
                       items[after].token.body.size();
    return width <= widest;
  }

  bool marker_at(std::size_t index, std::size_t& origin_index) const {
    const auto& view = items[index].token;
    if (view.width != 1 || !is_visible(view)) return false;
    // The row-control slot of a row is the length byte that opens its
    // display line (Format/logical-controls.md).  Where the record's display
    // lines parse, a token inside a line is never the slot, however much its
    // token geometry looks like one: FA1PLMM0 17.2.3.1 record 713 ends the
    // display line `   CEOS.  CEMS makes ... available to a` with the
    // one-byte word `a`, which the geometry rule took for the slot and the
    // *next* line's length byte for its origin run -- hosted DT
    // 19910927114801 serves `available to a user; CEOS a subset ...`.
    if (display_lines_of(view.record) != nullptr && !opens_display_line(view))
      return false;
    // Example blocks style every displayed word with the block's `CFONT`
    // spans (hosted `<samp>...</samp>` per word), so a one-byte token that
    // falls inside a span of the open row is display text and not a row
    // slot: SC09-2417-00 4.5.2.2 `void payroll::calc (employee *pe) {`
    // covers the trailing `{`, while the `;` that ends the next row is
    // covered by no span and stays the slot.
    if (xmp_mode && covered_by_font_span(view)) return false;
    // CZ dialect: a compact one-byte token is a whole dictionary word
    // (`and`, `a`, `protocol`) displayed wherever it stands, and the rows are
    // justified, so a space run behind it is an in-row gap or a plain row
    // break (packet 1.1 `PRNET,   and   SATNET`, packet 3.2 `... to  send
    // and` + 10 spaces + `receive`).  Only glyphs and placeholder slots mark
    // rows here.  Residual: packet 3.2 record 84 `NET/ROM` + `an` + fill +
    // origin, where hosted drops `an`; no positioned distinction separates it
    // from the cases above yet.
    if (cz_mode && alnum_word(view)) return false;
    const auto space = next_token(index);
    // The ST payload is one control segment, and hosted BookServer serves it
    // as the topic's own heading element, so no display row runs from the
    // title into the body.  Reading a marker/origin pair across the segment
    // end steals the title's last token: SC24-546 E.2 record 1169 ends the
    // `ST` segment with `)` (token 35), and the fill/origin run that follows
    // belongs to the next segment's first row, which turned the heading into
    // `The File Block (FBLOCK`.  Hosted DT 19940323131240 serves
    // `<H2> E.2   The File Block (FBLOCK)</H2>`.
    // Only a punctuation glyph is rescued this way: a one-byte token spelling
    // a whole dictionary *word* is the documented compact-marker collision
    // (Format/markup.md) and closes the title wherever it stands -- FA1PLMM0
    // I.6.1 record 254 token 33 (`access`, encoded value 43), SC24-5520-00
    // 3.7.5.2 (`and`) and SH20-918 3.33.14 (`an`) all end their heading there.
    if (in_title && !alnum_word(view) && segment_end_between(index, space))
      return false;
    if (!space_at(space)) return false;
    if (space_at(next_token(space))) {
      // Fill/origin pair: a standalone glyph is a marker here, and so is a
      // one-byte alphanumeric piece glued (no space) onto a preceding word:
      // genuine text never joins two alphanumeric pieces without
      // punctuation (FA1PLMM0 record 1133 `Messages` + `access`).
      const auto attached =
          !pending_space || view.prefix == 0 || view.prefix == 1;
      // The first visible token of a freshly opened row stands at the row
      // origin, so it is glued to nothing even though no pending space
      // separates it: ACPZMST1 6.2 record 305 stores `cfont 4 4 R,` + a
      // four-cell origin run + the one-byte word `GUPI` + a fill/origin
      // pair, and hosted serves that row as `    <B>GUPI</B>`.
      const auto row_origin_word = line_open && line_visible_cells == 0;
      // A glued one-byte word in the row-control byte range is the slot
      // whatever precedes it: N2AH1MST PREFACE.4 `to:` + `access` (0x1c),
      // `Reference.` + `an` (the compact-marker collision in Format/markup.md).
      // "Glued onto a preceding word" needs that word on the *open* row: a
      // one-byte token that opens a row after its own origin run is the
      // row's text, whatever the previous row ended with.  QS3X36CM EDITION
      // record 3 lists the trademark `400` on its own row directly after
      // `RPG/400` (hosted DT 19910524075122 prints `   RPG/400` then
      // `   400`), and `400` (encoded value 219) is above the row-control
      // range.
      const auto glued_word =
          attached && !row_origin_word && alnum_word(view) &&
          ((!last_visible.empty() &&
            std::isalnum(static_cast<unsigned char>(last_visible.back())) !=
                0) ||
           view.value < row_control_byte_limit);
      // A free-standing width-1 token in the row-control byte range is the
      // row's slot whatever its dictionary spelling.  XWEBDEMO 1.4.2-1.4.4
      // open every external-link row with `<INTERNET>` (byte 12), `<OTHER>`
      // (13) or `<IMAGE>` (11) before the row's fill/origin pair; hosted
      // BookServer prints none of them (`<a href="http://www.ibm.com/">The
      // IBM Home Page</a>.` is the whole row of 1.4.4).
      // Restricted to a token that stands before any row is open: inside an
      // open row such a word is display text that the row's own CFONT and
      // CSELECT columns count (ITPPIBOK 2.5, SH12-565 3.1.5).
      const auto control_byte_slot =
          attached && !line_open && !in_title && !in_index &&
          !alnum_word(view) && !punctuation_glyph(view) &&
          view.value < row_control_byte_limit;
      if (!(punctuation_glyph(view) && !attached) && !glued_word &&
          !control_byte_slot)
        return false;
      auto last = next_token(space);
      while (space_at(next_token(last))) last = next_token(last);
      origin_index = last;
      return true;
    }
    const auto after = next_token(space);
    if (space_at(after) || !visible_at(after)) return false;
    // CZ dialect: a one-byte token glued to the word before it (SC09-2417-00
    // 3.1.7 `QXXITOP(` + `)` before the next row's origin run) ends an
    // exactly full row as text; only a free-standing glyph is a slot.
    if (cz_mode && line_open && line_visible_cells != 0 &&
        (view.prefix == 0 || view.prefix == 1 ||
         (!pending_space && !out.lines.back().cells.empty() &&
          !out.lines.back().cells.back().space)))
      return false;
    // `( sp1 │ text` / `a sp1 │ text` (ACPZMST1 record 35 tokens 134 and
    // 163): the one-byte slot before a one-cell origin and the row's visual
    // marker glyph; the glyph proves the row start.
    const auto glyph_before_slot =
        run_length(space) < 3 && pending_space &&
        (punctuation_glyph(view) || view.value < row_control_byte_limit) &&
        is_placeholder_run(items[after].token) &&
        items[after].token.width == 1;
    if (run_length(space) < 3 && !glyph_before_slot) return false;
    if (run_length(space) >= 3 && alnum_word(view) &&
        view.value >= row_control_byte_limit)
      return false;
    // Exception: the following token is itself a marker candidate.  In the
    // CZ dialect a one-byte word after the origin run is text (justified
    // rows: packet 2.2 `-` + 3 spaces + `a` + 3 spaces + `network`).
    const auto& following = items[after].token;
    if (following.width == 1 && !is_bullet_glyph(following) &&
        !(cz_mode && alnum_word(following))) {
      const auto space2 = next_token(after);
      if (space_at(space2) && run_length(space2) >= 3) {
        const auto after2 = next_token(space2);
        if (visible_at(after2) && !space_at(after2)) return false;
      }
    }
    origin_index = space;
    return true;
  }

  // Every compiled `cz FLOW FN` body ends with a row-terminator `.` token
  // that hosted does not print: packet 3.2 record 85 `... start with ax so,
  // ax0..` renders `ax0.`, record 86 `... connections)!.` renders
  // `connections)!`, and packet 1.1 record 17 `technique..` renders
  // `technique.`.  The terminator is always a standalone one-cell `.`
  // token, so a body that ends any other way is not modelled.
  bool trim_footnote_terminator() {
    if (out.lines.empty() || out.lines.back().directive != current_directive)
      return fail(error, "cz FLOW FN body has no display row");
    auto& cells = out.lines.back().cells;
    if (cells.empty())
      return fail(error, "cz FLOW FN body has no display row");
    // The row terminator is the length byte of the display line that follows
    // the footnote body, so where the record's display lines parse the row
    // model has already taken it as that line's control slot and it never
    // reaches the body's cells.  Only an unparsed record still needs the
    // trailing `.` trimmed out of the text.
    if (cells.back().record != npos &&
        display_lines_of(cells.back().record) != nullptr)
      return true;
    if (cells.size() < 2 || cells.back().space ||
        cells.back().record == npos || cells.back().text != ".")
      return fail(error, "cz FLOW FN body does not end with a row terminator");
    const auto& terminator = cells.back();
    const auto& previous = cells[cells.size() - 2];
    if (previous.record == terminator.record &&
        previous.token == terminator.token)
      return fail(error,
                  "cz FLOW FN row terminator is glued to the last word");
    auto& entry = ledger.at(terminator.record, terminator.token);
    if (entry.role != ProseTokenRoleIR::text)
      return fail(error, "cz FLOW FN row terminator is not display text");
    entry.role = ProseTokenRoleIR::marker;
    cells.pop_back();
    return true;
  }

  bool run() {
    cz_mode = std::any_of(items.begin(), items.end(), [](const auto& item) {
      return item.kind == ItemKind::layout;
    });
    // Drawn box regions are a flattened-dialect shape; the CZ dialect names
    // its own verbatim blocks (`cz OFF XMP`).
    if (cz_mode) boxes.clear();
    for (std::size_t index = 0; index < items.size(); ++index) {
      const auto& item = items[index];
      // Every item inside a drawn box region belongs to its preformatted
      // block: the region is emitted once, at its first token, and the
      // CFONT controls inside it style nothing the block keeps.
      if (item.kind == ItemKind::token) {
        const auto* region = box_at(item.token.record, item.token.token);
        if (region != nullptr) {
          if (region->begin_record == item.token.record &&
              region->begin_token == item.token.token && !emit_box(*region))
            return false;
          continue;
        }
      } else if ((item.kind == ItemKind::font ||
                  item.kind == ItemKind::select) &&
                 item.source.token_end > item.source.token_begin &&
                 box_at_logical(item.source.logical_record,
                                item.source.token_begin) != nullptr) {
        continue;
      }
      switch (item.kind) {
      case ItemKind::segment_end:
        // An `ST` control with no payload completes the title with no words;
        // the topic's heading is then its number alone (see
        // `prose_topic_stream.cpp`, the empty-title case).
        if (item.empty_title) title_done = true;
        if (!finish_title()) return false;
        // A segment boundary that stands inside the `SI` keyword's own
        // display line is not one the record encoder wrote, so it does not
        // end the index term (see `index_line_open_after`).
        if (!index_line_open_after(index) && !finish_index()) return false;
        break;
      case ItemKind::layout: {
        if (!finish_title() || !finish_index()) return false;
        if (item.directive.mode == "off" && item.directive.tag == "fn" &&
            !trim_footnote_terminator())
          return false;
        if (item.directive.mode == "off") {
          if (cz_verbatim_region_tag(item.directive.tag))
            xmp_mode = true;
          else if (cz_verbatim_region_closer(item.directive.tag))
            xmp_mode = false;
          else if (cz_title_page_tag(item.directive.tag))
            title_page_mode = true;
          else if (cz_title_page_closer(item.directive.tag))
            title_page_mode = false;
        }
        out.directives.push_back(item.directive);
        current_directive = out.directives.size() - 1;
        // A row opened by the previous directive's trailing slot and a lone
        // origin run that carries no text yet belongs to this directive.
        if (line_open && line_visible_cells == 0 && !out.lines.empty() &&
            out.lines.back().cells.size() <= out.lines.back().text_begin &&
            !out.lines.back().bullet) {
          out.lines.back().directive = current_directive;
        } else {
          line_open = false;
        }
        pending_space = false;
        break;
      }
      case ItemKind::font:
      case ItemKind::select:
        if (in_title) return fail(error, "font/selector inside the ST title");
        pending_controls.push_back(index);
        break;
      case ItemKind::anchor: {
        if (!finish_title() || !finish_index()) return false;
        ProseAnchorIR anchor;
        anchor.id = item.anchor_id;
        anchor.source = item.source;
        out.body_anchors.push_back(std::move(anchor));
        anchor_pending = true;
        pending_anchor_index = out.body_anchors.size() - 1;
        line_open = false;
        break;
      }
      case ItemKind::span:
        if (!finish_title() || !finish_index()) return false;
        // A CFONT/CSELECT whose display text lies inside the span styles the
        // block's own content; the block models it and owns those tokens.
        pending_controls.clear();
        out.span_marks.push_back(
            {item.span_index, out.lines.size(), out.body_anchors.size()});
        line_open = false;
        pending_space = false;
        skip_until = npos;
        break;
      case ItemKind::token:
        if (!token(index)) return false;
        break;
      }
    }
    if (!finish_title() || !finish_index()) return false;
    if (!pending_controls.empty())
      return fail(error, "font/selector control has no display text");
    if (anchor_pending) {
      // Trailing anchor: precedes no line; keep as terminal anchor.
      anchor_pending = false;
    }
    if (!title_done) return fail(error, "ST title was never completed");
    return true;
  }

  bool token(std::size_t index) {
    const auto& item = items[index];
    const auto& view = item.token;
    if (skip_until != npos) {
      if (index <= skip_until) {
        if (is_bare(view)) {
          pending_space = false;
          ++trailing_bare;
          return assign(view, ProseTokenRoleIR::spacing);
        }
        return true;
      }
      skip_until = npos;
    }
    // An index entry is exactly one display line: its last token is the last
    // token of the line that the `SI` keyword opened.  This is the same
    // framing that decides the entry draws nothing, so it bounds the term of
    // every `SI`, structured or plain.
    if (in_index && index_line_end != npos && view.record == index_record &&
        view.token >= index_line_end) {
      if (!finish_index()) return false;
    }
    if (item.title_start) {
      in_title = true;
      in_title_segment = true;
      title_marker_seen = false;
      out.title.clear();
      out.title_run.clear();
    }
    if (item.index_start) {
      if (in_index) return fail(error, "nested SI control");
      in_index = true;
      current_term = {};
      term_refs.clear();
      const auto keyword = body_text(view);
      if (ascii_lower(keyword) != "si")
        return fail(error, "SI keyword mismatch");
      index_record = view.record;
      index_line_end = display_line_end_at(view.record, view.token);
      return assign(view, ProseTokenRoleIR::index_keyword);
    }

    if (is_bare(view)) {
      pending_space = false;
      if (blank_display_line(view)) ++trailing_bare;
      return assign(view, ProseTokenRoleIR::spacing);
    }

    // The length byte that opens a display line is that row's control slot,
    // whatever dictionary word a token reader resolves it to: it is never
    // the row's origin run and never display text.  SC33-033 4.5 record 176
    // spells the length bytes of its three `SI` lines as three- and
    // six-cell space runs, and SC31-711 3.1 record 46 spells the length
    // bytes of the `nettl` log example as the words `as`, `a` and `are`;
    // hosted (DT 19941010174546) prints none of them and keeps the example
    // rows apart.
    if (opens_display_line(view)) return row_control_length_byte(index);

    if (is_space_run(view)) {
      const auto next = next_token(index);
      // Measured and reverted: guarding this fill/origin pair with "a span of
      // the open row reaches past the cells written so far"
      // (Format/markup.md, "A span holds its row open") does hold the row
      // open where the pair is the interior padding of one display line --
      // SC33-033 record 872 line 18 is
      // `     C*  ...(64 cells)...  *  02100000` under
      // `cfont 5 2 E 74 1 E 77 8 E`, an 85-column comment-box row the model
      // cuts after `C*`.  Exporting all 34 fixtures with and without that
      // guard moves exactly six topics (SC33-033 `A.3.2`..`A.3.7`) and every
      // one of them comes out *worse*: the recovered rows are all `E`-styled,
      // so the reflow joins the whole FORTRAN listing into a single inline
      // code span where hosted (DT 19930422134757) serves a `<pre>` of 120
      // lines and the legacy route keeps one row per paragraph.  It is also a
      // one-book rule.  Both reasons say leave the pair alone until the
      // fixed-layout regions render verbatim.
      if (space_at(next)) {
        // Fill/origin pair: every run but the last is fill.
        auto cursor = index;
        auto last = next;
        while (space_at(next_token(last))) last = next_token(last);
        while (cursor != last) {
          if (!assign(items[cursor].token, ProseTokenRoleIR::fill)) return false;
          cursor = next_token(cursor);
        }
        const auto& origin = items[last].token;
        if (!assign(origin, ProseTokenRoleIR::origin)) return false;
        if (!finish_title() || !finish_index()) return false;
        open_line(origin.body.size(), &origin);
        // Skip the consumed runs (bare tokens between were handled as they
        // came; controls stay in sequence).
        skip_until = last;
        return true;
      }
      // Lone run: literal in-line gap (or trailing fill before a control).
      if (in_title) {
        out.title.push_back(' ');
        return assign(view, ProseTokenRoleIR::gap);
      }
      if (in_index) {
        current_term.term.push_back(' ');
        return assign(view, ProseTokenRoleIR::gap);
      }
      if (!line_open) {
        // Padding before the first line of the body: fill.
        return assign(view, ProseTokenRoleIR::fill);
      }
      // A lone run of three or more cells after a word and before the next
      // word is a row break without a marker byte: the word ends its row
      // and the next row keeps the block indent (SC31-711 LR57 `check` + 10
      // spaces + `the following` renders at indent 3; SC24-5520-00 LR51
      // `are` + 3 spaces + `discussed`).  The gap after a ballot token
      // (`__`) is display spacing inside the row.
      // CZ dialect rows are justified to one width (packet 1.1: 77 cells
      // `PRNET,   and   SATNET   (a   satellite ...`); a run that keeps the
      // next word inside the widest row seen so far is an in-row gap.
      const auto fits_row = [&]() { return run_fits_row(0, index); };
      if (view.body.size() >= 3 && pending_space && line_visible_cells != 0 &&
          !span_continues_row() &&
          index + 1 < items.size() && items[index + 1].kind == ItemKind::token &&
          visible_at(next_token(index)) &&
          !is_placeholder_run(items[next_token(index)].token) &&
          !ballot_token(last_visible) && !fits_row()) {
        if (!assign(view, ProseTokenRoleIR::fill)) return false;
        if (!finish_title() || !finish_index()) return false;
        open_line(implied_origin, nullptr);
        for (std::size_t cell = 0; cell < implied_origin; ++cell)
          line().cells.push_back({npos, 0, " ", true});
        line().text_begin = line().cells.size();
        return true;
      }
      if (pending_space) {
        line().cells.push_back({npos, 0, " ", true});
        pending_space = false;
      }
      append_space_cells(view, true);
      return assign(view, ProseTokenRoleIR::gap);
    }
    // Visible token.
    if (cz_mode && line_open && line_visible_cells == 0 && !in_title &&
        !in_index && view.width == 1 && view.body.size() == 1) {
      const auto next = next_token(index);
      const auto after = space_at(next) ? next_token(next) : npos;
      const auto text_follows = visible_at(after) && !space_at(after);
      // A one-cell slot after the origin run and before a one- or two-cell
      // gap is the bullet of a `cz FLOW LI` row (SC09-2417-00 2.1 record
      // 137 `<` + 3 spaces + slot + 2 spaces + `"Introducing`; hosted
      // `<li>`).  It counts one display cell plus the synthetic space, which
      // keeps CFONT/CSELECT columns aligned (`cselect 7 55` on LI 3 7).
      // The slot decodes as a placeholder word or as the literal decoder
      // separator `?` (SC09-2417-00 4.2 record 894); neither is text.
      const auto slot = is_placeholder_run(view) ||
                        (item.separator && (view.body.front() == '?' ||
                                            is_bullet_glyph(view)));
      if (slot && text_follows && run_length(next) <= 2) {
        line().bullet = true;
        if (!assign(view, ProseTokenRoleIR::bullet)) return false;
        line().cells.push_back({view.record, view.token, "?", false});
        line().cells.push_back({npos, 0, " ", true});
        const auto& gap = items[next].token;
        append_space_cells(gap, true);
        if (!assign(gap, ProseTokenRoleIR::gap)) return false;
        ++line_visible_cells;
        pending_space = false;
        skip_until = next;
        line().text_begin = line().cells.size();
        return true;
      }
      // A change bar opening the row before its gap (SC41-485 1.2.2 record
      // 52 `|` + 4 spaces + `Object name` on DT 7 16, hosted `| Object
      // name`): one display cell in the margin, never text.  The bar's
      // encoded value is above the row-control range, unlike marker slots.
      if (view.body.front() == '|' && view.value >= row_control_byte_limit &&
          space_at(next) && text_follows) {
        if (!assign(view, ProseTokenRoleIR::marker)) return false;
        line().cells.push_back({view.record, view.token, "|", false});
        line().cells.push_back({npos, 0, " ", true});
        const auto& gap = items[next].token;
        append_space_cells(gap, true);
        if (!assign(gap, ProseTokenRoleIR::gap)) return false;
        pending_space = false;
        skip_until = next;
        line().text_begin = line().cells.size();
        return true;
      }
    }
    if (cz_mode && item.separator && !is_placeholder_run(view) &&
        (is_separator(view) || is_bullet_glyph(view)) &&
        view.body.size() == 1) {
      // Unclaimed one-cell decoder separators of the CZ dialect.  A `,`
      // glued (attach prefix) to the word before it is text the decoder split
      // off (SC09-2417-00 3.1.7 `QXXITOP(),`; hosted prints it).  A `?`
      // before a lone space run and text is the slot that closes the row and
      // the run is the next row's origin (SC41-485 1.1 record 6
      // `configuration` + `?` + 7 spaces + `descriptions`).  Anything else is
      // padding.
      if (view.body.front() == ',' && line_open && line_visible_cells != 0 &&
          (view.prefix == 0 || view.prefix == 1))
        return append_visible(view, ProseTokenRoleIR::text);
      // A `?` stored as a dictionary word (width 2) is a question mark
      // (packet 1.1 record 15 `network` + bare + `?`); the one-byte `?` is
      // the row slot.
      if (view.body.front() == '?' && view.width == 2 && line_open &&
          line_visible_cells != 0)
        return append_visible(view, ProseTokenRoleIR::text);
      if (view.body.front() == '?' || is_bullet_glyph(view)) {
        const auto next = next_token(index);
        if (space_at(next) && !space_at(next_token(next)) &&
            visible_at(next_token(next))) {
          if (!finish_title() || !finish_index()) return false;
          if (!assign(view, ProseTokenRoleIR::marker)) return false;
          const auto& origin = items[next].token;
          if (!assign(origin, ProseTokenRoleIR::origin)) return false;
          open_line(origin.body.size(), &origin);
          skip_until = next;
          return true;
        }
      }
      return assign(view, ProseTokenRoleIR::padding);
    }
    // Inside a `cz OFF XMP` / `cz OFF SCREEN` verbatim region the drawn
    // frame is display content, not row geometry: hosted BookServer prints
    // the box words in their own columns inside the region's `<pre
    // width="80">` (SC09-2417-00 3.2.3, served as `SC09-241` DT
    // 19961114175628, reproduces the `PURCHASE ORDER FORM` frame line for
    // line).  The cells carry the hosted display glyph of each word, exactly
    // as an admitted drawn box region does.
    // The same holds outside a verbatim region wherever the record's own
    // display line puts a displayed word in front of the run: the run then
    // stands inside a drawn row, so it is that row's display content and not
    // row geometry.  It is the rule `demote_display_line_owned_controls`
    // applies to control-shaped words, read for box-drawing runs.  Hosted
    // evidence on four books:
    //   SC24-546 record 44 line 17
    //     `       The >>___ symbol indicates the beginning of a statement.`
    //     -- `The` and `>>` precede the three `U+2500` cells (DT
    //     19940323131240);
    //   SC09-2417-00 record 715 line 26
    //     `   >>__extern__"string-literal"__{ declaration-list }____...__><`
    //     -- a railroad diagram whose first rule follows `>>` (`SC09-241`
    //     DT 19961114175628);
    //   SC33-033 record 75 line 14
    //     `    ------------------ General-Use Programming Interface ---...`
    //     -- an ASCII-dash fence whose first and last cell are drawn
    //     (DT 19930422134757);
    //   SC24-5520-00 record 45 line 12 `    <-________ 4 bytes _____->`,
    //     the arrow caption above a drawn box.
    // A run with nothing displayed in front of it on its line keeps every
    // reading it had: the box region, the `U+2500` rule line and the marker
    // slot all start their own line.
    // The row must already carry a display cell as well: a control opcode is
    // a visible token of its display line but draws nothing, and the `ST`
    // title line is written `ST` + slot + title (ACPZMST1 record 284 tokens
    // 26/28/29 `ST` `U+2502` `/`), where the box word is the documented title
    // marker slot and not a drawn cell.
    if (is_placeholder_run(view) &&
        (xmp_mode ||
         (line_open && line_visible_cells != 0 &&
          display_word_precedes_in_line(view))) &&
        std::all_of(view.body.begin(), view.body.end(),
                    [](const auto word) { return box_word(word); })) {
      if (!ensure_line()) return false;
      if (pending_space && view.prefix != 0 && view.prefix != 1)
        line().cells.push_back({npos, 0, " ", true});
      for (const auto word : view.body)
        line().cells.push_back(
            {view.record, view.token, figure_display_glyph(word), false});
      ++line_visible_cells;
      last_visible.clear();
      pending_space = view.prefix != 2;
      trailing_bare = 0;
      return assign(view, ProseTokenRoleIR::text);
    }
    std::size_t origin_index = npos;
    const auto line_start_marker =
        line_open && line_visible_cells == 0 && view.width == 1 &&
        index + 1 < items.size() && items[index + 1].kind == ItemKind::token &&
        is_visible(items[index + 1].token) &&
        !is_placeholder_run(items[index + 1].token);
    if (is_placeholder_run(view) && !line_start_marker &&
        !marker_at(index, origin_index)) {
      // A placeholder run that does not open a row is layout padding: either
      // trailing before a control/segment end, the slot of a fill/origin
      // pair (whose runs are classified when they are reached), or the
      // marker glued between `ST` and the title (ACPZMST1 record 78).
      const auto next = next_token(index);
      // An inter-segment placeholder stands at a control boundary as well.
      const auto before_control =
          index + 1 >= items.size() ||
          items[index + 1].kind != ItemKind::token || item.separator;
      const auto title_marker = in_title && out.title.empty();
      if (next != npos && !space_at(next) && !before_control && !title_marker) {
        if (in_index && index_line_end != npos && view.record == index_record &&
            view.token < index_line_end) {
          // Field separator of a structured subject-index line; the whole
          // line is hidden (hosted QSYSINFO 2.1.1 DT 19910524120827 and
          // SC09-138 2.1.1.2 DT 19910321130500 display none of it).
          current_term.structured = true;
          return assign(view, ProseTokenRoleIR::index_structure);
        }
        if (opens_display_line(view)) return row_control_length_byte(index);
        if (display_rule_line(index)) return true;
        return fail(error, "placeholder run '" + body_text(view) +
                               "' is followed by visible text at " +
                               where(view));
      }
      if (title_marker) {
        title_marker_seen = true;
        return assign(view, ProseTokenRoleIR::marker);
      }
      if (in_title && !finish_title()) return false;
      if (in_index && !finish_index()) return false;
      if (!assign(view, ProseTokenRoleIR::marker)) return false;
      // The slot ends the current row even when its origin run is absent
      // (GC23-046 record 151: `for {9472}... [cfont] | the CIDTABL`).
      line_open = false;
      // A placeholder slot followed by one space run of any width opens a
      // row: GC23-046 record 151 `{9524} sp1 | ◆ The total number`.
      if (space_at(next) && !space_at(next_token(next)) &&
          visible_at(next_token(next))) {
        const auto& origin = items[next].token;
        if (!assign(origin, ProseTokenRoleIR::origin)) return false;
        open_line(origin.body.size(), &origin);
        skip_until = next;
      }
      return true;
    }
    // A span of the open row that reaches past the cells written so far
    // proves the row has not ended, so a one-byte token standing there is
    // display text and not the next row's length byte or marker slot.
    // Hosted evidence: SC24-546 4.3.1 serves
    // `     ABBREV('Print','Pri')      -><B>    1</B>` as one row, whose
    // second `cfont` names columns 32..34; IEAC6MST 5.1.2 serves
    // `          RECORDSIZE(384 3072) )` with `cfont 31 1 E` over the
    // trailing `)`; SC09-138 8.4.2.5 serves
    // `          a = b*(x*y*z);            /* Duplicates recognized */`,
    // whose `;` the row model had read as the next line's length byte.
    // Decoder placeholder runs carry no character, so they stay slots
    // whatever the geometry says (ACPZMST1 3.11 record 180).
    // A token the record's own display-line structure names as a length byte
    // is a row boundary whatever the spans say.
    const auto span_holds_row = !cz_mode && line_open && !in_title &&
                                !in_index && line_visible_cells != 0 &&
                                !is_placeholder_run(view) &&
                                inside_display_line(view.record, view.token) &&
                                span_continues_row();
    // A glued alphabetic one-byte token in the row-control byte range is the
    // display-line length byte of the next row: N2AH1MST record 17 `to:` +
    // `access` (0x1c) directly before the `/` row marker of the next row.
    // The byte stands at a row boundary, so a plain word glued behind it on
    // the same row is display text: SC26-457 3.14.2.8 record 560 token 113
    // `(` + `and` + `their` is served as `(and their associated entries)`.
    // The byte also may not sit inside a glued compound: SG24-204 PREFACE
    // record 17 spells `step-by-step` as the glued run `step` `-` `by` `-`
    // `step`, where `by` has encoded value 0x2f and both neighbours are
    // one-cell punctuation; hosted (DT 19971218054640) serves
    // `a step-by-step manner`, so the slot reading would drop `by`.
    const auto glued_continuation = [&]() {
      bool attach = false;
      for (auto cursor = index + 1; cursor < items.size(); ++cursor) {
        const auto& following = items[cursor];
        if (following.kind != ItemKind::token) return false;
        const auto& view_next = following.token;
        if (is_bare(view_next)) {
          if (view_next.prefix != 0 && view_next.prefix != 1) return false;
          attach = true;
          continue;
        }
        if (!is_visible(view_next)) return false;
        return attach || view_next.prefix == 0 || view_next.prefix == 1;
      }
      return false;
    };
    if (!cz_mode && !span_holds_row && view.width == 1 &&
        view.value < row_control_byte_limit &&
        (!pending_space || view.prefix == 0 || view.prefix == 1) &&
        !in_title && !in_index && line_open && line_visible_cells != 0 &&
        alpha_word(view) && !last_visible.empty() &&
        std::isalnum(static_cast<unsigned char>(last_visible.back())) == 0) {
      const auto after = next_token(index);
      const auto row_boundary =
          after == npos || space_at(after) ||
          (items[after].token.width == 1 &&
           (punctuation_glyph(items[after].token) ||
            is_placeholder_run(items[after].token)));
      if (row_boundary && !glued_continuation())
        return assign(view, ProseTokenRoleIR::marker);
    }
    // Two width-1 tokens in the row-control byte range in a row: only the
    // second opens the display row, the first is padding.  XWEBDEMO 1.4.2
    // and 1.4.3 place the external selector's kind word (`<OTHER>` 13,
    // `<INTERNET>` 12) in front of the row's own marker (`/` 18, `:H1` 21);
    // the hosted page shows neither and starts the row at its origin.
    if (view.width == 1 && view.value < row_control_byte_limit && !line_open &&
        !alnum_word(view) && !in_title && !in_index) {
      const auto next = next_token(index);
      std::size_t next_origin = npos;
      if (next != npos && is_token(next) && items[next].token.width == 1 &&
          items[next].token.value < row_control_byte_limit &&
          marker_at(next, next_origin))
        return assign(view, ProseTokenRoleIR::padding);
    }
    // A pending span that opens on this token's own display column proves it
    // is styled display text rather than the row's marker slot: GC28-183
    // 2.2.3 `cfont 5 2 E 15 4 E` over `     //        PEND` is served as
    // `<samp>//</samp>        <samp>PEND</samp>`.
    // CZ rows carry their marker slots explicitly, so the geometry exemption
    // is limited to the flowed dialect (SC09-2417-00 2.1.3.4 `++` before a
    // `cz flow nt` label stays a slot).
    const auto styled_at_column =
        line_open && !cz_mode && !is_placeholder_run(view) &&
        line_visible_cells == 0 &&
        pending_span_covers(line().cells.size(), view.body.size());
    if (!styled_at_column && !span_holds_row &&
        marker_at(index, origin_index)) {
      if (!assign(view, ProseTokenRoleIR::marker)) return false;
      for (auto cursor = next_token(index); cursor != origin_index;
           cursor = next_token(cursor))
        if (!assign(items[cursor].token, ProseTokenRoleIR::fill)) return false;
      const auto& origin = items[origin_index].token;
      if (!assign(origin, ProseTokenRoleIR::origin)) return false;
      if (!finish_title() || !finish_index()) return false;
      // A change bar in the slot stands for the row's whole left margin:
      // hosted ACPZMST1 8.14.1 serves ` |     XC_NOTIFY_MSG, ...` for the
      // stored `U+2502` + four-cell origin run and marks the phrase with
      // `cfont 7 13 9`; GC23-046 6.1 serves ` |     Note:` for `U+2502` +
      // four cells with `cfont 7 5 2`; GG24-395 2.4.1 serves
      // ` |     variable until the Thread Y ...` with `cfont 35 7 1`.
      open_line(origin.body.size(), &origin,
                change_bar_slot(view) ? change_bar_margin_cells : 0);
      skip_until = origin_index;
      return true;
    }

    if ((in_title || in_index) && is_bullet_glyph(view))
      return assign(view, ProseTokenRoleIR::padding);
    // The title's own row carries at most one marker slot.  A second
    // one-cell glyph in front of the first title word is display text:
    // ACPZMST1 5.4 and 5.5 store `ST` + spacing + a placeholder slot + `/` +
    // `etc` + `/` + `inittab`, and the catalog title is
    // `/etc/inittab File Definitions`.
    if (in_title && !title_marker_seen && out.title.empty() &&
        view.width == 1 && punctuation_glyph(view)) {
      title_marker_seen = true;
      return assign(view, ProseTokenRoleIR::marker);
    }
    if (in_title) {
      if (pending_space && view.prefix != 0 && view.prefix != 1)
        out.title.push_back(' ');
      out.title += body_text(view);
      append_title_run(view);
      last_visible = body_text(view);
      pending_space = view.prefix != 2;
      out.title_refs.emplace_back(view.record, view.token);
      trailing_bare = 0;
      return assign(view, ProseTokenRoleIR::title);
    }
    if (in_index) {
      if (pending_space && view.prefix != 0 && view.prefix != 1)
        current_term.term.push_back(' ');
      current_term.term += body_text(view);
      last_visible = body_text(view);
      pending_space = view.prefix != 2;
      term_refs.emplace_back(view.record, view.token);
      trailing_bare = 0;
      return assign(view, ProseTokenRoleIR::index_term);
    }
    if ((is_glyph(view) || is_placeholder_run(view)) && opens_display_line(view))
      return row_control_length_byte(index);
    if (!line_open) open_line(0, nullptr);
    trailing_bare = 0;
    if (line_visible_cells == 0 && view.width == 1 &&
        (punctuation_glyph(view) || is_placeholder_run(view)) &&
        // CZ rows carry their markers as explicit slots (bullet, change bar,
        // `?`), so an ordinary punctuation glyph opening a row is display
        // text there: packet 3.2 record 80 token 255 `#` before `name` is
        // styled by `cfont 5 1 E` and hosted prints `<samp>#</samp>`.
        (!cz_mode || is_placeholder_run(view) || view.body.front() == '|') &&
        // A pending CFONT/CSELECT span that opens on the glyph's own display
        // column proves the glyph is styled display text, not a row marker:
        // FA1PLMM0 3.5.1 `cfont 5 2 E 8 3 E ...` over `     // JOB COPY ...`
        // and GC28-183 2.2.3 `cfont 5 2 E 15 4 E` over `     //        PEND`
        // are served as `<samp>//</samp> <samp>JOB</samp> ...` and
        // `<samp>//</samp>        <samp>PEND</samp>`.
        // Decoder placeholder runs stay markers whatever the geometry says
        // (ACPZMST1 3.11 record 180): they carry no character.
        (is_placeholder_run(view) ||
         !pending_span_covers(line().cells.size(), view.body.size())) &&
        marker_precedes_row_text(index)) {
      // A visual row marker (`|`, box glyph) opening the row directly before
      // its text (GC23-046 record 151 `| ◆ The number of orders`, ACPZMST1
      // record 78 `│ The following sections`).  The glyph is not prose text,
      // but hosted BookServer prints it in its own display column
      // (ACPZMST1 1.2.3.1 ` | A local resource ...`, GG24-395 PREFACE.3
      // ` | Part 1, "Introduction"`), so the row keeps a blank cell for it
      // and for the spacing that follows.  Dropping those cells shifted
      // every CFONT/CSELECT column of the row by two.
      if (!assign(view, ProseTokenRoleIR::marker)) return false;
      for (std::size_t word = 0; word < view.body.size(); ++word)
        line().cells.push_back({npos, 0, " ", true});
      line().text_begin = line().cells.size();
      pending_space = view.prefix != 2;
      return true;
    }
    // The bullet is a dictionary word, so the encoder is free to store it in a
    // two-byte token; only the decoder's unmapped word has to stay one byte
    // (a width-2 `?` is a question mark, see the CZ separator branch above).
    // Byte-level evidence: QS3X36CM record 7 token 81 is value 56323 width 2,
    // one word `U+2666`, opening the display line
    // `   °   Press F4 on a blank command line ...` which hosted DT
    // 19910524075122 serves verbatim; IBMMMSTR record 44 token 145 is value
    // 46595 width 2 opening `   °   Compiler control messages (numbers 0002
    // through 0049) are mainly`, served by DT 19911004151140.
    if (line_visible_cells == 0 && is_glyph(view) &&
        (view.width == 1 || is_bullet_glyph(view))) {
      // A glyph opening the line is the list bullet.
      // The bullet keeps the spacing that separates it from whatever the row
      // already carries: a change bar in front of it holds its own column and
      // the assembler's space behind the bar is a display column too.  Hosted
      // GG24-395 PREFACE.3 serves ` | °   Chapter 1, "A Client/Server
      // Overview"` for `U+2502` + `U+2666` + a two-cell gap and links it with
      // `cselect 7 37`; GC23-046 7.5.4 serves ` | °   Do an APPLY CHECK ...
      // Figure 19 ...` with `cselect 53 12`.
      if (pending_space && view.prefix != 0 && view.prefix != 1)
        line().cells.push_back({npos, 0, " ", true});
      line().bullet = true;
      if (!assign(view, ProseTokenRoleIR::bullet)) return false;
      line().cells.push_back({view.record, view.token, word_text(view.body[0]),
                              false});
      ++line_visible_cells;
      pending_space = true;
      const auto next = next_token(index);
      if (space_at(next) && !space_at(next_token(next))) {
        line().cells.push_back({npos, 0, " ", true});
        pending_space = false;
        const auto& gap = items[next].token;
        append_space_cells(gap, true);
        if (!assign(gap, ProseTokenRoleIR::gap)) return false;
        skip_until = next;
      }
      line().text_begin = line().cells.size();
      return true;
    }
    if (is_glyph(view) || is_placeholder_run(view))
      return fail(error, "placeholder glyph '" + body_text(view) +
                             "' inside prose text at " + where(view));
    if (std::any_of(view.body.begin(), view.body.end(),
                    [](const auto word) { return word == unmapped_word; }))
      return fail(error, "unmapped word '" + body_text(view) +
                             "' inside prose text at " + where(view));
    // A `c.<xx>` word is a body control opcode, never visible text: the
    // `c.cp <n>` pagination form is modelled by the stream pass and hosted
    // BookServer serves no such word at all (checked on SH20-918 3.31.1,
    // DREICMST 1.7.7.3, SH12-565 1.1.2 and GG24-4302-00 8.1.5).  When the
    // decoder loses the control boundary the opcode stays glued to a text
    // run; fail the topic closed instead of printing the control.
    {
      const auto text = body_text(view);
      for (std::size_t at = text.find("c."); at != std::string::npos;
           at = text.find("c.", at + 1)) {
        if (at != 0 && text[at - 1] != ' ') continue;
        std::size_t end = at + 2;
        while (end < text.size() &&
               std::islower(static_cast<unsigned char>(text[end])) != 0)
          ++end;
        if (end - at < 4) continue;  // `c.` plus at least two opcode letters
        if (end != text.size() && text[end] != ' ') continue;
        if (body_control_line(index)) return true;
        return fail(error, "body control '" + text.substr(at, end - at) +
                               "' is glued into prose text at " + where(view));
      }
    }
    return append_visible(view, ProseTokenRoleIR::text);
  }

  std::size_t skip_until = npos;
};


bool build_lines(const std::vector<DecodedLogicalRecordSource>& records,
                 const std::vector<Item>& items, Ledger& ledger,
                 LineBuild& out, std::string* error) {
  LineBuilder builder(records, items, ledger, out, error);
  // A drawn box region whose tokens a table or figure span already owns is
  // that span's business (the box outline of an SRTBL envelope); only a
  // region the prose model owns end to end becomes a preformatted block.
  for (auto& region : plan_boxes(records)) {
    bool owned = false;
    for (auto record = region.begin_record;
         record <= region.end_record && !owned; ++record) {
      const auto begin = record == region.begin_record ? region.begin_token : 0;
      const auto end = record == region.end_record
                           ? region.end_token
                           : records[record].ir.tokens.size() - 1;
      for (auto token = begin; token <= end; ++token) {
        const auto role = ledger.at(record, token).role;
        if (role == ProseTokenRoleIR::table ||
            role == ProseTokenRoleIR::figure) {
          owned = true;
          break;
        }
      }
    }
    if (!owned) builder.boxes.push_back(std::move(region));
  }
  return builder.run();
}

} // namespace geist::detail::prose_internal
