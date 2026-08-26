#include "geist/detail/book_topic_catalog_ir.hpp"

#include "geist/detail/internal.hpp"

#include <sstream>
#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
  return false;
}

bool same_header(const std::optional<BookTopicHeaderEvidenceIR> &left,
                 const std::optional<BookTopicHeaderEvidenceIR> &right) {
  if (left.has_value() != right.has_value())
    return false;
  return !left ||
         (left->title == right->title &&
          left->heading_level == right->heading_level &&
          left->topic_number == right->topic_number &&
          left->start_logical_record == right->start_logical_record &&
          left->end_logical_record == right->end_logical_record &&
          left->topic_info_index == right->topic_info_index);
}

bool same_toc(const BookTopicTocEvidenceIR &left,
              const BookTopicTocEvidenceIR &right) {
  return left.raw_id == right.raw_id && left.title == right.title &&
         left.level == right.level && left.style == right.style &&
         left.heading_level == right.heading_level &&
         left.topic_number == right.topic_number &&
         left.start_logical_record == right.start_logical_record &&
         left.end_logical_record == right.end_logical_record &&
         left.toc_index == right.toc_index;
}

bool same_catalog(const BookTopicCatalogIR &left,
                  const BookTopicCatalogIR &right) {
  if (left.topics.size() != right.topics.size())
    return false;
  for (std::size_t index = 0; index < left.topics.size(); ++index) {
    const auto &a = left.topics[index];
    const auto &b = right.topics[index];
    if (a.raw_topic_id != b.raw_topic_id ||
        !same_header(a.topic_header, b.topic_header) ||
        a.toc_entries.size() != b.toc_entries.size())
      return false;
    for (std::size_t toc_index = 0; toc_index < a.toc_entries.size();
         ++toc_index)
      if (!same_toc(a.toc_entries[toc_index], b.toc_entries[toc_index]))
        return false;
  }
  return true;
}

} // namespace

const BookTopicCatalogEntryIR *
find_book_topic_catalog_entry(const BookTopicCatalogIR &catalog,
                              const std::string &raw_topic_id) {
  for (const auto &entry : catalog.topics)
    if (ascii_equals_case_insensitive(entry.raw_topic_id, raw_topic_id))
      return &entry;
  return nullptr;
}

std::optional<BookTopicCatalogIR>
build_book_topic_catalog_ir(const std::vector<TopicInfo> &topics,
                            const std::vector<TocEntry> &toc,
                            std::string *error) {
  const auto reject =
      [&](std::string message) -> std::optional<BookTopicCatalogIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  BookTopicCatalogIR result;
  for (std::size_t index = 0; index < topics.size(); ++index) {
    const auto &topic = topics[index];
    if (topic.id.empty())
      return reject("topic catalog contains an empty TopicInfo identity");
    if (find_book_topic_catalog_entry(result, topic.id) != nullptr)
      return reject("topic catalog contains a case-insensitive duplicate "
                    "TopicInfo identity: " + topic.id);
    BookTopicCatalogEntryIR entry;
    entry.raw_topic_id = topic.id;
    entry.topic_header = BookTopicHeaderEvidenceIR{
        topic.title, topic.heading_level, topic.topic_number,
        topic.start_logical_record, topic.end_logical_record, index};
    result.topics.push_back(std::move(entry));
  }
  for (std::size_t index = 0; index < toc.size(); ++index) {
    const auto &item = toc[index];
    if (item.id.empty())
      return reject("topic catalog contains an empty TOC identity");
    BookTopicCatalogEntryIR *entry = nullptr;
    for (auto &candidate : result.topics)
      if (ascii_equals_case_insensitive(candidate.raw_topic_id, item.id)) {
        entry = &candidate;
        break;
      }
    if (entry == nullptr) {
      result.topics.push_back({item.id, std::nullopt, {}});
      entry = &result.topics.back();
    }
    entry->toc_entries.push_back(
        {item.id, item.title, item.level, item.style, item.heading_level,
         item.topic_number, item.start_logical_record,
         item.end_logical_record, index});
  }
  if (error != nullptr)
    error->clear();
  return result;
}

bool verify_book_topic_catalog_ir(const std::vector<TopicInfo> &topics,
                                  const std::vector<TocEntry> &toc,
                                  const BookTopicCatalogIR &catalog,
                                  std::string *error) {
  const auto canonical = build_book_topic_catalog_ir(topics, toc, error);
  if (!canonical)
    return false;
  if (!same_catalog(*canonical, catalog))
    return fail(error, "topic catalog differs from canonical extraction");
  if (error != nullptr)
    error->clear();
  return true;
}

std::string format_book_topic_catalog_ir(const BookTopicCatalogIR &catalog) {
  std::ostringstream out;
  out << "book_topic_catalog topics=" << catalog.topics.size() << '\n';
  for (const auto &entry : catalog.topics)
    out << "topic id='" << entry.raw_topic_id << "' header="
        << (entry.topic_header ? 1 : 0)
        << " toc_entries=" << entry.toc_entries.size() << '\n';
  return out.str();
}

} // namespace geist::detail
