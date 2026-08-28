#include "geist/detail/prose_topic_internal.hpp"

#include "geist/detail/book_topic_catalog_ir.hpp"
#include "geist/detail/menu_ir.hpp"
#include "geist/detail/menu_topic_ir.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace geist::detail::prose_internal {

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------

struct Attr {
  FontStyleIR style = FontStyleIR::unknown;
  std::size_t link = npos;
  bool operator==(const Attr& other) const {
    return style == other.style && link == other.link;
  }
  bool operator!=(const Attr& other) const { return !(*this == other); }
};

struct BlockChar {
  std::string text;
  Attr attr;
  std::size_t record = npos;
  std::size_t token = 0;
  bool space = false;
};

// A span boundary must not split a run of word characters; attached
// punctuation (`AIX.`, `"Bibliography"`) stays outside the styled phrase, as
// BookServer renders it.
bool word_char(const std::string& text) {
  if (text.empty()) return false;
  const auto ch = static_cast<unsigned char>(text.front());
  return std::isalnum(ch) != 0 || ch >= 0x80 || ch == '_';
}

bool boundary_between(const Line& line, std::size_t left, std::size_t right) {
  if (left >= line.cells.size() || right >= line.cells.size()) return true;
  return line.cells[left].space || line.cells[right].space ||
         !word_char(line.cells[left].text) || !word_char(line.cells[right].text);
}

std::string line_text(const Line& line) {
  std::string text;
  for (const auto& cell : line.cells) text += cell.text;
  return text;
}

bool resolve_spans(const Line& line, std::vector<Attr>& attrs,
                   std::vector<std::string>& targets, std::string* error) {
  attrs.assign(line.cells.size(), Attr{});
  const auto apply = [&](const Span& span, bool link) -> bool {
    auto begin = span.begin;
    auto end = span.end;
    const auto where = [&]() {
      return " [" + std::to_string(span.begin) + "," +
             std::to_string(span.end) + ") on '" + line_text(line) + "'";
    };
    if (begin >= end || end > line.cells.size())
      return fail(error, "font/selector span [" + std::to_string(begin) + "," +
                             std::to_string(end) +
                             ") exceeds the display line of " +
                             std::to_string(line.cells.size()) + " cells");
    while (begin < end && line.cells[begin].space) ++begin;
    while (end > begin && line.cells[end - 1].space) --end;
    if (begin >= end) return fail(error, "font/selector span is blank" + where());
    if (begin > line.text_begin && !boundary_between(line, begin - 1, begin))
      return fail(error, "span starts inside a word" + where());
    if (end < line.cells.size() && !boundary_between(line, end - 1, end))
      return fail(error, "span ends inside a word" + where());
    if (begin < line.text_begin) return fail(error, "span covers the bullet");
    for (auto cell = begin; cell < end; ++cell) {
      if (line.cells[cell].space) continue;
      auto& attr = attrs[cell];
      if (link) {
        if (attr.link != npos)
          return fail(error, "overlapping selector spans");
        attr.link = targets.size();
      } else {
        if (attr.style != FontStyleIR::unknown)
          return fail(error, "overlapping font spans");
        attr.style = span.style;
      }
    }
    if (link) targets.push_back(span.target);
    return true;
  };
  for (const auto& span : line.links)
    if (!apply(span, true)) return false;
  for (const auto& span : line.fonts)
    if (!apply(span, false)) return false;
  for (const auto& attr : attrs)
    if (attr.link != npos && attr.style != FontStyleIR::unknown)
      return fail(error, "font span inside a selector span");
  return true;
}

