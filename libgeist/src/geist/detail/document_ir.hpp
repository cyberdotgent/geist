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

struct AnchorBlockIR {
  std::string id;
};

struct ListItemIR {
  InlineSequenceIR content;
  DocumentNodeOriginIR origin;
  // An explicit source ordinal is distinct from the item's position in this
  // vector. It permits ordered lists that start above one or contain gaps.
  // The trailing field preserves existing two-member aggregate callers.
  std::optional<std::uint64_t> source_ordinal = std::nullopt;
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

// A reader-generated subtopic menu (BOO `CMENU` ... `CEMENU`).  BookServer
// synthesizes the `Subtopics:` lead line and prefixes every visible label with
// its target topic id at render time (`bookmgr.exe` `sub_405FC`, see
// Format/markup.md); neither exists in BOO source, so this block carries only
// the typed items and each renderer expands that presentation itself.
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
};

bool verify_document_ir(const DocumentIR& document,
                        std::string* error = nullptr);
std::string format_document_ir(const DocumentIR& document);

} // namespace geist::detail
