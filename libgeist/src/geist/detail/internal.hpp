#pragma once

#include "geist/boo.hpp"
#include "geist/detail/fixed_display.hpp"
#include "img/image.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace geist::detail {

using TokenWords = std::vector<std::uint16_t>;

struct LogicalDecodeContext {
  std::vector<std::uint8_t> bytes;
  BooDirectory directory;
  std::vector<std::uint32_t> content_page_record_starts;
  std::vector<std::uint32_t> topic_record_starts;
  std::vector<std::string> decoded_records;
};

struct TopicData {
  std::string id;
  std::string title;
  std::string heading_level;
  std::uint32_t topic_number = 0;
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  std::vector<std::string> raw_records;
};

enum class EbcdicCodePage {
  cp037,
  cp500,
};

class EbcdicCodec {
public:
  explicit EbcdicCodec(EbcdicCodePage code_page) noexcept;

  static const EbcdicCodec& cp037() noexcept;
  static const EbcdicCodec& cp500() noexcept;

  std::uint16_t decode_word(std::uint8_t byte) const noexcept;
  char decode_ascii_byte(std::uint8_t byte,
                         char replacement = '?') const noexcept;
  std::string decode_ascii(const std::vector<std::uint8_t>& bytes,
                           std::size_t offset,
                           std::size_t count,
                           const char* range_error) const;

private:
  const std::array<std::uint16_t, 256>* table_;
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path);
std::uint16_t read_be16(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset);
std::uint32_t read_be24(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset);
std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset);
bool byte_range_is_valid(const std::vector<std::uint8_t>& bytes,
                         std::uint64_t offset,
                         std::uint64_t size);

std::uint16_t map_token_word_to_lower_ascii(std::uint16_t word);
std::uint16_t map_token_word_to_upper_ascii(std::uint16_t word);
std::string token_words_to_ascii(const TokenWords& words);

std::string trim_right_spaces(std::string value);
std::string trim_ascii(std::string value);
std::string ascii_lower(std::string value);
bool ascii_equals_case_insensitive(const std::string& left,
                                   const std::string& right);
bool ascii_starts_with_case_insensitive(const std::string& value,
                                        std::string_view prefix);
bool ascii_starts_with_case_insensitive(const std::string& value,
                                        std::size_t offset,
                                        std::string_view prefix);
void replace_all_case_insensitive(std::string& value,
                                  const std::string& needle,
                                  const std::string& replacement);
std::string capitalize_bookmanager_words(std::string value);
std::string normalize_logical_control_value(const std::string& key,
                                            std::string value);
std::string normalize_toc_title(std::string value);
std::string normalize_toc_id(std::string value);
std::string collapse_ascii_whitespace(std::string value);
std::string annotate_decoded_placeholders(const std::string& value);

std::vector<ResourceEntry> build_resources(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory);
std::vector<std::string> render_gml_records(
    const std::vector<std::string>& decoded_records);
std::string strip_fixed_line_overflow_tokens(
    std::string value,
    bool allow_wide_short_boundary = false,
    bool allow_content_origin = false);
std::map<std::string, std::string> extract_font_definitions(
    const std::vector<std::string>& decoded_records);
std::vector<BooLogicalRecordTrace> trace_gml_records(
    const std::vector<std::string>& decoded_records,
    std::uint32_t first_logical_record,
    const std::map<std::string, std::string>& font_definitions);
std::string render_markdown_records(const std::vector<std::string>& records);
bool looks_like_control_boundary(const std::string& decoded_record,
                                 const std::string& lower_record,
                                 std::size_t offset);
bool looks_like_gml_control_at(const std::string& value, std::size_t offset);
std::size_t skip_decoded_separators(const std::string& value);
std::string extract_topic_header_id(const std::string& decoded_record);
std::string extract_control_value_until_boundary(const std::string& record,
                                                 const std::string& marker);
std::uint32_t extract_uint_control_value(const std::string& record,
                                         const std::string& marker);

std::optional<std::uint16_t> read_compact_length(
    const std::vector<std::uint8_t>& bytes,
    std::size_t& offset,
    std::size_t end_offset);
std::uint32_t physical_page_for_logical(const BooDirectory& directory,
                                        std::uint32_t logical_page);
std::map<std::uint16_t, TokenWords> decode_experimental_dictionary(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory);
TokenWords assemble_logical_record(const std::vector<TokenWords>& tokens);
std::vector<BooLogicalControl> extract_logical_controls(
    const std::string& decoded_record);
std::vector<std::string> decode_experimental_logical_records(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory);
std::vector<std::uint32_t> parse_content_page_record_starts(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory);
std::vector<std::uint32_t> parse_topic_record_starts(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory);
std::vector<BooLogicalControl> extract_book_logical_controls(
    const std::vector<std::string>& decoded_records);
const TopicData* find_topic_data(const std::vector<TopicData>& topics,
                                 const std::string& topic_id);

std::vector<TocEntry> extract_toc_entries(const std::string& decoded_record);
bool is_contents_topic_record(const std::string& decoded_record);
bool is_topic_header_record(const std::string& decoded_record);
void attach_topic_data(TocEntry& entry, const TopicData& topic);
std::vector<TocEntry> build_table_of_contents(
    const std::vector<std::string>& decoded_records,
    const std::vector<TopicData>& topics,
    bool attach_records = true);
std::vector<std::string> build_raw_gml_records(
    const std::vector<TopicData>& topics);
std::vector<TopicData> build_topics(
    const std::vector<std::string>& decoded_records,
    bool copy_records = true);

BooBookProperties build_book_properties(
    const std::vector<BooLogicalControl>& controls);

BooPageRole classify_run(std::uint32_t start_page,
                         std::uint16_t page_class,
                         const BooDirectory& directory);
std::vector<BooPageRun> build_page_runs(const std::vector<std::uint8_t>& bytes,
                                        const BooDirectory& directory);

} // namespace geist::detail
