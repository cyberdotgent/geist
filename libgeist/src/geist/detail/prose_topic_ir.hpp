#pragma once

#include "geist/detail/control_ir.hpp"
#include "geist/detail/figure_block_ir.hpp"
#include "geist/detail/fixed_table_block_ir.hpp"
#include "geist/detail/font_span_ir.hpp"
#include "geist/detail/layout_ir.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/provenance_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;
struct BookTopicCatalogIR;

// Whole-topic typed model for ordinary prose topics of the flattened
// fixed-row BookManager dialect (see Format/markup.md, "Flattened fixed
// prose"): a heading, optional `SR<id>` anchors, and a body that is a
// sequence of spans in source order: prose spans (display lines that lower
// to paragraphs and simple lists, with inline emphasis from CFONT spans,
// inline cross-references from CSELECT spans and suppressed `SI` index
// terms) interleaved with table spans (one admitted `SRTBL ... SRETBL`
// envelope each, modelled by the fixed-table block) and figure spans (one
// admitted picture region each, modelled by the figure block), and an
// optional trailing CMENU.  Every source token of every record receives
// exactly one disposition below; a token inside a table/figure region is
// claimed by exactly that span.  Any token that cannot be placed, and any
// table envelope or figure region the block extractors decline, rejects
// the whole topic: there are no partially typed topics.
enum class ProseTokenRoleIR {
  unassigned,
  envelope,        // topic metadata control opcode/operand tokens
  control,         // body control opcode/operand tokens (CFONT, CSELECT, SR..)
  padding,         // structural padding runs inside control envelopes
  title,           // ST title words
  spacing,         // bare spacing-prefix token (paragraph/attach control)
  fill,            // end-of-display-line fill run
  origin,          // display-line origin (indent) run
  marker,          // one-byte physical row marker slot, not visible
  bullet,          // list-item glyph
  gap,             // literal in-line space run
  text,            // visible prose cell(s)
  index_keyword,   // the `SI` word
  index_term,      // hidden subject-index term words
  index_structure, // hidden separators of a structured subject-index line
  menu,            // CMENU/CMITEM/CEMENU tokens (validated separately)
  table,           // claimed by a table span (fixed-table block)
  figure,          // claimed by a figure span (figure block)
};

struct ProseTokenRefIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
};

struct ProseTokenDispositionIR {
  ProseTokenRefIR token;
  ProseTokenRoleIR role = ProseTokenRoleIR::unassigned;
  // Owning block/inline for text cells; npos otherwise.
  std::size_t block = static_cast<std::size_t>(-1);
  std::size_t inline_index = static_cast<std::size_t>(-1);
  // Owning span (index into ProseTopicIR::spans) for table/figure tokens;
  // npos otherwise.
  std::size_t span = static_cast<std::size_t>(-1);
};

enum class ProseInlineKindIR {
  text,
  emphasis,
  cross_reference,
};

struct ProseInlineIR {
  ProseInlineKindIR kind = ProseInlineKindIR::text;
  std::string text;
  FontStyleIR style = FontStyleIR::unknown;
  std::string target;
  // Contiguous source token ranges, in source order.
  std::vector<DocumentSourceSliceIR> slices;
};

enum class ProseBlockKindIR {
  paragraph,
  list_item,
};

struct ProseBlockIR {
  ProseBlockKindIR kind = ProseBlockKindIR::paragraph;
  // Consecutive list items sharing a list ordinal form one list.
  std::size_t list_ordinal = 0;
  std::size_t origin = 0;
  std::vector<ProseInlineIR> inlines;
  std::vector<DocumentSourceSliceIR> slices;
};

struct ProseAnchorIR {
  std::string id;
  DocumentSourceSliceIR source;
  // The anchor precedes blocks[position] (== blocks.size() at the end).
  std::size_t position = 0;
  // Placed after the trailing menu rather than before it.
  bool after_menu = false;
};

struct ProseIndexTermIR {
  std::string term;
  // A structured `SI` display line: the hidden term carries decoder
  // placeholder separators around index formatting fields
  // (`SI ??3HI1?0?Physical Planning Guide`, QSYSINFO 2.1.1 record 72).  The
  // whole display line is hidden, so the fields stay opaque; `term` is the
  // line's visible words in source order.
  bool structured = false;
  std::vector<DocumentSourceSliceIR> slices;
};

enum class ProseSpanKindIR {
  table,
  figure,
};

// A non-prose span of the body.  `index` addresses ProseTopicIR::tables or
// ProseTopicIR::figures; the span precedes blocks[position] (==
// blocks.size() at the end) and, among the anchors placed at that position,
// follows the first `anchors_before` of them in source order.
struct ProseSpanIR {
  ProseSpanKindIR kind = ProseSpanKindIR::table;
  std::size_t index = 0;
  std::size_t position = 0;
  std::size_t anchors_before = 0;
};

// A CSELECT inside a table span: `<column> <length> <target>` over the
// display columns of one physical row (origin cell == column 0), with the
// tokens of its display payload.  A table cell line lowers to a cross
// reference when every one of its source cells lies in the payload and in
// the column range (SC31-711 4.0 `"LNM OS/2 Agent Application Traps" in`,
// hosted `<a href="4.1?...#HDRLMATRP">`); a partially covered line stays
// text.
struct ProseTableLinkIR {
  std::size_t span = 0;
  std::size_t column = 0;
  std::size_t length = 0;
  std::string target;
  std::uint32_t logical_record = 0;
  std::vector<std::size_t> payload_tokens;  // ascending
  DocumentSourceSliceIR source;
};

struct ProseMenuItemIR {
  std::string target;
  std::string label;
  DocumentSourceSliceIR source;
};

struct ProseTopicIR {
  std::size_t record_count = 0;
  std::size_t token_count = 0;
  std::string heading_level;
  std::string title;
  DocumentSourceSliceIR title_source;
  std::vector<ProseAnchorIR> anchors;
  std::vector<ProseBlockIR> blocks;
  std::vector<ProseIndexTermIR> index_terms;
  std::vector<ProseMenuItemIR> menu_items;
  // Block spans in source order, and the typed blocks they address.  The
  // block sets carry the extractors' decline lists so the block verifiers
  // can re-check them; an admitted topic has no declines other than figure
  // regions that lie inside an admitted table.
  std::vector<ProseSpanIR> spans;
  std::vector<ProseTableLinkIR> table_links;
  FixedTableBlocksIR tables;
  FigureBlocksIR figures;
  std::vector<ProseTokenDispositionIR> ledger;
};

// `title` is the canonical TOC title the ST payload must agree with.  The
// book topic catalog is optional; without it any trailing menu rejects.
// `resource_ids` are the book's resource catalog ids (lower-cased) so a
// figure's `PIC<n>` selector can be proven to address a stored picture;
// without them every book-resource figure region declines and the topic
// fails closed.
std::optional<ProseTopicIR> extract_prose_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const std::string& title, const BookTopicCatalogIR* book_topic_catalog,
    std::string* error = nullptr,
    const std::set<std::string>* resource_ids = nullptr);
bool verify_prose_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const std::string& title, const BookTopicCatalogIR* book_topic_catalog,
    const ProseTopicIR& topic, std::string* error = nullptr,
    const std::set<std::string>* resource_ids = nullptr);
std::string format_prose_topic_ir(const ProseTopicIR& topic);
const char* prose_token_role_name(ProseTokenRoleIR role);

} // namespace geist::detail
