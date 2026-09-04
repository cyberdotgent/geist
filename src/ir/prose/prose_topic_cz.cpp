// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/prose/prose_topic_internal.hpp"

#include <algorithm>
#include <cctype>

// The `CZ` dialect of the prose family (doc/boo-spec/markup.adoc, "CZ layout
// directives"): SC09-2417-00, SC41-485, GX27-3999-00 and packet store every
// block boundary as an explicit `cz <mode> <tag> <left> <indent>` control
// whose payload rows use the flattened display-row grammar of
// prose_topic_lines.cpp.  This unit parses the directives for the stream
// layer and turns the governed display lines into typed blocks.
namespace geist::detail::prose_internal {

namespace {

bool digits(const std::string& text) {
  return !text.empty() && text.size() <= 6 &&
         std::all_of(text.begin(), text.end(), [](unsigned char ch) {
           return std::isdigit(ch) != 0;
         });
}

std::size_t number(const std::string& text) {
  std::size_t value = 0;
  for (const auto ch : text) value = value * 10 + static_cast<std::size_t>(ch - '0');
  return value;
}

}  // namespace

bool collect_layout_directive(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::size_t record_index, const ControlSegmentIR& segment,
    std::string& pending_footnote_id, Ledger& ledger,
    std::vector<Item>& items, std::string* error) {
  const auto& record = records[record_index];
  LayoutDirective directive;
  // Header: `cz`, the mode word, the tag word (none for BREAK), then the
  // numeric operands: one for BREAK, none or two for FLOW/OFF.  Everything
  // after the header is the directive's display-row payload.
  std::size_t stage = 0;  // 0 opcode, 1 mode, 2 tag, 3 numbers, 4 payload
  std::size_t numbers = 0;
  std::size_t header_begin = npos;
  std::size_t header_end = 0;
  std::vector<Item> payload;
  for (const auto token : segment.source_tokens) {
    const auto view = view_token(records, record_index, token);
    if (stage < 4) {
      const auto text = body_text(view);
      const auto lower = ascii_lower(text);
      bool consumed = false;
      if (stage == 0) {
        if (lower != "cz")
          return fail(error, "cz control does not start with its opcode");
        consumed = true;
        stage = 1;
      } else if (stage == 1) {
        if (lower != "flow" && lower != "off" && lower != "break")
          return fail(error, "cz mode '" + text + "' is not FLOW, OFF or BREAK");
        directive.mode = lower;
        consumed = true;
        stage = lower == "break" ? 3 : 2;
      } else if (stage == 2) {
        if (is_padding(view) || lower.empty() ||
            !std::all_of(lower.begin(), lower.end(), [](unsigned char ch) {
              return std::isalnum(ch) != 0;
            }))
          return fail(error, "cz tag '" + text + "' is not a layout tag");
        directive.tag = lower;
        consumed = true;
        stage = 3;
      } else if (digits(text) &&
                 numbers < (directive.mode == "break" ? 1u : 2u)) {
        // The BREAK count carries no layout state the prose model needs.
        if (directive.mode != "break") {
          if (numbers == 0) directive.left = number(text);
          else directive.indent = number(text);
        }
        ++numbers;
        consumed = true;
      } else {
        stage = 4;
      }
      if (consumed) {
        if (!ledger.assign(record_index, token, ProseTokenRoleIR::control,
                           error))
          return false;
        if (header_begin == npos) header_begin = token;
        header_end = token + 1;
        continue;
      }
    }
    Item item;
    item.kind = ItemKind::token;
    item.token = view;
    payload.push_back(std::move(item));
  }
  if (stage < 3) return fail(error, "cz control is incomplete");
  if (directive.mode != "break" && numbers == 1)
    return fail(error, "cz " + directive.mode + " " + directive.tag +
                           " carries one layout operand");
  if (directive.mode == "break" && numbers == 0)
    return fail(error, "cz BREAK has no count");
  if (directive.mode == "flow" && directive.tag == "fn") {
    if (pending_footnote_id.empty())
      return fail(error, "cz FLOW FN has no preceding SRFTN anchor");
    directive.anchor_id = pending_footnote_id;
    pending_footnote_id.clear();
  } else if (!pending_footnote_id.empty()) {
    return fail(error, "SR" + pending_footnote_id +
                           " is not followed by cz FLOW FN");
  }
  directive.source = token_slice(record, header_begin, header_end);
  Item layout;
  layout.kind = ItemKind::layout;
  layout.directive = std::move(directive);
  items.push_back(std::move(layout));
  for (auto& item : payload) items.push_back(std::move(item));
  Item end;
  end.kind = ItemKind::segment_end;
  items.push_back(std::move(end));
  return true;
}

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------

namespace {

bool list_tag(const std::string& tag) {
  return tag == "ul" || tag == "ol" || tag == "sl" || tag == "notel" ||
         tag == "dl" || tag == "parml";
}

bool heading_tag(const std::string& tag) {
  return tag.size() == 2 && tag.front() == 'h' && tag.back() >= '2' &&
         tag.back() <= '5';
}

bool has_text(const Line& line) { return line.cells.size() > line.text_begin; }

// Groups [begin, end) of a directive's lines: a bare spacing token or an
// anchor before a row starts a new group; rows without text separate groups.
std::vector<std::pair<std::size_t, std::size_t>> group_lines(
    const std::vector<Line>& lines, std::size_t begin, std::size_t end) {
  std::vector<std::pair<std::size_t, std::size_t>> groups;
  std::size_t index = begin;
  while (index < end) {
    if (!has_text(lines[index])) {
      ++index;
      continue;
    }
    auto stop = index + 1;
    while (stop < end && lines[stop].breaks_before == 0 &&
           !lines[stop].anchor_before) {
      // A row that carries no text is a layout artefact of a control that
      // opened a row without display words (packet 3.2 record 80 `SI Linux
      // AX.25, ...` between `... use tabs for` and `everything, not
      // spaces:`, which hosted prints as one `<p>`).  Only an explicit
      // paragraph break separates groups.
      ++stop;
    }
    while (stop > index && !has_text(lines[stop - 1])) --stop;
    groups.emplace_back(index, stop);
    index = stop;
  }
  return groups;
}

std::string cells_text(const Line& line, std::size_t begin, std::size_t end) {
  std::string text;
  for (auto cell = begin; cell < end && cell < line.cells.size(); ++cell)
    text += line.cells[cell].text;
  return text;
}

// First run of non-space cells at or after `from`: [begin, end).
std::pair<std::size_t, std::size_t> first_word(const Line& line,
                                               std::size_t from) {
  auto begin = from;
  while (begin < line.cells.size() && line.cells[begin].space) ++begin;
  auto end = begin;
  while (end < line.cells.size() && !line.cells[end].space) ++end;
  return {begin, end};
}

// Drops spans that end before `text_begin`; a span crossing it fails.
bool clip_spans_before(Line& line, std::string* error) {
  for (auto* spans : {&line.fonts, &line.links}) {
    for (auto it = spans->begin(); it != spans->end();) {
      auto begin = it->begin;
      auto end = it->end;
      while (begin < end && begin < line.cells.size() && line.cells[begin].space)
        ++begin;
      while (end > begin && end - 1 < line.cells.size() &&
             line.cells[end - 1].space)
        --end;
      if (end <= line.text_begin) {
        it = spans->erase(it);
        continue;
      }
      if (begin < line.text_begin)
        return fail(error, "CFONT/CSELECT span crosses a CZ label boundary on '" +
                               line_text(line) + "'");
      ++it;
    }
  }
  return true;
}

// Keeps only spans that lie inside [0, cut); a span crossing `cut` fails.
bool clip_spans_after(Line& line, std::size_t cut, std::string* error) {
  for (auto* spans : {&line.fonts, &line.links}) {
    for (auto it = spans->begin(); it != spans->end();) {
      auto begin = it->begin;
      auto end = it->end;
      while (begin < end && begin < line.cells.size() && line.cells[begin].space)
        ++begin;
      while (end > begin && end - 1 < line.cells.size() &&
             line.cells[end - 1].space)
        --end;
      if (begin >= cut) {
        it = spans->erase(it);
        continue;
      }
      if (end > cut)
        return fail(error, "CFONT/CSELECT span crosses the definition term "
                           "boundary on '" +
                               line_text(line) + "'");
      ++it;
    }
  }
  return true;
}

struct Frame {
  std::string tag;
  std::size_t list_ordinal = 0;
  std::size_t items = 0;
};

struct CzBuilder {
  const std::vector<DecodedLogicalRecordSource>& records;
  const LineBuild& build;
  Ledger& ledger;
  ProseTopicIR& topic;
  std::string* error;
  std::vector<Line> lines;
  std::vector<std::size_t> block_first_line;
  std::vector<Frame> stack;
  std::size_t next_list_ordinal = 0;
  // Display-line range [first, second) of every directive (npos: none).
  std::vector<std::pair<std::size_t, std::size_t>> ranges;

