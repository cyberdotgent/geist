// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/trap_catalog_document_lowering.hpp"

#include "geist/detail/core/internal.hpp"
#include "geist/detail/lowering/font_style_inlines.hpp"

#include <algorithm>
#include <iterator>
#include <tuple>
#include <variant>
#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
  return false;
}

DocumentNodeOriginIR origin(std::string detail) {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::semantic_lowering;
  result.detail = std::move(detail);
  return result;
}

DocumentNodeOriginIR synthesized(std::string detail) {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::synthesized;
  result.detail = std::move(detail);
  return result;
}

void add_slice(DocumentNodeOriginIR &destination,
               const DocumentSourceSliceIR &slice) {
  if (slice.logical_record != 0 && slice.token_begin < slice.token_end)
    destination.slices.push_back(slice);
}

void add_rows(DocumentNodeOriginIR &destination,
              const std::vector<DocumentSourceRowIR> &rows) {
  for (const auto &row : rows)
    if (row.display_run != 0)
      destination.rows.push_back(row);
}

using SliceKey = std::tuple<std::uint32_t, std::size_t, std::size_t,
                            std::size_t, std::uint32_t, std::uint32_t>;

SliceKey slice_key(const DocumentSourceSliceIR &slice) {
  return {slice.logical_record, slice.segment_index, slice.token_begin,
          slice.token_end,      slice.byte_begin,    slice.byte_end};
}

DocumentNodeOriginIR canonical(DocumentNodeOriginIR value) {
  std::sort(value.slices.begin(), value.slices.end(),
            [](const auto &left, const auto &right) {
              return slice_key(left) < slice_key(right);
            });
  value.slices.erase(std::unique(value.slices.begin(), value.slices.end(),
                                 [](const auto &left, const auto &right) {
                                   return slice_key(left) == slice_key(right);
                                 }),
                     value.slices.end());
  std::sort(value.rows.begin(), value.rows.end(),
            [](const auto &left, const auto &right) {
              return std::make_pair(left.display_run, left.row_index) <
                     std::make_pair(right.display_run, right.row_index);
            });
  value.rows.erase(std::unique(value.rows.begin(), value.rows.end(),
                               [](const auto &left, const auto &right) {
                                 return left.display_run == right.display_run &&
                                        left.row_index == right.row_index;
                               }),
                   value.rows.end());
  return value;
}

DocumentNodeOriginIR text_origin(const TrapTextIR &text, std::string detail) {
  auto result = origin(std::move(detail));
  for (const auto &slice : text.source_slices)
    add_slice(result, slice);
  for (const auto& link : text.links)
    add_slice(result, link.source);
  add_rows(result, text.source_rows);
  return canonical(std::move(result));
}

DocumentNodeOriginIR line_origin(const TrapLineIR &line, std::string detail) {
  auto result = text_origin(line.body, std::move(detail));
  add_slice(result, line.font_source);
  return canonical(std::move(result));
}

std::optional<EmphasisKindIR> emphasis_for(FontStyleIR style) {
  switch (style) {
  case FontStyleIR::highlight_1:
    return EmphasisKindIR::emphasis;
  // A `V` (PV) span is a programming variable, hosted `<var>`: N2AH1MST 2.0
  // (DT 19910329000100) serves `<B>MEMBER</B> <var>mem</var>` inside a
  // headline and `<var>prm</var><B>:</B>  <var>text</var>` on its next row.
  // It takes the variable convention every other family lowers it to.
  case FontStyleIR::variable:
    return font_style_emphasis_kind(style);
  case FontStyleIR::highlight_2:
  case FontStyleIR::bold_phrase:
    return EmphasisKindIR::strong;
  case FontStyleIR::highlight_3:
    return EmphasisKindIR::strong_emphasis;
  case FontStyleIR::highlight_5:
  case FontStyleIR::highlight_6:
  case FontStyleIR::highlight_7:
  case FontStyleIR::highlight_8:
  case FontStyleIR::highlight_9:
  case FontStyleIR::citation:
  case FontStyleIR::example_phrase:
  case FontStyleIR::keyword:
  case FontStyleIR::keyword_define:
  case FontStyleIR::italic_phrase:
  case FontStyleIR::warning:
  case FontStyleIR::warning_text:
  case FontStyleIR::apl_symbol:
  case FontStyleIR::unknown:
    break;
  }
  return std::nullopt;
}

