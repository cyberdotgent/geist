// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/trap_catalog_document_lowering.hpp"

#include "geist/detail/core/internal.hpp"

#include <algorithm>
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
  case FontStyleIR::highlight_2:
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
  case FontStyleIR::variable:
  case FontStyleIR::bold_phrase:
  case FontStyleIR::italic_phrase:
  case FontStyleIR::warning:
  case FontStyleIR::warning_text:
  case FontStyleIR::unknown:
    break;
  }
  return std::nullopt;
}

// Highlighted span words as separate inlines: BookServer renders each CFONT
// span as its own run, separated by the gap its columns leave. Spans that
// abut leave no gap and no separator. The entry headline is the term of the
// definition item and is always strong, which also covers the headline CFONT
// code the decoder leaves glued to its control boundary (`2,`).
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
      collapse_ascii_whitespace(topic.title) != catalog.title)
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
            {AnchorBlockIR{"TBL" + region.identifier}, region_anchor});
      }
      PreformattedBlockIR drawn;
      drawn.lines = region.lines;
      auto region_origin = origin("trap introduction table");
      for (std::size_t line = 0; line < region.lines.size(); ++line) {
        auto line_origin = origin("trap introduction table line");
        add_slice(line_origin, region.line_sources[line]);
        drawn.line_origins.push_back(canonical(std::move(line_origin)));
        add_slice(region_origin, region.line_sources[line]);
      }
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

  for (std::size_t paragraph_index = 0;
       paragraph_index < catalog.introduction.size(); ++paragraph_index) {
    const auto &paragraph = catalog.introduction[paragraph_index];
    auto paragraph_origin = origin("trap catalog introduction paragraph");
    for (const auto &slice : paragraph.source_slices)
      add_slice(paragraph_origin, slice);
    add_rows(paragraph_origin, paragraph.source_rows);
    paragraph_origin = canonical(std::move(paragraph_origin));
    InlineSequenceIR content;
    std::string rest = paragraph.text;
    if (!paragraph.leading_spans.empty()) {
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
      if (!rest.empty())
        rest.insert(rest.begin(), ' ');
    }
    if (!rest.empty())
      content.push_back({TextInlineIR{rest}, paragraph_origin});
    if (content.empty())
      return reject("introduction paragraph has no content");
    document.blocks.push_back(
        {ParagraphBlockIR{std::move(content)}, paragraph_origin});
    if (!emit_introduction_regions(paragraph_index + 1))
      return reject("trap introduction region lines have no line provenance");
  }
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

    ListItemIR item;
    item.origin = origin("trap entry");
    auto headline_origin = line_origin(entry.headline, "trap entry headline");
    if (entry.headline.spans.empty())
      return reject("trap entry headline has no highlighted spans: " +
                    entry.id);
    append_spans(item.content, entry.headline.spans, headline_origin, true);
    if (!append_body_after(item.content, entry.headline.body,
                           entry.headline.spans_text, headline_origin))
      return reject("trap entry headline highlight is outside its text: " +
                    entry.id);
    item.origin.slices = headline_origin.slices;
    item.origin.rows = headline_origin.rows;
    for (std::size_t index = 0; index < entry.fields.size(); ++index) {
      const auto &field = entry.fields[index];
      item.content.push_back(
          {TextInlineIR{index == 0 ? " — " : " "},
           synthesized(index == 0 ? "trap entry definition separator"
                                  : "trap field separator")});
      auto field_origin = line_origin(field.line, "trap entry field");
      append_spans(item.content, field.line.spans, field_origin);
      if (!append_body_after(item.content, field.line.body, field.label_text,
                             field_origin))
        return reject("trap field highlight is outside its text: " + entry.id +
                      " " + field.label_text);
      item.origin.slices.insert(item.origin.slices.end(),
                                field_origin.slices.begin(),
                                field_origin.slices.end());
      item.origin.rows.insert(item.origin.rows.end(), field_origin.rows.begin(),
                              field_origin.rows.end());
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
    for (const auto &region : entry.embedded_regions) {
      if (region.lines.size() != region.line_sources.size())
        return reject("trap embedded region lines have no line provenance: " +
                      entry.id);
      if (!region.identifier.empty()) {
        auto region_anchor = origin("trap embedded table source anchor");
        add_slice(region_anchor, region.start.source);
        document.blocks.push_back(
            {AnchorBlockIR{"TBL" + region.identifier}, region_anchor});
      }
      PreformattedBlockIR drawn;
      drawn.lines = region.lines;
      auto region_origin = origin("trap embedded table");
      for (std::size_t line = 0; line < region.lines.size(); ++line) {
        auto line_origin = origin("trap embedded table line");
        add_slice(line_origin, region.line_sources[line]);
        drawn.line_origins.push_back(canonical(std::move(line_origin)));
        add_slice(region_origin, region.line_sources[line]);
      }
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