  CzBuilder(const std::vector<DecodedLogicalRecordSource>& sources,
            const LineBuild& lines_build, Ledger& owner, ProseTopicIR& out,
            std::string* message)
      : records(sources), build(lines_build), ledger(owner), topic(out),
        error(message), lines(lines_build.lines) {}

  bool emit(ProseBlockIR block, std::size_t begin, std::size_t end) {
    const auto index = topic.blocks.size();
    if (!build_block(records, lines, begin, end, block, ledger, index, error))
      return false;
    if (begin < lines.size()) block.origin = lines[begin].origin;
    topic.blocks.push_back(std::move(block));
    block_first_line.push_back(begin);
    return true;
  }

  bool paragraphs(const std::vector<std::pair<std::size_t, std::size_t>>& groups,
                  std::size_t from) {
    for (auto index = from; index < groups.size(); ++index) {
      ProseBlockIR block;
      block.kind = ProseBlockKindIR::paragraph;
      if (!emit(std::move(block), groups[index].first, groups[index].second))
        return false;
    }
    return true;
  }

  // `1.`, `12.`, `a.`: the explicit ordinal label of an ordered-list row.
  bool take_ordinal(Line& line, std::string& ordinal) {
    const auto [begin, end] = first_word(line, line.text_begin);
    const auto text = cells_text(line, begin, end);
    if (text.size() < 2 || text.back() != '.') return true;
    const auto body = text.substr(0, text.size() - 1);
    const auto alpha = body.size() == 1 &&
                       std::isalpha(static_cast<unsigned char>(body[0])) != 0;
    if (!digits(body) && !alpha) return true;
    if (end >= line.cells.size() || !line.cells[end].space) return true;
    ordinal = text;
    for (auto cell = begin; cell < end; ++cell) {
      const auto& source = line.cells[cell];
      if (source.record == npos) continue;
      auto& entry = ledger.at(source.record, source.token);
      if (entry.role == ProseTokenRoleIR::text)
        entry.role = ProseTokenRoleIR::ordinal;
    }
    auto text_begin = end;
    while (text_begin < line.cells.size() && line.cells[text_begin].space)
      ++text_begin;
    line.text_begin = text_begin;
    return clip_spans_before(line, error);
  }

  bool list_item(const LayoutDirective& directive,
                 const std::vector<std::pair<std::size_t, std::size_t>>& groups) {
    if (stack.empty() || (stack.back().tag != "ul" && stack.back().tag != "ol" &&
                          stack.back().tag != "sl" && stack.back().tag != "notel"))
      return fail(error, "cz FLOW LI outside an open list");
    if (groups.empty()) return fail(error, "cz FLOW LI has no text");
    auto& frame = stack.back();
    ProseBlockIR block;
    block.kind = ProseBlockKindIR::list_item;
    block.list_ordinal = frame.list_ordinal;
    block.ordered = frame.tag == "ol" || frame.tag == "notel";
    ++frame.items;
    auto& first = lines[groups.front().first];
    if (block.ordered && !take_ordinal(first, block.ordinal)) return false;
    if (!has_text(first)) return fail(error, "cz FLOW LI has no text");
    (void)directive;
    return emit(std::move(block), groups.front().first, groups.front().second) &&
           paragraphs(groups, 1);
  }

