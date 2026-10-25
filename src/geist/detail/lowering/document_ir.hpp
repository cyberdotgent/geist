// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/container/provenance_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace geist::detail {

struct TextInlineIR {
  std::string text;
};

enum class EmphasisKindIR {
  emphasis,
  strong,
  strong_emphasis,
};

// The phrase a styled run names, beyond its emphasis or code kind: the
// `CFONT` code hosted BookServer serves in an element of its own -- `E`
// (XMP) `<samp>`, `P` (PK) `<kbd>`, `C` (CIT) `<cite>`, `V` (PV) `<var>`,
// `Q` (PKDEF) `<dfn>` (doc/boo-spec/markup.adoc, the message-entry,
// variable and defined-keyword censuses).  `none` is every other run.  It
// refines the presentation the kind already states and never changes it: a
// sample, keyboard or definition phrase is code, a citation or variable is
// emphasis.  HTML picks
// its element by it; Markdown has no such elements and ignores it.
enum class PhraseKindIR {
  none,
  sample,
  keyboard,
  citation,
  variable,
  definition,
};

// Typed inline emphasis. The kind is a presentation fact carried from typed
// font provenance (HP1/HP2/HP3); renderers choose the target syntax.
struct EmphasisInlineIR {
  std::string text;
  EmphasisKindIR kind = EmphasisKindIR::emphasis;
  PhraseKindIR phrase = PhraseKindIR::none;
};

struct CodeInlineIR {
  std::string code;
  PhraseKindIR phrase = PhraseKindIR::none;
};

// Typed underline: a CFONT `_` (UNDERSCORE) run, which hosted BookServer
// serves as `<u>` with every blank column of the run drawn as `_`.  The text
// already carries those underscores; renderers choose the underline syntax.
struct UnderlineInlineIR {
  std::string text;
};

enum class CrossReferenceTargetKindIR {
  topic,
  anchor,
  resource,
  external,
  book_contents,
  book_heading,
  book_spot,  // a spot (`SRSPT<id>`) inside another book
  book_anchor,  // a figure or table (`SRFIG<id>`, `SRTBL<id>`) of another book
};

struct CrossReferenceTargetIR {
  CrossReferenceTargetIR() = default;
  CrossReferenceTargetIR(CrossReferenceTargetKindIR kind_value,
                         std::string value_value)
      : kind(kind_value), value(std::move(value_value)) {}

  CrossReferenceTargetKindIR kind = CrossReferenceTargetKindIR::topic;
  std::string value;
  // The complete LNK tuple, without angle brackets. Empty for ordinary
  // in-book selectors.
  std::vector<std::string> alternatives;
  std::string document_number;
  std::string document_level;
  std::string heading_anchor;
  // Alternative 2 of a `<SPT>` reference: the spot anchor inside the other
  // book, which hosted BookServer addresses as `SPT<anchor>`.
  std::string spot_anchor;
  // A `<FIG>` or `<TBL>` reference's destination inside the other book, as
  // hosted BookServer addresses it: alternative 1 then alternative 2
  // (`FIGLSA1`, `TBLDFHE503`).
  std::string book_anchor;
  std::string url;
};

struct CrossReferenceTargetClaimIR {
  CrossReferenceTargetIR target;
  // The control bytes that assert this destination. This is separate from
  // InlineIR::origin, which owns the one visible label shared by all claims.
  std::optional<DocumentNodeOriginIR> control_origin;
};

struct ImageInlineIR {
  std::string resource;
  std::string alt_text;
  // Set only when the image stands inside a styled preformatted row: the
  // row cells it occupies, exactly as the source row spells them (a picture
  // selector's covered columns, ` PICTURE 23`).  The row still flattens to
  // its literal source text through them, which is the block's geometry
  // rule.  Hosted BookServer draws the image at the first of these columns
  // and prints every one of them blank, so the rest of the row -- a frame's
  // right rail -- keeps its place after the image (G544-3305-00 2.3.1).
  // Empty for an image in running text, where it occupies no cell.
  std::string cells = {};
};

struct CrossReferenceInlineIR {
  CrossReferenceInlineIR() = default;
  CrossReferenceInlineIR(CrossReferenceTargetIR target_value,
                         std::string label_value)
      : targets({{std::move(target_value), std::nullopt}}),
        label(std::move(label_value)) {}