bool build_block(const std::vector<DecodedLogicalRecordSource>& records,
                 const std::vector<Line>& lines, std::size_t begin,
                 std::size_t end, ProseBlockIR& block, Ledger& ledger,
                 std::size_t block_index, std::string* error) {
  std::vector<BlockChar> chars;
  std::vector<std::string> targets;
  for (auto index = begin; index < end; ++index) {
    const auto& line = lines[index];
    std::vector<Attr> attrs;
    std::vector<std::string> line_targets;
    if (!resolve_spans(line, attrs, line_targets, error)) return false;
    const auto target_base = targets.size();
    targets.insert(targets.end(), line_targets.begin(), line_targets.end());
    if (index != begin) chars.push_back({" ", {}, npos, 0, true});
    for (std::size_t cell = line.text_begin; cell < line.cells.size(); ++cell) {
      const auto& source = line.cells[cell];
      auto attr = attrs[cell];
      if (attr.link != npos) attr.link += target_base;
      chars.push_back({source.text, attr, source.record, source.token,
                       source.space});
    }
  }
  // Collapse whitespace; a space takes the attributes of its neighbours when
  // they agree and is plain text otherwise.
  std::vector<BlockChar> collapsed;
  for (std::size_t index = 0; index < chars.size(); ++index) {
    const auto& ch = chars[index];
    if (ch.space) {
      if (collapsed.empty() || collapsed.back().space) continue;
      collapsed.push_back(ch);
      collapsed.back().attr = {};
      continue;
    }
    collapsed.push_back(ch);
  }
  while (!collapsed.empty() && collapsed.back().space) collapsed.pop_back();
  if (collapsed.empty()) return fail(error, "block has no visible text");
  for (std::size_t index = 1; index + 1 < collapsed.size(); ++index) {
    if (collapsed[index].space &&
        collapsed[index - 1].attr == collapsed[index + 1].attr)
      collapsed[index].attr = collapsed[index - 1].attr;
  }
  std::vector<std::pair<std::size_t, std::size_t>> block_refs;
  std::size_t run_begin = 0;
  while (run_begin < collapsed.size()) {
    auto run_end = run_begin;
    while (run_end < collapsed.size() &&
           collapsed[run_end].attr == collapsed[run_begin].attr)
      ++run_end;
    ProseInlineIR inline_node;
    const auto& attr = collapsed[run_begin].attr;
    if (attr.link != npos) {
      inline_node.kind = ProseInlineKindIR::cross_reference;
      inline_node.target = targets[attr.link];
    } else if (attr.style != FontStyleIR::unknown) {
      inline_node.kind = ProseInlineKindIR::emphasis;
      inline_node.style = attr.style;
    }
    std::vector<std::pair<std::size_t, std::size_t>> refs;
    for (auto index = run_begin; index < run_end; ++index) {
      inline_node.text += collapsed[index].text;
      if (collapsed[index].record != npos)
        refs.emplace_back(collapsed[index].record, collapsed[index].token);
    }
    // Space cells carry the space-run token; drop those refs (their role is
    // gap/origin) and keep only text tokens.
    std::vector<std::pair<std::size_t, std::size_t>> text_refs;
    for (const auto& ref : refs) {
      auto& entry = ledger.at(ref.first, ref.second);
      if (entry.role != ProseTokenRoleIR::text) continue;
      if (entry.block != npos && entry.block != block_index)
        return fail(error, "text token shared by two blocks");
      entry.block = block_index;
      entry.inline_index = block.inlines.size();
      text_refs.push_back(ref);
    }
    std::sort(text_refs.begin(), text_refs.end());
    text_refs.erase(std::unique(text_refs.begin(), text_refs.end()),
                    text_refs.end());
    inline_node.slices = slices_for(records, text_refs);
    block_refs.insert(block_refs.end(), text_refs.begin(), text_refs.end());
    if (inline_node.kind != ProseInlineKindIR::text &&
        inline_node.slices.empty())
      return fail(error, "styled inline has no source provenance");
    block.inlines.push_back(std::move(inline_node));
    run_begin = run_end;
  }
  std::sort(block_refs.begin(), block_refs.end());
  // A CZ definition entry or note builds its term and body with two calls
  // on one block; the term precedes the body in source order.
  auto slices = slices_for(records, block_refs);
  block.slices.insert(block.slices.end(), slices.begin(), slices.end());
  return true;
}