// Highlighted span words as separate inlines: BookServer renders each CFONT
// span as its own run, separated by the gap its columns leave. Spans that
// abut leave no gap and no separator. The entry headline is the term of the
// definition item: a span whose style has no rendering of its own is strong,
// which also covers the headline CFONT code the decoder leaves glued to its
// control boundary (`2,`). A styled span keeps its own style there.
void append_spans(InlineSequenceIR &content,
                  const std::vector<TrapStyledSpanIR> &spans,
                  const DocumentNodeOriginIR &span_origin,
                  bool definition_term = false) {
  for (std::size_t index = 0; index < spans.size(); ++index) {
    // Two spans that abut in the CFONT columns spell one word between them:
    // N2AH1MST record 1729 line 8 is `   CSV028I JOBNAME=jjj STEPNAME=sss
    // [ABENDcde-rc]` and hosted 2.0 (DT 19910329000100) serves
    // `<B>[ABEND</B><var>cde</var><B>-</B><var>rc</var><B>]</B>` with no
    // space anywhere. `map_leading_chain` already joins the run that way;
    // the separator here follows the same column evidence.
    const auto abuts =
        index != 0 && spans[index].span.column ==
                          spans[index - 1].span.column +
                              spans[index - 1].span.length;
    if (index != 0 && !abuts)
      content.push_back({TextInlineIR{" "}, synthesized("span separator")});
    auto kind = emphasis_for(spans[index].span.style);
    if (definition_term && !kind)
      kind = EmphasisKindIR::strong;
    // Abutting spans that land on the same emphasis are one run: emitting
    // two closes and reopens the markup inside a word.
    if (abuts && !content.empty()) {
      auto &previous = content.back().node;
      if (kind) {
        if (auto *emphasis = std::get_if<EmphasisInlineIR>(&previous);
            emphasis != nullptr && emphasis->kind == *kind) {
          emphasis->text += spans[index].text;
          continue;
        }
      } else if (auto *text = std::get_if<TextInlineIR>(&previous);
                 text != nullptr) {
        text->text += spans[index].text;
        continue;
      }
    }
    if (kind)
      content.push_back(
          {EmphasisInlineIR{spans[index].text, *kind}, span_origin});
    else
      content.push_back({TextInlineIR{spans[index].text}, span_origin});
  }
}

std::string after_prefix(const std::string &text, const std::string &prefix) {
  if (text.size() <= prefix.size())
    return {};
  return trim_ascii(text.substr(prefix.size()));
}

// The body text after `prefix`, split at the highlighted runs the entry drew
// inside it. With no highlight this is the one text inline the family has
// always emitted, character for character; with highlights it is the same
// characters in order, with each run carried as its own inline so the style
// the source states survives lowering. Fails closed when a recorded range
// does not lie inside the text it names.
bool append_body_after(InlineSequenceIR &content, const TrapTextIR &body,
                       const std::string &prefix,
                       const DocumentNodeOriginIR &body_origin) {
  const auto rest = after_prefix(body.text, prefix);
  if (rest.empty())
    return true;
  // `after_prefix` trims, and the projected text is already collapsed, so the
  // remainder begins at the first non-space byte after the prefix.
  const auto from = body.text.find_first_not_of(' ', prefix.size());
  if (from == std::string::npos ||
      body.text.compare(from, rest.size(), rest) != 0)
    return false;
  if (!body.links.empty()) {
    // Links and emphasis share the body's UTF-8 byte coordinates. This IR
    // cannot nest emphasis inside a link, so reject overlaps rather than
    // silently discard either source claim.
    struct Range {
      std::size_t begin, end;
      std::optional<EmphasisKindIR> emphasis;
      const TrapTextLinkIR* link = nullptr;
    };
    std::vector<Range> ranges;
    for (const auto& span : body.highlights) {
      if (span.end <= from) continue;
      ranges.push_back({span.begin, span.end, emphasis_for(span.style), nullptr});
    }
    for (const auto& link : body.links)
      ranges.push_back({link.begin, link.end, std::nullopt, &link});
    std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) {
      return std::make_pair(a.begin, a.end) < std::make_pair(b.begin, b.end);
    });
    content.push_back({TextInlineIR{" "}, body_origin});
    auto cursor = from;
    for (const auto& range : ranges) {
      if (range.begin < cursor || range.begin >= range.end || range.end > body.text.size())
        return false;
      if (range.begin > cursor)
        content.push_back({TextInlineIR{body.text.substr(cursor, range.begin - cursor)}, body_origin});
      auto label = body.text.substr(range.begin, range.end - range.begin);
      if (range.link != nullptr) {
        if (range.link->target.empty()) return false;
        content.push_back({CrossReferenceInlineIR{
            {CrossReferenceTargetKindIR::anchor, range.link->target}, std::move(label)}, body_origin});
      } else if (range.emphasis)
        content.push_back({EmphasisInlineIR{std::move(label), *range.emphasis}, body_origin});
      else
        content.push_back({TextInlineIR{std::move(label)}, body_origin});
      cursor = range.end;
    }
    if (cursor < body.text.size())
      content.push_back({TextInlineIR{body.text.substr(cursor)}, body_origin});
    return true;
  }
  std::vector<std::pair<std::string, std::optional<EmphasisKindIR>>> pieces;
  const auto push = [&](std::string text, std::optional<EmphasisKindIR> kind) {
    if (text.empty())
      return;
    if (!pieces.empty() && pieces.back().second == kind) {
      pieces.back().first += text;
      return;
    }
    pieces.push_back({std::move(text), kind});
  };
  const auto limit = from + rest.size();
  auto at = from;
  for (const auto &highlight : body.highlights) {
    if (highlight.begin >= highlight.end || highlight.end > body.text.size())
      return false;
    if (highlight.end <= at)
      continue;
    if (highlight.begin < at || highlight.end > limit)
      return false;
    push(body.text.substr(at, highlight.begin - at), std::nullopt);
    push(body.text.substr(highlight.begin, highlight.end - highlight.begin),
         emphasis_for(highlight.style));
    at = highlight.end;
  }
  push(body.text.substr(at, limit - at), std::nullopt);
  // The separator the family has always put between a label and its body. It
  // stays outside the run when the body opens on one, so no highlight covers
  // a space the source did not highlight.
  if (!pieces.empty() && !pieces.front().second)
    pieces.front().first.insert(pieces.front().first.begin(), ' ');
  else if (!pieces.empty())
    content.push_back({TextInlineIR{" "}, body_origin});
  for (auto &piece : pieces) {
    if (piece.second)
      content.push_back(
          {EmphasisInlineIR{piece.first, *piece.second}, body_origin});
    else
      content.push_back({TextInlineIR{piece.first}, body_origin});
  }
  return true;
}

