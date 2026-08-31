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
// fixed-row BookManager dialect (see doc/boo-spec/markup.adoc, "Flattened fixed
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
  ordinal,         // explicit item number of a CZ ordered-list row (`1.`)
  table,           // claimed by a table span (fixed-table block)
  figure,          // claimed by a figure span (figure block)
};

struct ProseTokenRefIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
};

// One inline's claim on a text token.  A token is normally claimed whole by
// exactly one inline (`character_begin == 0` and `character_end` the length of
// the token's decoded word); a token whose display columns a CFONT/CSELECT
// span splits is claimed by two or more inlines, each owning one byte range of
// the decoded word, contiguous and in order (doc/boo-spec/markup.adoc, "Spans And The
// Display Row").
struct ProseInlineClaimIR {
  std::size_t block = static_cast<std::size_t>(-1);
  std::size_t inline_index = static_cast<std::size_t>(-1);
  std::uint32_t character_begin = 0;
  std::uint32_t character_end = 0;
};

struct ProseTokenDispositionIR {
  ProseTokenRefIR token;
  ProseTokenRoleIR role = ProseTokenRoleIR::unassigned;
  // Owning block/inline for text cells; npos otherwise.  For a token split
  // between inlines these name the first claim; `claims` carries them all.
  std::size_t block = static_cast<std::size_t>(-1);
  std::size_t inline_index = static_cast<std::size_t>(-1);
  // Source order, contiguous, covering the token's decoded word exactly.
  std::vector<ProseInlineClaimIR> claims;
  // Owning span (index into ProseTopicIR::spans) for table/figure tokens;
  // npos otherwise.
  std::size_t span = static_cast<std::size_t>(-1);
};

enum class ProseInlineKindIR {
  text,
  emphasis,
  cross_reference,
  // A `PIC<n>` selector covering the `PICTURE n` placeholder columns of a
  // prose display row: the image is placed inside the sentence, `target` is
  // the resource catalog id and `text` is the placeholder the image
  // replaces, which hosted BookServer also uses as its `alt` text.
  image,
  // A display-row boundary the block keeps instead of reflowing across.
  // Emitted only by the generated title-page projection (`CZ OFF COVER` /
  // `CZ OFF TIPAGE`), whose rows hosted BookServer serves one per line
  // inside one paragraph.  It carries no text and no source slice of its
  // own: the boundary is the framing between two rows, not a token.
  line_break,
};

struct ProseInlineIR {
  ProseInlineKindIR kind = ProseInlineKindIR::text;
  std::string text;
  FontStyleIR style = FontStyleIR::unknown;
  std::string target;
  // Cross references only.  An internal `CSELECT <anchor>` is an anchor in
  // the same book; a `CSELECT ... LNK` selector addresses another book or an
  // external URL (see doc/boo-spec/markup.adoc, "LNK selector alternatives").
  CrossReferenceTargetKindIR target_kind = CrossReferenceTargetKindIR::anchor;
  // Contiguous source token ranges, in source order.
  std::vector<DocumentSourceSliceIR> slices;
};

// The `CZ` dialect (doc/boo-spec/markup.adoc, "CZ layout directives") adds the
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
  // Preformatted blocks: the display rows verbatim, at the display columns
  // they occupy -- the region's own left margin is kept, as hosted
  // BookServer keeps it inside `<pre>` -- with blank rows kept; `inlines`
  // then holds one text inline per non-blank row for provenance.
  std::vector<std::string> preformatted_lines;
  // Machine-readable name of the verbatim region kind when the block is
  // preformatted for a reason other than a source `cz OFF XMP`/`SCREEN`
  // declaration: a drawn box region sets `prose-drawn-box-verbatim`.  Both
  // render the same way -- character art the compiler rasterized at build
  // time, reproduced line for line as hosted BookServer serves it inside
  // `<pre>` -- so this names the region for consumers and provenance and no
  // longer degrades the topic's render severity.
  std::string verbatim_kind;
  // Preformatted blocks: row -> index into `inlines`, or `npos` for a blank
  // row, which is a display-line break owning no token.  Parallel to
  // `preformatted_lines`.
  std::vector<std::size_t> preformatted_line_inlines;
  // Issue #81.  Set only when the block is emitted verbatim because its
  // structure could not be proven, never when verbatim is what the source
  // states: a `cz OFF XMP` example and a drawn box are preformatted *by
  // right* and stay clean.  A non-empty code makes the lowered node
  // `DocumentFidelityIR::degraded`, which is what moves the topic to
  // `typed-degraded` and puts a marker on the fence in the rendered file.
  std::string degradation_code;
  std::string degradation_detail;
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
  // Canonical destination and its kind.  An in-book `CSELECT <anchor>` is an
  // anchor; a `LNK` selector in a cell carries the same alternative list as
  // an inline one (selector_link_ir.hpp) and resolves to an external
  // destination -- GG24-395 3.3.6 `TBLUNIQ12` cell `IBM AIX Version 3.2 ...`
  // is served as `<a href="../../DOCNUM/SC23-2456/CCONTENTS">`.
  std::string target;
  CrossReferenceTargetKindIR target_kind = CrossReferenceTargetKindIR::anchor;
  // A `PIC<n>` selector: `target` is the resource catalog id and the cell
  // line it covers is an image, not a link.  Hosted BookServer serves the
  // cell as `<a href="picture-16?mode=zoom"><img ... alt="PICTURE 16"></a>`
  // (GX27-3999-00 3.2 `NOSENV2`, DT 19950730184057), replacing exactly the
  // `PICTURE 16` placeholder words the compiler wrote into the cell.
  bool picture = false;
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
  // The raw lower-cased CHDLEVEL operand this level was proven from: `h1`..
  // `h6`, or one of the front-matter forms (`cover`, `toc`, `preface`, ...)
  // the reader serves as a level-1 heading.
  std::string heading_form;
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
    const LayoutIR& layout, const VerifiedOwnershipIR& ownership,
    const std::string& title, const BookTopicCatalogIR* book_topic_catalog,
    std::string* error = nullptr,
    const std::set<std::string>* resource_ids = nullptr);
bool verify_prose_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const VerifiedOwnershipIR& ownership,
    const std::string& title, const BookTopicCatalogIR* book_topic_catalog,
    const ProseTopicIR& topic, std::string* error = nullptr,
    const std::set<std::string>* resource_ids = nullptr);
std::string format_prose_topic_ir(const ProseTopicIR& topic);
const char* prose_token_role_name(ProseTokenRoleIR role);
const char* prose_block_kind_name(ProseBlockKindIR kind);

} // namespace geist::detail
