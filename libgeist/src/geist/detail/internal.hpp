#pragma once

#include "geist/boo.hpp"
#include "geist/detail/atomic_cache.hpp"
#include "geist/detail/book_ir.hpp"
#include "geist/detail/control_ir.hpp"
#include "geist/detail/glossary_catalog_ir.hpp"
#include "geist/detail/glossary_ir.hpp"
#include "geist/detail/fixed_display.hpp"
#include "geist/detail/fixed_prose_ir.hpp"
#include "geist/detail/layout_ir.hpp"
#include "geist/detail/menu_ir.hpp"
#include "geist/detail/message_ir.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/publication_ir.hpp"
#include "geist/detail/selector_ir.hpp"
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
  // Everything above is built by `BooDocument::open()` and never written
  // again, so it is safe to read from any number of threads. The two caches
  // below are the only mutable state a decode context has.
  //
  // Source provenance is rare and substantially heavier than the compact
  // payload index. Rebuild the dictionary only on first provenance request,
  // then retain it for candidate-local follow-up queries. It is a pure
  // function of `bytes` and `directory`, so it is filled once and published
  // atomically rather than locked; see geist/detail/atomic_cache.hpp.
  mutable CacheSlot<std::map<std::uint16_t, TokenWords>> source_dictionary;
  // Last record decoded for a provenance query. Resolving a rendered span
  // asks for many slices of one record in a row, so one memo turns a whole
  // topic's proof from quadratic into linear.
  //
  // This one is a *replacement* cache -- the slot takes a different value per
  // record -- so the publish-once rule does not apply and it keeps a mutex.
  // Reach it only through `memoized_source_record`, which owns the lock: the
  // guard used to live at the single call site, which was correct only for as
  // long as there was exactly one caller.
  mutable std::mutex source_record_memo_mutex;
  mutable std::uint32_t source_record_memo_id = 0;
  mutable std::shared_ptr<const LogicalRecordIR> source_record_memo;
};