  bool definition(const LayoutDirective& directive,
                  const std::vector<std::pair<std::size_t, std::size_t>>& groups) {
    if (stack.empty() || stack.back().tag != "dl")
      return fail(error, "cz FLOW DT outside an open definition list");
    if (groups.empty()) return fail(error, "cz FLOW DT has no text");
    auto& frame = stack.back();
    ++frame.items;
    const auto [begin, end] = groups.front();
    auto& row = lines[begin];
    // The term occupies the columns from the left margin to the indent; a
    // word straddling the indent column belongs to the term (SC41-485 1.2.2
    // `Generic object name` at DT 7 16 keeps `object` in the term).
    auto split = directive.indent;
    if (split < row.text_begin) split = row.text_begin;
    if (split < row.cells.size() && !row.cells[split].space && split > 0 &&
        !row.cells[split - 1].space)
      while (split < row.cells.size() && !row.cells[split].space) ++split;
    if (split > row.cells.size()) split = row.cells.size();
    Line term = row;
    term.cells.resize(split);
    if (!clip_spans_after(term, split, error)) return false;
    Line rest = row;
    rest.text_begin = split;
    while (rest.text_begin < rest.cells.size() && rest.cells[rest.text_begin].space)
      ++rest.text_begin;
    if (!clip_spans_before(rest, error)) return false;
    const auto rest_has_text = has_text(rest);
    // Term and definition are built from a private line vector so the
    // shared line indices stay valid for provenance.
    std::vector<Line> parts;
    parts.push_back(std::move(term));
    if (rest_has_text) parts.push_back(std::move(rest));
    for (auto index = begin + 1; index < end; ++index) parts.push_back(lines[index]);
    // A term whose definition is a block of its own has no inline definition
    // at all -- SC09-2417-00 PREFACE.2.1 record 33 is `cz FLOW DT 3 13` +
    // `   /L[+|-]` followed by `cz OFF LINES` .. `cz OFF ELINES 10 10`, and
    // hosted (DT 19961114175628) serves `<dt>   <tt>/L[+|-]</tt>` with no
    // `<dd>` and then the `<pre width="80">` holding `/L`, `/L+`, `/L-`.
    // `DefinitionEntryIR` is a term plus an *inline* definition, so that
    // shape has no typed spelling yet and still fails closed here.
    if (parts.size() < 2)
      return fail(error, "cz FLOW DT term '" + line_text(parts.front()) +
                             "' has no definition");
    ProseBlockIR block;
    block.kind = ProseBlockKindIR::definition_entry;
    block.list_ordinal = frame.list_ordinal;
    block.origin = row.origin;
    const auto index = topic.blocks.size();
    if (!build_block(records, parts, 0, 1, block, ledger, index, error))
      return false;
    block.term_inline_count = block.inlines.size();
    if (!build_block(records, parts, 1, parts.size(), block, ledger, index, error))
      return false;
    topic.blocks.push_back(std::move(block));
    block_first_line.push_back(begin);
    return paragraphs(groups, 1);
  }

  bool note(const std::vector<std::pair<std::size_t, std::size_t>>& groups) {
    if (groups.empty()) return fail(error, "cz FLOW NT has no text");
    const auto [begin, end] = groups.front();
    auto& row = lines[begin];
    const auto [label_begin, label_end] = first_word(row, row.text_begin);
    const auto label = cells_text(row, label_begin, label_end);
    if (label.size() < 2 || label.back() != ':')
      return fail(error, "note block does not start with a label: '" +
                             line_text(row) + "'");
    Line term = row;
    term.cells.resize(label_end);
    if (!clip_spans_after(term, label_end, error)) return false;
    Line rest = row;
    rest.text_begin = label_end;
    while (rest.text_begin < rest.cells.size() && rest.cells[rest.text_begin].space)
      ++rest.text_begin;
    if (!clip_spans_before(rest, error)) return false;
    std::vector<Line> parts;
    parts.push_back(std::move(term));
    if (has_text(rest)) parts.push_back(std::move(rest));
    for (auto index = begin + 1; index < end; ++index) parts.push_back(lines[index]);
    if (parts.size() < 2) return fail(error, "note block has no content");
    ProseBlockIR block;
    block.kind = ProseBlockKindIR::note;
    block.origin = row.origin;
    const auto index = topic.blocks.size();
    if (!build_block(records, parts, 0, 1, block, ledger, index, error))
      return false;
    block.term_inline_count = block.inlines.size();
    if (!build_block(records, parts, 1, parts.size(), block, ledger, index, error))
      return false;
    topic.blocks.push_back(std::move(block));
    block_first_line.push_back(begin);
    return paragraphs(groups, 1);
  }

  // `cz OFF XMP` .. `cz OFF EXMP`: the display rows verbatim.  CFONT spans
  // inside (hosted `<samp>` per word) carry no structure the block keeps.
  bool preformatted(std::size_t begin, std::size_t end,
                    const std::string& degradation_code = {},
                    const std::string& degradation_detail = {}) {
    ProseBlockIR block;
    block.kind = ProseBlockKindIR::preformatted;
    block.degradation_code = degradation_code;
    block.degradation_detail = degradation_detail;
    const auto index = topic.blocks.size();
    std::vector<std::string> rows;
    // Row -> index into `block.inlines`, or `npos` for a blank row, which is
    // a display-line break and owns no token.  A degraded block is the only
    // statement its consumer gets about the region, so it carries per-row
    // provenance and not just one range for the whole fence.
    std::vector<std::size_t> row_inlines;
    std::vector<std::pair<std::size_t, std::size_t>> block_refs;
    for (auto line_index = begin; line_index < end; ++line_index) {
      const auto& line = lines[line_index];
      for (std::size_t blank = 0; blank < line.breaks_before; ++blank) {
        rows.emplace_back();
        row_inlines.push_back(npos);
      }
      if (!has_text(line)) {
        if (line.breaks_before == 0) {
          rows.emplace_back();
          row_inlines.push_back(npos);
        }
        continue;
      }
      auto text = line_text(line);
      while (!text.empty() && text.back() == ' ') text.pop_back();
      ProseInlineIR inline_node;
      inline_node.text = text;
      std::vector<std::pair<std::size_t, std::size_t>> refs;
      for (const auto& cell : line.cells) {
        if (cell.record == npos) continue;
        auto& entry = ledger.at(cell.record, cell.token);
        if (entry.role != ProseTokenRoleIR::text) continue;
        if (!claim_token_whole(records, ledger, cell.record, cell.token, index,
                               block.inlines.size(), error))
          return false;
        refs.emplace_back(cell.record, cell.token);
      }
      std::sort(refs.begin(), refs.end());
      refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
      if (refs.empty())
        return fail(error, "preformatted row '" + text + "' has no source");
      inline_node.slices = slices_for(records, refs);
      block_refs.insert(block_refs.end(), refs.begin(), refs.end());
      row_inlines.push_back(block.inlines.size());
      block.inlines.push_back(std::move(inline_node));
      rows.push_back(std::move(text));
    }
    while (!rows.empty() && rows.back().empty()) {
      rows.pop_back();
      row_inlines.pop_back();
    }
    while (!rows.empty() && rows.front().empty()) {
      rows.erase(rows.begin());
      row_inlines.erase(row_inlines.begin());
    }
    if (block.inlines.empty())
      return fail(error, "cz OFF XMP block has no display rows");
    // The region's own left margin is content: hosted BookServer serves the
    // rows inside `<pre>` at the columns the display lines put them in, so
    // the block keeps them.  Measured over all 486 `cz OFF XMP` blocks the
    // corpus emits (SC09-2417-00 263, packet 223) against hosted: with the
    // common indent removed not one block matched hosted line for line;
    // keeping it makes 454 of 486 (93.4%) exact, and no block that already
    // differed got worse.  The margin is per region, not per book --
    // SC09-2417-00 `3.1.3.5` (DT 19961114175628) serves its `#pragma
    // mapinc` example at column 5 and the three DDS listings below it at
    // column 10, on one page.
    //
    // This is not the re-indent `prose_topic_lines.cpp` guards against.
    // That one *infers* a margin from a two-run display line and would add
    // ten columns SC09-2417-00 `4.1.9.4`'s COBOL listing does not have; the
    // rows here are already at their read columns and are simply not shifted.
    block.preformatted_lines = std::move(rows);
    block.preformatted_line_inlines = std::move(row_inlines);
    std::sort(block_refs.begin(), block_refs.end());
    block.slices = slices_for(records, block_refs);
    block.origin = lines[begin].origin;
    topic.blocks.push_back(std::move(block));
    block_first_line.push_back(begin);
    return true;
  }

