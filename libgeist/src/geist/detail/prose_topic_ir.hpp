#pragma once

#include "geist/detail/control_ir.hpp"
#include "geist/detail/document_ir.hpp"
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
  menu,            // CMENU/CMITEM/CEMENU tokens (validated separately)
  ordinal,         // explicit item number of a CZ ordered-list row (`1.`)
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
  // Cross references only.  An internal `CSELECT <anchor>` is an anchor in
  // the same book; a `CSELECT ... LNK` selector addresses another book or an
  // external URL (see Format/markup.md, "LNK selector alternatives").
  CrossReferenceTargetKindIR target_kind = CrossReferenceTargetKindIR::anchor;
  // Contiguous source token ranges, in source order.
  std::vector<DocumentSourceSliceIR> slices;
};

// The `CZ` dialect (Format/markup.md, "CZ layout directives") adds the
// explicit block kinds below; the flattened dialect only produces paragraphs
// and list items.
enum class ProseBlockKindIR {
  paragraph,
  list_item,
  definition_entry,  // `CZ FLOW DT`: term inlines, then definition inlines
  heading,           // `CZ FLOW H2`..`H5` with visible text
  note,              // `CZ FLOW NT` / `NOTE`: label inlines, then content
  footnote,          // `CZ FLOW FN` between `SRFTN<id>` and `SREFTN`
  preformatted,      // `CZ OFF XMP` .. `CZ OFF EXMP`: verbatim display rows
};

struct ProseBlockIR {
  ProseBlockKindIR kind = ProseBlockKindIR::paragraph;
  // Consecutive list items (or definition entries) sharing a list ordinal
  // form one list.
  std::size_t list_ordinal = 0;
  std::size_t origin = 0;
  // List items of an ordered list (`CZ FLOW OL` / `NOTEL`); `ordinal` is the
  // explicit source number text (`1.`) when the row carries one.
  bool ordered = false;
  std::string ordinal;
  // Heading level (2..5) of a heading block.
  std::size_t heading_level = 0;
  // Footnote anchor id of a footnote block.
  std::string anchor_id;
  // The first `term_inline_count` inlines form the term (definition entry)
  // or the label (note); the remaining inlines are the body.
  std::size_t term_inline_count = 0;
  // Preformatted blocks: the display rows verbatim (common indent removed,
  // blank rows kept); `inlines` then holds one text inline per non-blank
  // row for provenance.
  std::vector<std::string> preformatted_lines;
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
const char* prose_block_kind_name(ProseBlockKindIR kind);

} // namespace geist::detail
