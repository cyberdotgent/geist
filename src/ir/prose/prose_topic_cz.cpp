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
// `message_rule`).
std::vector<std::pair<std::size_t, std::size_t>> group_lines(
    const std::vector<Line>& lines, std::size_t begin, std::size_t end,
    bool rules = false) {
  std::vector<std::pair<std::size_t, std::size_t>> groups;
  std::size_t index = begin;
  while (index < end) {
    if (rules && lines[index].thematic_break) {
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
           !lines[stop].anchor_before &&
           !(rules && lines[stop].thematic_break)) {
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

  bool list_item(const LayoutDirective& directive,
                 const std::vector<std::pair<std::size_t, std::size_t>>& groups) {
    if (stack.empty() || (stack.back().tag != "ul" && stack.back().tag != "ol" &&
                          stack.back().tag != "sl" && stack.back().tag != "notel"))
      return fail(error, "cz FLOW LI outside an open list");
    if (groups.empty()) return fail(error, "cz FLOW LI has no text");
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
    if (frame.pending_definition != npos)
      return fail(error, "cz FLOW DT follows a term with no cz FLOW DD body");
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
    frame.pending_definition = npos;
    frame.body_owner = block_index;
    return paragraphs(groups, 1);
  }

  bool empty_definition_with_block_body(const LayoutDirective& directive,
                                        std::size_t first_body_line) {
    if (stack.empty() || stack.back().tag != "dl")
      return fail(error, "cz FLOW DT outside an open definition list");
    auto& frame = stack.back();
    if (frame.pending_definition != npos)
      return fail(error, "cz FLOW DT follows a term with no cz FLOW DD body");
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

  bool parameter_term(
      const std::vector<std::pair<std::size_t, std::size_t>>& groups) {
    if (stack.empty() || stack.back().tag != "parml")
      return fail(error, "cz FLOW PT outside an open parameter list");
    auto& frame = stack.back();
    if (frame.pending_definition != npos)
      return fail(error, "cz FLOW PT follows a term with no cz BREAK body");
    if (groups.empty()) return fail(error, "cz FLOW PT has no text");
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
    if (!build_block(records, lines, begin, end, block, ledger, block_index,
                     error))
      return false;
    block.term_inline_count = block.inlines.size();
    append(std::move(block), begin);
    frame.pending_definition = block_index;
    return true;
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

  bool implicit_parameter_list(std::size_t index) const {
    if (index == 0) return false;
    const auto& previous = build.directives[index - 1];
    if (previous.mode != "off" || previous.tag != "esyntax") return false;
    // This dialect omits PARML after a syntax diagram, but retains a complete
    // PT/BREAK sequence and an explicit EPARML boundary.  Prove that framing
    // from the controls themselves before treating the first PT as an opener.
    for (auto cursor = index; cursor < build.directives.size(); ++cursor) {
      const auto& candidate = build.directives[cursor];
      if (candidate.mode == "off" && candidate.tag == "eparml") return true;
      if (candidate.mode == "break") continue;
      if (candidate.mode == "flow" &&
          (candidate.tag == "pt" || candidate.tag == "p" ||
           candidate.tag == "pc" || candidate.tag == "gd"))
        continue;
      return false;
    }
    return false;
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
    append(std::move(block), begin);
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
    return paragraphs(group_lines(lines, closer.first, closer.second,
                                  in_message_list()),
                      0);
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
    return paragraphs(group_lines(lines, closer.first, closer.second,
                                  in_message_list()),
                      0)
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
  bool leading_rows(std::size_t end) {
    auto list = npos;
    std::size_t item_origin = 0;
    for (const auto& [group_begin, group_end] : group_lines(lines, 0, end)) {
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
    if (!leading_rows(leading_end)) return false;
    for (std::size_t index = 0; index < directives.size(); ++index) {
      const auto& directive = directives[index];
      const auto range = ranges[index];
      const auto groups =
          range.first == npos
              ? std::vector<std::pair<std::size_t, std::size_t>>{}
              : group_lines(lines, range.first, range.second,
                            in_message_list());
      const auto name = "cz " + directive.mode + " " + directive.tag;
      const auto& tag = directive.tag;
      if (directive.mode == "break") {
        if (!stack.empty() && stack.back().tag == "parml" &&
            stack.back().pending_definition != npos) {
          if (!parameter_body(groups)) return false;
          continue;
        }
        if (!paragraphs(groups, 0)) return false;
        continue;
      }
      if (!handle(index, directive, range, groups, name, tag)) {
        if (error != nullptr && error->rfind("cz ", 0) != 0)
          *error = name + ": " + *error;
        return false;
      }
    }
    // A message list announced at the very end of a topic holds the messages
    // of the topics that follow: GC35-0271-02 6.0 ends its menu of the
    // seventy-three message topics 6.1 .. 6.73 with `cemenu`, `cz BREAK 3`,
    // `cz FLOW MSGL 3 3`, `cz OFF MSGL` (record 628 segments 18-21) and
    // nothing after, and hosted (DT 19980714150144) serves only an empty
    // `<pre width="80">` for the pair.  The list is closed elsewhere, so the
    // pair draws nothing here; any row or directive after it would be
    // content of a list this topic never closes, and still fails.
    if (stack.size() == 1 && stack.back().tag == "msgl" &&
        build.directives.size() >= 2) {
      const auto last = build.directives.size() - 1;
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
      if (stack.empty() || stack.back().tag != "msgl")
        return fail(error, name + " closes no open msgl");
      stack.pop_back();
      return paragraphs(groups, 0);
    }
    // A message body block.
    if (stack.empty() || stack.back().tag != "msgl")
      return fail(error, name + " is outside a message list");
    return paragraphs(groups, 0);
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
        // A term may hand its body directly to a source-declared block
        // region without an intervening DD.  The pending term is the owner of
        // that region, not an inline definition missing its text.
        if (tag == "lines" && !stack.empty() && stack.back().tag == "dl" &&
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
        auto closer_index = index + 1;
        auto region_begin = range.first;
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
        if (region_begin == npos)
          return fail(error, "cz OFF " + opener + " block has no display rows");
        if (!preformatted(region_begin, region_end)) return false;
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
        return paragraphs(group_lines(lines, closer.first, closer.second,
                                    in_message_list()), 0);
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
          if (stack.back().pending_definition != npos)
            return fail(error, opened == "parml"
                                   ? "cz parameter term has no cz BREAK body"
                                   : "cz definition term has no cz FLOW DD body");
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
      if (tag == "pt") {
        if ((stack.empty() || stack.back().tag != "parml") &&
            implicit_parameter_list(index)) {
          stack.push_back({"parml", next_list_ordinal++, 0});
        }
        return parameter_term(groups);
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
        if (!list_item(directive, groups)) return false;
        return true;
      }
      if (tag == "lp") {
        if (stack.empty())
          return fail(error, "cz FLOW LP outside an open list");
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
        if (groups.empty() && index + 1 < build.directives.size() &&
            build.directives[index + 1].mode == "off" &&
            build.directives[index + 1].tag == "lines")
          return empty_definition_body_before_block();
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