  // Ordered selector claims over this one displayed run. Several controls
  // may intentionally cover the same cells; none is deduplicated.
  std::vector<CrossReferenceTargetClaimIR> targets;
  std::string label;
  // Presentation proven for this label's exact source run. Selectors and
  // CFONT instructions make independent, overlapping claims; retaining the
  // destination must not flatten the font claim. Adjacent runs may name the
  // same target when only part of one selector label is styled.
  std::optional<EmphasisKindIR> label_emphasis;
  bool label_code = false;
  // The phrase the label's run names, as on `CodeInlineIR`/`EmphasisInlineIR`.
  PhraseKindIR label_phrase = PhraseKindIR::none;
  // An image drawn at the start of the link's content, before the label.  A
  // `LNK <IMAGE>` selector in running prose places one there (XWEBDEMO 1.0;
  // doc/boo-spec/markup.adoc, "LNK selector alternatives").  Like a figure's
  // external image, `resource` is the URL the source spells.
  std::optional<ImageInlineIR> label_image;
};

struct HardBreakInlineIR {};

// Permanent escape hatch for understood-but-unrepresentable inline content.
struct OpaqueInlineIR {
  std::string kind;
  std::string content;
};

struct InlineIR;

// Text the source draws on display lines that carry a revision mark in their
// margin.  The book compiler resolved SCRIPT revision codes and drew the
// mark's character in cell 1 of each revised row (doc/boo-spec/markup.adoc,
// "The three-column left margin"); the character is the only trace the file
// keeps.  A paragraph is often revised in part, so the mark wraps the inlines
// of the marked rows rather than flagging a whole block.  `children` holds
// ordinary inlines, never another revised run.  Hosted BookServer prints the
// mark in the row's margin (` | Second Edition ...`); HTML marks the run,
// Markdown has no margin and prints the children only.
struct RevisedInlineIR {
  char32_t mark = 0;
  std::vector<InlineIR> children;
};

using InlineNodeIR =
    std::variant<TextInlineIR, EmphasisInlineIR, CodeInlineIR,
                 CrossReferenceInlineIR, ImageInlineIR, HardBreakInlineIR,
                 OpaqueInlineIR, UnderlineInlineIR, RevisedInlineIR>;

struct InlineIR {
  InlineNodeIR node;
  DocumentNodeOriginIR origin;
};

using InlineSequenceIR = std::vector<InlineIR>;

struct HeadingBlockIR {
  std::uint32_t level = 1;
  InlineSequenceIR content;
  // The revision mark the topic's `ST` line stores in its revision column,
  // the cell glued directly after the opcode (doc/boo-spec/
  // logical-controls.adoc, the title line), as stored: `|`, `#`, `U+2502`.
  // Hosted BookServer prints it in front of the topic number, never inside
  // the title: SC41-5401-00 2.1.2.5 `<H3>| 2.1.2.5   CSLBUFSIZE ...</H3>`.
  std::optional<char32_t> revision_mark = std::nullopt;
};

struct ParagraphBlockIR {
  InlineSequenceIR content;
};

// What a named destination in the source is, which decides how a book-wide
// link map resolves references to it.  The legacy GML projection stated this
// by using a different tag for each -- `:anchor id=`, `:fig id=`, `:table
// id=` -- and stated nothing at all for a footnote, whose `SRFTN` control
// produces no record.  The typed IR lowers all four to an anchor block, so
// the distinction has to travel with the node instead.
enum class AnchorRoleIR {
  // `SR<id>`: a destination other topics reference by that id.  Resolves to
  // this topic's file, with no fragment.
  cross_reference,
  // `SRFIG<id>`: the anchor id is `FIG` + the id cross references name.
  figure,
  // `SRTBL<id>`: the anchor id is `TBL` + the object id.
  table,
  // Reachable only from inside the topic that emits it (`SRFTN<id>`), or a
  // second spelling of a destination another anchor already names.
  local,
};

struct AnchorBlockIR {
  std::string id;
  AnchorRoleIR role = AnchorRoleIR::cross_reference;
};

// A source-native horizontal separator. Its cells are structural rather than
// text: keeping it as a block lets renderers express the rule without
// fabricating a preformatted row or losing the row's source coordinates.
struct ThematicBreakBlockIR {};

struct BlockIR;
using BlockSequenceIR = std::vector<BlockIR>;

