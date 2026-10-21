// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "catalog.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

namespace geist_apache::service {
namespace {

SourceCoordinates coordinates_from(
    const geist::RenderSourceCoordinates &source) {
  SourceCoordinates result;
  result.start_logical_record = source.start_logical_record;
  result.end_logical_record = source.end_logical_record;
  result.logical_record = source.logical_record;
  result.segment_index = source.segment_index;
  result.token_begin = source.token_begin;
  result.token_end = source.token_end;
  result.byte_begin = source.byte_begin;
  result.byte_end = source.byte_end;
  return result;
}

std::string ascii_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](char value) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  });
  return value;
}

Diagnostic diagnostic_from(const geist::RenderDiagnostic &source) {
  Diagnostic result;
  result.severity = geist::to_string(source.severity);
  result.route = source.route;
  result.family = source.family;
  result.reason = source.reason;
  result.detail = source.detail;
  result.source = coordinates_from(source.source);
  result.degradations.reserve(source.degradations.size());
  for (const auto &degradation : source.degradations) {
    result.degradations.push_back(
        {degradation.block, degradation.reason, degradation.detail,
         coordinates_from(degradation.source)});
  }
  result.degradation_count = source.degradations.size();
  return result;
}

TopicSummary topic_from(const geist::TopicInfo &source,
                        std::size_t source_index, const geist::TocEntry *toc) {
  TopicSummary result;
  result.id = source.id;
  result.title = source.title;
  result.heading_level = source.heading_level;
  result.source_index = source_index;
  result.topic_number = source.topic_number;
  result.start_logical_record = source.start_logical_record;
  result.end_logical_record = source.end_logical_record;
  result.in_toc = toc != nullptr;
  if (toc != nullptr) {
    result.toc_level = toc->level;
    result.toc_style = toc->style;
  }
  return result;
}

TopicSummary topic_from(const geist::TocEntry &source,
                        std::size_t source_index) {
  TopicSummary result;
  result.id = source.id;
  result.title = source.title;
  result.heading_level = source.heading_level;
  result.source_index = source_index;
  result.topic_number = source.topic_number;
  result.start_logical_record = source.start_logical_record;
  result.end_logical_record = source.end_logical_record;
  result.in_toc = true;
  result.toc_level = source.level;
  result.toc_style = source.style;
  return result;
}

ResourceSummary resource_from(const geist::ResourceEntry &source) {
  ResourceSummary result;
  result.id = source.id;
  result.name = source.name;
  result.kind = source.kind;
  result.stored_format = source.stored_format;
  result.description = source.description;
  result.layout = geist::to_string(source.layout);
  result.size = source.size;
  return result;
}

const geist::TopicInfo *find_topic(const geist::BooDocument &document,
                                   const std::string &topic_id,
                                   std::size_t *source_index = nullptr) {
  const auto &topics = document.topics();
  const auto found = std::find_if(
      topics.begin(), topics.end(),
      [&](const geist::TopicInfo &topic) { return topic.id == topic_id; });
  if (found == topics.end())
    return nullptr;
  if (source_index != nullptr) {
    *source_index = static_cast<std::size_t>(found - topics.begin());
  }
  return &*found;
}

const geist::TocEntry *find_exact_toc(const geist::BooDocument &document,
                                      const std::string &topic_id) {
  const auto &toc = document.table_of_contents();
  const auto found =
      std::find_if(toc.begin(), toc.end(), [&](const geist::TocEntry &entry) {
        return entry.id == topic_id;
      });
  return found == toc.end() ? nullptr : &*found;
}

const geist::ResourceEntry *find_resource(const geist::BooDocument &document,
                                          const std::string &resource_id) {
  const auto &resources = document.resources();
  const auto found = std::find_if(
      resources.begin(), resources.end(),
      [&](const geist::ResourceEntry &item) { return item.id == resource_id; });
  return found == resources.end() ? nullptr : &*found;
}

