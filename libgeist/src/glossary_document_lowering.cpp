#include "geist/detail/glossary_document_lowering.hpp"

#include "geist/detail/internal.hpp"

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

bool verify_catalog_shape(const GlossaryCatalogIR &catalog,
                          std::string *error) {
  if (catalog.first_logical_record == 0 ||
      catalog.first_logical_record >= catalog.end_logical_record ||
      catalog.heading_level != "GLOSSARY" ||
      catalog.introduction.title.empty() ||
      catalog.introduction.lead.text.empty() || catalog.sections.empty() ||
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
        entry.definition.rows.size() < 2 ||
        entry.definition.rows.front().role !=
            GlossaryDefinitionRowRoleIR::term_echo)
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
      if (row.role != GlossaryDefinitionRowRoleIR::prose)
        continue;
      if (row.continuation_prefix) {
        if (row.continuation_prefix->semantic_text.empty() ||
            row.continuation_prefix->cells.empty() ||
            row.continuation_prefix->source.logical_record == 0)
          return fail(error,
                      "glossary continuation prefix has no source semantics");
        if (!composed.empty())
          composed.push_back(' ');
        composed += row.continuation_prefix->semantic_text;
      }
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
  block.content.push_back(text_inline(paragraph.text, source));
  return {std::move(block), std::move(source)};
}

BlockIR table_block(const GlossaryEmbeddedTableIR &source) {
  auto block_origin = origin("glossary embedded table");
  for (const auto &control : source.controls)
    add_slice(block_origin, control.source);
  for (const auto &row : source.physical_rows)
    merge_origin(block_origin, row_origin(row, "glossary embedded table row"));

  TableBlockIR table;
  table.header_rows = source.header_rows;
  for (const auto &source_row : source.rows) {
    TableRowIR row;
    row.origin = origin("glossary embedded table semantic row");
    for (const auto &source_cell : source_row.cells) {
      TableCellIR cell;
      cell.origin = origin("glossary embedded table semantic cell");
      for (const auto &source_coordinate : source_cell.source_cells)
        add_row(cell.origin, source_coordinate.run,
                source_coordinate.row_index);
      canonicalize(cell.origin);
      cell.content.push_back(text_inline(source_cell.text, cell.origin));
      merge_origin(row.origin, cell.origin);
      row.cells.push_back(std::move(cell));
    }
    table.rows.push_back(std::move(row));
  }
  canonicalize(block_origin);
  return {std::move(table), std::move(block_origin)};
}

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
      if (row.role == GlossaryDefinitionRowRoleIR::prose)
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

  std::string document_error;
  if (!verify_document_ir(document, &document_error)) {
    fail(error, "invalid glossary DocumentIR: " + document_error);
    return std::nullopt;
  }
  if (error != nullptr)
    error->clear();
  return document;
}

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