DocumentNodeOriginIR paragraph_origin(const TrapFlowParagraphIR &paragraph,
                                      std::string detail) {
  auto result = origin(std::move(detail));
  for (const auto &slice : paragraph.source_slices)
    add_slice(result, slice);
  for (const auto &boundary : paragraph.break_before)
    add_slice(result, boundary.source);
  for (const auto &link : paragraph.links)
    add_slice(result, link.source);
  add_rows(result, paragraph.source_rows);
  return canonical(std::move(result));
}

bool paragraph_inlines(const TrapFlowParagraphIR &paragraph,
                       const DocumentNodeOriginIR &paragraph_origin,
                       InlineSequenceIR &content, std::size_t from = 0) {
  struct Range {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::optional<EmphasisKindIR> emphasis;
    const TrapTextLinkIR *link = nullptr;
  };
  std::vector<Range> ranges;
  for (const auto &highlight : paragraph.highlights)
    ranges.push_back({highlight.begin, highlight.end,
                      emphasis_for(highlight.style), nullptr});
  for (const auto &link : paragraph.links)
    ranges.push_back({link.begin, link.end, std::nullopt, &link});
  std::sort(ranges.begin(), ranges.end(), [](const auto &left,
                                             const auto &right) {
    return std::make_pair(left.begin, left.end) <
           std::make_pair(right.begin, right.end);
  });
  if (from > paragraph.text.size())
    return false;
  std::size_t at = from;
  for (const auto &range : ranges) {
    if (range.end <= from)
      continue;
    if (range.begin < at || range.begin >= range.end ||
        range.end > paragraph.text.size())
      return false;
    if (range.begin > at)
      content.push_back(
          {TextInlineIR{paragraph.text.substr(at, range.begin - at)},
           paragraph_origin});
    auto text = paragraph.text.substr(range.begin, range.end - range.begin);
    if (range.link != nullptr) {
      if (range.link->target.empty())
        return false;
      content.push_back(
          {CrossReferenceInlineIR{{CrossReferenceTargetKindIR::anchor,
                                   range.link->target},
                                  std::move(text)},
           paragraph_origin});
    } else if (range.emphasis) {
      content.push_back(
          {EmphasisInlineIR{std::move(text), *range.emphasis},
           paragraph_origin});
    } else {
      content.push_back({TextInlineIR{std::move(text)}, paragraph_origin});
    }
    at = range.end;
  }
  if (at < paragraph.text.size())
    content.push_back(
        {TextInlineIR{paragraph.text.substr(at)}, paragraph_origin});
  return !content.empty();
}

// The anchor a drawn envelope's first line carries: the opening opcode
// without `SR` (`SRTBLT1` -> `TBLT1`, `SRFIGFIGUNIQ20` -> `FIGFIGUNIQ20`).
// The rows a region draws, one origin per row. A row the source styles
// also carries its runs as a styled row that flattens byte for byte back to
// its text, so the block keeps every column and only gains the emphasis the
// row's `CFONT` states. Each row's slices are added to `region_origin`.
PreformattedBlockIR drawn_region(const TrapEmbeddedRegionIR &region,
                                 const char *line_detail,
                                 DocumentNodeOriginIR &region_origin) {
  PreformattedBlockIR drawn;
  drawn.lines = region.lines;
  bool styled = false;
  for (const auto &highlights : region.line_highlights)
    for (const auto &highlight : highlights)
      styled = styled || emphasis_for(highlight.style).has_value();
  for (std::size_t line = 0; line < region.lines.size(); ++line) {
    auto line_origin = origin(line_detail);
    add_slice(line_origin, region.line_sources[line]);
    line_origin = canonical(std::move(line_origin));
    add_slice(region_origin, region.line_sources[line]);
    if (styled) {
      const auto &text = region.lines[line];
      InlineSequenceIR row;
      std::size_t at = 0;
      const auto plain = [&](const std::size_t end) {
        if (end <= at)
          return;
        if (!row.empty() &&
            std::holds_alternative<TextInlineIR>(row.back().node))
          std::get<TextInlineIR>(row.back().node).text +=
              text.substr(at, end - at);
        else
          row.push_back({TextInlineIR{text.substr(at, end - at)}, line_origin});
        at = end;
      };
      for (const auto &highlight : region.line_highlights[line]) {
        const auto kind = emphasis_for(highlight.style);
        if (!kind)
          continue;
        plain(highlight.begin);
        row.push_back({EmphasisInlineIR{text.substr(highlight.begin,
                                                    highlight.end -
                                                        highlight.begin),
                                        *kind},
                       line_origin});
        at = highlight.end;
      }
      plain(text.size());
      drawn.styled_lines.push_back(std::move(row));
    }
    drawn.line_origins.push_back(std::move(line_origin));
  }
  return drawn;
}

std::string region_anchor_id(const TrapEmbeddedRegionIR &region) {
  return (region.start.kind == TrapEmbeddedControlKindIR::figure_start
              ? "FIG"
              : "TBL") +
         region.identifier;
}

