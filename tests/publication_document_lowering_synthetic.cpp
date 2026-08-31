// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/publication_document_lowering.hpp"

#include <iostream>
#include <string>

namespace {

using namespace geist::detail;

bool require(bool condition, const std::string &message) {
  if (!condition)
    std::cerr << "publication_document_lowering_synthetic: " << message << '\n';
  return condition;
}

PublicationCatalogIR catalog() {
  PublicationCatalogIR result;
  result.heading_level = "h2";
  result.title = "Related publications";
  result.title_source_rows = {{10, 0}};
  result.introduction = "The following publications are useful.";
  // A wide-field title row contributes to both semantic fields.
  result.introduction_source_rows = {{10, 0}, {10, 1}};
  result.entries = {
      {"Book One, GC00-0001", {{"Book One, GC00-0001", {{11, 0}}}}, {{11, 0}}},
      {"Long title Second independent citation",
       {{"Long title", {{12, 0}, {12, 1}}},
        {"Second independent citation", {{12, 2}, {12, 2}}}},
       {{12, 0}, {12, 1}, {12, 2}}}};
  return result;
}

} // namespace

int main() {
  TopicIdentityIR topic;
  topic.id = "BACK.1";
  topic.title = "Related publications";
  topic.topic_number = 17;
  topic.start_logical_record = 100;
  topic.end_logical_record = 102;

  std::string error;
  const auto lowered =
      lower_publication_catalog_to_document_ir(topic, catalog(), &error);
  if (!require(lowered.has_value(), error) ||
      !require(lowered->topic.heading_level == "h2",
               "catalog heading level was not carried to topic identity") ||
      !require(lowered->blocks.size() == 5,
               "heading, introduction, and all source paragraphs were not "
               "lowered") ||
      !require(
          verify_publication_catalog_document_ir(catalog(), *lowered, &error),
          error))
    return 1;

  const auto *heading =
      std::get_if<HeadingBlockIR>(&lowered->blocks.front().node);
  const auto *final_paragraph =
      std::get_if<ParagraphBlockIR>(&lowered->blocks.back().node);
  if (!require(heading != nullptr && heading->level == 2,
               "publication title is not a level-two heading") ||
      !require(lowered->blocks.front().origin.rows.size() == 1 &&
                   lowered->blocks.front().origin.rows.front().display_run ==
                       10 &&
                   lowered->blocks.front().origin.rows.front().row_index == 0,
               "publication title provenance changed") ||
      !require(lowered->blocks[1].origin.rows.size() == 2 &&
                   lowered->blocks[1].origin.rows.front().display_run == 10 &&
                   lowered->blocks[1].origin.rows.front().row_index == 0,
               "shared title/introduction source row was not conserved") ||
      !require(final_paragraph != nullptr &&
                   std::get<TextInlineIR>(final_paragraph->content.front().node)
                           .text == "Second independent citation",
               "publication paragraph content changed") ||
      !require(final_paragraph->content.front().origin.rows.size() == 1,
               "duplicate source-row evidence was not canonicalized") ||
      !require(
          final_paragraph->content.front().origin.rows.front().display_run ==
                  12 &&
              final_paragraph->content.front().origin.rows.front().row_index ==
                  2,
          "publication source-row evidence changed"))
    return 1;

  auto changed = *lowered;
  auto &changed_text = std::get<TextInlineIR>(
      std::get<ParagraphBlockIR>(changed.blocks.back().node)
          .content.front()
          .node);
  changed_text.text = "renderer-shaped replacement";
  error.clear();
  if (!require(
          !verify_publication_catalog_document_ir(catalog(), changed, &error) &&
              error == "publication DocumentIR differs from canonical lowering",
          "verifier admitted changed publication content"))
    return 1;

  auto lost_source = *lowered;
  std::get<ParagraphBlockIR>(lost_source.blocks.back().node)
      .content.front()
      .origin.rows.clear();
  error.clear();
  if (!require(!verify_publication_catalog_document_ir(catalog(), lost_source,
                                                       &error) &&
                   error ==
                       "publication DocumentIR differs from canonical lowering",
               "verifier admitted lost publication provenance"))
    return 1;

  auto inconsistent = catalog();
  inconsistent.entries.back().text += " extra";
  error.clear();
  if (!require(!lower_publication_catalog_to_document_ir(topic, inconsistent,
                                                         &error) &&
                   error ==
                       "publication entry text differs from its paragraphs",
               "lowerer admitted an inconsistent semantic catalog"))
    return 1;

  auto mismatched_topic = topic;
  mismatched_topic.heading_level = "h3";
  error.clear();
  if (!require(!lower_publication_catalog_to_document_ir(mismatched_topic,
                                                         catalog(), &error) &&
                   error == "topic and publication heading levels differ",
               "lowerer admitted conflicting topic geometry"))
    return 1;

  // A title-only envelope has no introduction and no introduction provenance;
  // it lowers to the heading followed directly by the entry paragraphs.
  auto title_only = catalog();
  title_only.introduction.clear();
  title_only.introduction_source_rows.clear();
  error.clear();
  const auto lowered_title_only =
      lower_publication_catalog_to_document_ir(topic, title_only, &error);
  if (!require(lowered_title_only.has_value(), error) ||
      !require(lowered_title_only->blocks.size() == 4,
               "title-only catalog did not lower to heading plus entries") ||
      !require(std::get_if<ParagraphBlockIR>(
                   &lowered_title_only->blocks[1].node) != nullptr &&
                   std::get<TextInlineIR>(
                       std::get<ParagraphBlockIR>(
                           lowered_title_only->blocks[1].node)
                           .content.front()
                           .node)
                           .text == "Book One, GC00-0001",
               "title-only catalog first entry is not the second block") ||
      !require(verify_publication_catalog_document_ir(
                   title_only, *lowered_title_only, &error),
               error))
    return 1;

  auto orphan_provenance = catalog();
  orphan_provenance.introduction.clear();
  error.clear();
  if (!require(!lower_publication_catalog_to_document_ir(
                   topic, orphan_provenance, &error) &&
                   error ==
                       "publication introduction provenance without text",
               "lowerer admitted introduction provenance without text"))
    return 1;

  auto lost_provenance = catalog();
  lost_provenance.introduction_source_rows.clear();
  error.clear();
  if (!require(!lower_publication_catalog_to_document_ir(
                   topic, lost_provenance, &error) &&
                   error == "publication introduction provenance is empty",
               "lowerer admitted an introduction without provenance"))
    return 1;

  std::cout << "publication document lowering synthetic checks passed\n";
  return 0;
}