std::shared_ptr<const geist::BooDocument> open(const BookSource &source) {
  if (!source.open) {
    throw std::runtime_error("book source has no open operation: " +
                             source.summary.id);
  }
  auto document = source.open();
  if (!document) {
    throw std::runtime_error("book source returned no document: " +
                             source.summary.id);
  }
  return document;
}

bool is_stored_web_image(const std::string &stored_format) {
  const auto format = ascii_lower(stored_format);
  return format == "image/png" || format == "image/gif" ||
         format == "image/jpeg" || format == "image/jpg" ||
         format == "image/webp" || format == "image/svg+xml";
}

std::string stored_media_type(const std::string &stored_format) {
  const auto format = ascii_lower(stored_format);
  if (format.rfind("image/", 0) == 0 || format.rfind("application/", 0) == 0 ||
      format.rfind("audio/", 0) == 0 || format.rfind("video/", 0) == 0 ||
      format.rfind("text/", 0) == 0) {
    return format == "image/jpg" ? "image/jpeg" : stored_format;
  }
  return "application/octet-stream";
}

} // namespace

Catalog::Catalog(std::vector<BookSource> books) : sources_(std::move(books)) {
  std::stable_sort(sources_.begin(), sources_.end(),
                   [](const BookSource &left, const BookSource &right) {
                     return left.summary.id < right.summary.id;
                   });
  for (std::size_t index = 1; index < sources_.size(); ++index) {
    if (sources_[index - 1].summary.id == sources_[index].summary.id) {
      throw std::invalid_argument("duplicate book source id: " +
                                  sources_[index].summary.id);
    }
  }
  books_.reserve(sources_.size());
  for (const auto &source : sources_)
    books_.push_back(source.summary);
}

const std::vector<BookSummary> &Catalog::books() const noexcept {
  return books_;
}

const BookSource *Catalog::source(const std::string &book_id) const noexcept {
  const auto found =
      std::lower_bound(sources_.begin(), sources_.end(), book_id,
                       [](const BookSource &source, const std::string &id) {
                         return source.summary.id < id;
                       });
  if (found == sources_.end() || found->summary.id != book_id)
    return nullptr;
  return &*found;
}

std::optional<BookDetail> Catalog::book(const std::string &book_id) const {
  const auto *selected = source(book_id);
  if (selected == nullptr)
    return std::nullopt;
  const auto document = open(*selected);
  const auto &properties = document->book_properties();
  BookDetail result;
  static_cast<BookSummary &>(result) = selected->summary;
  result.language = properties.language;
  result.format_version = properties.version;
  result.build_version = properties.build_version;
  result.copyright = properties.copyright;
  result.security = properties.security;
  result.date = properties.date;
  result.authors = properties.authors;
  result.topic_count = document->topics().size();
  result.toc_count = document->table_of_contents().size();
  result.resource_count = document->resources().size();
  return result;
}

std::optional<std::vector<TopicSummary>>
Catalog::topics(const std::string &book_id) const {
  const auto *selected = source(book_id);
  if (selected == nullptr)
    return std::nullopt;
  const auto document = open(*selected);
  std::vector<TopicSummary> result;
  result.reserve(document->topics().size());
  for (std::size_t index = 0; index < document->topics().size(); ++index) {
    const auto &topic = document->topics()[index];
    result.push_back(
        topic_from(topic, index, find_exact_toc(*document, topic.id)));
  }
  return result;
}

std::optional<std::vector<TopicSummary>>
Catalog::toc(const std::string &book_id) const {
  const auto *selected = source(book_id);
  if (selected == nullptr)
    return std::nullopt;
  const auto document = open(*selected);
  std::vector<TopicSummary> result;
  result.reserve(document->table_of_contents().size());
  for (const auto &entry : document->table_of_contents()) {
    std::size_t index = document->topics().size();
    (void)find_topic(*document, entry.id, &index);
    result.push_back(topic_from(entry, index));
  }
  return result;
}