  // ---------------------------------------------------------------------
  // The generated title-page projection
  // ---------------------------------------------------------------------
  //
  // `cz OFF COVER` .. `cz OFF ECOVER` and `cz OFF TIPAGE` .. `cz OFF ETIPAGE`
  // are not stored prose and not a verbatim region.  The book compiler
  // generated the region from the source prolog's title-block and metadata
  // fields (`:library.`, `:topic.`, `:release.`, `:docnum.`, `:partnum.`,
  // `:filenum.`, `:date.`, `:author.`) and laid them out as display rows; the
  // reader re-flows those rows instead of reproducing their columns
  // (doc/boo-spec/markup.adoc, "Cover And Title Page Rendering").  Three facts of the
  // hosted pages settle the projection, and each is a fact of the region, not
  // of any one book:
  //
  //  * **The rows' left margin is layout origin, not content.**  packet
  //    stores the same three generated fields at column 3 in `COVER` and at
  //    columns 57/58/66 in `TITLE` -- one field per row, each row centred or
  //    right-aligned by itself -- and hosted (DT 20260614112503) serves both
  //    flush left.  The standing rule that a verbatim region keeps its margin
  //    holds because hosted keeps *those* regions' columns; here it does not
  //    keep them, and the same field appears at two different columns in one
  //    book, so no column of the region is content.
  //
  //  * **A blank display row is the paragraph boundary.**  packet `COVER`
  //    stores a blank row between every generated field and hosted serves
  //    `<p>` between every one of them; packet `TITLE` stores the three
  //    title-block rows with no blank row between them and hosted serves them
  //    as three lines of one paragraph, with `<p>` only where the stored
  //    blank rows are.  A `cz BREAK` inside the region is a row boundary and
  //    not a paragraph one: SC41-4853-00 `COVER` (DT 19951003131222) breaks
  //    between `System API Reference` and `OS/400 Configuration APIs` and
  //    hosted serves `<br>` there, against `<p>` for its blank rows.
  //
  //  * **The `CFONT` spans are the emphasis, applied per word.**  Every
  //    title-block row carries one triple per word (packet `TITLE`
  //    `cfont 57 7 2 65 6 2 72 5 2`) and hosted serves
  //    `<B>Amateur</B> <B>Packet</B> <B>Radio</B>`.  A row with no triple is
  //    served unemphasised, which is what distinguishes a title row from a
  //    metadata row: packet `COVER` carries no triple on `Evie Cooper` and
  //    hosted serves it plain, while packet `TITLE` carries
  //    `cfont 66 4 2 71 6 2` on the same words and hosted bolds them.  The
  //    reader reads the operands, so the model does too; nothing is inferred
  //    from which field the row holds.
  //
  // A `U+2500` rule row is the frame the reader draws as `<hr>` and prints no
  // character of it (the same rule `prose_topic_lines.cpp` applies to the
  // flattened dialect's cover frames).  It draws no word, so it separates the
  // paragraphs around it and its cells take a structural ledger role.

  // A display row whose every visible cell comes from a token drawn entirely
  // from `U+2500` box-rule words.
  bool rule_row(const Line& line) const {
    bool rule = false;
    for (const auto& cell : line.cells) {
      if (cell.space || cell.record == npos) continue;
      const auto view = view_token(records, cell.record, cell.token);
      if (!std::all_of(view.body.begin(), view.body.end(),
                       [](const auto word) { return word == 0x2500; }))
        return false;
      rule = true;
    }
    return rule;
  }

  // The rule draws no word, so its cells are structural like the row markers
  // `display_rule_line` assigns in the flattened dialect.
  void claim_rule_row(const Line& line) {
    for (const auto& cell : line.cells) {
      if (cell.record == npos) continue;
      auto& entry = ledger.at(cell.record, cell.token);
      if (entry.role == ProseTokenRoleIR::text)
        entry.role = ProseTokenRoleIR::marker;
    }
  }

  // One paragraph of the projection: the display rows [begin, end) with their
  // row boundaries kept and their left margin dropped.
  bool title_paragraph(std::size_t begin, std::size_t end) {
    ProseBlockIR block;
    block.kind = ProseBlockKindIR::paragraph;
    block.origin = lines[begin].origin;
    const auto block_index = topic.blocks.size();
    std::size_t rows = 0;
    for (auto line = begin; line < end; ++line) {
      if (!has_text(lines[line])) continue;
      if (rows != 0) {
        ProseInlineIR boundary;
        boundary.kind = ProseInlineKindIR::line_break;
        block.inlines.push_back(std::move(boundary));
      }
      if (!build_block(records, lines, line, line + 1, block, ledger,
                       block_index, error))
        return false;
      ++rows;
    }
    if (rows == 0) return true;
    topic.blocks.push_back(std::move(block));
    block_first_line.push_back(begin);
    return true;
  }

  // The projection's rows [begin, end), split into paragraphs at the blank
  // display rows `group_lines` already reads as boundaries.
  bool title_rows(std::size_t begin, std::size_t end) {
    for (const auto& group : group_lines(lines, begin, end))
      if (!title_paragraph(group.first, group.second)) return false;
    return true;
  }

