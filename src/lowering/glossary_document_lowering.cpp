// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/glossary_document_lowering.hpp"

#include "geist/detail/core/internal.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
  return false;
}

bool same_slice(const DocumentSourceSliceIR &a,
                const DocumentSourceSliceIR &b) {
  return a.logical_record == b.logical_record &&
         a.segment_index == b.segment_index && a.token_begin == b.token_begin &&
         a.token_end == b.token_end && a.byte_begin == b.byte_begin &&
         a.byte_end == b.byte_end;
}

DocumentNodeOriginIR origin(std::string detail) {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::semantic_lowering;
  result.detail = std::move(detail);
  return result;
}

void add_slice(DocumentNodeOriginIR &destination,
               const DocumentSourceSliceIR &slice) {
  if (slice.logical_record != 0)
    destination.slices.push_back(slice);
}

void add_row(DocumentNodeOriginIR &destination, DisplayRunId run,
             std::size_t row) {
  if (run != 0)
    destination.rows.push_back({run, row});
}

void canonicalize(DocumentNodeOriginIR &value) {
  std::sort(value.slices.begin(), value.slices.end(),
            [](const auto &a, const auto &b) {
              return std::tie(a.logical_record, a.segment_index, a.token_begin,
                              a.token_end, a.byte_begin, a.byte_end) <
                     std::tie(b.logical_record, b.segment_index, b.token_begin,
                              b.token_end, b.byte_begin, b.byte_end);
            });
  value.slices.erase(
      std::unique(value.slices.begin(), value.slices.end(), same_slice),
      value.slices.end());
  std::sort(value.rows.begin(), value.rows.end(),
            [](const auto &a, const auto &b) {
              return std::tie(a.display_run, a.row_index) <
                     std::tie(b.display_run, b.row_index);
            });
  value.rows.erase(std::unique(value.rows.begin(), value.rows.end(),
                               [](const auto &a, const auto &b) {
                                 return a.display_run == b.display_run &&
                                        a.row_index == b.row_index;
                               }),
                   value.rows.end());
}

DocumentNodeOriginIR paragraph_origin(const GlossaryParagraphIR &paragraph,
                                      std::string detail) {
  auto result = origin(std::move(detail));
  for (const auto &row : paragraph.source_rows)
    add_row(result, row.first, row.second);
  canonicalize(result);
  return result;
}

DocumentNodeOriginIR row_origin(const GlossaryDefinitionRowIR &row,
                                std::string detail) {
  auto result = origin(std::move(detail));
  add_slice(result, row.source);
  if (row.continuation_prefix)
    add_slice(result, row.continuation_prefix->source);
  add_row(result, row.source_row.display_run, row.source_row.row_index);
  canonicalize(result);
  return result;
}

void merge_origin(DocumentNodeOriginIR &destination,
                  const DocumentNodeOriginIR &source) {
  destination.slices.insert(destination.slices.end(), source.slices.begin(),
                            source.slices.end());
  destination.rows.insert(destination.rows.end(), source.rows.begin(),
                          source.rows.end());
  canonicalize(destination);
}

InlineIR text_inline(std::string text, const DocumentNodeOriginIR &source) {
  return {TextInlineIR{std::move(text)}, source};
}

bool verify_paragraph_emphasis(const GlossaryParagraphIR &paragraph,
                               std::string *error) {
  auto previous_end = std::size_t{0};
  for (const auto &span : paragraph.emphasis) {
    if (span.begin >= span.end || span.end > paragraph.text.size() ||
        span.begin < previous_end)
      return fail(error, "glossary emphasis range is not a conserved span");
    if (paragraph.text[span.begin] == ' ' ||
        paragraph.text[span.end - 1] == ' ')
      return fail(error, "glossary emphasis range is not a display word");
    if (span.source.logical_record == 0 ||
        span.source.token_begin >= span.source.token_end)
      return fail(error, "glossary emphasis has no font control provenance");
    previous_end = span.end;
  }
  return true;
}