struct ListItemIR {
  InlineSequenceIR content;
  DocumentNodeOriginIR origin;
  // An explicit source ordinal is distinct from the item's position in this
  // vector. It permits ordered lists that start above one or contain gaps.
  // The trailing field preserves existing two-member aggregate callers.
  std::optional<std::uint64_t> source_ordinal = std::nullopt;
  // Nesting level inside the same list block, 0 for a top-level item.  A
  // source that states the nesting of every item as a number -- the generated
  // table of contents (`CTOCE <depth> ...`) and the generated index (`CITERM
  // <D>term<D><level>...`) -- keeps one list block whose items carry their own
  // depth, rather than a tree the source never draws.
  std::uint32_t depth = 0;
  // The source wrote this item with no text of its own, and the lowering says
  // so rather than leaving an unexplained empty item behind.  A list item
  // carries content unless it declares this, so the strict rule stands
  // everywhere it is not declared, and an item that declares it may carry no
  // inline at all.  Its one use is the generated index of a book that states
  // an entry whose term field is empty (`CITERM <D><D><level>`): the entries
  // below it are its children, so it can be neither dropped -- that would
  // reparent them -- nor given text the line does not carry.
  bool empty_content = false;
  // Most list items are one inline run.  When the source carries paragraph
  // boundaries or nested block structure, `blocks` owns that structure and
  // `content` is empty.  The verifier enforces the exclusive representation.
  BlockSequenceIR blocks = {};
  // Text the source printed in front of the item as its own label, kept
  // apart from the list's generated number.  A CZ ordered-list head of
  // several words (`Step 1.`, `__ Step a.`) loses its first word in the
  // reader and keeps the rest as text in front of the item's content
  // (doc/boo-spec/markup.adoc, "List item heads").  Empty for every other
  // item; when set, it is serialized before `content` or the first block.
  InlineSequenceIR label = {};
  // Every display row of the item carries this revision mark in its margin
  // (see `RevisedInlineIR`); the item's content then holds no revised run of
  // its own.
  std::optional<char32_t> revision_mark = std::nullopt;
};

enum class OrderedListMarkerIR {
  decimal,
  lower_alpha,
  upper_alpha,
};

struct ListBlockIR {
  bool ordered = false;
  std::vector<ListItemIR> items;
  // The source label alphabet is semantic content. CZ ordered lists can use
  // `a.`/`b.` as well as `1.`/`2.`; keeping only the numeric position loses
  // the visible label in both serializers.
  OrderedListMarkerIR marker = OrderedListMarkerIR::decimal;
};

struct DefinitionEntryIR {
  InlineSequenceIR term;
  InlineSequenceIR definition;
  DocumentNodeOriginIR origin;
  // A definition may contain a sequence of paragraphs, lists, tables, and
  // other non-heading blocks.  This is exclusive with `definition`.
  BlockSequenceIR definition_blocks = {};
  // The source explicitly supplied an empty definition term.  This is not
  // inferred from an accidentally empty inline sequence; the lowering must
  // opt in from source-framed evidence such as an empty CZ DT control.
  bool empty_term = false;
};

struct DefinitionListBlockIR {
  std::vector<DefinitionEntryIR> entries;
};

// A zero-width named destination the source opens at one display line of a
// table cell: `line` counts the cell's content lines (`HardBreakInlineIR`s
// in `content`) before it, and equals that count when the anchor opens a
// line the cell leaves blank after its last line.  Hosted BookServer opens
// an `SR<id>` that stands between the rows of a declared table in the first
// cell of its row, at its display line (SC31-8569-00 10.2
// `<td ...><a name="SPTNEWRUS"> API INQPER </a></td>`).
struct TableCellAnchorIR {
  std::size_t line = 0;
  AnchorBlockIR anchor;
  DocumentNodeOriginIR origin;
};

struct TableCellIR {
  InlineSequenceIR content;
  DocumentNodeOriginIR origin;
  // A cell with compound source content uses blocks instead of `content`.
  // Both may be empty for a genuinely empty cell.
  BlockSequenceIR blocks = {};
  // Ordered by line; only on a cell whose content is inline.
  std::vector<TableCellAnchorIR> line_anchors = {};
};

struct TableRowIR {
  std::vector<TableCellIR> cells;
  DocumentNodeOriginIR origin;
  // A display line of the row carries this revision mark in the margin, one
  // cell left of the box's border (doc/boo-spec/markup.adoc, "Revision bar
  // beside a box table").
  std::optional<char32_t> revision_mark = std::nullopt;
};

struct TableBlockIR {
  std::vector<TableRowIR> rows;
  std::size_t header_rows = 0;
};

// A cross reference carried *inside* a preformatted row.
//
// Both routes that reproduce drawn rows character for character need
// this: the verbatim route (a topic no family claimed) and the typed
// figure body (character art the compiler rasterized).  Hosted
// BookServer serves both as `<pre>` with the anchors *inside* the rows,
// so the node is shared rather than duplicated per route.

