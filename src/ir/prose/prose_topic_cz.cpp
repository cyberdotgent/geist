// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/prose/prose_topic_internal.hpp"

#include <algorithm>
#include <cctype>
#include <map>

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

// A `cz` control line whose mode word is none of FLOW, OFF and BREAK, or a
// bare `cz`, is inert (doc/boo-spec/markup.adoc, "Reading a CZ body"):
// BookServer matches a control line against literal `CZ FLOW`, `CZ OFF` and
// `CZ BREAK` prefixes, and a line that matches no prefix prints nothing and
// changes no layout state.  SG24-5314-00 2.3.5.16 record 178 closes its
// table with the line `cz ON TABLE 0 0`; hosted (DT 19990209152814) prints
// nothing for it.  The whole display line of the opcode is the control; the
// segment's later tokens are ordinary rows that continue the open layout, so
// no layout item is pushed for them.
bool collect_inert_directive(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::size_t record_index, const ControlSegmentIR& segment, Ledger& ledger,
    std::vector<Item>& items, std::string* error) {
  const auto& record = records[record_index];
  if (segment.source_tokens.empty())
    return fail(error, "cz control has no source tokens");
  const auto* line =
      display_line_of_token(record, segment.source_tokens.front());
  if (line == nullptr)
    return fail(error, "cz control with an unknown mode has no display line");
  // The opcode itself was assigned when it was read.
  for (const auto token : segment.source_tokens) {
    if (token == segment.source_tokens.front()) continue;
    if (token < line->token_end) {
      if (!ledger.assign(record_index, token, ProseTokenRoleIR::control,
                         error))
        return false;
      continue;
    }
    Item item;
    item.kind = ItemKind::token;
    item.token = view_token(records, record_index, token);
    items.push_back(std::move(item));
  }
  Item end;
  end.kind = ItemKind::segment_end;
  items.push_back(std::move(end));
  return true;
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
  // The header stands on the opcode's own display line.  A token past it is
  // the next row's length byte or text, even when the dictionary spells it
  // like an operand: SC31-8046-00 1.8.280 record 4483 line 7 is
  // `cz OFF XMP` (tokens 55-57), and token 58, the next row's length byte,
  // spells `21`.
  const auto* opcode_line =
      segment.source_tokens.empty()
          ? nullptr
          : display_line_of_token(record, segment.source_tokens.front());
  // The reader reads a control only from a display line whose first byte is
  // not a blank; a row that starts with blanks is text whatever words it
  // holds.  G544-3290-04 GLOSSARY.2 stores the row ` | to any size.  The
  // IBM outline font character sets have a CZ prefix.`, and the dictionary
  // word `CZ` in it opened a segment here; hosted prints the row.  A `cz`
  // that is not the first token of its display line is such text: the
  // segment's tokens re-enter the stream as row tokens.
  if (opcode_line != nullptr &&
      segment.source_tokens.front() > opcode_line->prefix_token + 1) {
    for (const auto token : segment.source_tokens) {
      Item item;
      item.kind = ItemKind::token;
      item.token = view_token(records, record_index, token);
      items.push_back(std::move(item));
    }
    Item end;
    end.kind = ItemKind::segment_end;
    items.push_back(std::move(end));
    return true;
  }
  for (const auto token : segment.source_tokens) {
    const auto view = view_token(records, record_index, token);
    if (stage == 3 && opcode_line != nullptr &&
        token >= opcode_line->token_end)
      stage = 4;
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
          return collect_inert_directive(records, record_index, segment,
                                         ledger, items, error);
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
  // A bare `cz` matches no BookServer prefix either.  `cz FLOW` or `cz OFF`
  // without a tag does match one (`CZ FLOW`, `CZ OFF`), but no block kind,
  // so it still declines.
  if (stage == 1) {
    // Only the opcode was read, and it is already the control.
    Item end;
    end.kind = ItemKind::segment_end;
    items.push_back(std::move(end));
    return true;
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
         tag.back() <= '6';
}

bool has_text(const Line& line) { return line.cells.size() > line.text_begin; }

// Groups [begin, end) of a directive's lines: a bare spacing token or an
// anchor before a row starts a new group; rows without text separate groups.
// With `rules`, a `U+2500` rule row is a group of its own (see
// `message_rule`); with `full_rules`, so is every rule row it flags.
std::vector<std::pair<std::size_t, std::size_t>> group_lines(
    const std::vector<Line>& lines, std::size_t begin, std::size_t end,
    bool rules = false, const std::vector<bool>* full_rules = nullptr) {
  std::vector<std::pair<std::size_t, std::size_t>> groups;
  const auto rule_at = [&](std::size_t index) {
    return lines[index].thematic_break &&
           (rules || (full_rules != nullptr && index < full_rules->size() &&
                      (*full_rules)[index]));
  };
  std::size_t index = begin;
  while (index < end) {
    if (rule_at(index)) {
      groups.emplace_back(index, index + 1);
      ++index;
      continue;
    }
    if (!has_text(lines[index])) {
      ++index;
      continue;
    }
    auto stop = index + 1;
    while (stop < end && lines[stop].breaks_before == 0 &&
           !lines[stop].anchor_before && !rule_at(stop)) {
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
  // A term awaiting its separately controlled body: DT waits for DD, while
  // PT waits for BREAK.  The block is created by the term directive so both
  // controls can claim one definition entry without inferring their
  // relationship from displayed text.
  std::size_t pending_definition = npos;
  // The `indent` operand of the pending term's DT: the column its
  // definition starts at.
  std::size_t pending_indent = 0;
  // The latest DT whose row carried an inline definition and nothing
  // else: its block index and its `indent` operand (npos: none).
  std::size_t inline_entry = npos;
  std::size_t inline_indent = npos;
  // Global block index of the term whose compound body is active. Child
  // blocks remain in the topic-wide source order and link back to this owner.
  std::size_t body_owner = npos;
  // The opener's `left` operand: the list's own margin (npos: unknown).
  std::size_t left = npos;
  // A PT term left alone by a row-less `cz BREAK` before the next term: the
  // term a later row-less term's break hands its rows to (`parameter_term`).
  std::size_t bare_term = npos;
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
  // Lines of the verbatim block being built that follow a `cz BREAK`
  // inside its region: each opens with a blank row (see `preformatted`).
  std::vector<std::size_t> verbatim_breaks;
  // Text line -> (block, row) of the generic regions emitted with native rows
  // (`generic_region`): an anchor standing before such a line opens that row.
  std::map<std::size_t, std::pair<std::size_t, std::size_t>> verbatim_rows;
  // Region closers whose region a `cz FLOW` already ended; they draw
  // nothing of their own (see the verbatim-region branch of `handle`).
  std::vector<std::size_t> inert_closers;
  // First display line of a body group merged into an earlier block (the
  // inline definition a `cz BREAK` or `cz FLOW DD` gives its term) -> that
  // block: an anchor standing before the line belongs to it.
  std::map<std::size_t, std::size_t> merged_lines;
  // Lines that are a full-width rule (`full_width_rule_row`): the page's
  // `<hr>` wherever the rows flow, not only inside a message list.
  std::vector<bool> full_rules;

  CzBuilder(const std::vector<DecodedLogicalRecordSource>& sources,
            const LineBuild& lines_build, Ledger& owner, ProseTopicIR& out,
            std::string* message)
      : records(sources), build(lines_build), ledger(owner), topic(out),
        error(message), lines(lines_build.lines) {}

  std::size_t active_body_owner() const {
    for (auto frame = stack.rbegin(); frame != stack.rend(); ++frame)
      if (frame->body_owner != npos) return frame->body_owner;
    return npos;
  }

  void append(ProseBlockIR block, std::size_t first_line) {
    const auto index = topic.blocks.size();
    const auto parent = active_body_owner();
    if (parent != npos) {
      block.parent_block = parent;
      topic.blocks[parent].body_blocks.push_back(index);
    }
    topic.blocks.push_back(std::move(block));
    block_first_line.push_back(first_line);
  }

  bool emit(ProseBlockIR block, std::size_t begin, std::size_t end) {
    const auto index = topic.blocks.size();
    if (!build_block(records, lines, begin, end, block, ledger, index, error))
      return false;
    if (begin < lines.size()) block.origin = lines[begin].origin;
    append(std::move(block), begin);
    return true;
  }

  // The separator rule of a message in a message list (`message_list`):
  // one native `U+2500` row, already claimed as markers by the display-row
  // reader, that draws `<hr>` and no character.
  bool message_rule(std::size_t line) {
    const auto& source = lines[line];
    if (source.thematic_record >= records.size() ||
        source.thematic_token_begin >= source.thematic_token_end)
      return fail(error, "thematic break has no native row provenance");
    ProseBlockIR block;
    block.kind = ProseBlockKindIR::thematic_break;
    block.origin = source.origin;
    block.slices.push_back(token_slice(records[source.thematic_record],
                                       source.thematic_token_begin,
                                       source.thematic_token_end));
    append(std::move(block), line);
    return true;
  }

  bool paragraphs(const std::vector<std::pair<std::size_t, std::size_t>>& groups,
                  std::size_t from) {
    for (auto index = from; index < groups.size(); ++index) {
      if (groups[index].second == groups[index].first + 1 &&
          lines[groups[index].first].thematic_break) {
        if (!message_rule(groups[index].first)) return false;
        continue;
      }
      ProseBlockIR block;
      block.kind = ProseBlockKindIR::paragraph;
      if (!emit(std::move(block), groups[index].first, groups[index].second))
        return false;
    }
    return true;
  }

  // `12` or `a` followed by `terminator`: an ordinal label word.
  static bool ordinal_word(const std::string& text, char terminator) {
    if (text.size() < 2 || text.back() != terminator) return false;
    const auto body = text.substr(0, text.size() - 1);
    return digits(body) ||
           (body.size() == 1 &&
            std::isalpha(static_cast<unsigned char>(body[0])) != 0);
  }

  // The head of an ordered-list row (doc/boo-spec/markup.adoc, "List item
  // heads").  `1.`, `12.`, `a.` opening the row is the explicit ordinal
  // label.  Standing alone in the head columns -- at `left`, with the text
  // resuming at `indent` -- so is `7)` or `h)`: SC21-8295-03 E.0 record 840
  // `cz FLOW LI 16 20` + ` |              7)  Press F3 three times.`,
  // served (`SC21-829`, DT 19970409135933) as `<li> <br> |
  // Press F3 three times.` inside `<ol>`.
  //
  // A head of several words ending in such a label is blanked by its first
  // word only: hosted numbers the item with its `<ol>` and keeps the rest of
  // the head as text.  E.0 record 836 `cz FLOW LI 3 12` + ` | Step 1.  The
  // new release ...` is served as `<li> <br> |      1.  The new release
  // ...`, packet 3.6.3 record 142 `cz FLOW LI 3 10` + `   __ 1.  /etc/...`
  // as `<li>       1.  <kbd>/etc/...`, and SA41-4150-01 2.2.3 record 165
  // `cz FLOW LI 3 15` + `   __ Step 1.  Sign on ...` (DT 19951214132613) as
  // `<li>       <B>Step</B> <B>1.</B>  Sign on ...`; 23 books agree.  The
  // list is always `<ol type=1>`, even when the kept label is `a.`, so the
  // kept words become the item's label (`label_end`, the end of its last
  // word) and the list numbers the item by position.
  bool take_ordinal(Line& line, const LayoutDirective& directive,
                    ProseBlockIR& block, std::size_t& label_end) {
    const auto [begin, end] = first_word(line, line.text_begin);
    const auto text = cells_text(line, begin, end);
    if (end >= line.cells.size() || !line.cells[end].space) return true;
    // The words of the head columns: from `left`, every word before
    // `indent`, with the text resuming exactly at `indent`.
    auto last = std::make_pair(begin, end);
    auto resume = first_word(line, end).first;
    while (resume < directive.indent && resume < line.cells.size()) {
      last = first_word(line, resume);
      resume = first_word(line, last.second).first;
    }
    const auto head = begin == directive.left &&
                      resume == directive.indent &&
                      resume < line.cells.size();
    const auto alone = head && last.first == begin;
    if (!ordinal_word(text, '.') && !(alone && ordinal_word(text, ')'))) {
      const auto label = cells_text(line, last.first, last.second);
      if (head && !alone &&
          (ordinal_word(label, '.') || ordinal_word(label, ')')))
        return take_head_word(line, begin, end, last.second, label_end);
      return true;
    }
    block.ordinal = text;
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

  // A revision flag in front of an ordered item's head
  // (doc/boo-spec/markup.adoc, "A revision flag before an ordered item"):
  // the row's first word is one visible cell at column 1, after the
  // one-cell blank the margin opens with, and the next word is a one-word
  // ordinal label (`1.`, `a.`, `7)`) standing at the item's `left` with the
  // text resuming exactly at `indent`.  SC33-1169-00 A.1 record 1840
  // `cz FLOW LI 3 7` + ` # 1.  Create the keys` is served (DT
  // 19990319122050) as `<li> <br> #     Create the keys`; `+`, `|` and `g`
  // flags in 20 more books are served the same way.  Hosted blanks the
  // label and keeps the flag, on the line its `<br>` opens.  Any other
  // shape -- a flag before a several-word head, a head off `left`, text
  // not at `indent` -- leaves the row as stored.
  bool revision_flag(const Line& line,
                     const LayoutDirective& directive) const {
    const auto [flag, flag_end] = first_word(line, line.text_begin);
    if (flag != 1 || flag_end != 2 || directive.left <= flag_end ||
        line.cells.empty() || !line.cells[0].space)
      return false;
    const auto& cell = line.cells[flag];
    if (cell.record == npos ||
        ledger.at(cell.record, cell.token).role != ProseTokenRoleIR::text)
      return false;
    for (auto column = flag_end; column < directive.left; ++column)
      if (column >= line.cells.size() || !line.cells[column].space)
        return false;
    const auto [begin, end] = first_word(line, flag_end);
    if (begin != directive.left || end >= line.cells.size()) return false;
    const auto label = cells_text(line, begin, end);
    if (!ordinal_word(label, '.') && !ordinal_word(label, ')')) return false;
    // The label is a word of its own: no token it shares with the flag or
    // the text, and no link or font run that crosses into it.
    const auto resume = first_word(line, end).first;
    if (resume != directive.indent || resume >= line.cells.size())
      return false;
    for (auto column = begin; column < end; ++column) {
      const auto& source = line.cells[column];
      if (source.record == npos ||
          (source.record == cell.record && source.token == cell.token) ||
          ledger.at(source.record, source.token).role !=
              ProseTokenRoleIR::text)
        return false;
      for (auto other = resume; other < line.cells.size(); ++other)
        if (line.cells[other].record == source.record &&
            line.cells[other].token == source.token)
          return false;
    }
    // A font run or link covers the flag alone, the label alone (bk9b5001
    // 5.5 sets its labels bold; hosted GC30-3675-02, DT 19960606151611,
    // serves `<li> <br> | <B>  </B>  At the OS/2 prompt ...`), or starts at
    // the text; one that crosses between them leaves the row as stored.
    for (const auto* spans : {&line.fonts, &line.links})
      for (const auto& span : *spans) {
        auto from = span.begin;
        auto to = span.end;
        while (from < to && from < line.cells.size() && line.cells[from].space)
          ++from;
        while (to > from && to - 1 < line.cells.size() &&
               line.cells[to - 1].space)
          --to;
        if (from >= to || from >= resume) continue;
        if (from >= flag && to <= flag_end) continue;
        if (from >= begin && to <= end) continue;
        return false;
      }
    return true;
  }

  // Blanks the first word [begin, end) of a several-word OL head whose last
  // word ends at `head_end`; the words between stay as the item's label.  A
  // token the blanked word shares with the label, or a link that crosses
  // into it, leaves the head whole: no token is ever half printed.
  bool take_head_word(Line& line, std::size_t begin, std::size_t end,
                      std::size_t head_end, std::size_t& label_end) {
    const auto label_begin = first_word(line, end).first;
    for (auto cell = begin; cell < end; ++cell) {
      const auto& source = line.cells[cell];
      if (source.record == npos) return true;
      for (auto other = label_begin; other < line.cells.size(); ++other)
        if (line.cells[other].record == source.record &&
            line.cells[other].token == source.token)
          return true;
      if (ledger.at(source.record, source.token).role !=
          ProseTokenRoleIR::text)
        return true;
    }
    for (const auto& link : line.links)
      if (link.begin < label_begin && link.end > end) return true;
    for (auto cell = begin; cell < end; ++cell) {
      const auto& source = line.cells[cell];
      ledger.at(source.record, source.token).role =
          ProseTokenRoleIR::list_head;
    }
    // A font run over the whole head (GA27-4220-00 2.10 sets `Step 1.` bold;
    // hosted serves `<B>    </B> <B>1.</B>`) keeps its words after the blank.
    for (auto& font : line.fonts)
      if (font.begin < label_begin && font.end > label_begin)
        font.begin = label_begin;
    line.text_begin = label_begin;
    label_end = head_end;
    return clip_spans_before(line, error);
  }

  // The head of an unordered item, the columns `left` up to `indent` of its
  // first row (doc/boo-spec/markup.adoc, "List item heads").  The first level
  // stores its `U+2666` bullet as a row slot, already consumed by the
  // display-row reader.  A nested level stores a plain `-`, alone at `left`,
  // with the text resuming exactly at `indent`: SC21-8295-03 3.1.3.2 record
  // 405 `cz FLOW LI 7 11` + `       -   Change management (DSNX)`, which
  // hosted (`SC21-829`, DT 19970409135933) serves as `<li>            Change
  // management (DSNX)`.  That glyph is the bullet.  Anything else in the
  // head columns, or a `-` the text does not resume after at `indent`,
  // stays text.
  bool take_bullet(Line& line, const LayoutDirective& directive) {
    if (directive.indent < directive.left + 2) return true;
    const auto [begin, end] = first_word(line, line.text_begin);
    if (begin != directive.left || end != begin + 1) return true;
    const auto& glyph = line.cells[begin];
    if (glyph.record == npos || glyph.text != "-") return true;
    const auto text = first_word(line, end).first;
    if (text != directive.indent || text >= line.cells.size()) return true;
    auto& entry = ledger.at(glyph.record, glyph.token);
    if (entry.role != ProseTokenRoleIR::text) return true;
    entry.role = ProseTokenRoleIR::bullet;
    line.text_begin = text;
    return clip_spans_before(line, error);
  }

  // The first display line after directive `index`, or npos at the end of
  // the topic: where a block that owns no row of its own stands.
  std::size_t next_line_after(std::size_t index) const {
    for (auto next = index + 1; next < ranges.size(); ++next)
      if (ranges[next].first != npos) return ranges[next].first;
    return npos;
  }

  bool list_item(std::size_t index, const LayoutDirective& directive,
                 const std::vector<std::pair<std::size_t, std::size_t>>& groups) {
    if (stack.empty() || (stack.back().tag != "ul" && stack.back().tag != "ol" &&
                          stack.back().tag != "sl" && stack.back().tag != "notel"))
      return fail(error, "cz FLOW LI outside an open list");
    if (groups.empty()) {
      // An item that owns no row: BookServer prints `<li>` and whatever
      // follows.  GC28-1251-08 EDITION (DT 19980123093613) serves the
      // row-less `cz FLOW LI 7 7` between two address lines of its `SL` as
      // `<li> <li>        FAX ...` -- an empty item; a block directive after
      // it (a `DL`, `PARML`, `XMP` ...) is the item's content.  An ordered
      // item has no ordinal to keep, and a NOTEL note no number, so those
      // still fail.
      //
      // Nor does the item need a row after it: SC31-8569-00 D.2.1.1 (DT
      // 19980126130303) ends its `SL` with two row-less `cz FLOW LI 7 7`
      // and `cz OFF ESL 0 0`, the last rows of the topic, and hosted serves
      // `<li>` `<li>` `</ul>` -- two empty items.  Such an item stands at
      // its directive's `left` column, after every row.
      const auto first_line = next_line_after(index);
      if (stack.back().tag == "ol" || stack.back().tag == "notel")
        return fail(error, "cz FLOW LI has no text");
      auto& frame = stack.back();
      frame.body_owner = npos;
      ProseBlockIR block;
      block.kind = ProseBlockKindIR::list_item;
      block.list_ordinal = frame.list_ordinal;
      block.slices.push_back(directive.source);
      block.origin =
          first_line == npos ? directive.left : lines[first_line].origin;
      ++frame.items;
      const auto block_index = topic.blocks.size();
      append(std::move(block), first_line == npos ? lines.size() : first_line);
      frame.body_owner = block_index;
      return true;
    }
    auto& frame = stack.back();
    // The previous LI's body ends at this sibling LI. Clear it before
    // appending the new item so the new item keeps the enclosing owner (for
    // example, a parameter definition) rather than becoming its sibling's
    // child.
    frame.body_owner = npos;
    // A NOTEL item is a numbered note, and its number is text.  Hosted
    // BookServer serves every item as a bare `<dd>` holding the row as
    // stored, number included, with no list element around the items:
    // SC09-2417-00 2.2.4 record 210 `cz FLOW NOTEL 3 3`, `cz FLOW LI 3 7`,
    // `   1.  The type parameter ...` is served (`SC09-241`, DT
    // 19961114175628) as `<dd>    1.  The <var>type</var> parameter ...`,
    // and SC21-8295-03 2.2, 2.4, 2.8, 3.1.3.2 and C.2 (`SC21-829`, DT
    // 19970409135933) the same way.  An OL item's head, by contrast, is a
    // label hosted's `<ol>` replaces.  So each note is a paragraph that
    // keeps its number, and what the source nests after it follows it.
    if (frame.tag == "notel") {
      ++frame.items;
      return paragraphs(groups, 0);
    }
    ProseBlockIR block;
    block.kind = ProseBlockKindIR::list_item;
    block.list_ordinal = frame.list_ordinal;
    block.ordered = frame.tag == "ol";
    ++frame.items;
    auto& first = lines[groups.front().first];
    auto label_end = npos;
    Line flag_line;
    const auto flagged = block.ordered && revision_flag(first, directive);
    if (flagged) {
      flag_line = first;
      const auto flag_end = first.text_begin + 1;
      flag_line.cells.resize(flag_end);
      if (!clip_spans_after(flag_line, flag_end, error)) return false;
      first.text_begin = directive.left;
    }
    if (block.ordered && !take_ordinal(first, directive, block, label_end))
      return false;
    if (flagged) {
      if (block.ordinal.empty() || label_end != npos)
        return fail(error, "cz FLOW LI revision flag has no ordinal label");
      // The label is blanked, but it does not number the item or pick the
      // list's alphabet: hosted serves a list whose first row is flagged
      // with an alphabet and start no stored byte explains (`SC33-1169-00`
      // A.1 ` #     a.` in `<ol type=1 start=1>`, `SC09-2133-01` 14.1
      // ` |     a.` in `<ol type=a start=2>`; doc/boo-spec/markup.adoc, "OL
      // with a one-word letter head").  The item keeps its position.
      block.ordinal.clear();
      // The flag's row opens on the line hosted's break starts: a row
      // boundary first, then the flag, then the item's text.
      const auto [begin, end] = groups.front();
      std::vector<Line> parts{std::move(flag_line), first};
      for (auto index = begin + 1; index < end; ++index)
        parts.push_back(lines[index]);
      const auto block_index = topic.blocks.size();
      block.origin = lines[begin].origin;
      ProseInlineIR boundary;
      boundary.kind = ProseInlineKindIR::line_break;
      block.inlines.push_back(std::move(boundary));
      if (!build_block(records, parts, 0, parts.size(), block, ledger,
                       block_index, error))
        return false;
      append(std::move(block), begin);
      frame.body_owner = block_index;
      return paragraphs(groups, 1);
    }
    if (frame.tag == "ul" && !take_bullet(first, directive)) return false;
    if (!has_text(first)) return fail(error, "cz FLOW LI has no text");
    const auto block_index = topic.blocks.size();
    if (label_end != npos) {
      // The label and the text are built from a private line vector, the
      // label first, so the shared line indices stay valid for provenance.
      const auto [begin, end] = groups.front();
      Line label = lines[begin];
      label.cells.resize(label_end);
      if (!clip_spans_after(label, label_end, error)) return false;
      Line rest = lines[begin];
      rest.text_begin = first_word(rest, label_end).first;
      if (!clip_spans_before(rest, error)) return false;
      std::vector<Line> parts{std::move(label), std::move(rest)};
      for (auto index = begin + 1; index < end; ++index)
        parts.push_back(lines[index]);
      block.origin = lines[begin].origin;
      if (!build_block(records, parts, 0, 1, block, ledger, block_index,
                       error))
        return false;
      block.term_inline_count = block.inlines.size();
      if (!build_block(records, parts, 1, parts.size(), block, ledger,
                       block_index, error))
        return false;
      if (block.term_inline_count == 0 ||
          block.term_inline_count == block.inlines.size())
        return fail(error, "cz FLOW LI label or text is empty");
      append(std::move(block), begin);
      frame.body_owner = block_index;
      return paragraphs(groups, 1);
    }
    if (!emit(std::move(block), groups.front().first, groups.front().second))
      return false;
    frame.body_owner = block_index;
    return paragraphs(groups, 1);
  }

  bool definition(const LayoutDirective& directive,
                  const std::vector<std::pair<std::size_t, std::size_t>>& groups,
                  bool separate_body) {
    if (stack.empty() || stack.back().tag != "dl")
      return fail(error, "cz FLOW DT outside an open definition list");
    if (groups.empty()) return fail(error, "cz FLOW DT has no text");
    auto& frame = stack.back();
    term_alone(frame);
    frame.body_owner = npos;
    frame.inline_entry = npos;
    frame.inline_indent = npos;
    ++frame.items;
    const auto [begin, end] = groups.front();
    auto& row = lines[begin];
    // An empty term: the first display row of the entry starts at or past
    // the `indent` column, so the term columns `left`..`indent` hold no
    // word and every row of the directive is the definition.  GA19-5486-13
    // 3.5.1 record 76 closes its notes with `cz FLOW DT 3 5` and the row
    // `     General Note: ...` at column 5; SC21-8295-03 2.2 record 72 has
    // `cz FLOW DT 3 13` and `The following display is shown ...` at column
    // 13.  The DT control is the entry's only source of its own; the rows
    // are its body paragraphs.  An empty term before a separate DD has no
    // such evidence and still fails.
    if (directive.indent > directive.left &&
        directive.indent <= row.text_begin && has_text(row)) {
      if (separate_body)
        return fail(error, "cz FLOW DT has an empty term before cz FLOW DD");
      ProseBlockIR block;
      block.kind = ProseBlockKindIR::definition_entry;
      block.list_ordinal = frame.list_ordinal;
      block.slices.push_back(directive.source);
      block.origin = row.origin;
      const auto index = topic.blocks.size();
      append(std::move(block), begin);
      frame.body_owner = index;
      return paragraphs(groups, 0);
    }
    if (separate_body) {
      if (groups.size() != 1)
        return fail(error, "cz FLOW DT before DD has multiple term groups");
      ProseBlockIR block;
      block.kind = ProseBlockKindIR::definition_entry;
      block.list_ordinal = frame.list_ordinal;
      block.origin = row.origin;
      const auto index = topic.blocks.size();
      if (!build_block(records, lines, begin, end, block, ledger, index,
                       error))
        return false;
      block.term_inline_count = block.inlines.size();
      append(std::move(block), begin);
      frame.pending_definition = index;
      frame.pending_indent = npos;
      return true;
    }
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
    // A following DD can supply an inline body. A following region instead
    // becomes a child block of this entry, retaining both global ledger
    // identity and compound definition ownership.
    ProseBlockIR block;
    block.kind = ProseBlockKindIR::definition_entry;
    block.list_ordinal = frame.list_ordinal;
    block.origin = row.origin;
    const auto index = topic.blocks.size();
    if (!build_block(records, parts, 0, 1, block, ledger, index, error))
      return false;
    block.term_inline_count = block.inlines.size();
    const auto body_has_text =
        std::any_of(parts.begin() + 1, parts.end(), has_text);
    if (!body_has_text) {
      append(std::move(block), begin);
      frame.pending_definition = index;
      frame.pending_indent = directive.indent;
      return true;
    }
    if (!build_block(records, parts, 1, parts.size(), block, ledger, index, error))
      return false;
    append(std::move(block), begin);
    if (groups.size() == 1) {
      frame.inline_entry = index;
      frame.inline_indent = directive.indent;
    }
    return paragraphs(groups, 1);
  }

  bool definition_body(
      const std::vector<std::pair<std::size_t, std::size_t>>& groups) {
    if (stack.empty() || stack.back().tag != "dl")
      return fail(error, "cz FLOW DD outside an open definition list");
    auto& frame = stack.back();
    if (frame.pending_definition == npos) {
      const auto body = groups.empty() ? std::string{} :
                                         line_text(lines[groups.front().first]);
      return fail(error, "cz FLOW DD has no preceding term before '" + body +
                             "'");
    }
    if (groups.empty()) return fail(error, "cz FLOW DD has no text");
    const auto block_index = frame.pending_definition;
    if (block_index >= topic.blocks.size())
      return fail(error, "internal: cz FLOW DD addresses no definition term");
    auto& block = topic.blocks[block_index];
    if (!build_block(records, lines, groups.front().first,
                     groups.front().second, block, ledger, block_index,
                     error))
      return false;
    merged_lines[groups.front().first] = block_index;
    frame.pending_definition = npos;
    frame.body_owner = block_index;
    return paragraphs(groups, 1);
  }

  bool empty_definition_with_block_body(const LayoutDirective& directive,
                                        std::size_t first_body_line) {
    if (stack.empty() || stack.back().tag != "dl")
      return fail(error, "cz FLOW DT outside an open definition list");
    auto& frame = stack.back();
    term_alone(frame);
    frame.body_owner = npos;
    ++frame.items;
    ProseBlockIR block;
    block.kind = ProseBlockKindIR::definition_entry;
    block.list_ordinal = frame.list_ordinal;
    // The term deliberately has no visible inline. Its DT control is the
    // stable source identity of the empty semantic node; the following LINES
    // region has its own source slices on its child block.
    block.slices.push_back(directive.source);
    if (first_body_line < lines.size())
      block.origin = lines[first_body_line].origin;
    const auto block_index = topic.blocks.size();
    append(std::move(block), first_body_line);
    frame.body_owner = block_index;
    return true;
  }

  bool empty_definition_body_before_block() {
    if (stack.empty() || stack.back().tag != "dl")
      return fail(error, "cz FLOW DD outside an open definition list");
    auto& frame = stack.back();
    if (frame.pending_definition == npos)
      return fail(error, "cz FLOW DD has no preceding term");
    frame.body_owner = frame.pending_definition;
    frame.pending_definition = npos;
    return true;
  }

  // A term whose body never comes -- the next term or the list's closer
  // follows -- is a term alone; the entry already holds it.  BookServer has
  // no term/body pairing at all: `CZ FLOW PT` and `CZ FLOW DT` print the
  // term and `CZ BREAK` or `CZ FLOW DD` the body, each on its own.
  // GC24-5851-01 11.5.2 (DT 19990506135028) stores `cz FLOW PT 7 11`,
  // `     parms`, an empty `cz FLOW P 11 11` and the next `cz FLOW PT`;
  // hosted serves `<p><br> |     <var>parms</var>` and nothing more.
  void term_alone(Frame& frame) {
    frame.pending_definition = npos;
  }

  bool parameter_term(
      const LayoutDirective& directive,
      const std::vector<std::pair<std::size_t, std::size_t>>& groups,
      std::size_t directive_index) {
    const auto next_is_verbatim_region =
        directive_index + 1 < build.directives.size() &&
        build.directives[directive_index + 1].mode == "off" &&
        cz_verbatim_region_tag(build.directives[directive_index + 1].tag);
    if (stack.empty() || stack.back().tag != "parml")
      return fail(error, "cz FLOW PT outside an open parameter list");
    auto& frame = stack.back();
    const auto waiting = frame.pending_definition;
    const auto bare = frame.bare_term;
    frame.bare_term = npos;
    term_alone(frame);
    if (groups.empty() && break_with_rows_follows(directive_index)) {
      // A term with no row of its own whose `cz BREAK` carries rows: the
      // reader prints nothing visible for the generic `CZ FLOW` and `<br>`
      // plus the rows for the break, so the rows run on from what precedes.
      // SC31-8513-00 `2.5.4.35` (DT 19980219192539) stores `cz FLOW PT 3 7`
      // `   LU - LU_name`, a row-less `cz FLOW PT 3 7` and `cz BREAK 3` with
      // the description, served `<kbd>LU</kbd> <kbd>-</kbd>
      // <var>LU_name</var>`, `<p><br>` and the description.  The rows are
      // the body of the term still waiting for one -- the term before, or a
      // term a row-less break left alone (dfhp3a06 4.6.4.2
      // `counter_options`, `cz BREAK 7`, then the row-less PT and
      // `cz BREAK 3`).  With no such term they are the body of an entry
      // whose term is empty, as for a row-less PT before a region.
      const auto owner = waiting != npos ? waiting : bare;
      if (owner != npos) {
        frame.pending_definition = owner;
        return true;
      }
      frame.body_owner = npos;
      ++frame.items;
      ProseBlockIR block;
      block.kind = ProseBlockKindIR::definition_entry;
      block.list_ordinal = frame.list_ordinal;
      block.slices.push_back(directive.source);
      const auto first_line = next_line_after(directive_index);
      block.origin = lines[first_line].origin;
      const auto block_index = topic.blocks.size();
      append(std::move(block), first_line);
      frame.body_owner = block_index;
      return true;
    }
    if (groups.empty() && next_is_verbatim_region) {
      // A term with no row of its own whose words are the syntax diagram
      // after it: BookServer prints nothing for the generic `CZ FLOW` and
      // the region's `<pre>` follows.  GC24-5837-01 `5.2.8.3` (DT
      // 19990503161545) stores `cz FLOW PT 7 11` and directly
      // `cz OFF SYNTAX` with the `MODENUM` diagram, hosted as `<p><p>`
      // and the diagram's `<pre width="80">`.  The entry's term is empty
      // and the region is its body, as for an empty `DT` before a region.
      frame.body_owner = npos;
      ++frame.items;
      ProseBlockIR block;
      block.kind = ProseBlockKindIR::definition_entry;
      block.list_ordinal = frame.list_ordinal;
      block.slices.push_back(directive.source);
      block.origin = directive.left;
      const auto block_index = topic.blocks.size();
      append(std::move(block), next_line_after(directive_index) == npos
                                   ? lines.size()
                                   : next_line_after(directive_index));
      frame.body_owner = block_index;
      return true;
    }
    if (groups.empty()) {
      // A term that owns no row, followed by nothing that draws (row-less
      // breaks, then the list's closer, the next term or the topic's end),
      // draws nothing: BookServer prints `<p>` and `<br>`.  GC31-8553-00
      // `4.9` (DT 19970730115116) ends its parameter list with
      // `cz FLOW PT 3 7`, `cz BREAK 3` and `cz OFF EPARML 0 0`, all
      // row-less, and hosted serves nothing visible before `Examples`.
      auto next = directive_index + 1;
      while (next < build.directives.size() &&
             build.directives[next].mode == "break" &&
             ranges[next].first == npos)
        ++next;
      if (next >= build.directives.size() ||
          (build.directives[next].mode == "off" &&
           build.directives[next].tag == "eparml") ||
          (build.directives[next].mode == "flow" &&
           build.directives[next].tag == "pt"))
        return true;
      return fail(error, "cz FLOW PT has no text");
    }
    // A term split over source paragraphs needs compound definition content;
    // flattening the groups here would erase a source-declared boundary.
    if (groups.size() != 1)
      return fail(error, "cz FLOW PT has multiple paragraph groups");
    frame.body_owner = npos;
    ++frame.items;
    const auto [begin, end] = groups.front();
    ProseBlockIR block;
    block.kind = ProseBlockKindIR::definition_entry;
    block.list_ordinal = frame.list_ordinal;
    block.origin = lines[begin].origin;
    const auto block_index = topic.blocks.size();
    // A term whose further rows all stand at the directive's indent column
    // carries its description itself, with no `cz BREAK`: SC31-8212-00
    // 2.3.4.2.3 (DT 19950906051034) stores `cz FLOW PT 5 8`,
    // `     RECEIVE_FILL_BUFFER` and three rows at column 8, which hosted
    // serves as `<p>     <tt>RECEIVE_FILL_BUFFER</tt>` and those rows.  The
    // first row is the term, the rest its description.
    const auto described =
        end - begin > 1 && directive.indent > directive.left &&
        lines[begin].text_begin < directive.indent &&
        std::all_of(lines.begin() + static_cast<std::ptrdiff_t>(begin + 1),
                    lines.begin() + static_cast<std::ptrdiff_t>(end),
                    [&directive](const Line& line) {
                      return !has_text(line) ||
                             line.text_begin == directive.indent;
                    });
    if (!build_block(records, lines, begin, described ? begin + 1 : end,
                     block, ledger, block_index, error))
      return false;
    block.term_inline_count = block.inlines.size();
    if (described &&
        !build_block(records, lines, begin + 1, end, block, ledger,
                     block_index, error))
      return false;
    append(std::move(block), begin);
    if (described) {
      frame.body_owner = block_index;
      return true;
    }
    frame.pending_definition = block_index;
    return true;
  }

  // The directive after `index` is a `cz BREAK` that owns a row with text.
  bool break_with_rows_follows(std::size_t index) const {
    const auto next = index + 1;
    if (next >= build.directives.size() ||
        build.directives[next].mode != "break" || ranges[next].first == npos)
      return false;
    return std::any_of(
        lines.begin() + static_cast<std::ptrdiff_t>(ranges[next].first),
        lines.begin() + static_cast<std::ptrdiff_t>(ranges[next].second),
        [](const Line& line) { return has_text(line); });
  }

  bool parameter_body(
      const std::vector<std::pair<std::size_t, std::size_t>>& groups) {
    if (stack.empty() || stack.back().tag != "parml" ||
        stack.back().pending_definition == npos)
      return fail(error, "cz BREAK is not the body of a cz FLOW PT term");
    if (groups.empty()) return fail(error, "cz BREAK after PT has no text");
    auto& frame = stack.back();
    const auto block_index = frame.pending_definition;
    if (block_index >= topic.blocks.size())
      return fail(error, "internal: cz BREAK addresses no parameter term");
    auto& block = topic.blocks[block_index];
    if (!build_block(records, lines, groups.front().first,
                     groups.front().second, block, ledger, block_index,
                     error))
      return false;
    merged_lines[groups.front().first] = block_index;
    frame.pending_definition = npos;
    frame.body_owner = block_index;
    return paragraphs(groups, 1);
  }

  // `cz FLOW LP` inside a DL: a list part, the paragraph at the list's own
  // margin that belongs to no entry and divides two runs of its entries.
  // Hosted BookServer serves it as `<br>` and the text inside the one `<dl>`,
  // and the `<dt>` entries after it continue that list:
  //  * SC33-1119-00 4.5.1.1 (DT 19960129110036) record 193 stores
  //    `cz FLOW LP 3 3` and the row `   The remaining options specify the
  //    attributes of the 4-tuple.` after the DT/DD entry `Protocol`, served
  //    as `<br>   The remaining options ...` and three more `<dt>` entries;
  //  * SC33-1692-01 2.3.4.1 (DT 19970821113553) record 175 stores the rows
  //    `   Definition:` and `   Operations:` on its two LPs, served as
  //    `<dl>\n<br>   Definition:\n<dt>` .. `<br>   Operations:\n<dt>`;
  //  * SC33-1692-02 2.3.4.1 (DT 19990303174038) record 170 stores each LP
  //    without rows and the row on the `cz FLOW P 3 3` that follows it at
  //    the same margin -- the DFHP7A00 2.3.4.1 shape -- and is served
  //    exactly like SC33-1692-01.
  // The part ends the body of the entry before it.  HTML has no paragraph
  // inside a `<dl>`, so the part lowers as a paragraph between two runs of
  // the list, like the list opener's lead-in rows.  A part anywhere but at
  // the list margin, after a term still waiting for its definition, or with
  // no text of its own and no paragraph at its margin, has no reference.
  bool list_part(std::size_t index, const LayoutDirective& directive,
                 const std::vector<std::pair<std::size_t, std::size_t>>& groups) {
    auto& frame = stack.back();
    if (frame.pending_definition != npos)
      return fail(error, "cz FLOW LP follows a term with no definition");
    if (frame.left == npos || directive.left != frame.left)
      return fail(error, "cz FLOW LP is not at its definition list's margin");
    if (groups.empty()) {
      const auto next = index + 1;
      const auto text_follows =
          next < build.directives.size() &&
          build.directives[next].mode == "flow" &&
          build.directives[next].tag == "p" &&
          build.directives[next].left == directive.left &&
          ranges[next].first != npos &&
          std::any_of(lines.begin() +
                          static_cast<std::ptrdiff_t>(ranges[next].first),
                      lines.begin() +
                          static_cast<std::ptrdiff_t>(ranges[next].second),
                      [](const Line& line) { return has_text(line); });
      if (!text_follows) return fail(error, "cz FLOW LP has no list-part text");
    }
    frame.body_owner = npos;
    frame.inline_entry = npos;
    frame.inline_indent = npos;
    return paragraphs(groups, 0);
  }

  // A `cz FLOW PT` with no open parameter list opens one when the topic
  // closes it: the compiler omits the `FLOW PARML` opener but keeps the
  // PT/BREAK entries and the `cz OFF EPARML` closer.  BookServer has no
  // parameter-list rule at all (no `PARML` string exists in the reader):
  // `CZ FLOW PT` is the generic `CZ FLOW` and `CZ BREAK` prints `<br>`, so
  // an entry is served identically with or without the opener.  The opener
  // is missing after a syntax diagram (DFHP7A00 2.5.3--2.5.7; SC31-8778-01
  // 4.1.4.51, DT 20010606144047), after a table (SC41-5605-00 3.3.7.24, DT
  // 19980512102401, `cz OFF ETABLE 4 4` then `cz FLOW PT 3 7` `DATA`,
  // served `<kbd>DATA</kbd>` `<br>` and the indented body), after a
  // paragraph (SC41-5864-02 1.12.1, `Length of receiver variable` `<br>`
  // `INPUT; BINARY(4)`), after a note (dknc3a02 1.5.1) and inside a list
  // item (SC31-8513-00 2.12.1.5, `<var>data_set_name</var>` `<br>`).  Entries
  // may hold paragraphs, lists, notes and regions between them, as in an
  // opened list.  The closer is the proof: the first `EPARML` that closes no
  // list opened after the term.  A term with none still fails.
  bool implicit_parameter_list(std::size_t index) const {
    std::size_t depth = 0;
    for (auto cursor = index + 1; cursor < build.directives.size(); ++cursor) {
      const auto& candidate = build.directives[cursor];
      if (candidate.mode == "flow" && candidate.tag == "parml") ++depth;
      if (candidate.mode != "off" || candidate.tag != "eparml") continue;
      if (depth == 0) return true;
      --depth;
    }
    return false;
  }

  bool note(const std::vector<std::pair<std::size_t, std::size_t>>& groups) {
    if (groups.empty()) return fail(error, "cz FLOW NT has no text");
    const auto [begin, end] = groups.front();
    auto& row = lines[begin];
    const auto [label_begin, label_end] = first_word(row, row.text_begin);
    const auto label = cells_text(row, label_begin, label_end);
    // BookServer has no note rule: `CZ FLOW NT` and `CZ FLOW NOTE` match only
    // the generic `CZ FLOW`, and the rows flow as a paragraph.  A note is
    // read only where the first word is its label; otherwise -- a revision
    // mark before `Note:`, a several-word label such as SC09-2671-00 H.0's
    // `Note on Skipping Releases:` (DT 19990222160950, served as plain
    // flowing text), or a label with nothing after it -- the rows are the
    // paragraphs BookServer prints.
    auto content_follows = end - begin > 1;
    for (auto cell = label_end; !content_follows && cell < row.cells.size();
         ++cell)
      content_follows = !row.cells[cell].space;
    if (label.size() < 2 || label.back() != ':' || !content_follows)
      return paragraphs(groups, 0);
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
    append(std::move(block), begin);
    return paragraphs(groups, 1);
  }

  // The rows a named closer carries -- `cz OFF ETABLE`, `EFIG`, `ELBLBOX`,
  // `EDL`.  The reader prints `</pre>` and opens `<pre width="132">` at such
  // a closer (unless a figure or labelled box is still open, when the rows
  // are that object's) and the next `CZ FLOW` closes it, so the rows are
  // drawn as stored: SC41-3512-05 APPENDIX1.7.1 (DT 19980528085436) serves
  // the prose and `__ 1.` rows after an unopened `cz OFF ELBLBOX 0 0` in
  // `<pre width="132"><!-- * -->`; 145 sampled books serve the rows after a
  // named closer inside that `<pre>`.
  bool named_closer_rows(std::pair<std::size_t, std::size_t> range) {
    if (range.first == npos ||
        std::none_of(lines.begin() + static_cast<std::ptrdiff_t>(range.first),
                     lines.begin() + static_cast<std::ptrdiff_t>(range.second),
                     [](const Line& line) { return has_text(line); }))
      return true;
    // The plain verbatim reading draws no link: rows that carry one keep
    // the flowed reading rather than drop it.  (The styled native reading
    // of generic regions does not verify for margin-marked rows here.)
    if (std::any_of(lines.begin() + static_cast<std::ptrdiff_t>(range.first),
                    lines.begin() + static_cast<std::ptrdiff_t>(range.second),
                    [](const Line& line) { return !line.links.empty(); })) {
      bool matched = false;
      if (!native_preformatted(range.first, range.second, matched))
        return false;
      if (matched) return true;
      return paragraphs(group_lines(lines, range.first, range.second,
                                    in_message_list(), &full_rules),
                        0);
    }
    return preformatted(range.first, range.second);
  }

  // The rows [begin, end) as one verbatim block that keeps their `CFONT`
  // highlighting and `CSELECT` links, when every text row is one whole
  // native display row (`build_native_verbatim_block`).  BookServer prints
  // the rows of every `<pre>` with their highlighting: GC35-0316-00 3.1.6
  // (DT 19980403120326) serves the `except` on a row its `cz OFF EXMP` closer
  // carries, inside the example's `<pre width="80">`, as `<I>except</I>`, and
  // GC33-1177-01 2.3.34 (DT 19990318184419) its `X'8001' - WRITE` rows as
  // `<samp>` words.  `matched` is false when the rows have no such reading
  // (a `cz BREAK` row boundary, a rule row, a row the line builder
  // assembled); the caller keeps its plain reading then.
  bool native_preformatted(std::size_t begin, std::size_t end, bool& matched) {
    matched = false;
    if (begin >= end) return true;
    for (auto line = begin; line < end; ++line) {
      if (lines[line].thematic_break) return true;
      if (line != begin &&
          std::find(verbatim_breaks.begin(), verbatim_breaks.end(), line) !=
              verbatim_breaks.end())
        return true;
    }
    ProseBlockIR block;
    std::vector<std::pair<std::size_t, std::size_t>> line_rows;
    if (!build_native_verbatim_block(records, lines, begin, end, block,
                                     ledger, topic.blocks.size(), matched,
                                     line_rows))
      return false;
    if (!matched) return true;
    for (const auto& [line, row] : line_rows)
      verbatim_rows[line] = {topic.blocks.size(), row};
    append(std::move(block), begin);
    return true;
  }

  // `cz OFF XMP` .. `cz OFF EXMP`: the display rows verbatim.  CFONT spans
  // inside (hosted `<samp>` per word) carry no structure the block keeps.
  bool preformatted(std::size_t begin, std::size_t end,
                    const std::string& degradation_code = {},
                    const std::string& degradation_detail = {}) {
    // A full-width rule inside the region is the page's `<hr>`, not a drawn
    // row (`full_width_rule` in the line builder): the region's rows on
    // either side of it are blocks of their own.
    for (auto line = begin; line < end; ++line) {
      if (!lines[line].thematic_break) continue;
      const auto draws = [&](std::size_t from, std::size_t to) {
        return std::any_of(
            lines.begin() + static_cast<std::ptrdiff_t>(from),
            lines.begin() + static_cast<std::ptrdiff_t>(to),
            [](const Line& row) { return has_text(row); });
      };
      if (draws(begin, line) &&
          !preformatted(begin, line, degradation_code, degradation_detail))
        return false;
      if (!message_rule(line)) return false;
      if (!draws(line + 1, end)) return true;
      return preformatted(line + 1, end, degradation_code,
                          degradation_detail);
    }
    if (degradation_code.empty()) {
      bool matched = false;
      if (!native_preformatted(begin, end, matched)) return false;
      if (matched) return true;
    }
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
      if (line.breaks_before == 0 && line_index != begin &&
          std::find(verbatim_breaks.begin(), verbatim_breaks.end(),
                    line_index) != verbatim_breaks.end()) {
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
    append(std::move(block), begin);
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

  // A rule row the reader empties: more than 71 consecutive `U+2500` cells
  // from display column 3 of its native display line, the rule it replaces
  // with the page's `<hr>` everywhere but in a `cz OFF TABLE` region
  // (`full_width_rule` in the line builder).  The region's rule rows are
  // such rows on every sampled hosted cover (26 of 26 `cz OFF COVER`
  // books, SC41-4715-00 COVER DT 19950620134301 `<hr>` .. `<hr>`).
  // Returns the row's single record and token extent, or nothing for a
  // shorter rule.
  std::optional<std::tuple<std::size_t, std::size_t, std::size_t>>
  full_width_rule_row(const Line& line) const {
    std::size_t record = npos, first = npos, last = 0;
    if (line.thematic_break && line.thematic_record < records.size() &&
        line.thematic_token_begin < line.thematic_token_end) {
      record = line.thematic_record;
      first = line.thematic_token_begin;
      last = line.thematic_token_end - 1;
    }
    for (const auto& cell : line.cells) {
      if (cell.record == npos) continue;
      if (record != npos && cell.record != record) return std::nullopt;
      record = cell.record;
      first = std::min(first, cell.token);
      last = std::max(last, cell.token);
    }
    if (record == npos) return std::nullopt;
    const auto* native = display_line_of_token(records[record], first);
    if (native == nullptr || last >= native->token_end) return std::nullopt;
    const auto columns = display_line_columns(records[record], *native);
    std::size_t run = 0;
    for (auto column = std::size_t{3};
         column < columns.size() && columns[column] == 0x2500; ++column)
      ++run;
    if (run <= 71) return std::nullopt;
    return std::make_tuple(record, first, last + 1);
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
    append(std::move(block), begin);
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
      if (!rule_row(lines[line]) && !lines[line].thematic_break) continue;
      if (!title_rows(rows_begin, line)) return false;
      claim_rule_row(lines[line]);
      if (const auto rule = full_width_rule_row(lines[line])) {
        ProseBlockIR block;
        block.kind = ProseBlockKindIR::thematic_break;
        block.origin = lines[line].origin;
        block.slices.push_back(token_slice(records[std::get<0>(*rule)],
                                           std::get<1>(*rule),
                                           std::get<2>(*rule)));
        append(std::move(block), line);
      }
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
    return paragraphs(group_lines(lines, closer.first, closer.second,
                                  in_message_list(), &full_rules),
                      0);
  }

  // What `generic_region` decided about one unmodelled `cz OFF <tag>`
  // directive.
  enum class RegionOutcome {
    // The region was emitted verbatim; `index` advanced past what it owns.
    emitted,
    // It is a region and lowering it failed; `error` is set.
    declined,
    // Not a bounded region at all.  The caller declines the topic with its
    // own message.
    not_a_region,
  };

  // A `cz OFF <tag>` the dialect gives no structure of its own -- DANGER,
  // DIRECT, CGRAPHIC, MAP, PARTSASM, LQ, SCHDATE ... -- is the reader's
  // generic region.  BookServer has one rule for every such tag (doc/boo-spec/
  // markup.adoc, "Reading a CZ body"): the generic `CZ OFF` opens
  // `<pre width="80">` and prints the rows in it as stored, keeping their
  // `CFONT` highlighting and `CSELECT` links; the generic `CZ OFF E` closer
  // prints nothing and leaves that `<pre>` open, so the rows the closer owns
  // are rows of the region too; only a `CZ FLOW` closes it.  Over 22 hosted
  // books with a closed generic region (SC41-4153-00, SC31-6071-04,
  // SC33-1658-04, SA41-4109-00, SY27-0345-05, SC24-5754-00 ...) every stored
  // row of the region is a row of that `<pre>`, highlighted where the source
  // highlights it, and over 30 books `cz OFF CGRAPHIC` regions that no closer
  // ends are served the same way up to the next `CZ FLOW` (SC33-0824-02
  // 2.1.1.1, SC31-8266-00 8.4.2, SC41-4206-00 B.0 ...).
  //
  // The region therefore ends at its own matched `cz OFF E<tag>` (whose rows
  // it keeps, as `artwork` keeps its closer's) or at the next `cz FLOW`.  A
  // footnote end marker that displays nothing, an `SRFIG`/`SRTBL` envelope
  // delimiter and a `cz BREAK` are rows of the region rather than breaks in
  // it.  Any other directive leaves the boundary unproven and the topic
  // declines whole.
  RegionOutcome generic_region(std::size_t& index,
                               std::pair<std::size_t, std::size_t> range,
                               const std::string& tag) {
    const auto closer_tag = "e" + tag;
    auto closer_index = index + 1;
    auto region_begin = range.first;
    auto region_end = range.second;
    std::vector<std::size_t> breaks;
    const auto take = [&](std::pair<std::size_t, std::size_t> rows,
                          bool is_break) {
      if (rows.first == npos) return;
      if (region_begin == npos) region_begin = rows.first;
      else if (is_break) breaks.push_back(rows.first);
      region_end = rows.second;
    };
    bool closed = false;
    while (closer_index < build.directives.size()) {
      const auto& inner = build.directives[closer_index];
      const auto inner_rows = ranges[closer_index];
      if (inner.mode == "flow") break;
      if (inner.mode == "break") {
        take(inner_rows, true);
      } else if (inner.mode != "off") {
        return RegionOutcome::not_a_region;
      } else if (inner.tag == closer_tag) {
        take(inner_rows, false);
        closed = true;
        break;
      } else if (inner.tag == "fn") {
        if (inner_rows.first != npos) return RegionOutcome::not_a_region;
      } else if (inner.tag == "table" || inner.tag == "etable" ||
                 inner.tag == "fig" || inner.tag == "efig") {
        take(inner_rows, false);
      } else if (cz_verbatim_region_tag(inner.tag)) {
        // A verbatim region opened inside the generic one prints nothing
        // at its opener (rule 5 only deepens the region); its rows are its
        // own block (qb3auv00 5.3 record 472 `cz OFF CGRAPHIC` .. `cz OFF
        // XMP`).
        break;
      } else if (inner.tag == tag) {
        // A second opener of the same tag inside the open region prints
        // nothing: the reader only counts its region depth (rule 5), so the
        // rows go on in the same `<pre width="80">`.  SC41-3425-00
        // APPENDIX1.2 record 1417 opens `cz OFF CGRAPHIC` on lines 25 and 32
        // with no closer and no `cz FLOW` between them.
        take(inner_rows, false);
      } else {
        return RegionOutcome::not_a_region;
      }
      ++closer_index;
    }
    // With no later directive the reader's `<pre>` stays open to the end
    // of the topic: SC41-5612-02 2.17.1.2 record 442 opens `cz OFF
    // CGRAPHIC` on line 11 and draws its rows to the topic's end.
    // The region owns its opener and everything up to its closer (its own
    // rows included) or up to the `cz FLOW` that ends it.
    index = closed ? closer_index : closer_index - 1;
    if (region_begin == npos) return RegionOutcome::emitted;
    return region_rows(region_begin, region_end, breaks, tag);
  }

  // The rows [begin, end) of a generic region (`generic_region`).  A
  // full-width rule row among them is the page's `<hr>`, not a drawn row
  // (`full_width_rule` in the line builder): SC33-1654-09 2.10.4.1.1 (DT
  // 20050714075613) opens `cz OFF MSG`, draws a 74-cell `U+2500` rule from
  // column 3 and then the message headline, and hosted serves
  // `<pre width="80"><!-- * -->`, `<hr>` and the headline in that `<pre>`.
  // The rows on either side of the rule are blocks of their own.
  RegionOutcome region_rows(std::size_t region_begin, std::size_t region_end,
                            const std::vector<std::size_t>& breaks,
                            const std::string& tag) {
    if (breaks.empty()) {
      for (auto line = region_begin; line < region_end; ++line) {
        if (!lines[line].thematic_break || has_text(lines[line])) continue;
        if (line > region_begin) {
          const auto before = region_rows(region_begin, line, breaks, tag);
          if (before != RegionOutcome::emitted) return before;
        }
        if (!message_rule(line)) return RegionOutcome::declined;
        return region_rows(line + 1, region_end, breaks, tag);
      }
    }
    if (std::none_of(lines.begin() + static_cast<std::ptrdiff_t>(region_begin),
                     lines.begin() + static_cast<std::ptrdiff_t>(region_end),
                     [](const Line& line) { return has_text(line); }))
      return RegionOutcome::emitted;
    // The rows keep their highlighting and links whenever each one is a
    // whole native display row; a `cz BREAK` row boundary inside the region
    // has no native row, so such a region keeps the plain reading.
    if (breaks.empty()) {
      ProseBlockIR block;
      bool matched = false;
      std::vector<std::pair<std::size_t, std::size_t>> line_rows;
      if (!build_native_verbatim_block(records, lines, region_begin,
                                       region_end, block, ledger,
                                       topic.blocks.size(), matched,
                                       line_rows))
        return RegionOutcome::declined;
      if (matched) {
        for (const auto& [line, row] : line_rows)
          verbatim_rows[line] = {topic.blocks.size(), row};
        append(std::move(block), region_begin);
        return RegionOutcome::emitted;
      }
    }
    // The plain reading draws no link: a region whose rows carry one fails
    // closed rather than drop it.
    if (std::any_of(lines.begin() + static_cast<std::ptrdiff_t>(region_begin),
                    lines.begin() + static_cast<std::ptrdiff_t>(region_end),
                    [](const Line& line) { return !line.links.empty(); })) {
      fail(error, "cz OFF " + tag + " region rows do not keep their links");
      return RegionOutcome::declined;
    }
    verbatim_breaks = breaks;
    const auto drawn = preformatted(region_begin, region_end);
    verbatim_breaks.clear();
    return drawn ? RegionOutcome::emitted : RegionOutcome::declined;
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
  //
  // `cz OFF TABDATA` is lowered the same way: its closer is the highlight end
  // `EHP1` (or `ETABDATA`), see `cz_tabdata_region_closer`.
  bool artwork(std::size_t& index, std::pair<std::size_t, std::size_t> range,
               const std::string& tag) {
    const auto tabdata = cz_tabdata_region_tag(tag);
    const auto opener = tabdata ? std::string("TABDATA")
                                : std::string("ARTWORK");
    const auto closer_index = index + 1;
    if (closer_index >= build.directives.size() ||
        build.directives[closer_index].mode != "off" ||
        !(tabdata ? cz_tabdata_region_closer(build.directives[closer_index].tag)
                  : cz_artwork_region_closer(
                        build.directives[closer_index].tag)))
      return fail(error, "cz OFF " + opener + " is not closed by cz OFF E" +
                             opener);
    const auto closer = ranges[closer_index];
    index = closer_index;
    auto begin = range.first;
    auto end = range.second;
    if (begin == npos) {
      begin = closer.first;
      end = closer.second;
    } else if (closer.first != npos) {
      if (closer.first != end)
        return fail(error, "cz OFF " + opener +
                               " region rows are not contiguous");
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

  // The rows [0, end) that precede the topic's first directive.  They are
  // served as the rows of a topic with no directive (see `run`): each group
  // between empty rows is a paragraph, and a row that opens with the
  // `U+2666` bullet slot opens a list item, as it does in the flattened
  // dialect.  SC31-8223-01 1.1 record 26 line 29 is `   °   Filtering
  // alerts` (tokens 250-254); hosted prints its four bulleted rows inside the
  // body's `<pre>`, each after a `<p>`, and the flattened dialect serves the
  // same shape as one unordered list.  Rows that continue an item stand to
  // the right of its bullet; a bullet at another column would be a level the
  // rows do not prove, and fails closed.
  // Rows [begin, end) of a stretch where the reader is inside its `<pre>`
  // (before the first directive, or after a named region closer): the rows
  // of a closed drawn box the line build admitted there
  // (`cz_pre_zone_box`, prose_topic_lines.cpp) become one verbatim block
  // per box, and the rows around them are read as `rows` reads them.
  template <typename Rows>
  bool rows_around_boxes(std::size_t begin, std::size_t end, Rows rows) {
    auto index = begin;
    while (index < end) {
      if (lines[index].box) {
        auto stop = index + 1;
        while (stop < end && lines[stop].box && !lines[stop].box_first) ++stop;
        ProseBlockIR block;
        if (!build_box_block(records, lines, index, stop, block, ledger,
                             topic.blocks.size(), error))
          return false;
        block.origin = lines[index].origin;
        append(std::move(block), index);
        index = stop;
        continue;
      }
      auto stop = index + 1;
      while (stop < end && !lines[stop].box) ++stop;
      if (!rows(index, stop)) return false;
      index = stop;
    }
    return true;
  }

  bool leading_rows(std::size_t end) {
    return rows_around_boxes(0, end, [&](std::size_t begin, std::size_t stop) {
      return leading_rows(begin, stop);
    });
  }

  bool leading_rows(std::size_t begin, std::size_t end) {
    auto list = npos;
    std::size_t item_origin = 0;
    for (const auto& [group_begin, group_end] :
         group_lines(lines, begin, end, false, &full_rules)) {
      // A full-width rule among the leading rows is the page's `<hr>`:
      // GC33-1694-01 `1.4.1` (DT 19990719095008) serves the rule under its
      // message heading as `<hr>` ahead of the `DFH0100` headline.
      if (group_end == group_begin + 1 && full_rules[group_begin]) {
        list = npos;
        if (!message_rule(group_begin)) return false;
        continue;
      }
      auto index = group_begin;
      while (index < group_end) {
        auto stop = index + 1;
        while (stop < group_end && !lines[stop].bullet) ++stop;
        ProseBlockIR block;
        if (!lines[index].bullet) {
          list = npos;
          block.kind = ProseBlockKindIR::paragraph;
        } else {
          const auto origin = lines[index].origin;
          if (list != npos && origin != item_origin)
            return fail(error, "bulleted rows before the first CZ directive "
                               "stand at columns " +
                                   std::to_string(item_origin) + " and " +
                                   std::to_string(origin));
          for (auto row = index + 1; row < stop; ++row)
            if (lines[row].origin <= origin)
              return fail(error, "row '" + line_text(lines[row]) +
                                     "' does not continue the bulleted row "
                                     "above it");
          if (list == npos) {
            list = next_list_ordinal++;
            item_origin = origin;
          }
          block.kind = ProseBlockKindIR::list_item;
          block.list_ordinal = list;
        }
        if (!emit(std::move(block), index, stop)) return false;
        index = stop;
      }
    }
    return true;
  }

  bool run() {
    const auto& directives = build.directives;
    // Line ranges per directive: lines follow their directive in order.
    ranges.assign(directives.size(), {npos, npos});
    // Rows before the first directive are body rows (doc/boo-spec/
    // markup.adoc, "Reading a CZ body"): BookServer opens the body's
    // `<pre width="132">` after the heading and prints every row until a
    // directive changes the layout, one `<p>` per empty row -- exactly how it
    // serves a topic with no directive at all.  SC31-8223-01 1.1 (DT
    // 19980528101033), whose only directive is its closing
    // `cz OFF EFIG 0 0`, is served that way from `The program-to-program
    // (PPI) interface ...` on.  See `leading_rows`.
    std::size_t leading_end = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
      const auto directive = lines[index].directive;
      if (directive == npos) {
        if (index == leading_end) {
          leading_end = index + 1;
        } else if (has_text(lines[index])) {
          return fail(error, "internal: display row '" +
                                 line_text(lines[index]) +
                                 "' follows the first CZ directive without "
                                 "one");
        }
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
    // A full-width rule is the page's `<hr>` outside a `cz OFF TABLE`
    // region wherever it stands (doc/boo-spec/markup.adoc, "Drawn box regions
    // in prose"), so in flowed rows too: SC31-6222-04 `2.1` (DT
    // 19961011013500) opens its code list's first message with `SRMSG 0801
    // 0000` and a 74-cell rule, served `<a name="MSG 0801 0000"><hr>`,
    // outside any message list.
    full_rules.assign(lines.size(), false);
    for (std::size_t line = 0; line < lines.size(); ++line)
      full_rules[line] =
          lines[line].thematic_break && full_width_rule_row(lines[line]);
    if (!leading_rows(leading_end)) return false;
    for (std::size_t index = 0; index < directives.size(); ++index) {
      const auto& directive = directives[index];
      auto range = ranges[index];
      if (!directive_rows(index, directive, range)) return false;
    }
    return finish();
  }

  // One directive with the display rows it owns.
  bool directive_rows(std::size_t& index, const LayoutDirective& directive,
                      std::pair<std::size_t, std::size_t> range) {
    const auto& directives = build.directives;
    // A `cz FLOW` (and the list directives after it) between the rows of a
    // closed box the line build draws as stored (`cz_pre_zone_box`): the
    // box owns every row, so the directive has none and formats nothing.
    // e2lh7000 2.7 record 72 opens `cz FLOW P 5 5` (line 24) right under the
    // top rule of its `DANGER` box; h0vb0e02 4.3 records 525-526 write a
    // `cz FLOW NOTE` .. `cz OFF EUL` note list inside a cell of its parts
    // table.
    if (directive.inside_box && range.first == npos &&
        (directive.mode == "flow" || directive.mode == "break" ||
         (directive.mode == "off" && !directive.tag.empty() &&
          directive.tag.front() == 'e' && directive.tag != "etable" &&
          directive.tag != "efig")))
      return true;
    {
      // A drawn box where the reader's `<pre>` is open (`cz_pre_zone_box`:
      // after a region opener or closer, or after an empty line that ended
      // the directive's flowed rows): the directive reads the rows before
      // it, and the box and the rows after it, up to the next directive, are
      // the verbatim stretch the reader prints as stored.
      std::size_t boxed = npos;
      if (range.first != npos)
        for (auto line = range.first; line < range.second; ++line)
          if (lines[line].box) {
            boxed = line;
            break;
          }
      // A glossary letter box (`cz_letter_box`) closes the rows of the
      // directive it follows and prints as its own letter divider: the
      // reader's look-ahead after a bare `SRGLS`, in flowed text as in
      // verbatim text.  Rows after it under the same paragraph directive are
      // the paragraphs of the next entry; under any other directive they
      // would be content the directive no longer owns, and decline.
      if (boxed != npos && lines[boxed].letter_box) {
        const auto paragraph_directive =
            directive.mode == "break" ||
            (directive.mode == "flow" &&
             (directive.tag == "p" || directive.tag == "gd"));
        if (boxed + 3 > range.second ||
            (boxed + 3 != range.second && !paragraph_directive))
          return fail(error, "cz " + directive.mode + " " + directive.tag +
                                 ": rows follow a glossary letter box");
        const auto head_range =
            boxed == range.first
                ? std::pair<std::size_t, std::size_t>{npos, npos}
                : std::pair<std::size_t, std::size_t>{range.first, boxed};
        const auto head =
            head_range.first == npos
                ? std::vector<std::pair<std::size_t, std::size_t>>{}
                : group_lines(lines, head_range.first, head_range.second,
                              in_message_list(), &full_rules);
        const auto name = "cz " + directive.mode + " " + directive.tag;
        if (directive.mode == "break") {
          // A break's rows are plain paragraphs (see below); one owning a
          // pending parameter term is not this shape.
          if (!stack.empty() && stack.back().tag == "parml")
            return fail(error, name + ": glossary letter box in a parameter "
                                      "list");
          if (!paragraphs(head, 0)) return false;
        } else if (!handle(index, directive, head_range, head, name,
                           directive.tag)) {
          if (error != nullptr && error->rfind("cz ", 0) != 0)
            *error = name + ": " + *error;
          return false;
        }
        return rows_around_boxes(boxed, range.second,
                                 [&](std::size_t begin, std::size_t stop) {
                                   return paragraphs(
                                       group_lines(lines, begin, stop,
                                                   in_message_list(),
                                                   &full_rules),
                                       0);
                                 });
      }
      if (boxed != npos) {
        const auto tail = range;
        range = boxed == range.first
                    ? std::pair<std::size_t, std::size_t>{npos, npos}
                    : std::pair<std::size_t, std::size_t>{range.first, boxed};
        // The rows before the box hold no box: the directive reads them as
        // it reads any rows.
        if (!directive_rows(index, directive, range)) return false;
        if (!rows_around_boxes(boxed, tail.second,
                               [&](std::size_t begin, std::size_t stop) {
                                 return named_closer_rows({begin, stop});
                               }))
          return false;
        return true;
      }
      const auto groups =
          range.first == npos
              ? std::vector<std::pair<std::size_t, std::size_t>>{}
              : group_lines(lines, range.first, range.second,
                            in_message_list(), &full_rules);
      const auto name = "cz " + directive.mode + " " + directive.tag;
      const auto& tag = directive.tag;
      if (directive.mode == "break") {
        if (!stack.empty() && stack.back().tag == "parml" &&
            stack.back().pending_definition != npos) {
          // A row-less break prints only `<br>`: GC31-8368-00 3.2.2 (DT
          // 19970123022321) serves `Reason`, `<br>` and the next term.
          // Before the next term or the list closer the term stands alone;
          // before a block, that block is the term's body.
          if (groups.empty()) {
            const auto next = index + 1 < directives.size()
                                  ? &directives[index + 1]
                                  : nullptr;
            auto& frame = stack.back();
            if (next == nullptr ||
                (next->mode == "flow" && next->tag == "pt") ||
                (next->mode == "off" && next->tag == "eparml")) {
              frame.bare_term = frame.pending_definition;
              term_alone(frame);
            } else {
              frame.body_owner = frame.pending_definition;
              frame.pending_definition = npos;
            }
            return true;
          }
          return parameter_body(groups);
        }
        return paragraphs(groups, 0);
      }
      if (!handle(index, directive, range, groups, name, tag)) {
        if (error != nullptr && error->rfind("cz ", 0) != 0)
          *error = name + ": " + *error;
        return false;
      }
      return true;
    }
  }

  // After the last directive: the lists the topic leaves open, then the
  // anchors and object spans.
  bool finish() {
    // A message list announced at the very end of a topic holds the messages
    // of the topics that follow: GC35-0271-02 6.0 ends its menu of the
    // seventy-three message topics 6.1 .. 6.73 with `cemenu`, `cz BREAK 3`,
    // `cz FLOW MSGL 3 3`, `cz OFF MSGL` (record 628 segments 18-21) and
    // nothing after, and hosted (DT 19980714150144) serves only an empty
    // `<pre width="80">` for the pair.  The list is closed elsewhere, so the
    // pair draws nothing here; any row or directive after it would be
    // content of a list this topic never closes, and still fails.
    //
    // Directives after the pair that own no display row draw nothing either:
    // GC33-1177-01 1.81.54 (DT 19990318184419) follows its announcement with
    // an empty `cz FLOW P 4 4`, and hosted serves `<p>`, the empty
    // `<pre width="80">` and `</pre>` -- nothing visible.
    if (stack.size() == 1 && stack.back().tag == "msgl" &&
        build.directives.size() >= 2) {
      auto last = build.directives.size() - 1;
      while (last >= 2 && ranges[last].first == npos &&
             !(build.directives[last].mode == "off" &&
               build.directives[last].tag == "msgl"))
        --last;
      const auto& opener = build.directives[last - 1];
      const auto& off = build.directives[last];
      if (opener.mode == "flow" && opener.tag == "msgl" &&
          off.mode == "off" && off.tag == "msgl" &&
          ranges[last].first == npos)
        stack.pop_back();
    }
    if (!stack.empty())
      return fail(error, "cz list " + stack.back().tag + " is not closed");
    const auto leading_anchors = topic.anchors.size();
    if (!place_anchors()) return false;
    return place_spans(leading_anchors);
  }

  bool in_message_list() const {
    return std::any_of(stack.begin(), stack.end(), [](const Frame& frame) {
      return frame.tag == "msgl";
    });
  }

  // `cz FLOW MSGL` .. `cz OFF EMSGL`, the message list (doc/boo-spec/
  // markup.adoc, "CZ message lists").  The opener is always followed by a
  // bare `cz OFF MSGL`; neither draws anything, and hosted BookServer
  // serves no list element for them.  Each message is an `SRMSG <id>` row
  // (the body anchor `MSG <id>`, prose_topic_stream.cpp), a `U+2500`
  // separator row served as `<hr>`, the message headline, and the message
  // body blocks, each a `cz FLOW <tag>` paragraph whose label is the
  // `CFONT`-bold word run it opens with.  GX27-3999-00 B.0 (DT
  // 19950730184057) serves record 105's `cz FLOW PROBD 3 10` as
  // `<B>Explanation:</B>  The value provided ...` and SC31-6242-02
  // APPENDIX1.2.1 (DT 19950803212024) serves `cz FLOW XPL`, `URESP`,
  // `MODULE` and `SEVERITY` as `<p>   <B>Explanation:</B> ...`,
  // `<p>   <B>Action:</B> ...`, `<p>   <B>Module:</B>  INTERNALSYS` and
  // `<p>   <B>Severity:</B>  WARNING`.
  static bool message_body_tag(const std::string& tag) {
    return tag == "xpl" || tag == "sysact" || tag == "uresp" ||
           tag == "oresp" || tag == "presp" || tag == "spresp" ||
           tag == "probd" || tag == "module" || tag == "dest" ||
           tag == "severity";
  }

  bool message_list(std::size_t index, const LayoutDirective& directive,
                    const std::vector<std::pair<std::size_t, std::size_t>>& groups,
                    const std::string& name) {
    const auto& tag = directive.tag;
    if (directive.mode == "flow" && tag == "msgl") {
      if (!groups.empty()) return fail(error, name + " carries display text");
      if (index + 1 >= build.directives.size() ||
          build.directives[index + 1].mode != "off" ||
          build.directives[index + 1].tag != "msgl")
        return fail(error, name + " is not followed by cz OFF MSGL");
      stack.push_back({"msgl", next_list_ordinal++, 0});
      return true;
    }
    if (directive.mode == "off" && tag == "msgl") {
      if (index == 0 || build.directives[index - 1].mode != "flow" ||
          build.directives[index - 1].tag != "msgl")
        return fail(error, name + " does not follow cz FLOW MSGL");
      return paragraphs(groups, 0);
    }
    if (directive.mode == "off" && tag == "emsgl") {
      // The last message topic of a list closes the list an earlier topic
      // opened.  BookServer reads the line as the generic `CZ OFF E`
      // closer, which prints nothing when no region is open: GC31-8601-00
      // `1.1.3` (DT 19970728124306) ends `   Procedure Name:`,
      // `cz OFF EMSGL 0 0` and hosted serves nothing after the name.  A
      // list this topic opened itself is closed once; a second closer
      // still fails.
      const auto opened_here = std::any_of(
          build.directives.begin(),
          build.directives.begin() + static_cast<std::ptrdiff_t>(index),
          [](const LayoutDirective& earlier) {
            return earlier.mode == "flow" && earlier.tag == "msgl";
          });
      if (!opened_here && !in_message_list()) return paragraphs(groups, 0);
      if (stack.empty() || stack.back().tag != "msgl")
        return fail(error, name + " closes no open msgl");
      stack.pop_back();
      return paragraphs(groups, 0);
    }
    // A message body block.  BookServer has no rule of its own for these
    // tags: `CZ FLOW XPL` matches only the generic `CZ FLOW` prefix, which
    // closes the open `<pre>` and flows the rows, so a body block outside a
    // message list is the same paragraph as one inside it.  GC33-1694-01
    // `1.4.1` (DT 19990719095008) has no `cz FLOW MSGL` and serves its
    // `cz FLOW XPL 3 4` rows as `</pre>    <B>Explanation:</B>  The CICS
    // subsystem ...`, then `</pre><pre width="132">` at the empty row.
    return paragraphs(groups, 0);
  }

  // One directive with its display-line range and text groups.
  bool handle(std::size_t& index, const LayoutDirective& directive,
              std::pair<std::size_t, std::size_t> range,
              const std::vector<std::pair<std::size_t, std::size_t>>& groups,
              const std::string& name, const std::string& tag) {
    // The closer of a region a `cz FLOW` already ended prints nothing; its
    // rows are body text, as any region closer's are.
    if (std::find(inert_closers.begin(), inert_closers.end(), index) !=
        inert_closers.end())
      return paragraphs(groups, 0);
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
        // An `SRFIG<id>` that opens no figure (`LineBuild::figure_anchors`)
        // leaves its `cz OFF FIG` .. `cz OFF EFIG` pair no figure to
        // delimit: the reader prints the rows of both as stored, in
        // `<pre width="132"><!-- figure -->` and then the closer's
        // `<pre width="132">` (SC33-6733-01 B.1.3.1, hosted DT 19990624).
        if (build.figure_anchors && kind == ProseSpanKindIR::figure) {
          const auto spanned =
              std::any_of(topic.spans.begin(), topic.spans.end(),
                          [](const ProseSpanIR& span) {
                            return span.kind == ProseSpanKindIR::figure;
                          });
          if ((tag == "fig" && !groups.empty()) || !spanned)
            return named_closer_rows(range);
        }
        // A table closer in a topic that opens no table and has no table
        // envelope closes nothing.  SC31-8778-01 ends the operand diagram of
        // 315 topics with an unopened `SRETBL`, `cz OFF ETABLE 0 0` and
        // `cz FLOW P 3 3` (2.2.3.4 record 226 lines 20-23); hosted
        // BookServer (DT 20010606144047) prints nothing for either closer.
        // A closer that owns display rows of its own is not shown by any
        // such topic and still declines.
        const auto no_span =
            std::none_of(topic.spans.begin(), topic.spans.end(),
                         [kind](const ProseSpanIR& span) {
                           return span.kind == kind;
                         });
        if (no_span && tag == "etable" && groups.empty() &&
            std::none_of(build.directives.begin(),
                         build.directives.begin() +
                             static_cast<std::ptrdiff_t>(index),
                         [](const LayoutDirective& earlier) {
                           return earlier.mode == "off" &&
                                  earlier.tag == "table";
                         }))
          return true;
        // An envelope drawn inside a closed box is the box's own markup
        // (prose_topic_spans.cpp, `box_markup_envelopes`), and so is the
        // pair of directives around it when the closer stands between the
        // box's rows: the opener's rows are the box, which the line build
        // draws as stored, and the closer reopens the reader's `<pre>` for
        // the box's last rows and the rows after them.  SH26-4009-01 1.0
        // record 56 opens `cz OFF TABLE` (line 4) and `SRTBLTBLUNIQ7` (line
        // 5) above the top rule of the two-column syntax-legend grid (line
        // 6) and closes `SRETBL` and `cz OFF ETABLE 0 0` above its bottom
        // rule; hosted BookServer (DT 19950628152824) prints the grid, the
        // nested example boxes in its cells included, row for row with no
        // `<table>`; SC24-5780-01 1.4 (DT 19961023165238) likewise.
        if (no_span) {
          const auto closer = tag == "etable" || tag == "efig";
          const auto& next = index + 1 < build.directives.size()
                                 ? build.directives[index + 1]
                                 : build.directives[index];
          const auto in_box =
              closer ? build.directives[index].inside_box
                     : groups.empty() && index + 1 < build.directives.size() &&
                           next.mode == "off" && next.tag == "e" + tag &&
                           next.inside_box;
          if (in_box) return closer ? named_closer_rows(range) : true;
        }
        if (no_span)
          return fail(error, name + " delimits no admitted " +
                                 (kind == ProseSpanKindIR::table
                                      ? "table envelope"
                                      : "figure region"));
        if (tag == "table" || tag == "fig") {
          // A table the topic closes with an inert line instead of
          // `cz OFF ETABLE`: the redbooks write `cz OFF TABLE`, the
          // envelope, `SRETBL` and `cz ON TABLE 0 0`, and then the topic's
          // rows (SG24-5363-00 2.1.9 records 97-100).  The inert line
          // changes no state, so hosted (DT 19990715181940) serves the
          // table, `</table><pre width="80"><!-- * -->` and the rows after
          // it as stored -- the reading a named closer's rows get.  Only
          // rows that follow the envelope are read so; a row between the
          // opener and the envelope still declines.
          if (tag == "table" && !groups.empty() &&
              std::any_of(build.span_marks.begin(), build.span_marks.end(),
                          [&](const SpanMark& mark) {
                            return mark.span < topic.spans.size() &&
                                   topic.spans[mark.span].kind ==
                                       ProseSpanKindIR::table &&
                                   range.first != npos &&
                                   mark.line >= range.first &&
                                   mark.line <= groups.front().first;
                          }))
            return named_closer_rows({groups.front().first, range.second});
          if (!groups.empty())
            return fail(error, name + " carries display text");
          return true;
        }
        return named_closer_rows(range);
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
        // A term may hand its body directly to a source-declared block
        // region without an intervening DD.  The pending term is the owner of
        // that region, not an inline definition missing its text.
        // Any verbatim region does: GC41-5435-03 9.2 (DT 19990304092651)
        // follows `cz FLOW DT 3 13` `Examples:` with two `cz OFF XMP`
        // regions, served as `<dt>   <B>Examples:</B>` and the two
        // `<pre width="80">` blocks after the `<dd>`.
        if (!stack.empty() && stack.back().tag == "dl" &&
            stack.back().pending_definition != npos &&
            !empty_definition_body_before_block())
          return false;
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
        //
        // Two directives a region holds without ending it, and one that ends
        // it early.  BookServer prints `<br>` for `CZ BREAK` whatever is
        // open: SC26-4834-03 3.169 (DT 19950908105009) serves `cz OFF XMP`,
        // `     SQLCODE:  -30041 ...`, `cz BREAK 5`, `     QMF Function:  All`,
        // `cz OFF EXMP 0 0` as one `<pre width="80">` with `<br>` between the
        // rows, so the break is a blank row of the block.  A `cz FLOW` closes
        // whatever `<pre>` is open: SC24-5784-00 3.2.522 (DT 19950910224116)
        // serves `</pre>` for the `cz FLOW P 5 5` inside its example region
        // and its rows as flowing text, and nothing for the
        // `cz OFF EXMP 0 0` after them (the generic `CZ OFF E` closer).
        auto closer_index = index + 1;
        auto region_begin = range.first;
        auto region_end = range.second;
        std::vector<std::size_t> breaks;
        auto ended_by_flow = false;
        auto foreign_closed = false;
        // Closers of the generic regions opened inside this one.
        std::vector<std::string> nested_openers;
        const auto nested_generic_opener = [&](const std::string& other) {
          return !other.empty() && other.front() != 'e' &&
                 other != "table" && other != "fig" && other != "fn" &&
                 other != "msgl" && !cz_verbatim_region_tag(other) &&
                 !cz_title_page_tag(other) && !cz_tabdata_region_tag(other) &&
                 !list_tag(other);
        };
        const auto foreign_closer = [&](const std::string& other,
                                        const std::string& own) {
          if (other.size() < 2 || other.front() != 'e' || other == own)
            return false;
          for (const auto* named : {"etable", "efig", "elblbox", "edl"})
            if (other.rfind(named, 0) == 0) return false;
          const auto opened = other.substr(1);
          if (list_tag(opened) || opened == "fn" || opened == "msgl" ||
              cz_verbatim_region_tag(opened))
            return false;
          return std::none_of(build.directives.begin(), build.directives.end(),
                              [&opened](const LayoutDirective& any) {
                                return any.mode == "off" && any.tag == opened;
                              });
        };
        while (closer_index < build.directives.size()) {
          const auto& inner = build.directives[closer_index];
          if (inner.mode == "break") {
            const auto break_rows = ranges[closer_index];
            if (break_rows.first != npos) {
              if (region_begin == npos) region_begin = break_rows.first;
              else breaks.push_back(break_rows.first);
              region_end = break_rows.second;
            }
            ++closer_index;
            continue;
          }
          if (inner.mode == "flow") {
            ended_by_flow = true;
            break;
          }
          if (inner.mode != "off") break;
          const auto inner_rows = ranges[closer_index];
          if (inner.tag == "fn") {
            // The footnote end marker displays nothing.
            if (inner_rows.first != npos) break;
          } else if (inner.tag == "table" || inner.tag == "etable" ||
                     inner.tag == "fig" || inner.tag == "efig") {
            if (inner_rows.first != npos) region_end = inner_rows.second;
          } else if (cz_verbatim_region_tag(inner.tag)) {
            // GX27-3999-00 2.6.1 and 2.6.2 explicitly nest XMP inside
            // LINES; 2.6.1 also nests LINES inside XMP.  The inner
            // delimiters are structural inside the outer fixed-line region:
            // both pairs declare verbatim display rows, and neither
            // delimiter draws text of its own.
            // Admit only an immediately balanced pair so a missing inner
            // closer can never make the outer region swallow a later
            // directive.
            const auto nested_closer = closer_index + 1;
            if (nested_closer >= build.directives.size() ||
                build.directives[nested_closer].mode != "off" ||
                build.directives[nested_closer].tag != "e" + inner.tag)
              break;
            if (inner_rows.first != npos) {
              if (region_begin == npos) region_begin = inner_rows.first;
              region_end = inner_rows.second;
            }
            const auto nested_closer_rows = ranges[nested_closer];
            if (nested_closer_rows.first != npos) {
              if (region_begin == npos)
                region_begin = nested_closer_rows.first;
              region_end = nested_closer_rows.second;
            }
            closer_index = nested_closer;
          } else if (nested_generic_opener(inner.tag) ||
                     std::find(nested_openers.begin(), nested_openers.end(),
                               inner.tag) != nested_openers.end()) {
            // A generic region opened inside the verbatim region only
            // counts the reader's region depth: it prints nothing and its
            // rows are rows of the open `<pre>`, as are those of its own
            // closer.  The outer closer then resets the depth.  SC24-5718-00
            // `B.10.5` (DT 19951023144552) opens `cz OFF XMP`, then
            // `cz OFF CGRAPHIC` with the OS/2 `DIR` screen and closes with
            // `cz OFF EXMP 2 2` alone; SC23-0448-01 `1.5` nests a
            // `CGRAPHIC` in its `SCREEN` the same way.  Hosted serves each
            // as one `<pre width="80">`.
            if (inner.tag.front() != 'e')
              nested_openers.push_back("e" + inner.tag);
            if (inner_rows.first != npos) {
              if (region_begin == npos) region_begin = inner_rows.first;
              region_end = inner_rows.second;
            }
          } else if (foreign_closer(inner.tag, closer_tag) &&
                     inner_rows.first == npos) {
            // Another region's closer, its own region never opened.  The
            // reader's generic `CZ OFF E` resets the region depth and prints
            // nothing, so the `<pre>` stays open until the next `CZ FLOW`
            // and the closer's rows are rows of it.  SC33-1369-09 `2.1.1`
            // (DT 19971205131216) opens `cz OFF SYNTAX`, draws its boxed
            // syntax diagram and closes it with `cz OFF EREV 0 0` -- no
            // `ESYNTAX` anywhere -- before `cz FLOW H3 3 3`; hosted serves
            // the whole diagram in one `<pre width="80">` up to the
            // heading's `</pre>`.  A foreign closer that carries rows of its
            // own leaves the region's extent to the reader's state alone and
            // still fails closed.
            foreign_closed = true;
            ++closer_index;
            break;
          } else {
            break;
          }
          ++closer_index;
        }
        if (foreign_closed) {
          // Admitted only where nothing else can still claim the region: the
          // next directive is a `cz FLOW` (or the topic ends), and the
          // region's own closer never comes.
          // A new region opener may follow as well: it opens its own
          // `<pre>`.  SC24-5760-00 `1.2.27.1` (DT 19950909201627) ends its
          // first `cz OFF SYNTAX` diagram with `cz OFF EHP1 0 0` and opens
          // the second with `cz OFF SYNTAX` at once.
          const auto next_ok =
              closer_index >= build.directives.size() ||
              build.directives[closer_index].mode == "flow" ||
              (build.directives[closer_index].mode == "off" &&
               !build.directives[closer_index].tag.empty() &&
               build.directives[closer_index].tag.front() != 'e');
          auto own_closer_follows = false;
          for (auto later = closer_index; later < build.directives.size();
               ++later) {
            const auto& next = build.directives[later];
            if (next.mode != "off") continue;
            if (next.tag == tag) break;
            if (next.tag == closer_tag) {
              own_closer_follows = true;
              break;
            }
          }
          if (!next_ok || own_closer_follows)
            return fail(error, "cz OFF " + opener +
                                   " is not closed by cz OFF E" + opener);
          index = closer_index - 1;
          if (region_begin == npos) return true;
          verbatim_breaks = breaks;
          const auto drawn = preformatted(region_begin, region_end);
          verbatim_breaks.clear();
          return drawn;
        }
        if (ended_by_flow) {
          // The region's own closer must still follow, with no second
          // opener of the same region before it; it then draws nothing.
          auto later = closer_index + 1;
          while (later < build.directives.size() &&
                 !(build.directives[later].mode == "off" &&
                   (build.directives[later].tag == closer_tag ||
                    build.directives[later].tag == tag)))
            ++later;
          if (later >= build.directives.size() ||
              build.directives[later].tag != closer_tag)
            return fail(error, "cz OFF " + opener +
                                   " is not closed by cz OFF E" + opener);
          inert_closers.push_back(later);
          index = closer_index - 1;
          if (region_begin == npos) return true;
          verbatim_breaks = breaks;
          const auto drawn = preformatted(region_begin, region_end);
          verbatim_breaks.clear();
          return drawn;
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
        // An empty region: the opener and its closer own no row.  The
        // generic `CZ OFF` opens `<pre width="80">` and the generic
        // `CZ OFF E` closer prints nothing and leaves that `<pre>` open
        // until the next `CZ FLOW`, so the region itself draws nothing and
        // the rows the closer owns are verbatim rows of that `<pre>`.
        // SC31-6552-00 1.6 (DT 19950503072955) serves its empty `SYNTAX`
        // pair as an empty `<pre width="80">`; SC33-1220-02 BACK_2 (DT
        // 19980424201448) serves the comment-form rows after its empty
        // `LINES` pair, `   Name:          ...   Address:`, inside it.
        if (region_begin == npos && closed) {
          const auto closer = ranges[closer_index];
          index = closer_index;
          if (closer.first == npos ||
              std::none_of(
                  lines.begin() + static_cast<std::ptrdiff_t>(closer.first),
                  lines.begin() + static_cast<std::ptrdiff_t>(closer.second),
                  [](const Line& line) { return has_text(line); }))
            return true;
          return preformatted(closer.first, closer.second);
        }
        if (region_begin == npos)
          return fail(error, "cz OFF " + opener + " block has no display rows");
        // The closing directive is the generic `CZ OFF E` closer: it prints
        // nothing and leaves the region's `<pre width="80">` open until the
        // next `CZ FLOW`, so the rows it carries are verbatim rows of the
        // same block.  Hosted packet 2.4.1 (DT 20260614112503) serves the
        // address row, `<p>` and `   Note that zeros are omitted ...` in
        // one `<pre width="80">`; 70 closer rows sampled in 40 books are
        // all served inside the region's `<pre width="80">`.
        auto closer =
            closed ? ranges[closer_index] : std::make_pair(npos, npos);
        // The closer leaves the `<pre>` open, and a `cz BREAK` after it only
        // prints `<br>` into it, so the rows of the breaks that follow the
        // closer are rows of the same block, up to the next other directive.
        // GC24-5837-01 `5.2.8.3` (DT 19990503161545) closes the syntax
        // diagram of its `MODENUM` parameter with `cz OFF ESYNTAX 4 4` and
        // follows it with `cz BREAK 7` and the parameter's description;
        // hosted serves `<br>` and the description inside the diagram's
        // `<pre width="80">`.
        std::vector<std::size_t> tail_breaks;
        auto last_absorbed = closer_index;
        if (closed) {
          for (auto next = closer_index + 1;
               next < build.directives.size() &&
               build.directives[next].mode == "break";
               ++next) {
            const auto rows = ranges[next];
            last_absorbed = next;
            if (rows.first == npos) continue;
            if (closer.first == npos) {
              closer = rows;
            } else {
              tail_breaks.push_back(rows.first);
              closer.second = rows.second;
            }
          }
        }
        const auto closer_draws =
            closer.first != npos &&
            std::any_of(
                lines.begin() + static_cast<std::ptrdiff_t>(closer.first),
                lines.begin() + static_cast<std::ptrdiff_t>(closer.second),
                [](const Line& line) { return has_text(line); });
        const auto joined = closer_draws && closer.first == region_end;
        verbatim_breaks = breaks;
        if (joined) {
          if (ranges[closer_index].first == npos)
            verbatim_breaks.push_back(closer.first);
          verbatim_breaks.insert(verbatim_breaks.end(), tail_breaks.begin(),
                                 tail_breaks.end());
        }
        const auto drawn =
            preformatted(region_begin, joined ? closer.second : region_end);
        verbatim_breaks.clear();
        if (!drawn) return false;
        index = closed ? last_absorbed : closer_index;
        if (!closed || !closer_draws || joined) return true;
        verbatim_breaks = tail_breaks;
        const auto tail = preformatted(closer.first, closer.second);
        verbatim_breaks.clear();
        return tail;
      }
      // The generated title-page projection, ahead of the unmodelled path:
      // its rows are re-flowed, not reproduced (see `title_page`).
      if (directive.mode == "off" && cz_title_page_tag(tag))
        return title_page(index, range, name, tag);
      if (tag == "msgl" || (directive.mode == "off" && tag == "emsgl") ||
          (directive.mode == "flow" && message_body_tag(tag)))
        return message_list(index, directive, groups, name);
      // The artwork region, ahead of the generic `cz OFF <tag>` fallback.
      if (directive.mode == "off" &&
          (cz_artwork_region_tag(tag) || cz_tabdata_region_tag(tag)))
        return artwork(index, range, tag);
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
            // An unopened `ELBLBOX` is still the named closer: its rows are
            // the `<pre width="132">` it opens -- unless a figure is still
            // open, when the reader treats the closer as inert and the rows
            // stay the figure's.
            // (A figure is open when an `EFIG` follows before any `FIG`; the
            // figure span consumes its own opener.)
            auto figure_open = false;
            for (auto later = index + 1; later < build.directives.size();
                 ++later) {
              const auto& next = build.directives[later];
              if (next.mode != "off") continue;
              if (next.tag == "fig") break;
              if (next.tag == "efig") {
                figure_open = true;
                break;
              }
            }
            if (tag == "elblbox" && !figure_open)
              return named_closer_rows(range);
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
          // A term still waiting for its body at the list's end is a term
          // alone (see `term_alone`).
          stack.back().pending_definition = npos;
          // A list that never wrote an item holds only the blocks between
          // its opener and closer, already emitted in order: BookServer
          // prints `<ul>`, those blocks and `</ul>`, and no list item.
          // GA27-3960-05 5.24.4 (DT 19961015143739) serves its
          // `cz FLOW SL 3 3` / `cz FLOW P 7 7` / `cz OFF ESL` as `<ul>` and the
          // paragraph's rows.
          stack.pop_back();
          // A list closer carries the body text that follows the list as
          // ordinary paragraphs (SC09-2417-00 4.2.2 record 898 `cz OFF EOL
          // 0 0   The best way to instantiate templates ...`, which hosted
          // prints after `</ol>`); `EDL` is a named closer and its rows are
          // drawn as stored (`named_closer_rows`).
          if (tag == "edl") return named_closer_rows(range);
          return paragraphs(groups, 0);
        }
        {
          bool handled = false;
          if (!generic_closer(index, tag, groups, handled)) return false;
          if (handled) return true;
        }
        // Every other `cz OFF <tag>` is the reader's generic region (see
        // `generic_region`).
        switch (generic_region(index, range, tag)) {
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
      if (tag == "pt") {
        if ((stack.empty() || stack.back().tag != "parml") &&
            implicit_parameter_list(index)) {
          stack.push_back({"parml", next_list_ordinal++, 0});
        }
        return parameter_term(directive, groups, index);
      }
      if (tag == "p" || tag == "pc" || tag == "gd") {
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
        //
        // A term that carries no definition text of its own may hand its
        // body directly to the list that opens next, exactly as it may hand
        // it to a LINES region: DFHP7A00 BACK_1.1.2.1 record 962 stores
        // `cz FLOW DT 3 13`, the row `   General`, then `cz FLOW SL 13 13`
        // with the publication titles, and DFHP7A00 2.6.1.1.2 record 314
        // stores `   Return codes` before `cz FLOW DL 13 13`.  The nested
        // list opens at the term's `indent` column, which is where its
        // definition starts; only that list is the term's body.  A list at
        // any other column leaves the term pending, and the term still fails
        // without a body.
        //
        // The same column decides for a term whose row already began its
        // definition: a list opened directly after that DT at its `indent`
        // continues the definition.  DFHP7A00 2.6.1.1.2 record 312 stores
        // `   Exit-specific parameters` at `cz FLOW DT 3 13` -- the term
        // ends with the word that straddles column 13, as hosted serves
        // SC41-485 1.2.2 `<dt> |     <B>Generic</B> <B>object</B><dd>
        // <B>name</B>` -- and then `cz FLOW DL 13 13`.  Only a DT whose rows
        // form one paragraph qualifies; a DT that went on to further
        // paragraphs has already left them outside the entry.
        if (!stack.empty() && stack.back().tag == "dl" &&
            stack.back().inline_entry != npos &&
            stack.back().inline_indent == directive.left && index > 0 &&
            build.directives[index - 1].mode == "flow" &&
            build.directives[index - 1].tag == "dt")
          stack.back().body_owner = stack.back().inline_entry;
        if (!stack.empty() && stack.back().tag == "dl" &&
            stack.back().pending_definition != npos &&
            directive.left == stack.back().pending_indent &&
            !empty_definition_body_before_block())
          return false;
        if (!paragraphs(groups, 0)) return false;
        stack.push_back({tag, next_list_ordinal++, 0});
        stack.back().left = directive.left;
        return true;
      }
      if (tag == "li") {
        if (!list_item(index, directive, groups)) return false;
        return true;
      }
      if (tag == "lp") {
        if (stack.empty())
          return fail(error, "cz FLOW LP outside an open list");
        // Inside an item list the part continues the item before it:
        // `CZ FLOW LP` prints `<br>` and its rows wherever it stands, so
        // GC33-0790-04 3.49.61 (DT 19990426180546) serves its `or` between
        // two `OL` items as `<br>   or` inside the first `<li>`, and the
        // numbering runs on.  With no item yet there is nothing to continue.
        if ((stack.back().tag == "ul" || stack.back().tag == "ol" ||
             stack.back().tag == "sl") &&
            stack.back().body_owner != npos)
          return paragraphs(groups, 0);
        // Before the first item the part is the list's lead-in, like rows
        // its opener carries: SC26-9920-00 `1.1.1.6.1` (DT 20000114153724)
        // opens `cz FLOW OL 3 3` with `cz FLOW LP 3 3` `Capturing data from
        // a source database`, served `<ol type=1 start=1><br>` and the
        // heading ahead of the first `<li>`.  In a parameter list or a
        // numbered-note list the part stands after the entry before it, a
        // block of its own: SC24-5760-00 `1.2.20.6` (DT 19950909201627)
        // serves its two `cz FLOW LP 3 3` parts after the `SXORGCTA` entry
        // as `<br>` and flowing rows outside any definition.
        if ((stack.back().tag == "ul" || stack.back().tag == "ol" ||
             stack.back().tag == "sl") &&
            stack.back().items == 0)
          return paragraphs(groups, 0);
        if (stack.back().tag == "parml" || stack.back().tag == "notel") {
          term_alone(stack.back());
          stack.back().body_owner = npos;
          return paragraphs(groups, 0);
        }
        if (stack.back().tag != "dl")
          return fail(error, name + " inside a " + stack.back().tag +
                                 " needs mixed list block content");
        return list_part(index, directive, groups);
      }
      if (tag == "dt") {
        // A DT with no display rows at all is an empty term whose body is
        // the block the next directive opens.  Hosted BookServer serves the
        // term empty and the block as its definition:
        //  * `cz OFF LINES` -- GA19-5486-13 FRONT_3;
        //  * `cz OFF SCREEN` -- SC21-8295-03 3.1.3.2 record 431 lines 32-33
        //    (`cz FLOW DT 3 13`, `cz OFF SCREEN`), served (as `SC21-829`,
        //    DT 19970409135933) `<dl><dt></pre><pre width="80">` .. `<dd>`
        //    and the `22.1 CNFIGSSP` screen;
        //  * `cz FLOW P` at the term's `indent` column -- the same topic,
        //    record 371 lines 20-21 (`cz FLOW DT 3 13`, `cz FLOW P 13 13`),
        //    served `<dt><p>            <dd><I>Prompt</I> <I>1:</I> You can
        //    use DisplayWrite/36 ...`.
        // A paragraph at any other column, or any other directive, leaves
        // the term without text and still fails.
        if (groups.empty() && index + 1 < build.directives.size()) {
          const auto& next = build.directives[index + 1];
          const auto block_body =
              (next.mode == "off" &&
               (next.tag == "lines" || next.tag == "screen")) ||
              (next.mode == "flow" && next.tag == "p" &&
               directive.indent > directive.left &&
               next.left == directive.indent);
          if (block_body) {
            const auto first_body_line = ranges[index + 1].first;
            if (first_body_line == npos)
              return fail(error, "cz " + next.mode + " " + next.tag +
                                     " body has no display rows");
            return empty_definition_with_block_body(directive,
                                                    first_body_line);
          }
        }
        const auto separate_body =
            index + 1 < build.directives.size() &&
            build.directives[index + 1].mode == "flow" &&
            build.directives[index + 1].tag == "dd";
        if (!definition(directive, groups, separate_body)) return false;
        return true;
      }
      if (tag == "dd") {
        // A DD that owns no row prints `<dd>` and leaves the definition to
        // what follows: GA27-4105-03 5.2.2.1 (DT 19980701154012) follows
        // `cz FLOW DT 3 13` `Service agreement` with a row-less
        // `cz FLOW DD 13 13` and a `cz FLOW P 13 13` paragraph, served as
        // `<dd>` and that paragraph; other books follow it with a `DL`,
        // `SL`, `OL` or `XMP`.  Followed by the next term or the list's
        // closer, the term stands alone.
        if (groups.empty()) {
          const auto next = index + 1 < build.directives.size()
                                ? &build.directives[index + 1]
                                : nullptr;
          if (next == nullptr ||
              (next->mode == "flow" && (next->tag == "dt" || next->tag == "dd")) ||
              (next->mode == "off" && next->tag == "edl")) {
            if (stack.empty() || stack.back().tag != "dl" ||
                stack.back().pending_definition == npos)
              return fail(error, "cz FLOW DD has no text");
            term_alone(stack.back());
            return true;
          }
          return empty_definition_body_before_block();
        }
        return definition_body(groups);
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
          // A heading directive with no rows of its own draws nothing,
          // wherever it stands.  The reader closes its open `<pre>` at every
          // `cz FLOW` line and otherwise ignores `cz FLOW H<n>`; a heading's
          // words are the rows after it, and here there are none.  A
          // trailing one announces the next topic's level (packet 1.1 record
          // 17; packet 4.5.1 record 225 ends `cz FLOW H5 3 3` and then its
          // footnote, hosted DT 20260614112503).  SC31-8212-00 2.3.3 record
          // 855 line 21 is `cz FLOW H3 3 3` followed at once by `cz BREAK 3`
          // and `cz FLOW P 3 3`, in the middle of the topic; hosted (DT
          // 19950906051034) prints only the empty rows around it.
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
      // A heading tag past the dialect's levels (`H7`) is only the
      // reader's `CZ FLOW H` prefix, which draws nothing of its own: the rows
      // flow.  SC24-5760-00 `4.9.1.1` (DT 19950909201627) stores
      // `cz FLOW H7 3 5` and `     Example:  It is recommended that ...`,
      // served as a flowing row after `</pre>`.
      if (tag.front() == 'h' && tag.size() == 2 &&
          std::isdigit(static_cast<unsigned char>(tag.back())) != 0)
        return paragraphs(groups, 0);
      // Any other tag is the generic `CZ FLOW` (see `generic_flow_tag`):
      // the reader closes its open `<pre>` and the rows flow.
      // A row-less `P..` tag draws nothing under either rule (SY27-0327-05
      // `8.4` announces its parts catalog with a trailing
      // `cz FLOW PARTSASM 3 3`).
      if (generic_flow_tag(tag) ||
          (groups.empty() && !tag.empty() && tag.front() == 'p'))
        return paragraphs(groups, 0);
      return fail(error, "CZ layout " + name + " is not modelled");
    }
  }

  // A `cz FLOW <tag>` the reader has no rule of its own for.  BookServer
  // matches a control line against literal upper-case prefixes
  // (doc/boo-spec/markup.adoc, "Reading a CZ body"); besides the generic
  // `CZ FLOW` the only `CZ FLOW` prefixes are `H`, `DT`, `DD`, `ED`, `LP`,
  // `UL`, `OL`, `SL`, `DL`, `LI` and -- in one book dialect -- `P`.  A tag
  // none of them starts matches the generic prefix alone: the open `<pre>`
  // closes and the rows flow, exactly like a message body block.
  // SC31-8046-00 `1.1.4` (DT 19950610061004) serves `cz FLOW NUMBYTES 3 3`
  // `   Related Commands:  LINEMAP` as `</pre>` and the flowing row, and
  // GC33-1177-01 and dfhcag49 open their code lists with `cz FLOW CODEL`.
  // Tags starting with `P` stay unmodelled: the `CZ FLOW P` prefix has a
  // rule of its own in the other dialect.
  static bool generic_flow_tag(const std::string& tag) {
    if (tag.empty() || tag.front() == 'h' || tag.front() == 'p') return false;
    static const char* const specific[] = {"dt", "dd", "ed", "lp", "ul",
                                           "ol", "sl", "dl", "li"};
    for (const auto* prefix : specific)
      if (tag.rfind(prefix, 0) == 0) return false;
    return true;
  }

  // A `cz OFF E<tag>` closer of a region the reader never opened in this
  // topic and the dialect gives no structure: the generic `CZ OFF E`
  // closer, which prints nothing and only resets the region depth.  Its
  // rows are whatever the reader has open; when the directive before it is
  // a `cz FLOW` (no `<pre>` open) they flow.  GC33-1177-01 `2.3.34` (DT
  // 19990318184419) ends `cz FLOW MODULE 3 4` `    Module:  DFHCPCOJ`,
  // `cz OFF ECODEL 0 0` -- the closer of a code list an earlier topic opened
  // -- and hosted serves nothing after the module name.  The named closers
  // `ETABLE`, `EFIG`, `ELBLBOX` and `EDL` (and every tag they prefix) have
  // rules of their own and never come here.
  bool generic_closer(std::size_t index, const std::string& tag,
                      const std::vector<std::pair<std::size_t, std::size_t>>&
                          groups,
                      bool& handled) {
    handled = false;
    if (tag.size() < 2 || tag.front() != 'e') return true;
    for (const auto* named : {"etable", "efig", "elblbox", "edl"})
      if (tag.rfind(named, 0) == 0) return true;
    const auto opened = tag.substr(1);
    if (std::any_of(build.directives.begin(),
                    build.directives.begin() + static_cast<std::ptrdiff_t>(index),
                    [&opened](const LayoutDirective& earlier) {
                      return earlier.mode == "off" && earlier.tag == opened;
                    }))
      return true;
    if (!groups.empty()) {
      // The rows need the reader's state before the closer: only after a
      // `cz FLOW` is no `<pre>` open.
      auto before = index;
      while (before > 0 && build.directives[before - 1].mode == "break")
        --before;
      if (before == 0 || build.directives[before - 1].mode != "flow")
        return true;
    }
    handled = true;
    return paragraphs(groups, 0);
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

  // Anchors precede the block whose first row follows them.  An anchor
  // followed by another before any row opened precedes that same row: the
  // `SRMSG` that closes one message of a message list and the `SRMSG <id>`
  // that opens the next (SC26-7132-01 2.3.2 record 256 segment 24 and record
  // 257 segment 3, with only `cz OFF EMSGL`, `cz FLOW MSGL` and `cz OFF MSGL`
  // between them), served (DT 19980422095213) as `<a name="MSG"></a>` and
  // then `<a name="MSG ILU901A"><hr>`.
  bool place_anchors() {
    const auto block_of = [&](std::size_t anchor) {
      for (std::size_t block = 0; block < block_first_line.size(); ++block) {
        const auto& first = lines[block_first_line[block]];
        if (first.anchor_before && first.anchor_index == anchor) return block;
      }
      // An anchor before the first row of a definition body the term's
      // block already holds stands at that entry: SC31-6552-00 4.4.3 record
      // 2021 stores `SRSPTSOAROL` between `cz BREAK 7` and the row
      // `       specifies that messages are issued in all SSCPs.`, and hosted
      // (DT 19950503072955) serves `<a name="SPTSOAROL">` on that row of
      // the `ASIRFMSG=ALLSSCP` entry.  The entry is the nearest block the
      // model has; without it the anchor fell to the end of the topic.
      for (const auto& [line, block] : merged_lines)
        if (lines[line].anchor_before && lines[line].anchor_index == anchor)
          return block;
      return npos;
    };
    for (std::size_t anchor = 0; anchor < build.body_anchors.size(); ++anchor) {
      auto placed = build.body_anchors[anchor];
      placed.position = topic.blocks.size();
      auto block = block_of(anchor);
      for (auto next = anchor;
           block == npos && next < build.anchor_successors.size() &&
           build.anchor_successors[next] != npos;) {
        next = build.anchor_successors[next];
        block = block_of(next);
      }
      if (block != npos) placed.position = block;
      // A rule row draws a block only inside a message list; an anchor it
      // carries anywhere else has no row to precede.
      if (block == npos &&
          std::any_of(lines.begin(), lines.end(), [anchor](const Line& line) {
            return line.thematic_break && line.anchor_before &&
                   line.anchor_index == anchor;
          }))
        return fail(error, "anchor " + placed.id +
                               " precedes a rule row outside a message list");
      // An anchor whose row is an empty row, which opens no block, precedes the
      // first block after that row, not the end of the topic.  SC31-8212-00
      // 2.3.3 record 855: `SRHDRHWEOP6`, the repeated `ST  Purpose` heading
      // and three empty rows stand before `cz FLOW P`; hosted (DT
      // 19950906051034) serves `<a name="HDRHWEOP6"><H2> 2.3.3
      // Purpose</H2></a>` ahead of the paragraph.
      if (block == npos)
        for (std::size_t line = 0; line < lines.size(); ++line) {
          if (!lines[line].anchor_before || lines[line].anchor_index != anchor)
            continue;
          // An anchor on a text row of a generic region opens that row,
          // as hosted's `<a name>` wraps the row inside the region's
          // `<pre>`: SY27-0327-05 2.1.8 (DT 19961016091447) serves
          // `<a name="STPSTPUNIQ112">    |    |    |</a>` inside its
          // `cz OFF MAP` step diagram.
          if (const auto found = verbatim_rows.find(line);
              found != verbatim_rows.end()) {
            placed.position = found->second.first;
            placed.row = found->second.second;
            break;
          }
          // Only an empty row: an anchor on a text row inside a block (a
          // definition body) keeps its previous placement.
          if (lines[line].cells.size() > lines[line].text_begin) break;
          for (std::size_t candidate = 0; candidate < block_first_line.size();
               ++candidate)
            if (block_first_line[candidate] >= line) {
              placed.position = candidate;
              break;
            }
          break;
        }
      topic.anchors.push_back(std::move(placed));
    }
    return true;
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