bool lower_flow_blocks(const std::vector<TrapFlowBlockIR> &source,
                       BlockSequenceIR &destination, std::string *error) {
  for (const auto &block : source) {
    if (const auto *anchor = std::get_if<TrapFlowAnchorIR>(&block.value)) {
      auto anchor_origin = origin("trap positioned source anchor");
      add_slice(anchor_origin, anchor->source);
      destination.push_back(
          {AnchorBlockIR{anchor->id}, canonical(std::move(anchor_origin))});
      continue;
    }
    if (const auto *footnote = std::get_if<TrapFlowFootnoteIR>(&block.value)) {
      // A footnote is a local destination followed by its own paragraphs,
      // as the prose family lowers `CZ FLOW FN`. The reader prints no label
      // for the id: hosted N2AH1MST 10.0 (DT 19910329000100) serves
      // `<a name="FTNFTNUNIQ19"><hr><h5> |  (*) VTAM is a trademark ...`,
      // whose `(*)` is the footnote's own source text. The closer is
      // provenance only.
      auto anchor_origin = origin("trap footnote envelope");
      add_slice(anchor_origin, footnote->start);
      add_slice(anchor_origin, footnote->end);
      destination.push_back({AnchorBlockIR{footnote->id, AnchorRoleIR::local},
                             canonical(std::move(anchor_origin))});
      if (footnote->blocks.empty())
        return fail(error, "trap footnote has no content");
      for (const auto &inner : footnote->blocks) {
        const auto *paragraph = std::get_if<TrapFlowParagraphIR>(&inner.value);
        if (paragraph == nullptr)
          return fail(error, "trap footnote holds a non-paragraph block");
        auto inner_origin = paragraph_origin(*paragraph, "trap footnote text");
        ParagraphBlockIR lowered;
        if (!paragraph_inlines(*paragraph, inner_origin, lowered.content))
          return fail(error, "trap footnote ranges overlap");
        if (lowered.content.empty())
          return fail(error, "trap footnote has no content");
        destination.push_back({std::move(lowered), std::move(inner_origin)});
      }
      continue;
    }
    if (std::holds_alternative<TrapFlowParagraphIR>(block.value)) {
      const auto &paragraph = std::get<TrapFlowParagraphIR>(block.value);
      auto paragraph_source =
          paragraph_origin(paragraph, "trap flow paragraph");
      InlineSequenceIR content;
      if (!paragraph_inlines(paragraph, paragraph_source, content))
        return fail(error, "trap flow paragraph ranges overlap");
      destination.push_back(
          {ParagraphBlockIR{std::move(content)}, std::move(paragraph_source)});
    } else if (std::holds_alternative<TrapFlowListIR>(block.value)) {
      const auto &source_list = std::get<TrapFlowListIR>(block.value);
      ListBlockIR list;
      list.ordered = source_list.ordered;
      for (const auto &source_item : source_list.items) {
        ListItemIR item;
        item.origin = origin("trap native list item marker");
        add_slice(item.origin, source_item.marker_source);
        item.origin = canonical(std::move(item.origin));
        if (!lower_flow_blocks(source_item.blocks, item.blocks, error))
          return false;
        if (item.blocks.empty())
          return fail(error, "trap native list item has no content");
        if (item.blocks.size() == 1 &&
            std::holds_alternative<ParagraphBlockIR>(item.blocks.front().node)) {
          item.content = std::move(
              std::get<ParagraphBlockIR>(item.blocks.front().node).content);
          item.blocks.clear();
        }
        list.items.push_back(std::move(item));
      }
      if (list.items.empty())
        return fail(error, "trap native list has no items");
      auto list_origin = list.items.front().origin;
      destination.push_back({std::move(list), std::move(list_origin)});
    } else {
      const auto &region = std::get<TrapEmbeddedRegionIR>(block.value);
      if (region.lines.size() != region.line_sources.size())
        return fail(error, "trap flow region lines have no provenance");
      if (!region.identifier.empty()) {
        auto anchor_source = origin("trap embedded table source anchor");
        add_slice(anchor_source, region.start.source);
        destination.push_back(
            {AnchorBlockIR{region_anchor_id(region)},
             canonical(std::move(anchor_source))});
      }
      auto region_origin = origin("trap embedded table");
      PreformattedBlockIR drawn =
          drawn_region(region, "trap embedded table line", region_origin);
      add_slice(region_origin, region.start.source);
      add_slice(region_origin, region.end.source);
      destination.push_back(
          {std::move(drawn), canonical(std::move(region_origin))});
    }
  }
  return true;
}

