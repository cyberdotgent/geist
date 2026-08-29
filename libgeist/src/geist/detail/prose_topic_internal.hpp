#pragma once

#include "geist/detail/display_lines.hpp"
#include "geist/detail/font_span_ir.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/prose_topic_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Internal working types of the prose topic family, shared by its
// extraction units: prose_topic_spans.cpp (table/figure span plan),
// prose_topic_stream.cpp (envelope and body token stream),
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
// Tokens whose decoded output intersects the opcode/operand ranges of a
// segment (sorted ascending); the remaining segment tokens are its payload.
std::vector<std::size_t> operand_tokens(
    const DecodedLogicalRecordSource& record, const ControlSegmentIR& segment);
DocumentSourceSliceIR token_slice(const DecodedLogicalRecordSource& record,
                                  std::size_t begin, std::size_t end);
std::vector<DocumentSourceSliceIR> slices_for(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::vector<std::pair<std::size_t, std::size_t>>& refs);
bool valid_anchor_id(const std::string& value);
// `<column> <length> <target>` selector operands.
bool parse_selector_operand(const std::string& text, std::size_t& column,
                            std::size_t& length, std::string& target);
std::string operand_text(const DecodedLogicalRecordSource& record,
                         const OutputRangeIR& range);
std::string normalize_title(std::string value);

enum class ItemKind {
  token,
  font,
  select,
  anchor,
  span,  // a table/figure span; every token of its region is already owned
  segment_end,
  layout,  // a `CZ` layout directive (prose_topic_cz.cpp)
};

// One `CZ <mode> <tag> [<left> <indent>]` directive of the CZ dialect
// (Format/markup.md, "CZ layout directives").  `mode` and `tag` are lower
// case; `anchor_id` carries the `SRFTN<id>` of a footnote (`flow fn`) and
// `SREFTN` arrives as `off fn`.
struct LayoutDirective {
  std::string mode;  // flow, off, break
  std::string tag;   // p, pc, ul, li, dl, dt, nt, fn, h2, eul, xmp, ...
  std::size_t left = 0;
  std::size_t indent = 0;
  std::string anchor_id;
  DocumentSourceSliceIR source;
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
  std::size_t span_index = 0;
  DocumentSourceSliceIR source;
  LayoutDirective directive;
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
              std::string* error, std::size_t span = npos) {
    auto& entry = at(record, token);
    if (entry.role != ProseTokenRoleIR::unassigned) {
      return fail(error, "token " + std::to_string(token) + " of record " +
                             std::to_string(entry.token.logical_record) +
                             " received two dispositions");
    }
    entry.role = role;
    entry.span = span;
    return true;
  }
};

// Source extent of one table/figure span: the segments of records
// [begin_record, end_record] from the region's first token to its last
// owned token.  A segment inside a region is skipped by the stream pass;
// tokens of the closing segment that the region does not own (the prose
// glued after `SRETBL`/`SREFIG`) enter the stream as body text.
struct SpanRegion {
  std::size_t span = 0;
  std::size_t begin_record = 0;
  std::size_t begin_segment = 0;
  std::size_t begin_token = 0;
  std::size_t end_record = 0;
  std::size_t end_segment = 0;
  std::size_t end_token = 0;  // inclusive
};

// A picture-less `SRFIG<id> ... SREFIG` frame around one or more table
// spans (SC31-711 4.0 `SRFIGTBLUNIQ6` around `SRTBLTBLUNIQ6`; hosted
// BookServer serves `<a name="FIGTBLUNIQ6">` before the table anchor).  The
// frame contributes only its anchor; its outline placeholders are padding
// and every visible token inside it belongs to a table span.
struct FrameRegion {
  std::size_t begin_record = 0;
  std::size_t begin_segment = 0;
  std::size_t end_record = 0;
  std::size_t end_segment = 0;
  std::string anchor_id;
  DocumentSourceSliceIR source;
};

struct SpanPlan {
  std::vector<SpanRegion> regions;  // source order
  std::vector<FrameRegion> frames;
  template <typename Region>
  static const Region* find_segment(const std::vector<Region>& list,
                                    std::size_t record, std::size_t segment) {
    for (const auto& region : list) {
      if (record < region.begin_record || record > region.end_record) continue;
      if (record == region.begin_record && segment < region.begin_segment)
        continue;
      if (record == region.end_record && segment > region.end_segment)
        continue;
      return &region;
    }
    return nullptr;
  }
  const SpanRegion* region_of_segment(std::size_t record,
                                      std::size_t segment) const {
    return find_segment(regions, record, segment);
  }
  const FrameRegion* frame_of_segment(std::size_t record,
                                      std::size_t segment) const {
    return find_segment(frames, record, segment);
  }
};

