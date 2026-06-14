#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace geist {

struct BooMetadata {
  std::filesystem::path path;
  std::uint64_t file_size = 0;
  std::vector<std::uint8_t> leading_bytes;
};

struct TocEntry {
  std::string id;
  std::string title;
  std::uint32_t level = 0;
};

struct ResourceEntry {
  std::string id;
  std::string name;
  std::string stored_format;
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
};

class BooDocument {
public:
  static BooDocument open(const std::filesystem::path& path);

  const BooMetadata& metadata() const noexcept;
  const std::vector<TocEntry>& table_of_contents() const noexcept;
  const std::vector<ResourceEntry>& resources() const noexcept;

  std::string render_chapter_markdown(const std::string& chapter_id) const;

private:
  BooMetadata metadata_;
  std::vector<TocEntry> toc_;
  std::vector<ResourceEntry> resources_;
};

std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes);

} // namespace geist