// An introduction paragraph whose rows carry `CFONT` runs. The introduction
// is prose, so each run lowers as the prose family lowers the same style
// (`font_style_is_code`, `font_style_emphasis_kind`); a style with no known
// presentation stays text. Row line breaks and selector ranges are placed
// with the runs; a run a selector also claims stays the selector's label,
// unstyled, and a break inside a run, or a range outside the text, fails.
bool styled_introduction_inlines(const TrapIntroductionParagraphIR &paragraph,
                                 const DocumentNodeOriginIR &paragraph_origin,
                                 const DocumentNodeOriginIR &break_origin,
                                 InlineSequenceIR &content,
                                 std::string *error) {
  std::size_t cursor = 0;
  if (!paragraph.leading_spans.empty()) {
    std::string joined;
    for (const auto &span : paragraph.leading_spans) {
      if (!joined.empty())
        joined.push_back(' ');
      joined += span.text;
    }
    if (paragraph.text.compare(0, joined.size(), joined) != 0)
      return fail(error, "introduction highlight does not prefix its paragraph");
    append_spans(content, paragraph.leading_spans, paragraph_origin);
    cursor = joined.size();
  }
  for (const auto offset : paragraph.line_breaks)
    if (offset < cursor)
      return fail(error, "introduction line break falls inside its highlight");
  struct Range {
    std::size_t begin = 0;
    std::size_t end = 0;
    FontStyleIR style = FontStyleIR::unknown;
    const TrapTextLinkIR *link = nullptr;
  };
  std::vector<Range> ranges;
  for (const auto &link : paragraph.links)
    ranges.push_back({link.begin, link.end, FontStyleIR::unknown, &link});
  for (const auto &highlight : paragraph.highlights)
    if (std::none_of(paragraph.links.begin(), paragraph.links.end(),
                     [&](const auto &link) {
                       return highlight.begin < link.end &&
                              link.begin < highlight.end;
                     }))
      ranges.push_back({highlight.begin, highlight.end, highlight.style});
  std::sort(ranges.begin(), ranges.end(),
            [](const auto &left, const auto &right) {
              return left.begin < right.begin;
            });
  const auto text = [&](std::size_t from, std::size_t to) {
    if (to <= from)
      return;
    auto piece = paragraph.text.substr(from, to - from);
    if (!content.empty() &&
        std::holds_alternative<TextInlineIR>(content.back().node))
      std::get<TextInlineIR>(content.back().node).text += piece;
    else
      content.push_back({TextInlineIR{std::move(piece)}, paragraph_origin});
  };
  const auto plain = [&](std::size_t from, std::size_t to) {
    for (const auto offset : paragraph.line_breaks) {
      if (offset < from || offset >= to)
        continue;
      text(from, offset);
      content.push_back({HardBreakInlineIR{}, break_origin});
      from = offset + 1;
    }
    text(from, to);
  };
  for (const auto &range : ranges) {
    if (range.begin < cursor || range.begin >= range.end ||
        range.end > paragraph.text.size())
      return fail(error, "introduction runs do not fit their paragraph");
    for (const auto offset : paragraph.line_breaks)
      if (offset >= range.begin && offset < range.end)
        return fail(error, "introduction line break falls inside a run");
    plain(cursor, range.begin);
    auto label = paragraph.text.substr(range.begin, range.end - range.begin);
    if (range.link != nullptr) {
      if (range.link->target.empty())
        return fail(error, "introduction selector has no target");
      content.push_back({CrossReferenceInlineIR{
                             {CrossReferenceTargetKindIR::anchor,
                              range.link->target},
                             std::move(label)},
                         paragraph_origin});
    } else if (font_style_is_code(range.style)) {
      content.push_back({CodeInlineIR{std::move(label)}, paragraph_origin});
    } else if (range.style != FontStyleIR::unknown) {
      content.push_back(
          {EmphasisInlineIR{std::move(label),
                            font_style_emphasis_kind(range.style)},
           paragraph_origin});
    } else {
      text(range.begin, range.end);
    }
    cursor = range.end;
  }
  plain(cursor, paragraph.text.size());
  return true;
}

bool style_flow_prefix(const TrapLineIR &line, const std::string &prefix,
                       bool definition_term, BlockSequenceIR &blocks,
                       std::string *error) {
  if (blocks.empty() || line.body.flow.blocks.empty() ||
      !std::holds_alternative<ParagraphBlockIR>(blocks.front().node) ||
      !std::holds_alternative<TrapFlowParagraphIR>(
          line.body.flow.blocks.front().value))
    return fail(error, "trap styled line does not open with a paragraph");
  const auto &source_paragraph = std::get<TrapFlowParagraphIR>(
      line.body.flow.blocks.front().value);
  if (source_paragraph.text.compare(0, prefix.size(), prefix) != 0)
    return fail(error, "trap styled prefix is not a paragraph prefix");

  auto source = line_origin(line, "trap styled flow prefix");
  InlineSequenceIR content;
  append_spans(content, line.spans, source, definition_term);
  const auto from =
      source_paragraph.text.find_first_not_of(' ', prefix.size());
  if (from != std::string::npos) {
    content.push_back({TextInlineIR{" "}, source});
    if (!paragraph_inlines(source_paragraph, source, content, from))
      return fail(error, "trap styled flow ranges overlap");
  }
  std::get<ParagraphBlockIR>(blocks.front().node).content = std::move(content);
  return true;
}

bool compound_flow(const TrapFlowSequenceIR &flow) {
  return flow.blocks.size() != 1 ||
         (flow.blocks.size() == 1 &&
          !std::holds_alternative<TrapFlowParagraphIR>(
              flow.blocks.front().value));
}

