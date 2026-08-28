#pragma once

#include "geist/detail/font_span_ir.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/prose_topic_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Internal working types of the prose topic family, shared by its
// extraction units: prose_topic_stream.cpp (envelope and body token stream),
// prose_topic_lines.cpp (display rows), prose_topic_blocks.cpp (blocks,
// inlines, trailing menu) and prose_topic_ir.cpp (extraction entry point,
// verification, formatting).
namespace geist::detail::prose_internal {

constexpr auto npos = static_cast<std::size_t>(-1);
constexpr std::uint16_t unmapped_word = 0xFFFF;
constexpr std::uint16_t bullet_glyph_word = 0x2666;

bool fail(std::string* error, std::string message);

struct TokenView {
  std::size_t record = 0;  // index into records
  std::size_t token = 0;
  std::uint16_t prefix = 3;  // spacing prefix, 3 == default
  bool has_prefix = false;
  std::vector<std::uint16_t> body;  // words after the prefix
  std::uint8_t width = 0;
  std::uint16_t value = 0;  // encoded one-byte value when width == 1
};

// Row-control bytes observed in one-byte marker slots across the corpus:
// the sentinel/box runs 4..6, glyph slots 15..27, and the word-shaped slots
// 28..43 (SC31-711 `a`/`action`/`any`/`application`/`access`, ACPZMST1 `a`
// 42, N2AH1MST `access` 0x1c).  Hosted pages never display those words.  A
// one-byte word above this range before a lone origin run is a genuine word
// of an exactly full row or of an aligned column (QSYSNEWG FRONT_1 `400`
// 214, SC31-711 1.2 `C` 139); such a topic fails closed.
constexpr std::uint16_t row_control_byte_limit = 48;

TokenView view_token(const std::vector<DecodedLogicalRecordSource>& records,
                     std::size_t record, std::size_t token);
bool is_bare(const TokenView& view);
bool is_space_run(const TokenView& view);
bool box_word(std::uint16_t word);
bool is_placeholder_run(const TokenView& view);
bool is_glyph(const TokenView& view);
bool is_bullet_glyph(const TokenView& view);
bool is_visible(const TokenView& view);
bool is_padding(const TokenView& view);
bool punctuation_glyph_token(const TokenView& view);
bool is_separator(const TokenView& view);
std::string word_text(std::uint16_t word);
std::string body_text(const TokenView& view);

std::size_t segment_of(const DecodedLogicalRecordSource& record,
                       std::size_t token);
DocumentSourceSliceIR token_slice(const DecodedLogicalRecordSource& record,
                                  std::size_t begin, std::size_t end);
std::vector<DocumentSourceSliceIR> slices_for(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::vector<std::pair<std::size_t, std::size_t>>& refs);
bool valid_anchor_id(const std::string& value);
std::string normalize_title(std::string value);

enum class ItemKind {
  token,
  font,
  select,
  anchor,
  segment_end,
};

struct Item {
  ItemKind kind = ItemKind::token;
  TokenView token;
  // An inter-segment token claimed by no control segment.
  bool separator = false;
  bool title_start = false;
  bool index_start = false;
  bool continuation_start = false;
  std::vector<FontSpanIR> spans;
  std::size_t column = 0;
  std::size_t length = 0;
  std::string target;
  std::string anchor_id;
  DocumentSourceSliceIR source;
};

struct Ledger {
  const std::vector<DecodedLogicalRecordSource>* records = nullptr;
  std::vector<ProseTokenDispositionIR> entries;
  std::map<std::pair<std::uint32_t, std::size_t>, std::size_t> index;

  explicit Ledger(const std::vector<DecodedLogicalRecordSource>& sources)
      : records(&sources) {
    for (const auto& record : sources)
      for (std::size_t token = 0; token < record.ir.tokens.size(); ++token) {
        index.emplace(std::make_pair(record.logical_record, token),
                      entries.size());
        entries.push_back({{record.logical_record, token},
                           ProseTokenRoleIR::unassigned, npos, npos});
      }
  }
  ProseTokenDispositionIR& at(std::size_t record, std::size_t token) {
    return entries[index.at({(*records)[record].logical_record, token})];
  }
  bool assign(std::size_t record, std::size_t token, ProseTokenRoleIR role,
              std::string* error) {
    auto& entry = at(record, token);
    if (entry.role != ProseTokenRoleIR::unassigned) {
      return fail(error, "token " + std::to_string(token) + " of record " +
                             std::to_string(entry.token.logical_record) +
                             " received two dispositions");
    }
    entry.role = role;
    return true;
  }
};

struct Envelope {
  std::string heading_level;
  std::size_t body_segment_begin = 0;  // first non-envelope segment of record 0
  std::vector<ProseAnchorIR> leading_anchors;
  bool glued_title = false;
  std::vector<std::size_t> glued_title_tokens;
};

struct StreamBuild {
  std::vector<Item> items;
  std::vector<ProseAnchorIR> leading_anchors;
  std::vector<ProseAnchorIR> trailing_anchors;
  std::vector<ProseIndexTermIR> trailing_index_terms;
  std::size_t menu_record = npos;  // first record holding menu controls
  std::size_t menu_segment = npos;
};

bool parse_envelope(const std::vector<DecodedLogicalRecordSource>& records,
                    Ledger& ledger, Envelope& envelope, std::string* error);
bool collect_stream(const std::vector<DecodedLogicalRecordSource>& records,
                    const Envelope& envelope, Ledger& ledger,
                    StreamBuild& build, std::string* error);

struct Cell {
  std::size_t record = npos;  // npos == synthetic inter-token space
  std::size_t token = 0;
  std::string text;
  bool space = false;
};

struct Span {
  std::size_t begin = 0;
  std::size_t end = 0;
  FontStyleIR style = FontStyleIR::unknown;
  std::string target;  // non-empty == cross-reference span
};

struct Line {
  std::size_t origin = 0;
  std::size_t breaks_before = 0;
  bool anchor_before = false;
  std::size_t anchor_index = npos;
  bool bullet = false;
  std::size_t text_begin = 0;  // first cell after origin/bullet/gap
  std::vector<Cell> cells;
  std::vector<Span> fonts;
  std::vector<Span> links;
};

struct LineBuild {
  std::vector<Line> lines;
  std::vector<ProseAnchorIR> body_anchors;
  std::vector<ProseIndexTermIR> index_terms;
  std::string title;
  std::vector<std::pair<std::size_t, std::size_t>> title_refs;
};

bool build_lines(const std::vector<DecodedLogicalRecordSource>& records,
                 const std::vector<Item>& items, Ledger& ledger,
                 LineBuild& out, std::string* error);
std::string line_text(const Line& line);
bool build_blocks(const std::vector<DecodedLogicalRecordSource>& records,
                  const LineBuild& lines_build, Ledger& ledger,
                  ProseTopicIR& topic, std::string* error);
bool build_menu(const std::vector<DecodedLogicalRecordSource>& records,
                const StreamBuild& build,
                const BookTopicCatalogIR* book_topic_catalog, Ledger& ledger,
                ProseTopicIR& topic, std::string* error);

} // namespace geist::detail::prose_internal
