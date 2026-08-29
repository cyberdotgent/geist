#include "geist/detail/prose_topic_internal.hpp"

#include <algorithm>
#include <cctype>

// The `CZ` dialect of the prose family (Format/markup.md, "CZ layout
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
  bool preformatted(std::size_t begin, std::size_t end) {
    ProseBlockIR block;
    block.kind = ProseBlockKindIR::preformatted;
    const auto index = topic.blocks.size();
    std::vector<std::string> rows;
    std::vector<std::pair<std::size_t, std::size_t>> block_refs;
    for (auto line_index = begin; line_index < end; ++line_index) {
      const auto& line = lines[line_index];
      for (std::size_t blank = 0; blank < line.breaks_before; ++blank)
        rows.emplace_back();
      if (!has_text(line)) {
        if (line.breaks_before == 0) rows.emplace_back();
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
      block.inlines.push_back(std::move(inline_node));
      rows.push_back(std::move(text));
    }
    while (!rows.empty() && rows.back().empty()) rows.pop_back();
    while (!rows.empty() && rows.front().empty()) rows.erase(rows.begin());
    if (block.inlines.empty())
      return fail(error, "cz OFF XMP block has no display rows");
    std::size_t indent = npos;
    for (const auto& row : rows) {
      if (row.empty()) continue;
      indent = std::min(indent, row.find_first_not_of(' '));
    }
    for (auto& row : rows)
      if (!row.empty()) row.erase(0, indent);
    block.preformatted_lines = std::move(rows);
    std::sort(block_refs.begin(), block_refs.end());
    block.slices = slices_for(records, block_refs);
    block.origin = lines[begin].origin;
    topic.blocks.push_back(std::move(block));
    block_first_line.push_back(begin);
    return true;
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
      if (directive.mode == "off" &&
          (tag == "xmp" || tag == "screen" || tag == "lblbox")) {
        std::string opener;
        for (const auto ch : tag)
          opener.push_back(
              static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        const auto closer_tag = "e" + tag;
        if (index + 1 >= build.directives.size() ||
            build.directives[index + 1].mode != "off" ||
            build.directives[index + 1].tag != closer_tag)
          return fail(error, "cz OFF " + opener + " is not closed by cz OFF E" +
                                 opener);
        if (range.first == npos)
          return fail(error, "cz OFF " + opener + " block has no display rows");
        if (!preformatted(range.first, range.second)) return false;
        // The closing directive carries the body text that follows the
        // example block as ordinary paragraphs at its own left/indent
        // (packet 2.4.1 record 57 `cz OFF EXMP 2 2   Note that zeros are
        // omitted ...`, which hosted prints after the `</pre>`).
        const auto closer = ranges[index + 1];
        ++index;
        if (closer.first == npos) return true;
        return paragraphs(group_lines(lines, closer.first, closer.second), 0);
      }
      if (directive.mode == "off") {
        if (tag == "fn") {
          if (!groups.empty())
            return fail(error, name + " carries display text");
          return true;
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
        if (!groups.empty()) return fail(error, name + " carries display text");
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
          // follow it (packet 1.1 record 17).
          for (auto later = index + 1; later < build.directives.size(); ++later)
            if (build.directives[later].tag != "fn")
              return fail(error,
                          name + " without text is not the last directive");
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
