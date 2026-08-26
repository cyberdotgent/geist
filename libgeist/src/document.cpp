#include "geist/detail/internal.hpp"
#include "geist/detail/implicit_grid.hpp"
#include "geist/detail/source_rows.hpp"

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

namespace geist {

using namespace detail;

namespace {

bool has_box_form_source_candidate(
    const std::vector<std::string>& records) {
  auto candidates = std::size_t{0};
  for (const auto& record : records) {
    const auto lower = ascii_lower(record);
    for (auto table = lower.find("srtbl"); table != std::string::npos;
         table = lower.find("srtbl", table + 5)) {
      const auto close = lower.find("sretbl", table + 5);
      const auto limit = close == std::string::npos ? record.size() : close;
      auto run = std::size_t{0};
      auto proven_form = false;
      for (auto cursor = table + 5; cursor < limit; ++cursor) {
        run = record[cursor] == '?' ? run + 1 : 0;
        proven_form = proven_form || run >= 40;
      }
      if (proven_form) {
        ++candidates;
      }
    }
  }
  return candidates == 1;
}

bool has_implicit_grid_source_candidate(
    const std::vector<std::string>& records) {
  for (const auto& record : records) {
    const auto lower = ascii_lower(record);
    for (auto found = lower.find("cfont "); found != std::string::npos;
         found = lower.find("cfont ", found + 6)) {
      std::istringstream values(record.substr(found + 6));
      std::vector<ImplicitGridHeaderSpan> spans;
      while (true) {
        std::size_t offset = 0;
        std::size_t length = 0;
        std::string code;
        if (!(values >> offset >> length >> code) ||
            !ascii_equals_case_insensitive(code, "2")) {
          break;
        }
        spans.push_back({offset, length});
      }
      if (is_implicit_grid_header_geometry(spans)) {
        return true;
      }
    }
  }
  return false;
}

bool has_generated_toc_source_candidate(
    const std::vector<std::string>& records) {
  return std::any_of(records.begin(), records.end(), [](const auto& record) {
    return ascii_lower(record).find("ctoce ") != std::string::npos;
  });
}

bool has_selector_source_candidate(const std::vector<std::string>& records) {
  return std::any_of(records.begin(), records.end(), [](const auto& record) {
    return ascii_lower(record).find("cselect ") != std::string::npos;
  });
}

void load_source_layout_if_candidate(
    const std::shared_ptr<LogicalDecodeContext>& context,
    TopicData& topic) {
  if (!has_box_form_source_candidate(topic.raw_records) &&
      !has_implicit_grid_source_candidate(topic.raw_records) &&
      !has_semantic_srmsg_source_candidate(topic.raw_records) &&
      !has_generated_toc_source_candidate(topic.raw_records) &&
      !has_selector_source_candidate(topic.raw_records)) {
    return;
  }
  topic.fixed_layout_sources = decode_logical_record_sources(
      *context, topic.start_logical_record, topic.end_logical_record);
}

} // namespace

BooDocument BooDocument::open(const std::filesystem::path& path) {
  BooDocument document;
  auto context = std::make_shared<LogicalDecodeContext>();
  context->bytes = read_file(path);
  const auto& bytes = context->bytes;

  if (bytes.size() < boo_page_size) {
    throw std::runtime_error("BOO file is smaller than one 4096-byte page: " +
                             path.string());
  }
  if (bytes.size() % boo_page_size != 0) {
    throw std::runtime_error("BOO file size is not a multiple of 4096 bytes: " +
                             path.string());
  }

  document.metadata_.path = path;
  document.metadata_.file_size =
      static_cast<std::uint64_t>(bytes.size());
  document.metadata_.page_count =
      static_cast<std::uint32_t>(bytes.size() / boo_page_size);

  document.file_header_.directory_page_number = read_be16(bytes, 0);
  document.file_header_.unknown_0002 = read_be16(bytes, 2);
  document.file_header_.unknown_0004 = read_be32(bytes, 4);
  document.file_header_.copyright_text =
      trim_right_spaces(EbcdicCodec::cp037().decode_ascii(
          bytes,
          0x000C,
          128,
          "unexpected end of BOO file while reading text"));
  if (bytes.size() >= 0x0106) {
    document.file_header_.unknown_0102 =
        std::array<std::uint8_t, 4>{bytes[0x0102],
                                    bytes[0x0103],
                                    bytes[0x0104],
                                    bytes[0x0105]};
  }

  const auto directory_page = document.file_header_.directory_page_number;
  if (directory_page >= document.metadata_.page_count) {
    throw std::runtime_error("BOO directory page is outside the file");
  }

  const std::size_t directory_base =
      static_cast<std::size_t>(directory_page) * boo_page_size;
  document.directory_.page_number = directory_page;
  document.directory_.version_text =
      EbcdicCodec::cp037().decode_ascii(
          bytes,
          directory_base + 0x0010,
          4,
          "unexpected end of BOO file while reading text");
  document.directory_.version_variant = bytes[directory_base + 0x0013];
  document.directory_.token_threshold = bytes[directory_base + 0x0014];
  document.directory_.last_page_number =
      read_be16(bytes, directory_base + 0x0016);
  document.directory_.token_map_offset =
      read_be16(bytes, directory_base + 0x0022);
  document.directory_.dictionary_start_page =
      read_be16(bytes, directory_base + 0x0028);
  document.directory_.dictionary_page_count =
      read_be16(bytes, directory_base + 0x002E);
  document.directory_.content_page_index_offset =
      read_be16(bytes, directory_base + 0x0034);
  document.directory_.logical_record_count =
      read_be16(bytes, directory_base + 0x0036);
  document.directory_.content_page_count =
      read_be16(bytes, directory_base + 0x0038);
  document.directory_.content_start_page =
      read_be16(bytes, directory_base + 0x003A);
  document.directory_.stream_table_offset =
      read_be16(bytes, directory_base + 0x003C);
  document.directory_.stream_table_count =
      read_be16(bytes, directory_base + 0x003E);
  document.directory_.secondary_table_offset =
      read_be16(bytes, directory_base + 0x0040);
  document.directory_.date =
      EbcdicCodec::cp037().decode_ascii(
          bytes,
          directory_base + 0x0044,
          8,
          "unexpected end of BOO file while reading text");
  document.directory_.time =
      EbcdicCodec::cp037().decode_ascii(
          bytes,
          directory_base + 0x004E,
          8,
          "unexpected end of BOO file while reading text");

  const auto last_physical_page =
      physical_page_for_logical(document.directory_,
                                document.directory_.last_page_number);
  if (last_physical_page >= document.metadata_.page_count) {
    throw std::runtime_error("BOO directory last-page field points outside the "
                             "file");
  }

  context->directory = document.directory_;
  context->content_page_record_starts =
      parse_content_page_record_starts(bytes, document.directory_);
  context->topic_record_starts = parse_topic_record_starts(
      bytes, document.directory_);
  document.decode_context_ = context;

  document.page_runs_ = build_page_runs(bytes, document.directory_);
  // The experimental decoder currently exposes compact token records rather
  // than the reader's fully assembled logical-record numbering. Decode that
  // inexpensive stream once to preserve established topic boundaries; GML
  // parsing and rendering remain deferred until a topic is requested.
  context->decoded_records =
      decode_experimental_logical_records(bytes,
                                          document.directory_,
                                          &context->record_payload_ranges);
  const auto topics = build_topics(context->decoded_records, false);
  const auto first_topic_record = topics.empty()
                                      ? context->decoded_records.size() + 1
                                      : topics.front().start_logical_record;
  const std::vector<std::string> book_header_records(
      context->decoded_records.begin(),
      context->decoded_records.begin() +
          static_cast<std::ptrdiff_t>(first_topic_record - 1));
  document.logical_controls_ =
      extract_book_logical_controls(book_header_records);
  document.book_properties_ =
      build_book_properties(document.logical_controls_);

  for (const auto& topic : topics) {
    document.topics_.push_back({topic.id,
                                topic.title,
                                topic.heading_level,
                                topic.topic_number,
                                topic.start_logical_record,
                                topic.end_logical_record});
  }

  std::vector<std::string> contents_records;
  for (const auto& topic : topics) {
    if (ascii_equals_case_insensitive(topic.id, "contents")) {
      contents_records.assign(
          context->decoded_records.begin() + topic.start_logical_record - 1,
          context->decoded_records.begin() + topic.end_logical_record - 1);
      break;
    }
  }
  document.toc_ = build_table_of_contents(contents_records, topics, false);
  for (auto& entry : document.toc_) {
    const auto* topic = find_topic_data(topics, entry.id);
    if (topic == nullptr) {
      continue;
    }
    auto topic_data = *topic;
    const auto entry_id = entry.id;
    const auto entry_title = entry.title;
    const auto entry_level = entry.level;
    const auto entry_style = entry.style;
    entry.raw_record_loader_ =
        [context, topic_data, entry_id, entry_title, entry_level,
         entry_style]() mutable {
          topic_data.raw_records.assign(
              context->decoded_records.begin() +
                  topic_data.start_logical_record - 1,
              context->decoded_records.begin() +
                  topic_data.end_logical_record - 1);
          load_source_layout_if_candidate(context, topic_data);
          TocEntry loaded;
          loaded.id = entry_id;
          loaded.title = entry_title;
          loaded.level = entry_level;
          loaded.style = entry_style;
          attach_topic_data(loaded, topic_data);
          return loaded.raw_records;
        };
  }
  document.resources_ = build_resources(bytes, document.directory_);
  return document;
}

const BooMetadata& BooDocument::metadata() const noexcept {
  return metadata_;
}

const BooPage0Header& BooDocument::file_header() const noexcept {
  return file_header_;
}

const BooDirectory& BooDocument::directory() const noexcept {
  return directory_;
}

const BooBookProperties& BooDocument::book_properties() const noexcept {
  return book_properties_;
}

const std::vector<BooPageRun>& BooDocument::page_runs() const noexcept {
  return page_runs_;
}

const std::vector<BooLogicalControl>& BooDocument::logical_controls()
    const noexcept {
  return logical_controls_;
}

const std::vector<std::string>& BooDocument::decoded_logical_records()
    const {
  return decode_context_->decoded_records;
}

const std::map<std::string, std::string>& BooDocument::font_definitions()
    const {
  if (!font_definitions_loaded_) {
    font_definitions_ = extract_font_definitions(decoded_logical_records());
    font_definitions_loaded_ = true;
  }
  return font_definitions_;
}

const std::vector<TocEntry>& BooDocument::table_of_contents() const noexcept {
  return toc_;
}

const std::vector<TopicInfo>& BooDocument::topics() const noexcept {
  return topics_;
}

const std::vector<std::string>& BooDocument::raw_gml_records() const {
  if (!raw_gml_records_loaded_) {
    for (const auto& topic : topics_) {
      std::vector<std::string> decoded(
          decode_context_->decoded_records.begin() +
              topic.start_logical_record - 1,
          decode_context_->decoded_records.begin() +
              topic.end_logical_record - 1);
      auto rendered = render_gml_records(decoded);
      raw_gml_records_.insert(
          raw_gml_records_.end(),
          std::make_move_iterator(rendered.begin()),
          std::make_move_iterator(rendered.end()));
    }
    raw_gml_records_loaded_ = true;
  }
  return raw_gml_records_;
}

const std::vector<ResourceEntry>& BooDocument::resources() const noexcept {
  return resources_;
}

const TocEntry* BooDocument::find_toc_entry(const std::string& topic_id)
    const noexcept {
  const auto normalized_id = normalize_toc_id(topic_id);
  const auto found = std::find_if(toc_.begin(), toc_.end(),
                                  [&](const TocEntry& entry) {
                                    return entry.id == normalized_id ||
                                           ascii_equals_case_insensitive(
                                               entry.id, topic_id);
                                  });
  if (found == toc_.end()) {
    return nullptr;
  }
  return &*found;
}

std::string BooDocument::topic_markdown(const std::string& topic_id) const {
  if (const auto* entry = find_toc_entry(topic_id)) {
    return entry->markdown();
  }

  const auto found = std::find_if(
      topics_.begin(), topics_.end(), [&](const TopicInfo& topic) {
        return topic.id == normalize_toc_id(topic_id) ||
               ascii_equals_case_insensitive(topic.id, topic_id);
      });
  if (found == topics_.end()) {
    throw std::out_of_range("BOO topic id was not found: " + topic_id);
  }

  TopicData topic;
  topic.id = found->id;
  topic.title = found->title;
  topic.heading_level = found->heading_level;
  topic.topic_number = found->topic_number;
  topic.start_logical_record = found->start_logical_record;
  topic.end_logical_record = found->end_logical_record;
  topic.raw_records.assign(
      decode_context_->decoded_records.begin() + topic.start_logical_record - 1,
      decode_context_->decoded_records.begin() + topic.end_logical_record - 1);
  load_source_layout_if_candidate(decode_context_, topic);
  TocEntry entry;
  entry.id = topic.id;
  entry.title = topic.title;
  attach_topic_data(entry, topic);
  return entry.markdown();
}

std::vector<BooLogicalRecordTrace> BooDocument::trace_logical_records(
    const std::string& topic_id) const {
  const auto topic = std::find_if(
      topics_.begin(), topics_.end(), [&](const TopicInfo& candidate) {
        return candidate.id == normalize_toc_id(topic_id) ||
               ascii_equals_case_insensitive(candidate.id, topic_id);
      });
  if (topic == topics_.end()) {
    throw std::out_of_range("BOO topic id was not found: " + topic_id);
  }
  if (topic->start_logical_record == 0 || topic->end_logical_record == 0 ||
      topic->end_logical_record <= topic->start_logical_record) {
    return {};
  }
  const auto& all_records = decoded_logical_records();
  const auto begin = static_cast<std::size_t>(topic->start_logical_record - 1);
  const auto end = std::min<std::size_t>(topic->end_logical_record - 1,
                                        all_records.size());
  std::vector<std::string> records(all_records.begin() + begin,
                                   all_records.begin() + end);
  return detail::trace_gml_records(
      records,
      topic->start_logical_record,
      font_definitions());
}

std::vector<std::uint8_t> BooDocument::read_page(
    std::uint32_t page_number) const {
  if (page_number >= metadata_.page_count) {
    throw std::out_of_range("BOO page number is outside the file");
  }

  const auto begin = decode_context_->bytes.begin() +
                     static_cast<std::ptrdiff_t>(page_number * boo_page_size);
  return {begin, begin + boo_page_size};
}

std::vector<std::uint8_t> BooDocument::read_resource_data(
    const std::string& resource_id) const {
  auto found = std::find_if(resources_.begin(), resources_.end(),
                            [&](const ResourceEntry& resource) {
                              return resource.id == resource_id ||
                                     ascii_equals_case_insensitive(
                                         resource.id, resource_id);
                            });
  if (found == resources_.end()) {
    throw std::out_of_range("BOO resource id was not found: " + resource_id);
  }
  if (!byte_range_is_valid(decode_context_->bytes, found->offset, found->size)) {
    throw std::runtime_error("BOO resource byte range is outside the file");
  }

  const auto begin = decode_context_->bytes.begin() +
                     static_cast<std::ptrdiff_t>(found->offset);
  return {begin, begin + static_cast<std::ptrdiff_t>(found->size)};
}

std::vector<std::uint8_t> BooDocument::read_resource_png(
    const std::string& resource_id) const {
  auto found = std::find_if(resources_.begin(), resources_.end(),
                            [&](const ResourceEntry& resource) {
                              return resource.id == resource_id ||
                                     ascii_equals_case_insensitive(
                                         resource.id, resource_id);
                            });
  if (found == resources_.end()) {
    throw std::out_of_range("BOO resource id was not found: " + resource_id);
  }

  return render_resource_png(*found, read_resource_data(found->id));
}

} // namespace geist