enum class VerbatimLinkKindIR {
  in_book,       // an `SR<id>` anchor somewhere in this book
  book_contents, // another book's contents page
  book_heading,  // a heading inside another book
  external_url,  // a URL the reader opens
};

// One cross reference, as a half-open byte range of its row plus everything
// the source says about where it points.  The range is the only thing the
// row itself knows about; the destination is a question for the backend.
struct VerbatimLinkIR {
  std::size_t begin = 0;
  std::size_t end = 0;
  VerbatimLinkKindIR kind = VerbatimLinkKindIR::in_book;
  // In-book: the anchor id the selector names.  Cross-book and external:
  // alternative 6, the target's own identifier.
  std::string target;
  // The `LNK` alternative list verbatim, without its angle brackets, all six
  // (or seven) fields in source order; empty for an in-book reference.  The
  // node carries the whole list whatever Markdown does with it, so #46 can
  // resolve a cross-book reference through a caller-supplied resolver
  // instead of re-reading the source.
  std::vector<std::string> alternatives;
  // Alternative 4: the order number of the book referenced.
  std::string document_number;
  // Alternative 5: the `DocnumLevel` a live BookServer appends to the
  // destination, which it uses to offer a revision picker.  It is not
  // addressing inside the target book.
  std::string document_level;
  // Alternative 2: the heading anchor of a `<HDR>` reference, which the
  // reader prefixes with `HDR`.
  std::string heading_anchor;
  // The absolute URL of an external reference; empty otherwise.  This is the
  // only cross-book destination a single-book Markdown export can prove.
  std::string url;
};

struct VerbatimRowIR {
  std::string text;
  // Disjoint, in column order.
  std::vector<VerbatimLinkIR> links;
};

// A zero-width named destination that the source opens immediately before a
// particular preformatted row. Keeping it on the row ledger preserves one
// continuous drawing; splitting the body into blocks would add boundaries
// the source does not contain.
struct PreformattedAnchorIR {
  std::size_t line = 0;
  AnchorBlockIR anchor;
  DocumentNodeOriginIR origin;
};

struct PreformattedBlockIR {
  std::vector<std::string> lines;
  // Optional per-line provenance.  Either empty, meaning the block's own
  // origin is the finest source coordinate available, or exactly one origin
  // per line.  A positioned display rectangle knows which record and tokens
  // each of its lines came from, and a reader hunting a rendering fault needs
  // that line, not the whole region.
  std::vector<DocumentNodeOriginIR> line_origins;
  // Optional per-line cross references, as byte ranges of the matching entry
  // of `lines`.  Either empty -- the block carries none, and a renderer emits
  // it as a fence exactly as before -- or exactly one (possibly empty) entry
  // per line.
  //
  // A drawn row can carry a link: hosted BookServer wraps the marked columns
  // of a figure body in an `<a href>` *inside* its `<pre>`, leaving every
  // other column where it was.  A fence cannot express that, so a block that
  // has any link is rendered as a `<pre>` instead -- the same form the
  // verbatim route uses -- and the rows keep their own bytes either way.
  std::vector<std::vector<VerbatimLinkIR>> line_links;
  // Sorted by line. Multiple distinct anchors may open the same line in
  // source order. Each is emitted immediately before that row inside the
  // same `<pre>` element.
  std::vector<PreformattedAnchorIR> line_anchors;
  // Optional styled, source-positioned rows. When present, every inline row
  // flattens byte-for-byte to the matching entry in `lines`, including its
  // spaces, and has a matching line origin. Links live in these inlines,
  // never simultaneously in `line_links`. No inline may create another row
  // or replace a source character with an opaque rendering.  An image may
  // stand in a row only over cells of its own (`ImageInlineIR::cells`): the
  // picture placeholder columns of a drawn frame, which it flattens back to.
  std::vector<InlineSequenceIR> styled_lines;
};

struct NoteBlockIR {
  InlineSequenceIR label;
  InlineSequenceIR content;
};

struct PublicationEntryBlockIR {
  InlineSequenceIR title;
  std::vector<InlineSequenceIR> paragraphs;
  DocumentNodeOriginIR origin;
};

struct PublicationListBlockIR {
  std::vector<PublicationEntryBlockIR> entries;
};

