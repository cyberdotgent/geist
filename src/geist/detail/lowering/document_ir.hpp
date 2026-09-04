// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/container/provenance_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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

// Typed inline emphasis. The kind is a presentation fact carried from typed
// font provenance (HP1/HP2/HP3); renderers choose the target syntax.
struct EmphasisInlineIR {
  std::string text;
  EmphasisKindIR kind = EmphasisKindIR::emphasis;
};

struct CodeInlineIR {
  std::string code;
};

enum class CrossReferenceTargetKindIR {
  topic,
  anchor,
  resource,
  external,
};

struct CrossReferenceTargetIR {
  CrossReferenceTargetKindIR kind = CrossReferenceTargetKindIR::topic;
  std::string value;
};

struct CrossReferenceInlineIR {
  CrossReferenceTargetIR target;
  std::string label;
};

struct ImageInlineIR {
  std::string resource;
  std::string alt_text;
};

struct HardBreakInlineIR {};

// Permanent escape hatch for understood-but-unrepresentable inline content.
struct OpaqueInlineIR {
  std::string kind;
  std::string content;
};

using InlineNodeIR =
    std::variant<TextInlineIR, EmphasisInlineIR, CodeInlineIR,
                 CrossReferenceInlineIR, ImageInlineIR, HardBreakInlineIR,
                 OpaqueInlineIR>;

struct InlineIR {
  InlineNodeIR node;
  DocumentNodeOriginIR origin;
};

using InlineSequenceIR = std::vector<InlineIR>;

struct HeadingBlockIR {
  std::uint32_t level = 1;
  InlineSequenceIR content;
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
};

struct ListBlockIR {
  bool ordered = false;
  std::vector<ListItemIR> items;
};

struct DefinitionEntryIR {
  InlineSequenceIR term;
  InlineSequenceIR definition;
  DocumentNodeOriginIR origin;
};

struct DefinitionListBlockIR {
  std::vector<DefinitionEntryIR> entries;
};

struct TableCellIR {
  InlineSequenceIR content;
  DocumentNodeOriginIR origin;
};

struct TableRowIR {
  std::vector<TableCellIR> cells;
  DocumentNodeOriginIR origin;
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
    std::variant<HeadingBlockIR, ParagraphBlockIR, AnchorBlockIR, ListBlockIR,
                 DefinitionListBlockIR, TableBlockIR, PreformattedBlockIR,
                 NoteBlockIR, PublicationListBlockIR, FigureBlockIR,
                 FootnoteBlockIR, IndexGroupBlockIR, MenuBlockIR,
                 OpaqueBlockIR>;

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

bool verify_document_ir(const DocumentIR& document,
                        std::string* error = nullptr);
std::string format_document_ir(const DocumentIR& document);

} // namespace geist::detail