std::optional<EmphasisKindIR> emphasis_kind(FontStyleIR style) {
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
  case FontStyleIR::unknown:
    break;
  }
  return std::nullopt;
}

// Lowers paragraph text to inlines. BookServer styles every display word by
// its own CFONT triple; consecutive spans of one control and style separated
// by exactly one space form a single typed emphasis run. Spans whose style
// code has no documented highlight mapping stay plain text.
InlineSequenceIR paragraph_inlines(const GlossaryParagraphIR &paragraph,
                                   const DocumentNodeOriginIR &source) {
  InlineSequenceIR content;
  auto cursor = std::size_t{0};
  const auto &text = paragraph.text;
  for (std::size_t index = 0; index < paragraph.emphasis.size();) {
    auto span = paragraph.emphasis[index];
    ++index;
    while (index < paragraph.emphasis.size()) {
      const auto &next = paragraph.emphasis[index];
      if (next.style != span.style || !(next.source == span.source) ||
          next.begin != span.end + 1 || text[span.end] != ' ')
        break;
      span.end = next.end;
      ++index;
    }
    const auto kind = emphasis_kind(span.style);
    if (!kind)
      continue;
    if (span.begin > cursor)
      content.push_back(
          text_inline(text.substr(cursor, span.begin - cursor), source));
    auto emphasis_origin = source;
    emphasis_origin.detail = "glossary font emphasis";
    add_slice(emphasis_origin, span.source);
    canonicalize(emphasis_origin);
    content.push_back(
        {EmphasisInlineIR{text.substr(span.begin, span.end - span.begin), *kind},
         std::move(emphasis_origin)});
    cursor = span.end;
  }
  if (cursor < text.size() || content.empty())
    content.push_back(text_inline(text.substr(cursor), source));
  return content;
}