struct FigureBlockIR {
  std::string resource;
  InlineSequenceIR caption;
  // The book's own description of the picture (BookMaster `:artdesc`,
  // carried by the BUILD 1.3 `cartdesc` lines), for a reader that cannot
  // show it.  Empty when the book gave none; a renderer then names the
  // picture as hosted BookServer does.
  std::string description;
};

struct FootnoteBlockIR {
  std::string id;
  InlineSequenceIR content;
};

// One entry of a reader-generated subtopic menu (BOO `CMITEM <id> <text>`).
// The target keeps the raw topic identity; `label` is the source-proven
// visible text without any reader-added prefix.
struct MenuBlockItemIR {
  CrossReferenceTargetIR target;
  std::string label;
  DocumentNodeOriginIR origin;
};

// A reader-generated subtopic menu (BOO `CMENU` ... `CEMENU`).  The reader
// synthesizes the `Subtopics:` lead line and prefixes every visible label with
// its target topic id at render time; neither exists in BOO source, so this
// block carries only the typed items and each renderer expands that
// presentation itself.
struct MenuBlockIR {
  std::vector<MenuBlockItemIR> items;
};

struct IndexEntryIR {
  InlineSequenceIR term;
  std::string target;
  DocumentNodeOriginIR origin;
};

struct IndexGroupBlockIR {
  InlineSequenceIR heading;
  std::vector<IndexEntryIR> entries;
};

// Permanent escape hatch for understood-but-unrepresentable block content.
// It must carry a stable source-format kind; renderers decide whether and how
// that kind can be represented in their target format.
struct OpaqueBlockIR {
  std::string kind;
  std::string content;
};

using BlockNodeIR =
    std::variant<HeadingBlockIR, ParagraphBlockIR, AnchorBlockIR,
                 ThematicBreakBlockIR, ListBlockIR, DefinitionListBlockIR,
                 TableBlockIR, PreformattedBlockIR, NoteBlockIR,
                 PublicationListBlockIR, FigureBlockIR, FootnoteBlockIR,
                 IndexGroupBlockIR, MenuBlockIR, OpaqueBlockIR>;

struct BlockIR {
  BlockNodeIR node;
  DocumentNodeOriginIR origin;
};

struct TopicIdentityIR {
  std::string id;
  std::string title;
  std::string heading_level;
  std::uint32_t topic_number = 0;
  std::uint32_t start_logical_record = 0;
  std::uint32_t end_logical_record = 0;
};

struct DocumentIR {
  TopicIdentityIR topic;
  std::vector<BlockIR> blocks;
  // Ids the source names this whole topic by, over and above the anchors the
  // document places.  BookManager lets one topic carry several named
  // destinations that all mean "this topic": N2AH1MST record 385 spells
  // `SRMSG AMD083I` and `SRSPTE083I` side by side, and a cross reference may
  // use either.  A reference to one of these resolves to the topic, so the
  // document does not have to place it anywhere -- keeping them out of
  // `blocks` is what stops them being rendered as anchors nobody links into.
  std::vector<std::string> named_destinations;
};

// Lifts every child node's source slices into the container that holds it, so
// a block, list item, table row, or catalog entry names at least the BOO
// bytes its own content names.  Slices are merged into the fewest ordered
// contiguous ranges, and a sub-token slice widens to its whole token when it
// is lifted.  A container that states it was `synthesized` is left alone: a
// node with no source of its own must not acquire one.  Lowerings call this
// before verification; it never changes rendered output.
void normalize_document_origin_slices(DocumentIR& document);

// The text a renderer prints for a stored revision mark: the box-drawing
// vertical `U+2502` is printed `|`, as hosted BookServer's code-page
// conversion prints it (ACPZMST1 5.4 `<H2>| 5.4 ...`); any other mark is its
// own character, UTF-8 encoded.
std::string revision_mark_text(char32_t mark);

// True when `mark` is a character a revision column can draw: printable,
// not a blank, not the decoder's unmapped word.
bool valid_revision_mark(char32_t mark);

// `inlines` with every revised run replaced by its children, in place: what a
// target without a margin (Markdown) prints.
InlineSequenceIR flatten_revised_inlines(const InlineSequenceIR& inlines);

// The cells an image occupies in a preformatted row, printed blank: one
// space per column of `ImageInlineIR::cells` (a UTF-8 sequence is one
// column).  Empty for an image in running text.
std::string blank_image_cells(const ImageInlineIR& image);
bool has_revised_inline(const InlineSequenceIR& inlines);

bool verify_document_ir(const DocumentIR& document,
                        std::string* error = nullptr);
std::string format_document_ir(const DocumentIR& document);

} // namespace geist::detail