std::optional<TopicDetail> Catalog::topic(const std::string &book_id,
                                          const std::string &topic_id) const {
  const auto *selected = source(book_id);
  if (selected == nullptr)
    return std::nullopt;
  const auto document = open(*selected);
  std::size_t index = 0;
  const auto *info = find_topic(*document, topic_id, &index);
  if (info == nullptr)
    return std::nullopt;
  const auto handle = document->topic_entry(info->id);
  TopicDetail result;
  static_cast<TopicSummary &>(result) =
      topic_from(*info, index, find_exact_toc(*document, info->id));
  result.diagnostic = diagnostic_from(handle.render_diagnostic());
  return result;
}

std::optional<TopicContent> Catalog::topic_content(const std::string &book_id,
                                                   const std::string &topic_id,
                                                   TopicFormat format) const {
  const auto *selected = source(book_id);
  if (selected == nullptr)
    return std::nullopt;
  const auto document = open(*selected);
  std::size_t index = 0;
  const auto *info = find_topic(*document, topic_id, &index);
  if (info == nullptr)
    return std::nullopt;

  // Keep one handle for the native content and the diagnostic so the service
  // cannot accidentally report fidelity from a different rendering pass.
  const auto handle = document->topic_entry(info->id);
  TopicContent result;
  static_cast<TopicSummary &>(result.topic) =
      topic_from(*info, index, find_exact_toc(*document, info->id));
  result.format = format;
  if (format == TopicFormat::markdown) {
    result.content = handle.markdown();
    result.media_type = "text/markdown; charset=utf-8";
  } else {
    result.content = handle.html_fragment();
    result.media_type = "text/html; charset=utf-8";
  }
  result.topic.diagnostic = diagnostic_from(handle.render_diagnostic());
  return result;
}

std::optional<std::vector<ResourceSummary>>
Catalog::resources(const std::string &book_id) const {
  const auto *selected = source(book_id);
  if (selected == nullptr)
    return std::nullopt;
  const auto document = open(*selected);
  std::vector<ResourceSummary> result;
  result.reserve(document->resources().size());
  for (const auto &item : document->resources()) {
    result.push_back(resource_from(item));
  }
  return result;
}

std::optional<ResourceSummary>
Catalog::resource(const std::string &book_id,
                  const std::string &resource_id) const {
  const auto *selected = source(book_id);
  if (selected == nullptr)
    return std::nullopt;
  const auto document = open(*selected);
  const auto *item = find_resource(*document, resource_id);
  if (item == nullptr)
    return std::nullopt;
  return resource_from(*item);
}

std::optional<ResourceContent>
Catalog::resource_content(const std::string &book_id,
                          const std::string &resource_id,
                          ResourceRepresentation representation) const {
  const auto *selected = source(book_id);
  if (selected == nullptr)
    return std::nullopt;
  const auto document = open(*selected);
  const auto *item = find_resource(*document, resource_id);
  if (item == nullptr)
    return std::nullopt;

  ResourceContent result;
  result.resource = resource_from(*item);
  result.representation = representation;
  if (representation == ResourceRepresentation::stored) {
    // Exact bytes are a download representation, not an instruction to a
    // browser to execute a book-supplied active media type on this origin.
    result.media_type = "application/octet-stream";
    result.bytes = document->read_resource_data(item->id);
    return result;
  }

  if (is_stored_web_image(item->stored_format)) {
    result.media_type = stored_media_type(item->stored_format);
    result.bytes = document->read_resource_data(item->id);
    return result;
  }
  const auto format = ascii_lower(item->stored_format);
  if (format == "legacy-gdf" || format == "legacy-mmr") {
    result.media_type = "image/png";
    result.bytes = document->read_resource_png(item->id);
    return result;
  }
  // This is intentionally distinct from a missing resource. Transports can
  // map an unsupported conversion to their protocol's representation error.
  throw UnsupportedRepresentation("resource format has no supported web "
                                  "representation: " + item->stored_format);
}

} // namespace geist_apache::service