std::optional<DocumentIR> canonical_document(TopicIdentityIR topic,
                                             const TrapCatalogIR &catalog,
                                             std::string *error) {
  const auto reject = [&](std::string message) {
    fail(error, std::move(message));
    return std::optional<DocumentIR>{};
  };
  if (catalog.entries.empty() || catalog.title.empty() ||
      catalog.raw_topic_id.empty() || catalog.heading_level.size() != 2)
    return reject("trap catalog envelope is incomplete");
  // The topic-start opcode carries the id in the case the record stored it,
  // which is not always the case the table of contents publishes; both name
  // the same topic. The published spelling is the one every cross reference
  // resolves against, so the catalog agrees with it rather than replacing it.
  if ((!topic.id.empty() &&
       !ascii_equals_case_insensitive(topic.id, catalog.raw_topic_id)) ||
      (topic.start_logical_record != 0 &&
       topic.start_logical_record != catalog.first_logical_record) ||
      (topic.end_logical_record != 0 &&
       topic.end_logical_record != catalog.end_logical_record))
    return reject("topic and trap catalog envelopes differ");
  if (!topic.title.empty() &&
      !ascii_equals_case_insensitive(collapse_ascii_whitespace(topic.title),
                                     catalog.title))
    return reject("topic title differs from the catalog title row: [" +
                  topic.title + "] vs [" + catalog.title + "]");
  if (topic.id.empty()) topic.id = catalog.raw_topic_id;
  topic.title = catalog.title;
  topic.heading_level = catalog.heading_level;
  topic.start_logical_record = catalog.first_logical_record;
  topic.end_logical_record = catalog.end_logical_record;

  DocumentIR document;
  document.topic = std::move(topic);

  auto heading_origin = origin("trap catalog heading");
  add_slice(heading_origin, catalog.title_source);
  add_rows(heading_origin, {catalog.title_row});
  heading_origin = canonical(std::move(heading_origin));
  const auto level =
      static_cast<std::uint32_t>(catalog.heading_level.back() - '0');
  document.blocks.push_back(
      {HeadingBlockIR{level, {{TextInlineIR{catalog.title}, heading_origin}}},
       heading_origin});

  document.named_destinations = catalog.entry_named_destinations;

  for (const auto &anchor : catalog.anchors) {
    auto anchor_origin = origin("trap catalog source anchor");
    add_slice(anchor_origin, anchor.source);
    document.blocks.push_back({AnchorBlockIR{anchor.id}, anchor_origin});
  }

  // An `SRTBL<id>` envelope the introduction draws sits between two of its
  // paragraphs, and is served exactly as an entry's is: the region's own
  // display lines with `<a name="TBL<id>">` on the first.
  std::size_t next_introduction_region = 0;
  const auto emit_introduction_regions = [&](std::size_t after_paragraph) {
    while (next_introduction_region < catalog.introduction_regions.size() &&
           catalog.introduction_regions[next_introduction_region].after_field ==
               after_paragraph) {
      const auto &region =
          catalog.introduction_regions[next_introduction_region];
      if (region.lines.size() != region.line_sources.size())
        return false;
      if (!region.identifier.empty()) {
        auto region_anchor = origin("trap introduction table source anchor");
        add_slice(region_anchor, region.start.source);
        document.blocks.push_back(
            {AnchorBlockIR{region_anchor_id(region)}, region_anchor});
      }
      auto region_origin = origin("trap introduction table");
      PreformattedBlockIR drawn =
          drawn_region(region, "trap introduction table line", region_origin);
      add_slice(region_origin, region.start.source);
      add_slice(region_origin, region.end.source);
      region_origin = canonical(std::move(region_origin));
      document.blocks.push_back({std::move(drawn), std::move(region_origin)});
      ++next_introduction_region;
    }
    return true;
  };
  if (!emit_introduction_regions(0))
    return reject("trap introduction region lines have no line provenance");

  // The introduction's native lists: a paragraph with a list marker opens an
  // item, `list_continuation` paragraphs are further blocks of it, and any
  // other paragraph or drawn region ends the list.
  std::optional<ListBlockIR> open_list;
  DocumentNodeOriginIR open_list_origin;
  const auto close_list = [&] {
    if (!open_list)
      return;
    document.blocks.push_back({std::move(*open_list), open_list_origin});
    open_list.reset();
  };
  for (std::size_t paragraph_index = 0;
       paragraph_index < catalog.introduction.size(); ++paragraph_index) {
    const auto &paragraph = catalog.introduction[paragraph_index];
    auto paragraph_origin = origin("trap catalog introduction paragraph");
    for (const auto &slice : paragraph.source_slices)
      add_slice(paragraph_origin, slice);
    for (const auto &link : paragraph.links)
      add_slice(paragraph_origin, link.source);
    add_rows(paragraph_origin, paragraph.source_rows);
    paragraph_origin = canonical(std::move(paragraph_origin));
    InlineSequenceIR content;
    std::string rest = paragraph.text;
    // The text after `from`, with each recorded row line break as a hard
    // break in place of its separator space. Rows the source ended on
    // purpose stay lines of the one paragraph; hosted BookServer serves them
    // one per line inside a single `<p>`.
    auto break_origin = paragraph_origin;
    break_origin.detail = "trap catalog introduction row line break";
    const auto append_broken = [&](std::size_t from) {
      for (const auto offset : paragraph.line_breaks) {
        if (offset < from)
          return false;
        if (offset > from)
          content.push_back(
              {TextInlineIR{paragraph.text.substr(from, offset - from)},
               paragraph_origin});
        content.push_back({HardBreakInlineIR{}, break_origin});
        from = offset + 1;
      }
      if (from < paragraph.text.size())
        content.push_back(
            {TextInlineIR{paragraph.text.substr(from)}, paragraph_origin});
      return true;
    };
    std::size_t rest_from = 0;
    // Selector ranges and row line breaks are not placed together: no
    // introduction paragraph is observed to carry both.
    if (!paragraph.links.empty() && !paragraph.line_breaks.empty())
      return reject("introduction selector shares its paragraph with a row "
                    "line break");
    if (!paragraph.highlights.empty()) {
      std::string styled_error;
      if (!styled_introduction_inlines(paragraph, paragraph_origin,
                                       break_origin, content, &styled_error))
        return reject(styled_error);
      rest.clear();
    } else if (!paragraph.leading_spans.empty()) {
      std::string joined;
      for (const auto &span : paragraph.leading_spans) {
        if (!joined.empty())
          joined.push_back(' ');
        joined += span.text;
      }
      if (paragraph.text.compare(0, joined.size(), joined) != 0)
        return reject("introduction highlight does not prefix its paragraph");
      append_spans(content, paragraph.leading_spans, paragraph_origin);
      rest = after_prefix(paragraph.text, joined);
      rest_from = paragraph.text.find_first_not_of(' ', joined.size());
      if (!rest.empty())
        rest.insert(rest.begin(), ' ');
      if (!paragraph.line_breaks.empty()) {
        // The highlighted lead is the first row's; a break inside it, or a
        // remainder that does not follow it at a separator, is not a
        // paragraph this lowering can place breaks in.
        if (paragraph.text.size() <= joined.size() ||
            paragraph.text[joined.size()] != ' ' ||
            !append_broken(joined.size()))
          return reject("introduction line break falls inside its highlight");
        rest.clear();
      }
    } else if (!paragraph.line_breaks.empty()) {
      if (!append_broken(0))
        return reject("introduction line break is out of order");
      rest.clear();
    }
    if (!paragraph.links.empty() && paragraph.highlights.empty()) {
      // Each selector range is its own cross reference; a range that
      // reaches into the highlighted prefix or overlaps another is not
      // placed.
      TrapFlowParagraphIR linked;
      linked.text = paragraph.text;
      linked.links = paragraph.links;
      if (rest.empty() || rest_from == std::string::npos)
        return reject("introduction selector has no text to mark");
      if (!content.empty())
        content.push_back({TextInlineIR{" "}, paragraph_origin});
      if (!paragraph_inlines(linked, paragraph_origin, content, rest_from))
        return reject("introduction selector ranges do not fit their "
                      "paragraph");
    } else if (!rest.empty()) {
      content.push_back({TextInlineIR{rest}, paragraph_origin});
    }
    if (content.empty())
      return reject("introduction paragraph has no content");
    if (paragraph.list_continuation) {
      if (!open_list || open_list->items.empty())
        return reject("introduction list continuation has no open item");
      auto &item = open_list->items.back();
      if (item.blocks.empty()) {
        item.blocks.push_back(
            {ParagraphBlockIR{std::move(item.content)}, item.origin});
        item.content.clear();
      }
      item.blocks.push_back(
          {ParagraphBlockIR{std::move(content)}, paragraph_origin});
    } else if (paragraph.list_marker) {
      ListItemIR item;
      item.origin = origin("trap introduction list item");
      add_slice(item.origin, *paragraph.list_marker);
      for (const auto &slice : paragraph.source_slices)
        add_slice(item.origin, slice);
      for (const auto &link : paragraph.links)
        add_slice(item.origin, link.source);
      add_rows(item.origin, paragraph.source_rows);
      item.origin = canonical(std::move(item.origin));
      item.content = std::move(content);
      if (!open_list) {
        open_list = ListBlockIR{};
        open_list_origin = item.origin;
      }
      open_list->items.push_back(std::move(item));
    } else {
      close_list();
      document.blocks.push_back(
          {ParagraphBlockIR{std::move(content)}, paragraph_origin});
    }
    // A region drawn after this paragraph ends any open list first.
    if (next_introduction_region < catalog.introduction_regions.size() &&
        catalog.introduction_regions[next_introduction_region].after_field ==
            paragraph_index + 1)
      close_list();
    if (!emit_introduction_regions(paragraph_index + 1))
      return reject("trap introduction region lines have no line provenance");
  }
  close_list();
  if (next_introduction_region != catalog.introduction_regions.size())
    return reject("trap introduction region names a missing paragraph");

  for (const auto &entry : catalog.entries) {
    auto anchor_origin = origin("trap entry source anchor");
    add_slice(anchor_origin, entry.start_source);
    // Hosted BookServer names the entry anchor after the whole SRMSG operand,
    // not after its first word: SC31-711 4.3.2 `SRMSG 256
    // (snmp_br_dot1dStpPortState)` is served as
    // `<a name="MSG 256 (snmp_br_dot1dStpPortState)">` and 4.4 `SRMSG 1
    // (fddiRPUNoResponse)` as `<a name="MSG 1 (fddiRPUNoResponse)">`
    // (DT 19941010174546).  Where the operand is one word -- 4.1.1's `MSG 0`,
    // 4.1.2's `MSG bridgeHistoryDataComplete` -- the two spellings coincide,
    // which is why the defect only shows on the catalogs with a symbolic tail.
    document.blocks.push_back(
        {AnchorBlockIR{"MSG " + (entry.operand.empty() ? entry.id
                                                       : entry.operand)},
         anchor_origin});
    // Footnotes the entry draws before its headline stand between the
    // entry's anchor and its first row, where hosted N2AH1MST 21.0
    // (DT 19910329000100) serves `FTNFTNUNIQ31` and `FTNFTNUNIQ32` for
    // `ERB415I`.
    if (!lower_flow_blocks(entry.leading.body.flow.blocks, document.blocks,
                           error))
      return std::nullopt;

    const auto uses_compound_flow =
        compound_flow(entry.headline.body.flow) ||
        std::any_of(entry.fields.begin(), entry.fields.end(),
                    [](const auto &field) {
                      return compound_flow(field.line.body.flow);
                    });
    ListItemIR item;
    item.origin = origin("trap entry");
    auto headline_origin = line_origin(entry.headline, "trap entry headline");
    if (entry.headline.spans.empty())
      return reject("trap entry headline has no highlighted spans: " +
                    entry.id);
    InlineSequenceIR headline_content;
    append_spans(headline_content, entry.headline.spans, headline_origin, true);
    if (!append_body_after(headline_content, entry.headline.body,
                           entry.headline.spans_text, headline_origin))
      return reject("trap entry headline highlight is outside its text: " +
                    entry.id);
    item.origin.slices = headline_origin.slices;
    item.origin.rows = headline_origin.rows;
    // The headline and every field are separate paragraphs. Hosted
    // BookServer serves each on its own row with `<p>` between them --
    // SC31-711 4.1.1 `<B>0</B>`, `<p>`, `<B>Description:</B>  coldStart`
    // (DT 19941010174546) -- and the source spells no separator character,
    // so none is synthesized.
    if (!uses_compound_flow) {
      item.blocks.push_back(
          {ParagraphBlockIR{std::move(headline_content)}, headline_origin});
      for (const auto &field : entry.fields) {
        auto field_origin = line_origin(field.line, "trap entry field");
        InlineSequenceIR content;
        append_spans(content, field.line.spans, field_origin);
        if (!append_body_after(content, field.line.body, field.label_text,
                               field_origin))
          return reject("trap field highlight is outside its text: " +
                        entry.id + " " + field.label_text);
        item.origin.slices.insert(item.origin.slices.end(),
                                  field_origin.slices.begin(),
                                  field_origin.slices.end());
        item.origin.rows.insert(item.origin.rows.end(),
                                field_origin.rows.begin(),
                                field_origin.rows.end());
        item.blocks.push_back(
            {ParagraphBlockIR{std::move(content)}, std::move(field_origin)});
      }
    } else {
      if (!lower_flow_blocks(entry.headline.body.flow.blocks, item.blocks,
                             error) ||
          !style_flow_prefix(entry.headline, entry.headline.spans_text, true,
                             item.blocks, error))
        return std::nullopt;
      for (const auto &field : entry.fields) {
        BlockSequenceIR field_blocks;
        if (!lower_flow_blocks(field.line.body.flow.blocks, field_blocks,
                               error))
          return std::nullopt;
        if (field_blocks.empty())
          return reject("trap field flow has no blocks: " + entry.id + " " +
                        field.label_text);
        // The field label's CFONT spans live on TrapLineIR, while recursive
        // flow owns the body. Reapply them to the first source paragraph.
        if (!style_flow_prefix(field.line, field.label_text, false,
                               field_blocks, error))
          return std::nullopt;
        item.blocks.insert(item.blocks.end(),
                           std::make_move_iterator(field_blocks.begin()),
                           std::make_move_iterator(field_blocks.end()));
        const auto field_origin =
            line_origin(field.line, "trap entry compound field");
        item.origin.slices.insert(item.origin.slices.end(),
                                  field_origin.slices.begin(),
                                  field_origin.slices.end());
        item.origin.rows.insert(item.origin.rows.end(),
                                field_origin.rows.begin(),
                                field_origin.rows.end());
      }
    }
    item.origin = canonical(std::move(item.origin));
    ListBlockIR list;
    list.ordered = false;
    list.items.push_back(std::move(item));
    auto list_origin = list.items.front().origin;
    document.blocks.push_back({std::move(list), std::move(list_origin)});

    // An `SRTBL<id>` envelope the entry draws. Hosted BookServer serves the
    // region's first display line carrying `<a name="TBL<id>">` and the rest
    // of the lines exactly as the record draws them; the family claims the
    // envelope and the drawing, and no grid.
    if (!uses_compound_flow)
      for (const auto &region : entry.embedded_regions) {
      if (region.lines.size() != region.line_sources.size())
        return reject("trap embedded region lines have no line provenance: " +
                      entry.id);
      if (!region.identifier.empty()) {
        auto region_anchor = origin("trap embedded table source anchor");
        add_slice(region_anchor, region.start.source);
        document.blocks.push_back(
            {AnchorBlockIR{region_anchor_id(region)}, region_anchor});
      }
      auto region_origin = origin("trap embedded table");
      PreformattedBlockIR drawn =
          drawn_region(region, "trap embedded table line", region_origin);
      add_slice(region_origin, region.start.source);
      add_slice(region_origin, region.end.source);
      region_origin = canonical(std::move(region_origin));
      document.blocks.push_back({std::move(drawn), std::move(region_origin)});
      }
  }

  // Every container names at least the BOO bytes its own content names
  // before the document is verified.
  normalize_document_origin_slices(document);
  std::string document_error;
  if (!verify_document_ir(document, &document_error))
    return reject("invalid trap DocumentIR: " + document_error);
  if (error != nullptr)
    error->clear();
  return document;
}

} // namespace

std::optional<DocumentIR>
lower_trap_catalog_to_document_ir(TopicIdentityIR topic,
                                  const TrapCatalogIR &catalog,
                                  std::string *error) {
  return canonical_document(std::move(topic), catalog, error);
}

bool verify_trap_catalog_document_ir(const TrapCatalogIR &catalog,
                                     const DocumentIR &document,
                                     std::string *error) {
  const auto expected = canonical_document(document.topic, catalog, error);
  if (!expected)
    return false;
  if (format_document_ir(*expected) != format_document_ir(document))
    return fail(error, "trap DocumentIR differs from canonical lowering");
  if (error != nullptr)
    error->clear();
  return true;
}

} // namespace geist::detail
