#include "geist/detail/internal.hpp"
#include "geist/detail/message_prose_rows.hpp"
#include "geist/detail/topic_header_title.hpp"

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
        TocEntry entry;
        entry.id = normalize_toc_id(id);
        entry.title = std::move(title);
        entry.level = level;
        entry.style = style;
        entries.push_back(std::move(entry));
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
  // The topic header and its metadata are normally assembled into one
  // logical record.  Some books store the header (for example `sh2.6`) as
  // its own record and put CTopicN/CHdLevel/ST in the following record.
  // The SH topic id is the boundary, so do not require metadata to be in the
  // same record.
  if (extract_topic_header_id(decoded_record).empty()) {
    return false;
  }
  const auto lower_record = ascii_lower(decoded_record);
  if (lower_record.find("ctopicn") != std::string::npos) {
    return true;
  }

  // Without same-record metadata, accept only a standalone SH<id> boundary.
  // This excludes ordinary prose records beginning with words such as SHOULD
  // or SHIPPED from the topic index.
  const auto start = skip_decoded_separators(decoded_record);
  auto cursor = start + 2;
  while (cursor < decoded_record.size()) {
    const auto ch = decoded_record[cursor];
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte) == 0 && ch != '.' && ch != '_' && ch != '-') {
      break;
    }
    ++cursor;
  }
  return cursor > start + 2 &&
         skip_decoded_separators(decoded_record.substr(cursor)) ==
             decoded_record.size() - cursor;
}

// M9 keep: fixed-body visual `|` rails.  Effect census at typed coverage
// 6,986/7,362 (disable, re-export the whole corpus, `diff -r`): 9 dependent
// topics - GG24-4302-00 FRONT_1, ITPPIBOK A.2.1, QSYSNEWG 6.3.1/7.7.2.1,
// SC24-546 2.1.3, SC24-5527-02 COMMENTS, SC33-033 A.2/A.3, SC34-425 2.5.6.
// Replaced by `LayoutIR` rows.
// M9 keep: legacy-route catalog introductions.  Effect census at typed
// coverage 6,986/7,362: 3 dependent topics - GX27-3999-00 B.0, SC09-138 F.1,
// SC31-711 4.3.5. The SC31-711 2.4.9/4.1.x/4.3.1-4.3.4 topics named in the
// earlier census are typed now. Replaced by `MessageTopicIR` introduction
// paragraphs (typed trap-catalog lowering).
// M9 keep: legacy-only SRMSG catalog introductions and `ST` form prefixes.
// Effect census at typed coverage 6,986/7,362: exactly 1 dependent topic,
// SC31-711 4.3.5 (`(`/`)` glyph slots); 4.1.1/4.3.1/4.3.2/4.3.4 are typed
// now, so the function still runs on them but its output is discarded.
// Retires with a typed trap-catalog lowering
// (`MessageTopicIR` introduction). The glyph slot
// characters before the four-space padding are the legacy fixed-row marker
// glyph set (`is_fixed_st_row_marker` plus `"`).
// M9 keep: glyph/`?`/wide-gap row-marker inference on flattened `ST` bodies
// (`is_fixed_st_row_marker`, `fixed_st_row_marker_at`,
// `has_reflow_off_line_markers`, `split_reflow_off_body_lines`,
// `preserve_reflow_off_st_body_lines`, `strip_leading_visual_bar`). Corpus
// effect census at typed coverage 6,986/7,362 (disable, re-export the whole
// corpus, `diff -r`): 14 dependent topics - FA1PLMM0 9.3/9.3.1, GG24-4302-00
// FRONT_1, IBMMMSTR TITLE, ITPPIBOK A.2.1, N2AH1MST 1.2.5, QSYSNEWG
// 6.3.1/7.7.2.1, SC24-546 2.1.3, SC24-5527-02 COMMENTS, SC31-711 BACK_1.12,
// SC33-033 A.2/A.3, SC34-425 2.5.6. Replaced by `LayoutIR` marker slots /
// `fixed_prose_ir.cpp` once the typed fixed-prose lowering admits those
// bodies.
constexpr char kSyntheticRecordBoundary = '\x1E';

