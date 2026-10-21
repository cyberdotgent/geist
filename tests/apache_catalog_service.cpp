// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "../apache/service/catalog.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist_apache::service::BookSource;
using geist_apache::service::BookSummary;
using geist_apache::service::Catalog;
using geist_apache::service::ResourceRepresentation;
using geist_apache::service::TopicFormat;

int failures = 0;

void require(bool condition, const std::string &message) {
  if (condition)
    return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

BookSource source(std::string id,
                  const std::shared_ptr<const geist::BooDocument> &document) {
  BookSource result;
  result.summary.id = std::move(id);
  result.summary.title = "shelf title";
  result.summary.document_number = "shelf number";
  result.summary.built = "shelf timestamp";
  result.summary.size = document->metadata().file_size;
  result.summary.readable = true;
  result.open = [document] { return document; };
  return result;
}

void test_stable_books_and_duplicate_rejection(
    const std::shared_ptr<const geist::BooDocument> &document) {
  Catalog catalog({source("z.boo", document), source("A.BOO", document)});
  require(catalog.books().size() == 2, "catalog retains both shelf children");
  require(catalog.books()[0].id == "A.BOO" && catalog.books()[1].id == "z.boo",
          "catalog orders books by exact public id");
  require(!catalog.book("a.boo"), "book ids are exact and case-sensitive");

  bool rejected = false;
  try {
    Catalog duplicate(
        {source("same.boo", document), source("same.boo", document)});
    (void)duplicate;
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "duplicate exact book ids are rejected deterministically");
}

void test_metadata_and_every_indexed_topic(
    const std::shared_ptr<const geist::BooDocument> &document) {
  Catalog catalog({source("packet.boo", document)});
  const auto detail = catalog.book("packet.boo");
  require(detail.has_value(), "known book has detail");
  if (!detail)
    return;
  require(detail->topic_count == document->topics().size(),
          "book count covers every indexed topic");
  require(detail->toc_count == document->table_of_contents().size(),
          "book count reports TOC separately");
  require(detail->resource_count == document->resources().size(),
          "book count reports resources");
  require(detail->language == document->book_properties().language &&
              detail->format_version == document->book_properties().version,
          "book detail projects decoded properties");

  const auto topics = catalog.topics("packet.boo");
  require(topics.has_value(), "known book has topics");
  if (!topics)
    return;
  require(topics->size() == document->topics().size(),
          "topics enumerates the complete source index, not only the TOC");
  for (std::size_t index = 0; index < topics->size(); ++index) {
    const auto &expected = document->topics()[index];
    const auto &actual = (*topics)[index];
    require(actual.source_index == index && actual.id == expected.id &&
                actual.start_logical_record == expected.start_logical_record &&
                actual.end_logical_record == expected.end_logical_record,
            "topic retains its stable source identity at index " +
                std::to_string(index));
    const auto *toc = document->find_toc_entry(expected.id);
    require(actual.in_toc == (toc != nullptr),
            "topic TOC membership matches the document");
    if (toc != nullptr) {
      require(actual.toc_level == toc->level && actual.toc_style == toc->style,
              "topic retains TOC level and style");
    }
  }

  const auto toc = catalog.toc("packet.boo");
  require(toc && toc->size() == document->table_of_contents().size(),
          "TOC endpoint retains every TOC entry");
}

void test_one_handle_drives_content_and_diagnostic(
    const std::shared_ptr<const geist::BooDocument> &document) {
  Catalog catalog({source("packet.boo", document)});
  const auto &info = document->topics().front();
  const auto handle = document->topic_entry(info.id);

  const auto markdown =
      catalog.topic_content("packet.boo", info.id, TopicFormat::markdown);
  const auto html =
      catalog.topic_content("packet.boo", info.id, TopicFormat::html);
  require(markdown && markdown->content == handle.markdown(),
          "catalog Markdown is the public topic handle's output");
  require(html && html->content == handle.html_fragment(),
          "catalog HTML is the same public topic handle's native output");
  require(markdown && markdown->media_type == "text/markdown; charset=utf-8",
          "Markdown has an explicit UTF-8 media type");
  require(html && html->media_type == "text/html; charset=utf-8",
          "HTML has an explicit UTF-8 media type");
  if (markdown) {
    const auto &diagnostic = handle.render_diagnostic();
    require(markdown->topic.diagnostic.severity ==
                    geist::to_string(diagnostic.severity) &&
                markdown->topic.diagnostic.route == diagnostic.route &&
                markdown->topic.diagnostic.family == diagnostic.family &&
                markdown->topic.diagnostic.reason == diagnostic.reason &&
                markdown->topic.diagnostic.detail == diagnostic.detail &&
                markdown->topic.diagnostic.source.start_logical_record ==
                    diagnostic.source.start_logical_record &&
                markdown->topic.diagnostic.source.end_logical_record ==
                    diagnostic.source.end_logical_record &&
                markdown->topic.diagnostic.degradations.size() ==
                    diagnostic.degradations.size() &&
                markdown->topic.diagnostic.degradation_count ==
                    diagnostic.degradations.size(),
            "content exposes the handle's fidelity diagnostic unchanged");
  }
  require(!catalog.topic("packet.boo", "no-such-topic"),
          "unknown topic is absence rather than an exception");
}

void test_resource_representations(
    const std::shared_ptr<const geist::BooDocument> &document) {
  Catalog catalog({source("packet.boo", document)});
  const auto resources = catalog.resources("packet.boo");
  require(resources && resources->size() == document->resources().size(),
          "resource inventory is complete");
  if (!resources || resources->empty())
    return;

  const auto &resource = resources->front();
  const auto stored = catalog.resource_content("packet.boo", resource.id,
                                               ResourceRepresentation::stored);
  require(stored && stored->bytes == document->read_resource_data(resource.id),
          "stored representation is byte-exact container data");
  require(stored && stored->media_type == "application/octet-stream",
          "opaque stored representation reports a valid binary media type");

  const auto legacy = std::find_if(
      resources->begin(), resources->end(), [](const auto &candidate) {
        return candidate.stored_format == "legacy-gdf" ||
               candidate.stored_format == "legacy-mmr";
      });
  if (legacy != resources->end()) {
    const auto web = catalog.resource_content("packet.boo", legacy->id,
                                              ResourceRepresentation::web);
    require(web && web->media_type == "image/png" && web->bytes.size() >= 8 &&
                web->bytes[0] == 0x89 && web->bytes[1] == 'P' &&
                web->bytes[2] == 'N' && web->bytes[3] == 'G',
            "supported legacy image gets an explicit PNG web representation");
  }
  require(!catalog.resource("packet.boo", "no-such-resource"),
          "unknown resource is absence rather than an exception");
}

} // namespace

int main() {
#ifndef GEIST_FIXTURE_DIR
#error "GEIST_FIXTURE_DIR must name the tests/fixtures directory"
#endif
  const auto document =
      std::make_shared<const geist::BooDocument>(geist::BooDocument::open(
          std::filesystem::path(GEIST_FIXTURE_DIR) / "packet.boo"));
  test_stable_books_and_duplicate_rejection(document);
  test_metadata_and_every_indexed_topic(document);
  test_one_handle_drives_content_and_diagnostic(document);
  test_resource_representations(document);
  if (failures != 0) {
    std::cerr << failures << " catalog assertion(s) failed\n";
    return 1;
  }
  std::cout << "Apache catalog service assertions complete\n";
  return 0;
}