bool build_blocks(const std::vector<DecodedLogicalRecordSource>& records,
                  const LineBuild& lines_build, Ledger& ledger,
                  ProseTopicIR& topic, std::string* error) {
  // The CZ dialect names every block boundary; the origin heuristics below
  // are for the flattened dialect only.
  if (!lines_build.directives.empty())
    return build_cz_blocks(records, lines_build, ledger, topic, error);
  const auto& lines = lines_build.lines;
  std::vector<std::pair<std::size_t, std::size_t>> ranges;  // [begin,end)
  std::vector<bool> is_item;
  std::vector<std::size_t> origins;
  // A table/figure span between two lines ends the block before it.
  std::set<std::size_t> span_lines;
  for (const auto& mark : lines_build.span_marks) {
    if (mark.span >= topic.spans.size() || mark.line > lines.size())
      return fail(error, "span mark addresses no span or line");
    span_lines.insert(mark.line);
  }
  std::size_t index = 0;
  std::size_t carried_breaks = 0;
  while (index < lines.size()) {
    const auto& first = lines[index];
    if (first.cells.size() <= first.text_begin) {
      // A row without text is vertical spacing between blocks.
      if (first.bullet)
        return fail(error, "bullet row has no text: '" + line_text(first) +
                               "'");
      carried_breaks += first.breaks_before + 1;
      ++index;
      continue;
    }
    carried_breaks = 0;
    auto end = index + 1;
    while (end < lines.size()) {
      const auto& next = lines[end];
      if (span_lines.count(end) != 0) break;
      if (next.breaks_before != 0 || next.anchor_before || next.bullet) break;
      if (next.cells.size() <= next.text_begin) break;
      if (first.bullet && next.origin <= first.origin) break;
      if (!first.bullet && next.origin < first.origin) break;
      ++end;
    }
    ranges.emplace_back(index, end);
    is_item.push_back(first.bullet);
    origins.push_back(first.origin);
    index = end;
  }
  std::size_t list_ordinal = 0;
  for (std::size_t block_index = 0; block_index < ranges.size(); ++block_index) {
    ProseBlockIR block;
    block.origin = origins[block_index];
    if (is_item[block_index]) {
      block.kind = ProseBlockKindIR::list_item;
      if (block_index == 0 || !is_item[block_index - 1])
        ++list_ordinal;
      else if (origins[block_index] != origins[block_index - 1])
        return fail(error, "nested or misaligned list items");
      block.list_ordinal = list_ordinal;
    }
    if (!build_block(records, lines, ranges[block_index].first,
                     ranges[block_index].second, block, ledger, block_index,
                     error))
      return false;
    topic.blocks.push_back(std::move(block));
  }
  // Anchors: leading ones precede block 0; body anchors precede the block
  // that starts at their line, or the end.
  const auto leading_anchors = topic.anchors.size();
  for (std::size_t anchor = 0; anchor < lines_build.body_anchors.size();
       ++anchor) {
    auto placed = lines_build.body_anchors[anchor];
    placed.position = ranges.size();
    for (std::size_t block_index = 0; block_index < ranges.size(); ++block_index) {
      const auto& first = lines[ranges[block_index].first];
      if (first.anchor_before && first.anchor_index == anchor) {
        placed.position = block_index;
        break;
      }
    }
    topic.anchors.push_back(std::move(placed));
  }
  // Spans precede the first block starting at or after their line (or the
  // end) and, within that position, follow the leading anchors and the body
  // anchors already seen at their line.
  std::vector<bool> placed_spans(topic.spans.size(), false);
  for (const auto& mark : lines_build.span_marks) {
    if (placed_spans[mark.span])
      return fail(error, "span is marked twice");
    placed_spans[mark.span] = true;
    std::size_t position = ranges.size();
    for (std::size_t block_index = 0; block_index < ranges.size(); ++block_index)
      if (ranges[block_index].first >= mark.line) {
        position = block_index;
        break;
      }
    auto& span = topic.spans[mark.span];
    span.position = position;
    span.anchors_before = position == 0 ? leading_anchors : 0;
    for (std::size_t anchor = 0;
         anchor < mark.anchors_seen && anchor < lines_build.body_anchors.size();
         ++anchor)
      if (topic.anchors[leading_anchors + anchor].position == position)
        ++span.anchors_before;
  }
  if (std::any_of(placed_spans.begin(), placed_spans.end(),
                  [](const bool placed) { return !placed; }))
    return fail(error, "table/figure span never reached the body stream");
  return true;
}

// ---------------------------------------------------------------------------
// Trailing menu
// ---------------------------------------------------------------------------