void attach_topic_data(
    TocEntry& entry,
    const TopicData& topic,
    const std::map<std::string, std::string>* topic_titles) {
  (void)topic_titles;
  entry.heading_level = topic.heading_level;
  entry.topic_number = topic.topic_number;
  entry.start_logical_record = topic.start_logical_record;
  entry.end_logical_record = topic.end_logical_record;
}

std::vector<TocEntry> build_table_of_contents(
    const std::vector<std::string>& decoded_records,
    const std::vector<TopicData>& topics,
    bool attach_records) {
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
        if (attach_records) {
          attach_topic_data(entry, *topic);
        } else {
          entry.heading_level = topic->heading_level;
          entry.topic_number = topic->topic_number;
          entry.start_logical_record = topic->start_logical_record;
          entry.end_logical_record = topic->end_logical_record;
        }
      }
    }
    toc.insert(toc.end(), entries.begin(), entries.end());
  }
  return toc;
}

std::vector<TopicData> build_topics(const LogicalDecodeContext& context,
                                    bool copy_records) {
  const auto& decoded_records = context.decoded_records;
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
    // Metadata can follow a standalone SH boundary.  Use the first record
    // carrying the topic controls, rather than assuming the boundary and
    // metadata share one logical record.
    std::size_t metadata_record = record_begin;
    if (extract_uint_control_value(decoded_records[metadata_record],
                                   "ctopicn ") == 0) {
      for (auto candidate = record_begin + 1;
           candidate < record_end;
           ++candidate) {
        const auto& candidate_record = decoded_records[candidate];
        if (extract_uint_control_value(candidate_record, "ctopicn ") != 0 &&
            (!extract_control_value_until_boundary(candidate_record,
                                                   "chdlevel ")
                  .empty() ||
             !extract_control_value_until_boundary(candidate_record, "st ")
                  .empty())) {
          metadata_record = candidate;
          break;
        }
      }
    }
    const auto& metadata = decoded_records[metadata_record];
    topic.topic_number = extract_uint_control_value(metadata, "ctopicn ");
    topic.start_logical_record =
        static_cast<std::uint32_t>(record_begin + 1);
    topic.end_logical_record = static_cast<std::uint32_t>(record_end + 1);
    if (copy_records) {
      topic.raw_records.assign(decoded_records.begin() +
                                   static_cast<std::ptrdiff_t>(record_begin),
                               decoded_records.begin() +
                                   static_cast<std::ptrdiff_t>(record_end));
    }

    topic.id = extract_topic_header_id(header);
    topic.heading_level =
        extract_control_value_until_boundary(metadata, "chdlevel ");
    // The header title is the visible text of the `ST` display line
    // (topic_header_title.hpp).  The flattened `ST` payload run below is only
    // the fallback for a record whose display lines do not parse or that
    // carries no `ST` line at all; it stops at the first decoder boundary,
    // which is not where the display row breaks.
    std::optional<std::string> display_line_title;
    for (auto candidate = metadata_record;
         candidate < record_end && candidate < metadata_record + 6;
         ++candidate) {
      const auto sources = decode_logical_record_sources(
          context, static_cast<std::uint32_t>(candidate + 1),
          static_cast<std::uint32_t>(candidate + 2));
      if (sources.empty()) break;
      display_line_title = topic_header_title_of_record(sources.front());
      if (display_line_title) break;
    }
    topic.title = normalize_toc_title(
        display_line_title
            ? *display_line_title
            : extract_control_value_until_boundary(metadata, "st "));
    if (!topic.id.empty() && seen_topic_ids.insert(topic.id).second) {
      topics.push_back(std::move(topic));
    }
  }
  return topics;
}

} // namespace geist::detail