struct TopicData {
  std::string id;
  std::string title;
  std::string heading_level;
  std::uint32_t topic_number = 0;
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  std::vector<std::string> raw_records;
  // Positioned source decode for the selected topic, populated lazily once
  // per topic and shared by typed lowering and the compatibility renderer.
  std::vector<DecodedLogicalRecordSource> fixed_layout_sources;
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
// Byte width one token word contributes to token_words_to_ascii, without
// materialising the projection. Callers that only need offsets or widths must
// use this instead of measuring a one-word projection string.
std::size_t token_word_ascii_width(std::uint16_t word);

std::string trim_right_spaces(std::string value);
std::string trim_ascii(std::string value);
// The ASCII case fold these helpers mean. std::tolower would do the same work
// through a locale lookup on every character; the library never installs a
// locale, so in the "C" locale the two agree on every byte, including the
// bytes at 0x80 and above that both leave alone.
inline char ascii_lower_char(char ch) {
  return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

std::string ascii_lower(std::string value);
bool ascii_equals_case_insensitive(const std::string& left,
                                   const std::string& right);
// Case-insensitive comparison that folds in place instead of building a
// lower-cased copy of either side.
bool ascii_equals_case_insensitive(std::string_view left,
                                   std::string_view right);
// True when `value` contains `needle` case-insensitively, without lower-casing
// either string into a temporary.
bool ascii_contains_case_insensitive(std::string_view value,
                                     std::string_view needle);
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
std::string strip_fixed_line_overflow_tokens(
    std::string value,
    bool allow_wide_short_boundary = false,
    bool allow_content_origin = false);
std::map<std::string, std::string> extract_font_definitions(
    const std::vector<std::string>& decoded_records);
// `sources` are the typed sources of the same records, in any order: the
// font-span trace reads its display columns off their carried display-line
// framing, because a CFONT operand is a column and the flattened decoded
// string above measures bytes (issue #82).
std::vector<BooLogicalRecordTrace> trace_decoded_records(
    const std::vector<std::string>& decoded_records,
    const std::vector<DecodedLogicalRecordSource>& sources,
    std::uint32_t first_logical_record,
    const std::map<std::string, std::string>& font_definitions);
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
// Byte offsets into token_words_to_ascii(assembled.words) at which each source
// token's own output begins. This is where one header control can end and the
// next begin: the separator between two controls is a rendering artefact --
// a decoder placeholder, a run of spaces, or nothing at all -- but the token
// boundary underneath it is carried by the decode and is the same in every
// book.
std::vector<std::size_t> assembled_token_output_offsets(
    const AssembledLogicalRecord& assembled);
// `token_offsets` are the offsets above for `decoded_record`. A control's
// value ends at the next token that itself begins a `c<name>=` key; with no
// token evidence no boundary is claimed and the value runs to the end of the
// record.
std::vector<BooLogicalControl> extract_logical_controls(
    const std::string& decoded_record,
    const std::vector<std::size_t>& token_offsets);
// True when a `c<name>=` control key begins exactly at `offset`.
bool control_key_begins_at(const std::string& decoded_record,
                           const std::string& lower_record,
                           std::size_t offset);
// `header_token_offsets`, when given, receives assembled_token_output_offsets
// for the leading records that make up the book header -- up to and including
// the record that carries `cdocnum=`, which is where the header's controls
// stop being read (extract_book_logical_controls).
std::vector<std::string> decode_experimental_logical_records(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    std::vector<LogicalRecordPayloadRange>* payload_ranges = nullptr,
    std::vector<std::vector<std::size_t>>* header_token_offsets = nullptr);
// Decodes one logical record's payload from the file bytes. This is the token
// decoder the whole pipeline is built on, exposed so a provenance slice can be
// proven against the file by decoding its record again.
LogicalRecordIR decode_record_payload_ir(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    const std::map<std::uint16_t, TokenWords>& token_strings,
    std::size_t payload_begin,
    std::size_t payload_end,
    std::uint32_t logical_record);
// Re-decodes the whole tokens stored in `[byte_begin, byte_end)` of the BOO
// file with no record or layout context, so a provenance slice can be proven
// against the file bytes it names. Empty when the window does not tile into
// whole tokens.
std::optional<std::vector<LogicalTokenIR>> decode_source_byte_range_tokens(
    const std::vector<std::uint8_t>& bytes,
    const BooDirectory& directory,
    const std::map<std::uint16_t, TokenWords>& token_strings,
    std::size_t byte_begin,
    std::size_t byte_end);
// The context's token dictionary, built on first use and thereafter
// immutable, so the returned reference is valid for the context's lifetime
// and may be read concurrently.
const std::map<std::uint16_t, TokenWords>& source_dictionary_for(
    const LogicalDecodeContext& context);
// The decoded payload of one logical record, answered from the context's
// one-record provenance memo. Owns the memo's lock, and returns a shared_ptr
// so the record outlives a concurrent replacement of the memo.
std::shared_ptr<const LogicalRecordIR> memoized_source_record(
    const LogicalDecodeContext& context,
    const std::map<std::uint16_t, TokenWords>& token_strings,
    std::uint32_t logical_record);
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
    const std::vector<std::string>& decoded_records,
    const std::vector<std::vector<std::size_t>>& record_token_offsets);
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
// Topic identities, boundaries and header titles.  The title is read off the
// `ST` display line of the topic's own metadata record, so this needs the
// positioned decode context and not only the flattened record strings
// (topic_header_title.hpp).
std::vector<TopicData> build_topics(const LogicalDecodeContext& context,
                                    bool copy_records = true);

BooBookProperties build_book_properties(
    const std::vector<BooLogicalControl>& controls);

BooPageRole classify_run(std::uint32_t start_page,
                         std::uint16_t page_class,
                         const BooDirectory& directory);
std::vector<BooPageRun> build_page_runs(const std::vector<std::uint8_t>& bytes,
                                        const BooDirectory& directory);

} // namespace geist::detail
