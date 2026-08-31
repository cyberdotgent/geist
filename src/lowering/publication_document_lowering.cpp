// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/publication_document_lowering.hpp"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace geist::detail {
namespace {

using SourceRow = std::pair<DisplayRunId, std::size_t>;

bool fail(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
  return false;
}

std::optional<std::uint32_t> heading_level(const std::string &value) {
  if (value.size() != 2 ||
      std::tolower(static_cast<unsigned char>(value.front())) != 'h' ||
      value.back() < '1' || value.back() > '6')
    return std::nullopt;
  return static_cast<std::uint32_t>(value.back() - '0');
}

std::vector<SourceRow> canonical_rows(std::vector<SourceRow> rows) {
  std::sort(rows.begin(), rows.end());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
  return rows;
}

bool verify_catalog(const PublicationCatalogIR &catalog, std::string *error) {
  if (!heading_level(catalog.heading_level))
    return fail(error, "publication heading level is invalid");
  if (catalog.title.empty())
    return fail(error, "publication title is empty");
  if (catalog.title_source_rows.empty())
    return fail(error, "publication title provenance is empty");
  // A catalog may have no introduction (title plus entries); provenance then
  // must be absent as well.
  if (catalog.introduction.empty() != catalog.introduction_source_rows.empty())
    return fail(error, catalog.introduction.empty()
                           ? "publication introduction provenance without text"
                           : "publication introduction provenance is empty");
  if (catalog.entries.empty())
    return fail(error, "publication catalog has no entries");

  const auto invalid_source = [](const auto &row) { return row.first == 0; };
  if (std::any_of(catalog.title_source_rows.begin(),
                  catalog.title_source_rows.end(), invalid_source) ||
      std::any_of(catalog.introduction_source_rows.begin(),
                  catalog.introduction_source_rows.end(), invalid_source))
    return fail(error, "publication provenance has no display run");

  for (const auto &entry : catalog.entries) {
    if (entry.text.empty() || entry.paragraphs.empty() ||
        entry.source_rows.empty())
      return fail(error, "publication entry is incomplete");

    std::string composed;
    std::vector<SourceRow> paragraph_rows;
    for (const auto &paragraph : entry.paragraphs) {
      if (paragraph.text.empty() || paragraph.source_rows.empty())
        return fail(error, "publication paragraph is incomplete");
      if (!composed.empty())
        composed.push_back(' ');
      composed += paragraph.text;
      paragraph_rows.insert(paragraph_rows.end(), paragraph.source_rows.begin(),
                            paragraph.source_rows.end());
    }
    if (composed != entry.text)
      return fail(error, "publication entry text differs from its paragraphs");
    if (canonical_rows(std::move(paragraph_rows)) !=
        canonical_rows(entry.source_rows))
      return fail(error, "publication entry and paragraph provenance differ");
    if (std::any_of(entry.source_rows.begin(), entry.source_rows.end(),
                    [](const auto &row) { return row.first == 0; }))
      return fail(error, "publication provenance has no display run");
  }
  return true;
}

DocumentNodeOriginIR semantic_origin(std::string detail) {
  DocumentNodeOriginIR origin;
  origin.derivation = DocumentDerivationIR::semantic_lowering;
  origin.detail = std::move(detail);
  return origin;
}

DocumentNodeOriginIR paragraph_origin(const PublicationParagraphIR &paragraph) {
  auto origin = semantic_origin("publication catalog paragraph");
  for (const auto &row : canonical_rows(paragraph.source_rows))
    origin.rows.push_back(DocumentSourceRowIR{row.first, row.second});
  return origin;
}

DocumentNodeOriginIR catalog_text_origin(const std::vector<SourceRow> &rows,
                                         std::string detail) {
  auto origin = semantic_origin(std::move(detail));
  for (const auto &row : canonical_rows(rows))
    origin.rows.push_back(DocumentSourceRowIR{row.first, row.second});
  return origin;
}

InlineIR text_inline(std::string text, DocumentNodeOriginIR origin) {
  InlineIR result;
  result.node = TextInlineIR{std::move(text)};
  result.origin = std::move(origin);
  return result;
}

bool same_origin(const DocumentNodeOriginIR &left,
                 const DocumentNodeOriginIR &right) {
  if (left.derivation != right.derivation || left.detail != right.detail ||
      left.slices.size() != right.slices.size() ||
      left.rows.size() != right.rows.size())
    return false;
  for (std::size_t index = 0; index < left.slices.size(); ++index) {
    const auto &a = left.slices[index];
    const auto &b = right.slices[index];
    if (a.logical_record != b.logical_record ||
        a.segment_index != b.segment_index || a.token_begin != b.token_begin ||
        a.token_end != b.token_end || a.byte_begin != b.byte_begin ||
        a.byte_end != b.byte_end)
      return false;
  }
  for (std::size_t index = 0; index < left.rows.size(); ++index)
    if (left.rows[index].display_run != right.rows[index].display_run ||
        left.rows[index].row_index != right.rows[index].row_index)
      return false;
  return true;
}

bool same_text_inline(const InlineIR &actual, const InlineIR &expected) {
  const auto *actual_text = std::get_if<TextInlineIR>(&actual.node);
  const auto *expected_text = std::get_if<TextInlineIR>(&expected.node);
  return actual_text != nullptr && expected_text != nullptr &&
         actual_text->text == expected_text->text &&
         same_origin(actual.origin, expected.origin);
}

bool same_document(const DocumentIR &actual, const DocumentIR &expected) {
  const auto &a = actual.topic;
  const auto &e = expected.topic;
  if (a.id != e.id || a.title != e.title ||
      a.heading_level != e.heading_level || a.topic_number != e.topic_number ||
      a.start_logical_record != e.start_logical_record ||
      a.end_logical_record != e.end_logical_record ||
      actual.blocks.size() != expected.blocks.size())
    return false;
  for (std::size_t index = 0; index < actual.blocks.size(); ++index) {
    const auto &actual_block = actual.blocks[index];
    const auto &expected_block = expected.blocks[index];
    if (!same_origin(actual_block.origin, expected_block.origin))
      return false;
    if (const auto *expected_heading =
            std::get_if<HeadingBlockIR>(&expected_block.node)) {
      const auto *actual_heading =
          std::get_if<HeadingBlockIR>(&actual_block.node);
      if (actual_heading == nullptr ||
          actual_heading->level != expected_heading->level ||
          actual_heading->content.size() != 1 ||
          !same_text_inline(actual_heading->content.front(),
                            expected_heading->content.front()))
        return false;
    } else {
      const auto *expected_paragraph =
          std::get_if<ParagraphBlockIR>(&expected_block.node);
      const auto *actual_paragraph =
          std::get_if<ParagraphBlockIR>(&actual_block.node);
      if (expected_paragraph == nullptr || actual_paragraph == nullptr ||
          actual_paragraph->content.size() != 1 ||
          !same_text_inline(actual_paragraph->content.front(),
                            expected_paragraph->content.front()))
        return false;
    }
  }
  return true;
}

} // namespace