  bool title_page(std::size_t& index, std::pair<std::size_t, std::size_t> range,
                  const std::string& name, const std::string& tag) {
    const auto closer_tag = "e" + tag;
    // The region runs to its own closer.  Only `cz BREAK` may stand inside
    // it: any other directive would carry layout the projection does not
    // model, and the region fails closed rather than swallowing it.
    auto closer_index = index + 1;
    auto begin = range.first;
    auto end = range.second;
    while (closer_index < build.directives.size() &&
           build.directives[closer_index].mode == "break") {
      const auto rows = ranges[closer_index];
      if (rows.first != npos) {
        if (begin == npos) begin = rows.first;
        end = rows.second;
      }
      ++closer_index;
    }
    if (closer_index >= build.directives.size() ||
        build.directives[closer_index].mode != "off" ||
        build.directives[closer_index].tag != closer_tag)
      return fail(error, name + " is not closed by cz off " + closer_tag);
    if (begin == npos)
      return fail(error, name + " region has no display rows");
    const auto blocks_before = topic.blocks.size();
    auto rows_begin = begin;
    for (auto line = begin; line < end; ++line) {
      if (!rule_row(lines[line])) continue;
      if (!title_rows(rows_begin, line)) return false;
      claim_rule_row(lines[line]);
      rows_begin = line + 1;
    }
    if (!title_rows(rows_begin, end)) return false;
    if (topic.blocks.size() == blocks_before)
      return fail(error, name + " region draws no word");
    // Like `cz OFF EXMP`, the closer carries whatever body text follows the
    // region as ordinary paragraphs.
    const auto closer = ranges[closer_index];
    index = closer_index;
    if (closer.first == npos) return true;
    return paragraphs(group_lines(lines, closer.first, closer.second), 0);
  }

  // Issue #81.  What `degraded_region` decided about one unmodelled
  // `cz OFF <tag>` directive.
  enum class RegionOutcome {
    // The region was emitted verbatim, marked degraded, and its closer's
    // trailing text was emitted as paragraphs.  `index` advanced past both.
    emitted,
    // It is a degradable region and lowering it failed; `error` is set.
    declined,
    // Not a closed region at all.  The caller declines the topic with its
    // own message, unchanged from before this path existed.
    not_a_region,
  };

