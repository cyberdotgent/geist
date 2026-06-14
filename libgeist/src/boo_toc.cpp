#include "geist/detail/boo_detail.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace geist::detail {

bool looks_like_toc_entry_boundary(const std::string& lower_record,
                                   std::size_t offset) {
  static const std::array<const char*, 5> boundaries = {
      "?ctoce ", ", ctoce ", "?ctocdef=", ", ctocdef=", "?sh"};
  for (const auto* boundary : boundaries) {
    const std::string boundary_text(boundary);
    if (offset + boundary_text.size() <= lower_record.size() &&
        lower_record.compare(offset, boundary_text.size(), boundary_text) ==
            0) {
      return true;
    }
  }
  return false;
}

std::size_t find_toc_end_marker(const std::string& lower_record,
                                std::size_t offset,
                                std::size_t limit) {
  std::size_t end_marker = std::string::npos;
  const auto cz_off_etoc = lower_record.find("cz off etoc", offset);
  if (cz_off_etoc != std::string::npos && cz_off_etoc < limit) {
    end_marker = std::min(end_marker, cz_off_etoc);
  }
  const auto etoc = lower_record.find("etoc", offset);
  if (etoc != std::string::npos && etoc < limit) {
    end_marker = std::min(end_marker, etoc);
  }
  return end_marker;
}

std::vector<TocEntry> extract_toc_entries(const std::string& decoded_record) {
  std::vector<TocEntry> entries;
  const auto lower_record = ascii_lower(decoded_record);
  std::size_t search_offset = 0;

  while (search_offset < decoded_record.size()) {
    const auto found = lower_record.find("ctoce ", search_offset);
    if (found == std::string::npos) {
      break;
    }
    const auto record_toc_end =
        find_toc_end_marker(lower_record, search_offset, found + 1);
    if (record_toc_end != std::string::npos && record_toc_end < found) {
      break;
    }

    const auto marker_size = std::string("ctoce ").size();
    auto value_begin = found + marker_size;
    auto value_end = decoded_record.size();
    const auto next_entry = lower_record.find("ctoce ", value_begin);
    const auto toc_end =
        find_toc_end_marker(lower_record, value_begin, value_end);
    if (toc_end != std::string::npos &&
        (next_entry == std::string::npos || toc_end < next_entry)) {
      value_end = toc_end;
    }
    if (next_entry != std::string::npos) {
      value_end = std::min(value_end, next_entry);
    }
    for (auto cursor = value_begin; cursor < value_end; ++cursor) {
      if (looks_like_toc_entry_boundary(lower_record, cursor)) {
        value_end = cursor;
        break;
      }
    }

    const auto value =
        trim_ascii(decoded_record.substr(value_begin, value_end - value_begin));
    std::istringstream input(value);
    std::uint32_t level = 0;
    std::uint32_t style = 0;
    std::string id;
    if (input >> level >> style >> id) {
      if (style == 0) {
        search_offset = found + marker_size;
        continue;
      }
      std::string title;
      std::getline(input, title);
      title = normalize_toc_title(trim_ascii(title));
      if (!id.empty() && !title.empty()) {
        entries.push_back(
            {normalize_toc_id(id), title, level, style});
      }
    }

    search_offset = found + marker_size;
  }

  return entries;
}

bool is_contents_topic_record(const std::string& decoded_record) {
  const auto lower_record = ascii_lower(decoded_record);
  return lower_record.find("shcontents") != std::string::npos ||
         lower_record.find("chdlevel :toc") != std::string::npos ||
         lower_record.find("ctocdef=") != std::string::npos;
}

bool is_topic_header_record(const std::string& decoded_record) {
  auto lower_record = ascii_lower(trim_ascii(decoded_record));
  lower_record.erase(0, skip_decoded_separators(lower_record));
  return lower_record.rfind("sh", 0) == 0 &&
         lower_record.find("ctopicn") != std::string::npos;
}

void attach_topic_data(TocEntry& entry, const TopicData& topic) {
  entry.heading_level = topic.heading_level;
  entry.topic_number = topic.topic_number;
  entry.start_logical_record = topic.start_logical_record;
  entry.end_logical_record = topic.end_logical_record;
  entry.raw_records = render_gml_records(topic.raw_records);
}

std::vector<TocEntry> build_table_of_contents(
    const std::vector<std::string>& decoded_records,
    const std::vector<TopicData>& topics) {
  std::vector<TocEntry> toc;
  bool in_contents_topic = false;
  for (const auto& decoded : decoded_records) {
    if (!in_contents_topic) {
      if (!is_contents_topic_record(decoded)) {
        continue;
      }
      in_contents_topic = true;
    } else if (is_topic_header_record(decoded)) {
      break;
    }

    auto entries = extract_toc_entries(decoded);
    if (entries.empty()) {
      continue;
    }
    for (auto& entry : entries) {
      if (const auto* topic = find_topic_data(topics, entry.id)) {
        attach_topic_data(entry, *topic);
      }
    }
    toc.insert(toc.end(), entries.begin(), entries.end());
  }
  return toc;
}

std::vector<std::string> build_raw_gml_records(
    const std::vector<TopicData>& topics) {
  std::vector<std::string> records;
  for (const auto& topic : topics) {
    auto topic_records = render_gml_records(topic.raw_records);
    records.insert(records.end(),
                   std::make_move_iterator(topic_records.begin()),
                   std::make_move_iterator(topic_records.end()));
  }
  return records;
}

std::vector<TopicData> build_topics(
    const std::vector<std::string>& decoded_records) {
  std::vector<TopicData> topics;

  std::vector<std::size_t> header_indexes;
  for (std::size_t index = 0; index < decoded_records.size(); ++index) {
    if (is_topic_header_record(decoded_records[index]) &&
        !extract_topic_header_id(decoded_records[index]).empty()) {
      header_indexes.push_back(index);
    }
  }
  if (header_indexes.empty()) {
    return topics;
  }

  topics.reserve(header_indexes.size());
  std::set<std::string> seen_topic_ids;
  for (std::size_t index = 0; index < header_indexes.size(); ++index) {
    const auto record_begin = header_indexes[index];
    const auto record_end =
        (index + 1 < header_indexes.size())
            ? header_indexes[index + 1]
            : decoded_records.size();
    if (record_begin >= record_end) {
      continue;
    }

    TopicData topic;
    const auto& header = decoded_records[record_begin];
    topic.topic_number = extract_uint_control_value(header, "ctopicn ");
    topic.start_logical_record =
        static_cast<std::uint32_t>(record_begin + 1);
    topic.end_logical_record = static_cast<std::uint32_t>(record_end + 1);
    topic.raw_records.assign(decoded_records.begin() +
                                 static_cast<std::ptrdiff_t>(record_begin),
                             decoded_records.begin() +
                                 static_cast<std::ptrdiff_t>(record_end));

    topic.id = extract_topic_header_id(header);
    topic.heading_level =
        extract_control_value_until_boundary(header, "chdlevel ");
    topic.title =
        normalize_toc_title(extract_control_value_until_boundary(header,
                                                                 "st "));
    if (!topic.id.empty() && seen_topic_ids.insert(topic.id).second) {
      topics.push_back(std::move(topic));
    }
  }
  return topics;
}

} // namespace geist::detail