bool verify_catalog_shape(const GlossaryCatalogIR &catalog,
                          std::string *error) {
  const auto &introduction = catalog.introduction;
  if (!verify_paragraph_emphasis(introduction.lead, error) ||
      !verify_paragraph_emphasis(introduction.cross_reference_lead, error))
    return false;
  for (const auto &paragraph : introduction.sources)
    if (!verify_paragraph_emphasis(paragraph, error))
      return false;
  for (const auto &paragraph : introduction.cross_references)
    if (!verify_paragraph_emphasis(paragraph, error))
      return false;
  for (const auto &paragraph : introduction.paragraphs)
    if (!verify_paragraph_emphasis(paragraph, error))
      return false;
  // Exactly one of the two introduction shapes carries content, so a
  // consumer cannot be handed both and have to guess which one is asserted.
  const auto articulated =
      introduction.shape == GlossaryIntroductionShapeIR::citation_list;
  const auto has_articulated_body =
      !introduction.lead.text.empty() || !introduction.sources.empty() ||
      !introduction.cross_reference_lead.text.empty() ||
      !introduction.cross_references.empty();
  if (articulated ? !introduction.paragraphs.empty() : has_articulated_body)
    return fail(error, "glossary introduction shape and body disagree");
  if (catalog.first_logical_record == 0 ||
      catalog.first_logical_record >= catalog.end_logical_record ||
      !ascii_equals_case_insensitive(catalog.heading_level, "glossary") ||
      catalog.introduction.title.empty() ||
      (articulated && catalog.introduction.lead.text.empty()) ||
      catalog.entries.empty() ||
      catalog.items.size() != catalog.sections.size() + catalog.entries.size())
    return fail(error, "glossary catalog lowering envelope is incomplete");

  std::set<std::size_t> sections;
  std::set<std::size_t> entries;
  auto previous = std::tuple<std::uint32_t, std::size_t, std::size_t>{};
  auto first = true;
  for (const auto &item : catalog.items) {
    if (item.boundary_source.logical_record == 0 ||
        item.boundary_source.token_begin >= item.boundary_source.token_end)
      return fail(error, "glossary catalog item has no source boundary");
    const auto key = std::make_tuple(item.boundary_source.logical_record,
                                     item.boundary_source.segment_index,
                                     item.boundary_source.token_begin);
    if (!first && key <= previous)
      return fail(error, "glossary catalog items are not source ordered");
    previous = key;
    first = false;
    if (item.kind == GlossaryCatalogItemKindIR::section) {
      if (item.index >= catalog.sections.size() ||
          !sections.insert(item.index).second ||
          !same_slice(item.boundary_source,
                      catalog.sections[item.index].marker_source))
        return fail(error, "glossary section sequence is not canonical");
    } else if (item.index >= catalog.entries.size() ||
               !entries.insert(item.index).second ||
               !same_slice(item.boundary_source,
                           catalog.entries[item.index].term_source)) {
      return fail(error, "glossary entry sequence is not canonical");
    }
  }
  if (sections.size() != catalog.sections.size() ||
      entries.size() != catalog.entries.size())
    return fail(error, "glossary source sequence does not conserve every item");

  for (const auto &entry : catalog.entries) {
    if (entry.term.empty() || entry.definition.prose.empty() ||
        entry.definition.rows.empty() ||
        (entry.definition.rows.front().role !=
             GlossaryDefinitionRowRoleIR::term_echo &&
         entry.definition.rows.front().role !=
             GlossaryDefinitionRowRoleIR::term_echo_prefix) ||
        (entry.definition.rows.front().role ==
             GlossaryDefinitionRowRoleIR::term_echo &&
         entry.definition.rows.size() < 2))
      return fail(error, "glossary definition semantics are incomplete");
    auto table_rows = std::size_t{0};
    std::string composed;
    for (const auto &row : entry.definition.rows) {
      if (row.role == GlossaryDefinitionRowRoleIR::embedded_table)
        ++table_rows;
      if ((row.marker_disposition ==
               GlossaryMarkerDispositionIR::lexical_carry ||
           row.marker_disposition ==
               GlossaryMarkerDispositionIR::prose_punctuation ||
           row.marker_disposition ==
               GlossaryMarkerDispositionIR::term_delimiter) &&
          !row.marker)
        return fail(error, "semantic glossary marker has no source slot");
      const auto echo_prefix =
          row.role == GlossaryDefinitionRowRoleIR::term_echo_prefix;
      if (row.role != GlossaryDefinitionRowRoleIR::prose && !echo_prefix)
        continue;
      if (row.continuation_prefix && !echo_prefix) {
        if (row.continuation_prefix->semantic_text.empty() ||
            row.continuation_prefix->cells.empty() ||
            row.continuation_prefix->source.logical_record == 0)
          return fail(error,
                      "glossary continuation prefix has no source semantics");
        if (!composed.empty())
          composed.push_back(' ');
        composed += row.continuation_prefix->semantic_text;
      }
      if (row.terminal_delimiter &&
          row.terminal_delimiter->disposition !=
              SourceDisposition::layout_padding)
        return fail(error,
                    "glossary terminal delimiter is not structural");
      auto row_text = row.semantic_text;
      if (row_text.empty())
        return fail(error, "glossary prose row has no visible content");
      if (row.marker_disposition ==
          GlossaryMarkerDispositionIR::prose_punctuation) {
        const auto &punctuation = row.marker->decoded_text;
        if (!composed.empty() && composed.back() != punctuation.front())
          composed += punctuation;
      } else if (row.marker_disposition ==
                 GlossaryMarkerDispositionIR::lexical_carry) {
        if (!composed.empty())
          composed.push_back(' ');
        composed += row.marker->decoded_text;
      }
      if (!composed.empty())
        composed.push_back(' ');
      composed += std::move(row_text);
    }
    if (composed != entry.definition.prose)
      return fail(error, "glossary definition differs from source rows");
    if (entry.definition.embedded_table) {
      if (table_rows != entry.definition.embedded_table->physical_rows.size())
        return fail(
            error,
            "embedded table physical rows are not replaced exactly once");
    } else if (table_rows != 0) {
      return fail(error, "glossary table row has no semantic table");
    }
  }
  return true;
}