  // The block-level degradation invariant, applied to the one construct that
  // satisfies it generically.
  //
  // Conditions 1 (frame proven) and 3 (the boundary is a block boundary) are
  // properties of where this is called from: the topic's metadata envelope,
  // segmentation, layout ledger and ownership all verified before the CZ
  // block builder ran, and a `cz OFF` directive is the CZ dialect's own block
  // delimiter -- the rows before it belong to the previous directive and the
  // rows after the closer belong to the closer, whatever this region holds.
  //
  // Condition 2 (boundary proven independently of the failing check) is what
  // this function tests.  The check that failed is "this tag is not
  // modelled"; the boundary is proven by a *different* fact, that the source
  // wrote a matched `cz OFF E<tag>` and the frame recognised it as a layout
  // directive.  A region that is never closed by its own `E<tag>` therefore
  // has no proven boundary and is refused here, so the topic still falls
  // whole -- that is the case the note counts as "no closed region", and it
  // stays on the declining side.
  //
  // Condition 4 (total region conservation) comes from `preformatted`, which
  // claims every text token of every display row between the delimiters, and
  // from the region being exactly one directive's line range: no other block
  // claims inside it and it claims nothing outside.
  //
  // Condition 5 (verbatim means verbatim) is the block kind: display rows in
  // source order, no inline model, no links, no ordinal, no nesting.
  //
  // Condition 6 (marked, at the block) is the degradation code carried out
  // through the lowering to `DocumentFidelityIR::degraded`.
  RegionOutcome degraded_region(std::size_t& index,
                                std::pair<std::size_t, std::size_t> range,
                                const std::string& tag) {
    std::string opener;
    for (const auto ch : tag)
      opener.push_back(
          static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    const auto closer_tag = "e" + tag;
    // The same region walk the modelled verbatim tags use: a footnote end
    // marker that displays nothing, and an `SRFIG`/`SRTBL` envelope
    // delimiter, are rows of the region rather than breaks in it.
    auto closer_index = index + 1;
    auto region_end = range.second;
    while (closer_index < build.directives.size()) {
      const auto& inner = build.directives[closer_index];
      if (inner.mode != "off") break;
      const auto inner_rows = ranges[closer_index];
      if (inner.tag == "fn") {
        if (inner_rows.first != npos) break;
      } else if (inner.tag == "table" || inner.tag == "etable" ||
                 inner.tag == "fig" || inner.tag == "efig") {
        if (inner_rows.first != npos) region_end = inner_rows.second;
      } else {
        break;
      }
      ++closer_index;
    }
    // Invariant 2.  Unlike a modelled verbatim region, an *unterminated*
    // region is not degradable: there the tag itself told the model where the
    // rows end, and here nothing does.  No matched closer, no boundary, no
    // degradation -- the topic declines whole with its original message.
    if (closer_index >= build.directives.size() ||
        build.directives[closer_index].mode != "off" ||
        build.directives[closer_index].tag != closer_tag)
      return RegionOutcome::not_a_region;
    // Invariant 5.  A region that displays nothing has no rows to keep, so
    // there is nothing to degrade into; it declines with its own message.
    if (range.first == npos) return RegionOutcome::not_a_region;
    if (!preformatted(range.first, region_end, "cz-off-region-unmodelled",
                      "cz OFF " + opener + " is not modelled; the region is "
                      "bounded by its own matched cz OFF E" + opener +
                      " and is emitted verbatim"))
      return RegionOutcome::declined;
    // The closer carries the body text that follows the region as ordinary
    // paragraphs, exactly as `cz OFF EXMP` does.  Those paragraphs are
    // *typed*: only the region between the delimiters is degraded.
    const auto closer = ranges[closer_index];
    index = closer_index;
    if (closer.first == npos) return RegionOutcome::emitted;
    return paragraphs(group_lines(lines, closer.first, closer.second), 0)
               ? RegionOutcome::emitted
               : RegionOutcome::declined;
  }

  // `cz OFF ARTWORK` .. its closer.  Hosted BookServer opens a
  // `<pre width="80">` at the opener and serves the region's display rows in
  // it character for character, up to the flow directive that follows the
  // closer; the closer itself draws nothing and its own rows stay inside the
  // region (GX27-3999-00 `FRONT_1`, DT 19950730184057: `cz OFF EHP0 0 0`
  // carries `   The adapter kit consists of:` and hosted prints it inside the
  // `<pre>`, before the `</pre>` the following `cz FLOW UL` emits).
  //
  // The region draws only what it still owns.  A `PIC<n>` selector whose
  // placeholder row shows nothing else is a block figure, claimed token for
  // token by the span plan before this pass runs (prose_topic_spans.cpp), and
  // hosted serves exactly that: GX27-3999-00 `2.4` replaces the row spelling
  // `       PICTURE 7` with the `<img ... alt="PICTURE 7">` of picture 7.
  // Such a row keeps no display cell here, so the region adds no verbatim row
  // for it and the figure block places the image.
  //
  // What the region does own stays verbatim: SC41-4853-00 `COMMENTS`
  // (DT 19951003131222) alternates regions that draw nothing at all -- hosted
  // serves an empty `<pre width="80">` for each -- with regions holding one
  // 74-column `U+2500` rule, the line a reader writes a comment on.
  bool artwork(std::size_t& index, std::pair<std::size_t, std::size_t> range) {
    const auto closer_index = index + 1;
    if (closer_index >= build.directives.size() ||
        build.directives[closer_index].mode != "off" ||
        !cz_artwork_region_closer(build.directives[closer_index].tag))
      return fail(error, "cz OFF ARTWORK is not closed by cz OFF EARTWORK");
    const auto closer = ranges[closer_index];
    index = closer_index;
    auto begin = range.first;
    auto end = range.second;
    if (begin == npos) {
      begin = closer.first;
      end = closer.second;
    } else if (closer.first != npos) {
      if (closer.first != end)
        return fail(error, "cz OFF ARTWORK region rows are not contiguous");
      end = closer.second;
    }
    if (begin == npos) return true;
    const auto drawn =
        std::any_of(lines.begin() + static_cast<std::ptrdiff_t>(begin),
                    lines.begin() + static_cast<std::ptrdiff_t>(end),
                    [](const Line& line) { return has_text(line); });
    // A region that draws nothing draws nothing: hosted's empty `<pre>`.
    return !drawn || preformatted(begin, end);
  }

  bool run() {
    const auto& directives = build.directives;
    // Line ranges per directive: lines follow their directive in order.
    ranges.assign(directives.size(), {npos, npos});
    for (std::size_t index = 0; index < lines.size(); ++index) {
      const auto directive = lines[index].directive;
      if (directive == npos) {
        if (has_text(lines[index]))
          return fail(error, "display row '" + line_text(lines[index]) +
                                 "' precedes the first CZ directive");
        continue;
      }
      if (directive >= directives.size())
        return fail(error, "internal: line refers to a missing directive");
      auto& range = ranges[directive];
      if (range.first == npos) range.first = index;
      else if (index != range.second)
        return fail(error, "internal: directive lines are not contiguous");
      range.second = index + 1;
    }
    for (std::size_t index = 0; index < directives.size(); ++index) {
      const auto& directive = directives[index];
      const auto range = ranges[index];
      const auto groups = range.first == npos
                              ? std::vector<std::pair<std::size_t, std::size_t>>{}
                              : group_lines(lines, range.first, range.second);
      const auto name = "cz " + directive.mode + " " + directive.tag;
      const auto& tag = directive.tag;
      if (directive.mode == "break") {
        if (!paragraphs(groups, 0)) return false;
        continue;
      }
      if (!handle(index, directive, range, groups, name, tag)) {
        if (error != nullptr && error->rfind("cz ", 0) != 0)
          *error = name + ": " + *error;
        return false;
      }
    }
    if (!stack.empty())
      return fail(error, "cz list " + stack.back().tag + " is not closed");
    const auto leading_anchors = topic.anchors.size();
    place_anchors();
    return place_spans(leading_anchors);
  }

  // One directive with its display-line range and text groups.
  bool handle(std::size_t& index, const LayoutDirective& directive,
              std::pair<std::size_t, std::size_t> range,
              const std::vector<std::pair<std::size_t, std::size_t>>& groups,
              const std::string& name, const std::string& tag) {
    {
      // Object regions.  `cz OFF TABLE` .. `cz OFF ETABLE` delimits an
      // `SRTBL` .. `SRETBL` table envelope and `cz OFF FIG` .. `cz OFF EFIG`
      // an `SRFIG` .. `SREFIG` figure region.  Both objects are already
      // claimed token for token by the span plan, which runs before the
      // stream pass (prose_topic_spans.cpp), so the directive delimits but
      // never draws: packet 2.4.4 record 65 segment 11 is `cz OFF TABLE`
      // immediately followed by `SRTBLTBLUNIQ17` (segment 12) and closed by
      // `SRETBL` / `cz OFF ETABLE 0 0` (record 67 segments 3-4); packet 1.3
      // record 29 segments 2-6 are `SRFIGFIGUNIQ5`, `cz OFF FIG`, the
      // `cselect 35 9 PIC1 ... PICTURE 1   Figure 1. ...` line, `SREFIG`,
      // `cz OFF EFIG 0 0`.  The opener therefore carries no rows of its own,
      // and the closer carries the body text that follows the object,
      // exactly like `cz OFF EXMP` and the list closers.  A delimiter with
      // no admitted span of its kind fails closed.
      if (directive.mode == "off" &&
          (tag == "table" || tag == "etable" || tag == "fig" ||
           tag == "efig")) {
        const auto kind = (tag == "table" || tag == "etable")
                              ? ProseSpanKindIR::table
                              : ProseSpanKindIR::figure;
        if (std::none_of(topic.spans.begin(), topic.spans.end(),
                         [kind](const ProseSpanIR& span) {
                           return span.kind == kind;
                         }))
          return fail(error, name + " delimits no admitted " +
                                 (kind == ProseSpanKindIR::table
                                      ? "table envelope"
                                      : "figure region"));
        if (tag == "table" || tag == "fig") {
          if (!groups.empty())
            return fail(error, name + " carries display text");
          return true;
        }
        return paragraphs(groups, 0);
      }
      // `SCREEN` is the second verbatim region of the dialect and behaves
      // exactly like `XMP`: hosted BookServer serves the rows between
      // `cz OFF SCREEN` and `cz OFF ESCREEN` inside its own `<pre
      // width="80">`, character for character, including the drawn frame
      // (SC09-2417-00 3.2.3, served as `SC09-241` DT 19961114175628, record
      // 549 lines 9-16 -- the `PURCHASE ORDER FORM` display; SC24-5527-02
      // draws the same shape).
      //
      // `SYNTAX` is the fourth and behaves the same way: the railroad syntax
      // diagram between `cz OFF SYNTAX` and `cz OFF ESYNTAX` is one drawn
      // display block.  SC09-2417-00 `4.1.2` (DT 19961114175628) serves both
      // of its regions as `<pre width="80">` holding
      // `   &gt;&gt;__<kbd>extern</kbd>__<var>&quot;string-literal&quot;</var>__ ...`
      // -- the same `<pre>` element `XMP` gets two blocks further down the
      // same page, at the region's own left margin of three columns, and the
      // prose around it stays typed (`<dl>`, `<dt>`, `<a href>`).
      if (directive.mode == "off" && cz_verbatim_region_tag(tag)) {
        std::string opener;
        for (const auto ch : tag)
          opener.push_back(
              static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        const auto closer_tag = "e" + tag;
        // The footnote end marker `SREFTN` lowers to a synthetic `cz OFF FN`
        // directive that displays nothing (prose_topic_stream.cpp), so a
        // footnote whose last block is a verbatim region carries that marker
        // between the region's opener and its closer: packet 4.5.1 record 225
        // line 24 is `SREFTN`, between `cz OFF XMP` (line 21) and
        // `cz OFF EXMP 6 6` (line 25).  A marker that displays nothing does
        // not break the region; anything that owns a display row does.
        //
        // A labelled box may also enclose an `SRFIG`/`SRTBL` envelope whose
        // `cz OFF ETABLE` closer therefore falls inside the box.  That
        // envelope is not a table: `cz OFF TABLE` is the only mark of one,
        // and there is no opener here.  Hosted (SC41-4853-00 `1.2` DT
        // 19951003131222) serves the whole box, drawn grid included, as one
        // `<pre width="132"><!-- lblbox -->` and emits no `<table>` -- the
        // envelope anchors become `<a name="TBLTBLUNIQ1">` on the box rows
        // they open.  So an object delimiter inside a verbatim region is a
        // row of that region, not a break in it.
        auto closer_index = index + 1;
        auto region_end = range.second;
        while (closer_index < build.directives.size()) {
          const auto& inner = build.directives[closer_index];
          if (inner.mode != "off") break;
          const auto inner_rows = ranges[closer_index];
          if (inner.tag == "fn") {
            // The footnote end marker displays nothing.
            if (inner_rows.first != npos) break;
          } else if (inner.tag == "table" || inner.tag == "etable" ||
                     inner.tag == "fig" || inner.tag == "efig") {
            if (inner_rows.first != npos) region_end = inner_rows.second;
          } else {
            break;
          }
          ++closer_index;
        }
        const auto closed =
            closer_index < build.directives.size() &&
            build.directives[closer_index].mode == "off" &&
            build.directives[closer_index].tag == closer_tag;
        // An unterminated region ends where the topic does.  SG24-204
        // `NOTICES` opens `cz OFF LBLBOX`, draws the closed `Take Note!` box
        // and stops: no directive follows it at all, and hosted (SG24-2047-00
        // DT 19971218054640) serves exactly that one `<!-- lblbox -->` block
        // as the whole body.  A region that merely runs into *other*
        // directives is still a decline.
        if (!closed && closer_index < build.directives.size())
          return fail(error, "cz OFF " + opener + " is not closed by cz OFF E" +
                                 opener);
        if (range.first == npos)
          return fail(error, "cz OFF " + opener + " block has no display rows");
        if (!preformatted(range.first, region_end)) return false;
        if (!closed) {
          index = closer_index;
          return true;
        }
        // The closing directive carries the body text that follows the
        // example block as ordinary paragraphs at its own left/indent
        // (packet 2.4.1 record 57 `cz OFF EXMP 2 2   Note that zeros are
        // omitted ...`, which hosted prints after the `</pre>`).
        const auto closer = ranges[closer_index];
        index = closer_index;
        if (closer.first == npos) return true;
        return paragraphs(group_lines(lines, closer.first, closer.second), 0);
      }
      // The generated title-page projection, ahead of the unmodelled path:
      // its rows are re-flowed, not reproduced (see `title_page`).
      if (directive.mode == "off" && cz_title_page_tag(tag))
        return title_page(index, range, name, tag);
      // The artwork region, ahead of the generic `cz OFF <tag>` fallback.
      if (directive.mode == "off" && cz_artwork_region_tag(tag))
        return artwork(index, range);
      if (directive.mode == "off") {
        if (tag == "fn") {
          if (!groups.empty())
            return fail(error, name + " carries display text");
          return true;
        }
        // A verbatim-region closer that closes nothing.  The book compiler
        // can write the end of a region whose opener it never wrote:
        // GX27-3999-00 `NOTICES` draws its `Note` box inside an
        // `SRFIG`/`cz OFF FIG` figure region and then closes *two* regions
        // where it opened one -- `cz OFF ELBLBOX 0 0` (record 3 line 22)
        // followed by `cz OFF EFIG 0 0` -- with no `cz OFF LBLBOX` anywhere
        // in the topic.
        //
        // Hosted BookServer (DT 19950730184057) serves that body as a single
        // `<pre width="132"><!-- figure -->` holding the six box rows,
        // anchored `<a name="FIGFIGUNIQ1">` on the first.  The block is the
        // *figure*'s, named for it, and the `ELBLBOX` contributes no block,
        // no comment and no character.  A labelled box that really is opened
        // is served as `<!-- lblbox -->`, so BookServer distinguishes the
        // two: an unopened region closer is inert.
        //
        // Admitted on exactly that evidence and no more.  No `cz OFF <tag>`
        // opened the region earlier in this topic -- an opener is consumed
        // together with its closer by the verbatim-region branch above, so
        // reaching here already implies it, but the directive stream is asked
        // rather than the control flow trusted -- and the closer owns no
        // display row of its own.  A closer that draws text marks a region
        // boundary the model cannot place, and still fails closed.
        if (cz_verbatim_region_closer(tag)) {
          const auto opened = tag.substr(1);
          const auto opener_before = std::any_of(
              build.directives.begin(),
              build.directives.begin() + static_cast<std::ptrdiff_t>(index),
              [&opened](const LayoutDirective& earlier) {
                return earlier.mode == "off" && earlier.tag == opened;
              });
          if (!opener_before) {
            if (!groups.empty())
              return fail(error, name + " closes no open " + opened +
                                     " and carries display text");
            return true;
          }
        }
        if (tag.size() > 1 && tag.front() == 'e' &&
            (list_tag(tag.substr(1)) || tag == "ent")) {
          const auto opened = tag.substr(1);
          if (stack.empty() || stack.back().tag != opened)
            return fail(error, name + " closes no open " + opened);
          if (list_tag(opened) && stack.back().items == 0)
            return fail(error, "cz list " + opened + " has no items");
          stack.pop_back();
          // Like `cz OFF EXMP`, a list closer carries the body text that
          // follows the list as ordinary paragraphs (SC09-2417-00 4.2.2
          // record 898 `cz OFF EOL 0 0   The best way to instantiate
          // templates ...`, which hosted prints after `</ol>`).
          return paragraphs(groups, 0);
        }
        // Issue #81, the generic unmodelled `cz OFF <tag>` fallback.  An
        // unmodelled tag that opens a region the source itself closes with a
        // matched `cz OFF E<tag>`, and whose rows are preformattable, has
        // already proved everything a degraded block needs: the topic frame
        // verified to get here, the boundary is a matched control pair the
        // frame recognised independently of the check that failed (the tag
        // not being modelled), that boundary is a block boundary because the
        // delimiters -- not the region's content -- fix where the blocks
        // before and after end, the block claims every cell between them,
        // and it lowers to display rows and nothing else.  So emit them,
        // marked degraded, instead of discarding the whole topic.
        switch (degraded_region(index, range, tag)) {
        case RegionOutcome::emitted: return true;
        case RegionOutcome::declined: return false;
        case RegionOutcome::not_a_region: break;
        }
        if (!groups.empty())
          return fail(error, name + " carries display text");
        return fail(error, "CZ layout " + name + " is not modelled");
      }
      if (directive.mode != "flow")
        return fail(error, "CZ layout " + name + " is not modelled");
      if (tag == "p" || tag == "pc" || tag == "gd" || tag == "pt") {
        if (tag == "pt" && (stack.empty() || stack.back().tag != "parml"))
          return fail(error, "cz FLOW PT outside an open parameter list");
        if (tag == "pt") ++stack.back().items;
        if (!paragraphs(groups, 0)) return false;
        return true;
      }
      if (list_tag(tag)) {
        // A list opener may carry the list's lead-in rows, exactly as the
        // list closer carries the body text that follows the list.  Hosted
        // BookServer serves them ahead of the first item and inside the list
        // element: SC09-2417-00 3.1.2.2 (DT 19961114175628) record 361
        // `cz FLOW DL 3 3` + `cfont 3 6 2 13 3 2   Option    Tag` becomes
        // `<dl>\n   <B>Option</B>    <B>Tag</B>` before the first `<dt>`, and
        // PREFACE.2.1 record 32 opens its `DL` with `Syntax    Possible
        // Choices` the same way.  Markdown has no in-list lead-in, so the
        // rows become the paragraphs that precede the list.
        if (!paragraphs(groups, 0)) return false;
        stack.push_back({tag, next_list_ordinal++, 0});
        return true;
      }
      if (tag == "li") {
        if (!list_item(directive, groups)) return false;
        return true;
      }
      if (tag == "dt") {
        if (!definition(directive, groups)) return false;
        return true;
      }
      if (tag == "nt" || tag == "note") {
        if (tag == "nt") stack.push_back({tag, next_list_ordinal++, 1});
        if (!note(groups)) return false;
        return true;
      }
      if (tag == "fn") {
        if (groups.empty()) return fail(error, "cz FLOW FN has no text");
        if (directive.anchor_id.empty())
          return fail(error, "cz FLOW FN has no footnote anchor");
        ProseBlockIR block;
        block.kind = ProseBlockKindIR::footnote;
        block.anchor_id = directive.anchor_id;
        if (!emit(std::move(block), groups.front().first, groups.front().second))
          return false;
        if (!paragraphs(groups, 1)) return false;
        return true;
      }
      if (heading_tag(tag)) {
        if (groups.empty()) {
          // A trailing empty heading directive announces the next topic's
          // level; it carries no content.  Only the topic's footnotes may
          // follow it (packet 1.1 record 17) -- but a footnote body is a
          // block stream of its own, so the directives that build one belong
          // to the footnote and not to the topic after the heading.  packet
          // 4.5.1 record 225 ends `cz FLOW H5 3 3` / `SRFTNFTNUNIQ50` /
          // `cz FLOW FN 3 7` and then opens `cz OFF XMP` .. `cz OFF EXMP 6 6`
          // inside that footnote; hosted (DT 20260614112503) serves six
          // separate `<pre width="80">` blocks for the topic, the last of
          // them under the footnote anchor.  What must not follow the
          // announcement is body content of the topic itself.
          bool in_footnote = false;
          for (auto later = index + 1; later < build.directives.size();
               ++later) {
            const auto& tail = build.directives[later];
            if (tail.tag == "fn") {
              in_footnote = true;
              continue;
            }
            if (in_footnote && tail.mode == "off" &&
                (tail.tag == "fn" || cz_verbatim_region_tag(tail.tag) ||
                 cz_verbatim_region_closer(tail.tag)))
              continue;
            return fail(error,
                        name + " without text is not the last directive");
          }
          return true;
        }
        ProseBlockIR block;
        block.kind = ProseBlockKindIR::heading;
        block.heading_level = static_cast<std::size_t>(tag.back() - '0');
        if (!emit(std::move(block), groups.front().first, groups.front().second))
          return false;
        if (!paragraphs(groups, 1)) return false;
        return true;
      }
      return fail(error, "CZ layout " + name + " is not modelled");
    }
  }
  // A table/figure span precedes the first block whose first display row is
  // at or after the span's own line, and follows the anchors already seen
  // there -- the same placement `build_blocks` gives the flattened dialect.
  bool place_spans(std::size_t leading_anchors) {
    std::vector<bool> placed(topic.spans.size(), false);
    for (const auto& mark : build.span_marks) {
      if (mark.span >= topic.spans.size())
        return fail(error, "span mark addresses no span");
      if (placed[mark.span]) return fail(error, "span is marked twice");
      placed[mark.span] = true;
      std::size_t position = topic.blocks.size();
      for (std::size_t block = 0; block < block_first_line.size(); ++block)
        if (block_first_line[block] >= mark.line) {
          position = block;
          break;
        }
      auto& span = topic.spans[mark.span];
      span.position = position;
      span.anchors_before = position == 0 ? leading_anchors : 0;
      for (std::size_t anchor = 0;
           anchor < mark.anchors_seen && anchor < build.body_anchors.size();
           ++anchor)
        if (topic.anchors[leading_anchors + anchor].position == position)
          ++span.anchors_before;
    }
    if (std::any_of(placed.begin(), placed.end(),
                    [](const bool done) { return !done; }))
      return fail(error, "table/figure span never reached the body stream");
    return true;
  }

  // Anchors precede the block whose first row follows them.
  void place_anchors() {
    for (std::size_t anchor = 0; anchor < build.body_anchors.size(); ++anchor) {
      auto placed = build.body_anchors[anchor];
      placed.position = topic.blocks.size();
      for (std::size_t block = 0; block < block_first_line.size(); ++block) {
        const auto& first = lines[block_first_line[block]];
        if (first.anchor_before && first.anchor_index == anchor) {
          placed.position = block;
          break;
        }
      }
      topic.anchors.push_back(std::move(placed));
    }
  }
};

}  // namespace

bool build_cz_blocks(const std::vector<DecodedLogicalRecordSource>& records,
                     const LineBuild& lines_build, Ledger& ledger,
                     ProseTopicIR& topic, std::string* error) {
  CzBuilder builder(records, lines_build, ledger, topic, error);
  return builder.run();
}

}  // namespace geist::detail::prose_internal
