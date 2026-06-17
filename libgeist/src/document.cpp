#include "geist/detail/internal.hpp"

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

BooDocument BooDocument::open(const std::filesystem::path& path) {
  BooDocument document;
  document.bytes_ = read_file(path);

  if (document.bytes_.size() < boo_page_size) {
    throw std::runtime_error("BOO file is smaller than one 4096-byte page: " +
                             path.string());
  }
  if (document.bytes_.size() % boo_page_size != 0) {
    throw std::runtime_error("BOO file size is not a multiple of 4096 bytes: " +
                             path.string());
  }

  document.metadata_.path = path;
  document.metadata_.file_size =
      static_cast<std::uint64_t>(document.bytes_.size());
  document.metadata_.page_count =
      static_cast<std::uint32_t>(document.bytes_.size() / boo_page_size);

  document.file_header_.directory_page_number = read_be16(document.bytes_, 0);
  document.file_header_.unknown_0002 = read_be16(document.bytes_, 2);
  document.file_header_.unknown_0004 = read_be32(document.bytes_, 4);
  document.file_header_.copyright_text =
      trim_right_spaces(decode_cp037(document.bytes_, 0x000C, 128));
  if (document.bytes_.size() >= 0x0106) {
    document.file_header_.unknown_0102 =
        std::array<std::uint8_t, 4>{document.bytes_[0x0102],
                                    document.bytes_[0x0103],
                                    document.bytes_[0x0104],
                                    document.bytes_[0x0105]};
  }

  const auto directory_page = document.file_header_.directory_page_number;
  if (directory_page >= document.metadata_.page_count) {
    throw std::runtime_error("BOO directory page is outside the file");
  }

  const std::size_t directory_base =
      static_cast<std::size_t>(directory_page) * boo_page_size;
  document.directory_.page_number = directory_page;
  document.directory_.version_text =
      decode_cp037(document.bytes_, directory_base + 0x0010, 4);
  document.directory_.version_variant = document.bytes_[directory_base + 0x0013];
  document.directory_.token_threshold = document.bytes_[directory_base + 0x0014];
  document.directory_.last_page_number =
      read_be16(document.bytes_, directory_base + 0x0016);
  document.directory_.token_map_offset =
      read_be16(document.bytes_, directory_base + 0x0022);
  document.directory_.dictionary_start_page =
      read_be16(document.bytes_, directory_base + 0x0028);
  document.directory_.dictionary_page_count =
      read_be16(document.bytes_, directory_base + 0x002E);
  document.directory_.content_page_count =
      read_be16(document.bytes_, directory_base + 0x0038);
  document.directory_.content_start_page =
      read_be16(document.bytes_, directory_base + 0x003A);
  document.directory_.stream_table_offset =
      read_be16(document.bytes_, directory_base + 0x003C);
  document.directory_.stream_table_count =
      read_be16(document.bytes_, directory_base + 0x003E);
  document.directory_.secondary_table_offset =
      read_be16(document.bytes_, directory_base + 0x0040);
  document.directory_.date =
      decode_cp037(document.bytes_, directory_base + 0x0044, 8);
  document.directory_.time =
      decode_cp037(document.bytes_, directory_base + 0x004E, 8);

  const auto last_physical_page =
      physical_page_for_logical(document.directory_,
                                document.directory_.last_page_number);
  if (last_physical_page >= document.metadata_.page_count) {
    throw std::runtime_error("BOO directory last-page field points outside the "
                             "file");
  }

  document.page_runs_ = build_page_runs(document.bytes_, document.directory_);
  const auto decoded_records =
      decode_experimental_logical_records(document.bytes_, document.directory_);
  document.logical_controls_ = extract_book_logical_controls(decoded_records);
  document.book_properties_ =
      build_book_properties(document.logical_controls_);
  const auto topics = build_topics(decoded_records);
  document.toc_ = build_table_of_contents(decoded_records, topics);
  document.raw_gml_records_ = build_raw_gml_records(topics);
  document.resources_ = build_resources(document.bytes_, document.directory_);
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

const std::vector<TocEntry>& BooDocument::table_of_contents() const noexcept {
  return toc_;
}

const std::vector<std::string>& BooDocument::raw_gml_records() const noexcept {
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

std::vector<std::uint8_t> BooDocument::read_page(
    std::uint32_t page_number) const {
  if (page_number >= metadata_.page_count) {
    throw std::out_of_range("BOO page number is outside the file");
  }

  const auto begin = bytes_.begin() +
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
  if (!byte_range_is_valid(bytes_, found->offset, found->size)) {
    throw std::runtime_error("BOO resource byte range is outside the file");
  }

  const auto begin = bytes_.begin() +
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