BlockIR paragraph_block(const GlossaryParagraphIR &paragraph,
                        std::string detail) {
  auto source = paragraph_origin(paragraph, std::move(detail));
  ParagraphBlockIR block;
  block.content = paragraph_inlines(paragraph, source);
  auto block_origin = source;
  for (const auto &in : block.content)
    merge_origin(block_origin, in.origin);
  return {std::move(block), std::move(block_origin)};
}

// The glossary's one embedded fixed-layout object, reproduced verbatim from
// its physical rows.  Hosted BookServer serves SC31-711's `TBLUNIQ7` inside
// the topic's `<pre>` as `   <B>DLCI</B> <B>Values</B>    <B>Function</B>` /
// `   0              in-channel signaling` / ... -- fixed columns of plain
// text, no `<table>` element on the page (DT 19941010174546).  The recovered
// two-column grid stays in `GlossaryEmbeddedTableIR::rows` for consumers and
// provenance; it is no longer what the Markdown asserts.
BlockIR table_block(const GlossaryEmbeddedTableIR &source) {
  auto block_origin = origin("glossary embedded fixed-layout object: verbatim "
                             "rows");
  for (const auto &control : source.controls)
    add_slice(block_origin, control.source);
  for (const auto &row : source.physical_rows)
    merge_origin(block_origin, row_origin(row, "glossary embedded table row"));

  // The physical rows of this object do not follow its display lines -- the
  // compact marker slots split one lexical value across two of them (the
  // recorded "1-" / "15" boundary) -- so the verbatim text is rebuilt from the
  // proven column grid instead: each row is its cells at a single column stop,
  // which is the shape hosted draws.
  std::size_t stop = 0;
  for (const auto &row : source.rows)
    if (!row.cells.empty())
      stop = std::max(stop, row.cells.front().text.size());
  stop += 2;
  PreformattedBlockIR body;
  for (const auto &row : source.rows) {
    std::string text;
    for (std::size_t index = 0; index < row.cells.size(); ++index) {
      if (index != 0 && text.size() < stop * index)
        text.append(stop * index - text.size(), ' ');
      text += row.cells[index].text;
    }
    while (!text.empty() && text.back() == ' ')
      text.pop_back();
    body.lines.push_back(std::move(text));
  }
  canonicalize(block_origin);
  return {std::move(body), std::move(block_origin)};
}

// Defined below, after the entry point that shows how the two introduction
// shapes share them.
void lower_glossary_catalog_items(DocumentIR &document,
                                  const GlossaryCatalogIR &catalog);
std::optional<DocumentIR> finish_glossary_document(DocumentIR document,
                                                   std::string *error);

} // namespace

std::optional<DocumentIR>
lower_glossary_catalog_to_document_ir(TopicIdentityIR topic,
                                      const GlossaryCatalogIR &catalog,
                                      std::string *error) {
  if (!verify_catalog_shape(catalog, error))
    return std::nullopt;
  if ((topic.start_logical_record != 0 &&
       topic.start_logical_record != catalog.first_logical_record) ||
      (topic.end_logical_record != 0 &&
       topic.end_logical_record != catalog.end_logical_record)) {
    fail(error, "topic and glossary logical-record envelopes differ");
    return std::nullopt;
  }
  topic.start_logical_record = catalog.first_logical_record;
  topic.end_logical_record = catalog.end_logical_record;
  topic.heading_level = catalog.heading_level;

  DocumentIR document;
  document.topic = std::move(topic);

  auto title_origin = paragraph_origin(catalog.introduction.lead,
                                       "glossary title and introduction row");
  HeadingBlockIR heading{
      1, {text_inline(catalog.introduction.title, title_origin)}};
  document.blocks.push_back({std::move(heading), title_origin});

  if (catalog.introduction.shape == GlossaryIntroductionShapeIR::paragraphs) {
    for (const auto &paragraph : catalog.introduction.paragraphs)
      document.blocks.push_back(
          paragraph_block(paragraph, "glossary introduction paragraph"));
    lower_glossary_catalog_items(document, catalog);
    return finish_glossary_document(std::move(document), error);
  }

  document.blocks.push_back(
      paragraph_block(catalog.introduction.lead, "glossary introduction lead"));

  ListBlockIR sources;
  for (const auto &source : catalog.introduction.sources) {
    auto source_origin = paragraph_origin(source, "glossary source citation");
    sources.items.push_back(
        {{text_inline(source.text, source_origin)}, source_origin});
  }
  auto sources_origin = origin("glossary source citations");
  for (const auto &item : sources.items)
    merge_origin(sources_origin, item.origin);
  document.blocks.push_back({std::move(sources), std::move(sources_origin)});
  document.blocks.push_back(
      paragraph_block(catalog.introduction.cross_reference_lead,
                      "glossary cross-reference introduction"));
  for (const auto &reference : catalog.introduction.cross_references)
    document.blocks.push_back(
        paragraph_block(reference, "glossary cross-reference definition"));

  lower_glossary_catalog_items(document, catalog);
  return finish_glossary_document(std::move(document), error);
}

