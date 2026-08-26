#pragma once

#include "geist/detail/provenance_ir.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace geist::detail {

struct TextInlineIR {
  std::string text;
};

struct EmphasisInlineIR {
  std::string text;
};

struct CodeInlineIR {
  std::string code;
};

struct CrossReferenceInlineIR {
  std::string target;
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
                 FootnoteBlockIR, IndexGroupBlockIR, OpaqueBlockIR,
                 LegacyGmlRegionIR>;

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
