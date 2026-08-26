#pragma once

#include "geist/boo.hpp"
#include "geist/detail/book_ir.hpp"
#include "geist/detail/control_ir.hpp"
#include "geist/detail/fixed_display.hpp"
#include "geist/detail/layout_ir.hpp"
#include "geist/detail/menu_ir.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/publication_ir.hpp"
#include "img/image.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace geist::detail {

enum class LogicalWordSourceKind {
  token_word,
  inserted_space,
};

struct LogicalWordSource {
  // token_index is local to one logical record and is only suitable for
  // grouping output words by their source fragment. It is not a dictionary
  // key or a book-global token identity.
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  LogicalWordSourceKind kind = LogicalWordSourceKind::token_word;
  bool has_control = false;
  // The explicit 0-3 prefix, or the default inter-token spacing mode 3 when
  // the source token has no prefix.
  std::uint16_t spacing_control = 2;
};

struct LogicalTokenSpan {
  // Record-local source-fragment index; grouping metadata only.
  std::size_t token_index = 0;
  bool has_control = false;
  std::uint16_t spacing_control = 2;
  std::size_t output_begin = 0;
  std::size_t output_end = 0;
  bool control_only = false;
};

struct AssembledLogicalRecord {
  TokenWords words;
  std::vector<LogicalWordSource> sources;
  std::vector<LogicalTokenSpan> tokens;
};

struct DecodedMarkupSegmentSpan {
  std::size_t output_begin = 0;
  std::size_t output_end = 0;
  std::string text;
};

bool output_spans_intersect(std::size_t left_begin, std::size_t left_end,
                            std::size_t right_begin, std::size_t right_end);
std::vector<std::size_t> source_tokens_intersecting_output(
    const AssembledLogicalRecord& assembled, std::size_t output_begin,
    std::size_t output_end);
std::vector<DecodedMarkupSegmentSpan> split_decoded_markup_segment_spans(
    const std::string& decoded_record);
std::vector<std::string> split_decoded_markup_segments(
    const std::string& decoded_record);

struct LogicalRecordPayloadRange {
  std::uint32_t begin = 0;
  std::uint32_t end = 0;
};

struct DecodedLogicalRecordSource {
  std::uint32_t logical_record = 0;
  // Authoritative lossless source representation. The parallel vectors below
  // are temporary compatibility projections for existing layout consumers.
  LogicalRecordIR ir;
  std::vector<TokenWords> tokens;
  std::vector<EncodedLogicalToken> encoded_tokens;
  AssembledLogicalRecord assembled;
  std::vector<ControlSegmentIR> control_segments;
};

std::vector<std::string> clean_source_owned_selector_display_markers(
    const std::vector<std::string>& decoded_records,
    const std::vector<DecodedLogicalRecordSource>& sources);

struct LogicalDecodeContext {
  std::vector<std::uint8_t> bytes;
  BooDirectory directory;
  std::vector<std::uint32_t> content_page_record_starts;
  std::vector<std::uint32_t> topic_record_starts;
  std::vector<std::string> decoded_records;
  std::vector<LogicalRecordPayloadRange> record_payload_ranges;
  // Source provenance is rare and substantially heavier than the compact
  // payload index. Rebuild the dictionary only on first provenance request,
  // then retain it for candidate-local follow-up queries.
  mutable std::shared_ptr<const std::map<std::uint16_t, TokenWords>>
      source_dictionary;
  mutable std::mutex source_dictionary_mutex;
};

struct TopicData {
  std::string id;
  std::string title;
  std::string heading_level;
  std::uint32_t topic_number = 0;
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  std::vector<std::string> raw_records;
  // Populated only for a selected topic whose lightweight decoded stream
  // contains a fixed-form candidate. Kept topic-local and discarded after
  // normalized GML construction.
  std::vector<DecodedLogicalRecordSource> fixed_layout_sources;
  bool use_legacy_source_layout = false;
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
std::optional<std::vector<std::string>>
render_verified_publication_catalog_gml(
    const std::vector<DecodedLogicalRecordSource>& sources);
bool project_verified_menu_gml(
    std::vector<std::string>& rendered,
    const std::vector<DecodedLogicalRecordSource>& sources,
    const std::map<std::string, std::string>& topic_titles);
std::vector<std::string> render_gml_records_with_source_layout(
    const std::vector<std::string>& decoded_records,
    const std::vector<DecodedLogicalRecordSource>& sources);
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
AssembledLogicalRecord assemble_logical_record_with_sources(
    const std::vector<TokenWords>& tokens);
std::vector<BooLogicalControl> extract_logical_controls(
    const std::string& decoded_record);
std::vector<std::string> decode_experimental_logical_records(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    std::vector<LogicalRecordPayloadRange>* payload_ranges = nullptr);
std::vector<DecodedLogicalRecordSource>
decode_logical_record_sources(const LogicalDecodeContext& context,
                              std::uint32_t first_logical_record,
                              std::uint32_t end_logical_record);
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
void attach_topic_data(
    TocEntry& entry,
    const TopicData& topic,
    const std::map<std::string, std::string>* topic_titles = nullptr);
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