// Extracts every table envelope and figure region of the topic, fails
// closed on any decline, claims every token of every region in the ledger
// (block cells with the span's role, blank/placeholder tokens as region
// structure, anything visible rejects) and records the spans on `topic`
// without positions.
bool plan_spans(const std::vector<DecodedLogicalRecordSource>& records,
                const LayoutIR& layout, const OwnershipIR& ownership,
                const std::set<std::string>* resource_ids, Ledger& ledger,
                ProseTopicIR& topic, SpanPlan& plan, std::string* error);

struct Envelope {
  std::string heading_level;
  // The raw lower-cased CHDLEVEL operand: `h1`..`h6`, or a front-matter form
  // such as `cover`/`toc`/`preface` that the reader serves as `h1`.
  std::string heading_form;
  std::size_t body_segment_begin = 0;  // first non-envelope segment of record 0
  std::vector<ProseAnchorIR> leading_anchors;
  bool glued_title = false;
  std::vector<std::size_t> glued_title_tokens;
};

struct StreamBuild {
  const SpanPlan* plan = nullptr;  // input: table/figure regions to skip
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
  // First text cell: the cells before it are the origin run, a bullet glyph
  // or slot, a change bar, and (CZ ordered items) the ordinal label.
  std::size_t text_begin = 0;
  // Governing CZ directive (index into LineBuild::directives), npos in the
  // flattened dialect.
  std::size_t directive = npos;
  // A row of a drawn box region: `cells` are the hosted display columns of
  // the source display line, verbatim, and the row joins the preformatted
  // block of its region instead of reflowing.
  bool box = false;
  std::vector<Cell> cells;
  std::vector<Span> fonts;
  std::vector<Span> links;
};

// A table/figure span between two display lines: the span precedes
// lines[line] (== lines.size() at the end) and follows the first
// `anchors_seen` body anchors.
struct SpanMark {
  std::size_t span = 0;
  std::size_t line = 0;
  std::size_t anchors_seen = 0;
};

struct LineBuild {
  std::vector<Line> lines;
  std::vector<SpanMark> span_marks;
  std::vector<ProseAnchorIR> body_anchors;
  std::vector<ProseIndexTermIR> index_terms;
  std::string title;
  std::vector<std::pair<std::size_t, std::size_t>> title_refs;
  // CZ directives in source order; empty for the flattened dialect.
  std::vector<LayoutDirective> directives;
};

// One drawn box region embedded in prose: a `U+250C ... U+2510` top rule,
// one or more `U+2502 ... U+2502` side rows and a `U+2514 ... U+2518` bottom
// rule, all at the same left/right columns, in consecutive display lines of
// the topic (Format/markup.md, "Drawn box regions in prose").  Hosted
// BookServer prints the region's display lines verbatim inside its <pre>.
struct BoxLine {
  std::size_t record = 0;
  DisplayLineIR line;
  std::string text;                    // hosted display text
  std::vector<std::uint16_t> columns;  // one word per display column
};

struct BoxRegion {
  std::vector<BoxLine> lines;  // source order, control-only lines dropped
  std::size_t begin_record = 0;
  std::size_t begin_token = 0;  // the top rule's length byte
  std::size_t end_record = 0;
  std::size_t end_token = 0;  // last token of the bottom rule, inclusive
};

// Every closed box region of the topic, in source order.  Declines silently
// (returns no region) when a record's display lines do not parse.
std::vector<BoxRegion> plan_boxes(
    const std::vector<DecodedLogicalRecordSource>& records);

bool build_lines(const std::vector<DecodedLogicalRecordSource>& records,
                 const std::vector<Item>& items, Ledger& ledger,
                 LineBuild& out, std::string* error);
std::string line_text(const Line& line);
// Builds one block's inlines from the display lines [begin, end); text cells
// before each line's `text_begin` are excluded.
bool build_block(const std::vector<DecodedLogicalRecordSource>& records,
                 const std::vector<Line>& lines, std::size_t begin,
                 std::size_t end, ProseBlockIR& block, Ledger& ledger,
                 std::size_t block_index, std::string* error);
bool build_blocks(const std::vector<DecodedLogicalRecordSource>& records,
                  const LineBuild& lines_build, Ledger& ledger,
                  ProseTopicIR& topic, std::string* error);

// CZ dialect (prose_topic_cz.cpp).  `collect_layout_directive` parses one
// `cz` control segment, assigns its opcode/operand tokens and pushes the
// layout item followed by the payload tokens; `build_cz_blocks` replaces the
// origin-based block grouping when the stream carried directives.
bool collect_layout_directive(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::size_t record_index, const ControlSegmentIR& segment,
    std::string& pending_footnote_id, Ledger& ledger,
    std::vector<Item>& items, std::string* error);
bool build_cz_blocks(const std::vector<DecodedLogicalRecordSource>& records,
                     const LineBuild& lines_build, Ledger& ledger,
                     ProseTopicIR& topic, std::string* error);
bool build_menu(const std::vector<DecodedLogicalRecordSource>& records,
                const StreamBuild& build,
                const BookTopicCatalogIR* book_topic_catalog, Ledger& ledger,
                ProseTopicIR& topic, std::string* error);

} // namespace geist::detail::prose_internal
