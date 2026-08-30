#pragma once

#include "geist/detail/provenance_ir.hpp"

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
// This is distinct from LegacyGmlRegionIR, which is temporary migration debt.
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

struct PreformattedBlockIR {
  std::vector<std::string> lines;
  // Optional per-line provenance.  Either empty, meaning the block's own
  // origin is the finest source coordinate available, or exactly one origin
  // per line.  A positioned display rectangle knows which record and tokens
  // each of its lines came from, and a reader hunting a rendering fault needs
  // that line, not the whole region.
  std::vector<DocumentNodeOriginIR> line_origins;
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

enum class LegacyRendererStateScopeIR {
  whole_topic,
};

// Temporary migration boundary.  The records must be passed to the legacy
// renderer together and in order so its cross-record state remains intact.
struct LegacyGmlRegionIR {
  std::vector<std::string> normalized_records;
  LegacyRendererStateScopeIR state_scope =
      LegacyRendererStateScopeIR::whole_topic;
};

using BlockNodeIR =
    std::variant<HeadingBlockIR, ParagraphBlockIR, AnchorBlockIR, ListBlockIR,
                 DefinitionListBlockIR, TableBlockIR, PreformattedBlockIR,
                 NoteBlockIR, PublicationListBlockIR, FigureBlockIR,
                 FootnoteBlockIR, IndexGroupBlockIR, MenuBlockIR,
                 OpaqueBlockIR, LegacyGmlRegionIR>;

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