namespace {

// The catalog body: one `## <letter>` heading per section marker and, per
// term, its anchor and its definition, emitted in source order.
void lower_glossary_catalog_items(DocumentIR &document,
                                  const GlossaryCatalogIR &catalog) {
  for (const auto &item : catalog.items) {
    if (item.kind == GlossaryCatalogItemKindIR::section) {
      const auto &section = catalog.sections[item.index];
      auto section_origin = origin("glossary source-ordered section");
      add_slice(section_origin, section.marker_source);
      for (const auto &row : section.label_rows)
        merge_origin(section_origin, row_origin(row, "glossary section row"));
      HeadingBlockIR section_heading{
          2, {text_inline(section.label, section_origin)}};
      document.blocks.push_back(
          {std::move(section_heading), std::move(section_origin)});
      continue;
    }

    const auto &entry = catalog.entries[item.index];
    auto term_origin = origin("glossary term boundary");
    add_slice(term_origin, entry.term_source);
    canonicalize(term_origin);
    document.blocks.push_back(
        {AnchorBlockIR{"GLS " + entry.term}, term_origin});

    auto definition_origin = origin("glossary definition prose");
    for (const auto &row : entry.definition.rows)
      if (row.role == GlossaryDefinitionRowRoleIR::prose ||
          row.role == GlossaryDefinitionRowRoleIR::term_echo_prefix)
        merge_origin(definition_origin,
                     row_origin(row, "glossary definition row"));
    auto entry_origin = origin("glossary term and definition");
    merge_origin(entry_origin, term_origin);
    merge_origin(entry_origin, definition_origin);
    DefinitionEntryIR definition;
    definition.term.push_back(text_inline(entry.term, term_origin));
    definition.definition.push_back(
        text_inline(entry.definition.prose, definition_origin));
    definition.origin = entry_origin;
    document.blocks.push_back(
        {DefinitionListBlockIR{{std::move(definition)}}, entry_origin});
    if (entry.definition.embedded_table)
      document.blocks.push_back(table_block(*entry.definition.embedded_table));
  }
}

std::optional<DocumentIR> finish_glossary_document(DocumentIR document,
                                                   std::string *error) {
  // Every container names at least the BOO bytes its own content names
  // before the document is verified.
  normalize_document_origin_slices(document);
  std::string document_error;
  if (!verify_document_ir(document, &document_error)) {
    fail(error, "invalid glossary DocumentIR: " + document_error);
    return std::nullopt;
  }
  if (error != nullptr)
    error->clear();
  return document;
}

} // namespace

bool verify_glossary_catalog_document_ir(const GlossaryCatalogIR &catalog,
                                         const DocumentIR &document,
                                         std::string *error) {
  auto expected =
      lower_glossary_catalog_to_document_ir(document.topic, catalog, error);
  if (!expected)
    return false;
  if (format_document_ir(*expected) != format_document_ir(document))
    return fail(error, "glossary DocumentIR differs from canonical lowering");
  if (error != nullptr)
    error->clear();
  return true;
}

} // namespace geist::detail
