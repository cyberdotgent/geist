#include "geist/detail/trap_catalog_document_lowering.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <tuple>
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
  case FontStyleIR::unknown:
    break;
  }
  return std::nullopt;
}

// Highlighted span words as separate inlines separated by single spaces:
// BookServer renders each CFONT span as its own bold run. The entry
// headline is the term of the definition item and is always strong, which
// also covers the headline CFONT code the decoder leaves glued to its
// control boundary (`2,`).
void append_spans(InlineSequenceIR &content,
                  const std::vector<TrapStyledSpanIR> &spans,
                  const DocumentNodeOriginIR &span_origin,
                  bool definition_term = false) {
  for (std::size_t index = 0; index < spans.size(); ++index) {
    if (index != 0)
      content.push_back({TextInlineIR{" "}, synthesized("span separator")});
    auto kind = emphasis_for(spans[index].span.style);
    if (definition_term && !kind)
      kind = EmphasisKindIR::strong;
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
  if ((!topic.id.empty() && topic.id != catalog.raw_topic_id) ||
      (topic.start_logical_record != 0 &&
       topic.start_logical_record != catalog.first_logical_record) ||
      (topic.end_logical_record != 0 &&
       topic.end_logical_record != catalog.end_logical_record))
    return reject("topic and trap catalog envelopes differ");
  if (!topic.title.empty() &&
      collapse_ascii_whitespace(topic.title) != catalog.title)
    return reject("topic title differs from the catalog title row: [" +
                  topic.title + "] vs [" + catalog.title + "]");
  topic.id = catalog.raw_topic_id;
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

  for (const auto &anchor : catalog.anchors) {
    auto anchor_origin = origin("trap catalog source anchor");
    add_slice(anchor_origin, anchor.source);
    document.blocks.push_back({AnchorBlockIR{anchor.id}, anchor_origin});
  }

  for (const auto &paragraph : catalog.introduction) {
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
  }

  for (const auto &entry : catalog.entries) {
    auto anchor_origin = origin("trap entry source anchor");
    add_slice(anchor_origin, entry.start_source);
    document.blocks.push_back({AnchorBlockIR{"MSG " + entry.id}, anchor_origin});

    ListItemIR item;
    item.origin = origin("trap entry");
    auto headline_origin = line_origin(entry.headline, "trap entry headline");
    if (entry.headline.spans.empty())
      return reject("trap entry headline has no highlighted spans: " +
                    entry.id);
    append_spans(item.content, entry.headline.spans, headline_origin, true);
    const auto headline_rest =
        after_prefix(entry.headline.body.text, entry.headline.spans_text);
    if (!headline_rest.empty())
      item.content.push_back(
          {TextInlineIR{" " + headline_rest}, headline_origin});
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
      const auto body = after_prefix(field.line.body.text, field.label_text);
      if (!body.empty())
        item.content.push_back({TextInlineIR{" " + body}, field_origin});
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
  }

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
