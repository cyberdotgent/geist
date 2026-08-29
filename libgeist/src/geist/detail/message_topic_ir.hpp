#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/detail/message_ir.hpp"

#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct MessageTopicMetadataIR {
  std::string raw_topic_id;
  std::string topic_number;
  std::string parent;
  std::string forward_level;
  std::string back_level;
  std::string summary;
  std::string heading_level;
  std::string source_file;
};

struct MessageTopicCellIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  std::uint16_t word = 0;
  SourceDisposition disposition = SourceDisposition::opaque;
};

// A lossless physical display row. Marker slots remain separate from visible
// text because deciding whether they are punctuation, lexical carry, or layout
// is a semantic/lowering decision.
struct MessageTopicRowIR {
  std::string visible_text;
  std::optional<MarkerSlotIR> marker;
  std::size_t native_origin = 0;
  PhysicalBreakKind break_before = PhysicalBreakKind::unknown;
  DocumentSourceRowIR source_row;
  DocumentSourceSliceIR source;
  std::vector<MessageTopicCellIR> cells;
};

struct MessageTopicAnchorIR {
  std::string id;
  DocumentSourceSliceIR source;
};

struct MessageTopicSelectorIR {
  CrossReferenceTargetIR target;
  std::string display_payload;
  std::size_t column = 0;
  std::size_t length = 0;
  DocumentSourceSliceIR source;
};

enum class MessageIntroductionCellRoleIR {
  text,
  selector,
  layout,
  paragraph_break,
};

struct MessageIntroductionCellIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  std::uint16_t word = 0;
  SourceDisposition source_disposition = SourceDisposition::opaque;
  MessageIntroductionCellRoleIR role = MessageIntroductionCellRoleIR::layout;
  std::size_t introduction_row = 0;
};

enum class MessageIntroductionAtomKindIR {
  text,
  selector,
};

struct MessageIntroductionAtomIR {
  MessageIntroductionAtomKindIR kind = MessageIntroductionAtomKindIR::text;
  std::string text;
  std::optional<CrossReferenceTargetIR> target;
  // Indexes into MessageIntroductionIR::cells. Each semantic source cell is
  // claimed by exactly one atom; whitespace introduced by reflow is derived
  // from the ordered source cells claimed by the surrounding text atom.
  std::vector<std::size_t> cell_indices;
};

struct MessageIntroductionParagraphIR {
  std::vector<MessageIntroductionAtomIR> atoms;
};

// Output-neutral prose semantics for the material between the topic heading
// and the first numeric SRMSG. Physical wrapping remains in cells; paragraph
// and selector boundaries are explicit and no renderer has to rediscover
// them from Markdown or legacy GML.
struct MessageIntroductionIR {
  std::vector<MessageIntroductionCellIR> cells;
  std::vector<MessageIntroductionParagraphIR> paragraphs;
};

enum class MessageTopicSegmentRoleIR {
  metadata,
  anchor,
  heading,
  introduction,
  selector,
  catalog,
};

// Exactly one ledger item exists for every decoded control segment in source
// order. Semantic structures may refer to these coordinates, but do not add a
// second ownership claim.
struct MessageTopicSegmentIR {
  BookControlKind kind = BookControlKind::text;
  std::string opcode;
  bool malformed = false;
  MessageTopicSegmentRoleIR role = MessageTopicSegmentRoleIR::catalog;
  DocumentSourceSliceIR source;
};

// Exact payload-token ledger. decoded_segment is absent for decoder separator
// tokens between control segments; retaining those tokens is what makes the
// whole-topic envelope lossless rather than merely display-complete.
struct MessageTopicSourceTokenIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  EncodedLogicalToken encoded;
  SourceByteRange bytes;
  std::optional<std::size_t> decoded_segment;
};

struct MessageTopicIR {
  std::uint32_t first_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  MessageTopicMetadataIR metadata;
  std::string title;
  std::vector<std::size_t> heading_row_indices;
  std::vector<std::size_t> introduction_row_indices;
  std::vector<MessageTopicAnchorIR> anchors;
  std::vector<MessageTopicSelectorIR> selectors;
  MessageIntroductionIR introduction;
  MessageCatalogIR catalog;
  // The final source segment is retained explicitly so a whole-topic
  // renderer cannot accidentally truncate the last message's Action text.
  DocumentSourceSliceIR terminal_content_source;
  std::vector<MessageTopicRowIR> rows;
  std::vector<MessageTopicSegmentIR> segments;
  std::vector<MessageTopicSourceTokenIR> source_tokens;
};

std::optional<MessageTopicIR>
extract_message_topic_ir(const std::vector<DecodedLogicalRecordSource> &records,
                         const LayoutIR &layout,
                         const VerifiedOwnershipIR &ownership,
                         std::string *error = nullptr);
bool verify_message_topic_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const VerifiedOwnershipIR &ownership,
    const MessageTopicIR &topic, std::string *error = nullptr);
std::string format_message_topic_ir(const MessageTopicIR &topic);

} // namespace geist::detail