bool build_menu(const std::vector<DecodedLogicalRecordSource>& records,
                const StreamBuild& build,
                const BookTopicCatalogIR* book_topic_catalog, Ledger& ledger,
                ProseTopicIR& topic, std::string* error) {
  if (build.menu_record == npos) return true;
  if (book_topic_catalog == nullptr)
    return fail(error, "trailing menu needs the book topic catalog");
  std::string menu_error;
  const auto raw = extract_source_menu_ir(records, &menu_error);
  if (!raw) return fail(error, "trailing menu rejected: " + menu_error);
  auto validation =
      validate_source_menu_targets(*raw, *book_topic_catalog, &menu_error);
  if (!validation) {
    // The catalog's topic-header title is a compatibility projection that can
    // carry glued body text (`<title>??? SI ...`); the TOC title is the
    // canonical spelling BookServer prints.  Accept a label that equals the
    // target's TOC title verbatim.
    MenuTargetValidationIR fallback;
    for (const auto& item : raw->items) {
      const auto* entry =
          find_book_topic_catalog_entry(*book_topic_catalog, item.target);
      if (entry == nullptr)
        return fail(error, "trailing menu targets rejected: " + menu_error);
      // The full label is tried first: a one-byte final word such as
      // SH20-918 `... SCRIPT/VS and GML` is title text.  Only when it
      // disagrees is the source-proven compact terminal token (SC31-711
      // record 127 `Generic Traps :`) excluded.
      const auto label_matches = [&](const std::string& candidate) {
        if (candidate.empty()) return false;
        if (std::any_of(entry->toc_entries.begin(), entry->toc_entries.end(),
                        [&](const auto& toc) {
                          return ascii_equals_case_insensitive(
                              candidate,
                              collapse_ascii_whitespace(trim_ascii(toc.title)));
                        }))
          return true;
        if (!entry->topic_header) return false;
        // A glued header title is the label followed by a one-cell row
        // marker and the first body row; the label must end at that
        // non-alphanumeric boundary.
        const auto header =
            collapse_ascii_whitespace(trim_ascii(entry->topic_header->title));
        return header.size() > candidate.size() &&
               ascii_equals_case_insensitive(header.substr(0, candidate.size()),
                                             candidate) &&
               std::isalnum(static_cast<unsigned char>(
                   header[candidate.size()])) == 0;
      };
      auto label = collapse_ascii_whitespace(trim_ascii(item.text));
      auto matches = label_matches(label);
      if (!matches && item.compact_terminal) {
        std::string stripped;
        for (std::size_t cell = 0;
             cell < item.compact_terminal->label_cell_begin &&
             cell < item.label_cells.size();
             ++cell)
          stripped += token_words_to_ascii({item.label_cells[cell].word});
        stripped = collapse_ascii_whitespace(trim_ascii(stripped));
        if (label_matches(stripped)) {
          label = stripped;
          matches = true;
        }
      }
      if (!matches)
        return fail(error, "trailing menu targets rejected: " + menu_error);
      MenuTargetValidationEntryIR validated;
      validated.target = item.target;
      validated.label = label;
      validated.existence =
          MenuTargetValidationEntryIR::ExistenceEvidence::toc_entry;
      validated.label_evidence =
          MenuTargetValidationEntryIR::LabelEvidence::toc_title;
      fallback.items.push_back(std::move(validated));
    }
    validation = std::move(fallback);
  }
  if (validation->items.size() != raw->items.size())
    return fail(error, "trailing menu validation is incomplete");
  // Every token from the menu start onwards belongs to the menu.
  for (auto record_index = build.menu_record; record_index < records.size();
       ++record_index) {
    const auto& record = records[record_index];
    std::size_t first_token = 0;
    if (record_index == build.menu_record) {
      if (build.menu_segment >= record.control_segments.size())
        return fail(error, "menu record has no segments");
      const auto& menu = record.control_segments[build.menu_segment];
      if (menu.source_tokens.empty())
        return fail(error, "menu start has no source token");
      first_token = menu.source_tokens.front();
    }
    for (auto token = first_token; token < record.ir.tokens.size(); ++token) {
      if (ledger.at(record_index, token).role != ProseTokenRoleIR::unassigned)
        continue;
      if (!ledger.assign(record_index, token, ProseTokenRoleIR::menu, error))
        return false;
    }
    // Anything before the menu start in this record must already be owned.
  }
  for (std::size_t index = 0; index < raw->items.size(); ++index) {
    const auto& item = raw->items[index];
    const auto& entry = validation->items[index];
    if (entry.target != item.target)
      return fail(error, "menu validation target mismatch");
    ProseMenuItemIR typed;
    typed.target = entry.target;
    typed.label = entry.label;
    const auto record = std::find_if(
        records.begin(), records.end(), [&](const auto& candidate) {
          return candidate.logical_record == item.logical_record;
        });
    if (record == records.end() || item.target_cells.empty())
      return fail(error, "menu item provenance is incomplete");
    auto begin = item.target_cells.front().token_index;
    auto end = begin + 1;
    for (const auto* cells : {&item.target_cells, &item.label_cells})
      for (const auto& cell : *cells) {
        begin = std::min(begin, cell.token_index);
        end = std::max(end, cell.token_index + 1);
      }
    typed.source = token_slice(*record, begin, end);
    topic.menu_items.push_back(std::move(typed));
  }
  if (topic.menu_items.empty()) return fail(error, "trailing menu is empty");
  return true;
}

} // namespace geist::detail::prose_internal