std::optional<DocumentIR>
lower_publication_catalog_to_document_ir(TopicIdentityIR topic,
                                         const PublicationCatalogIR &catalog,
                                         std::string *error) {
  if (!verify_catalog(catalog, error))
    return std::nullopt;
  if (topic.heading_level.empty()) {
    topic.heading_level = catalog.heading_level;
  } else if (topic.heading_level != catalog.heading_level) {
    fail(error, "topic and publication heading levels differ");
    return std::nullopt;
  }

  DocumentIR document;
  document.topic = std::move(topic);

  auto heading_origin = catalog_text_origin(catalog.title_source_rows,
                                            "publication catalog heading");
  HeadingBlockIR heading;
  heading.level = *heading_level(catalog.heading_level);
  heading.content.push_back(text_inline(catalog.title, heading_origin));
  document.blocks.push_back(
      BlockIR{std::move(heading), std::move(heading_origin)});

  if (!catalog.introduction.empty()) {
    auto introduction_origin = catalog_text_origin(
        catalog.introduction_source_rows, "publication catalog introduction");
    ParagraphBlockIR introduction;
    introduction.content.push_back(
        text_inline(catalog.introduction, introduction_origin));
    document.blocks.push_back(
        BlockIR{std::move(introduction), std::move(introduction_origin)});
  }

  for (const auto &entry : catalog.entries) {
    for (const auto &source_paragraph : entry.paragraphs) {
      auto origin = paragraph_origin(source_paragraph);
      ParagraphBlockIR paragraph;
      paragraph.content.push_back(text_inline(source_paragraph.text, origin));
      document.blocks.push_back(
          BlockIR{std::move(paragraph), std::move(origin)});
    }
  }

  // Every container names at least the BOO bytes its own content names
  // before the document is verified.
  normalize_document_origin_slices(document);
  std::string document_error;
  if (!verify_document_ir(document, &document_error)) {
    fail(error, "invalid publication DocumentIR: " + document_error);
    return std::nullopt;
  }
  if (error != nullptr)
    error->clear();
  return document;
}

bool verify_publication_catalog_document_ir(const PublicationCatalogIR &catalog,
                                            const DocumentIR &document,
                                            std::string *error) {
  std::string document_error;
  if (!verify_document_ir(document, &document_error))
    return fail(error, "invalid publication DocumentIR: " + document_error);
  auto expected =
      lower_publication_catalog_to_document_ir(document.topic, catalog, error);
  if (!expected)
    return false;
  if (!same_document(document, *expected))
    return fail(error,
                "publication DocumentIR differs from canonical lowering");
  if (error != nullptr)
    error->clear();
  return true;
}

} // namespace geist::detail
