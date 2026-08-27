#pragma once

#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/provenance_ir.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace geist::detail {

enum class MessageSectionKind {
  meaning,
  action,
};

using MessageSourceRowIR = std::pair<DisplayRunId, std::size_t>;

enum class MessageMarkerDispositionIR {
  absent,
  lexical_prefix,
  opaque_continuation_suffix,
  punctuation_suffix,
  list_prefix,
  layout_artifact,
};

// A compact dictionary token which occupies the terminal control slot of a
// pre-section display row. Its decoded spelling is not message prose, but the
// token remains explicit source evidence rather than disappearing during text
// projection.
struct MessageTerminalLayoutTokenIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  EncodedLogicalToken encoded;
  SourceByteRange bytes;
  std::string decoded_text;
};

inline bool operator==(const MessageTerminalLayoutTokenIR &left,
                       const MessageTerminalLayoutTokenIR &right) {
  return left.logical_record == right.logical_record &&
         left.token_index == right.token_index &&
         left.encoded == right.encoded &&
         left.bytes.begin == right.bytes.begin &&
         left.bytes.end == right.bytes.end &&
         left.decoded_text == right.decoded_text;
}

struct MessageSemanticRowIR {
  MessageSourceRowIR source_row;
  MessageMarkerDispositionIR marker_disposition =
      MessageMarkerDispositionIR::absent;
  std::optional<MessageTerminalLayoutTokenIR> terminal_layout_token;
  // Printable cells between this row and the next admitted row boundary.
  // These retain the exact token/byte slices of lexical trailing fields that
  // LayoutIR could not safely include in the primary physical row.
  std::vector<DocumentSourceSliceIR> trailing_source_slices;
  std::string text;
};

inline bool operator==(const MessageSemanticRowIR &left,
                       const MessageSemanticRowIR &right) {
  return left.source_row == right.source_row &&
         left.marker_disposition == right.marker_disposition &&
         left.terminal_layout_token == right.terminal_layout_token &&
         left.trailing_source_slices == right.trailing_source_slices &&
         left.text == right.text;
}

struct MessageParagraphIR {
  std::string text;
  bool recovered_unformatted_segment = false;
  std::vector<MessageSourceRowIR> source_rows;
  std::vector<std::pair<std::uint32_t, std::size_t>> source_segments;
  std::vector<DocumentSourceSliceIR> source_slices;
  // Exact row-by-row semantic projection. Marker disposition records why a
  // compact source slot contributes text or remains layout evidence.
  std::vector<MessageSemanticRowIR> semantic_rows;
};

struct MessageSectionIR {
  MessageSectionKind kind = MessageSectionKind::meaning;
  DisplayRunId run = 0;
  std::size_t row = 0;
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  bool recovered_record_continuation = false;
  std::vector<MessageSourceRowIR> label_source_rows;
  std::vector<MessageParagraphIR> paragraphs;
  std::vector<MessageSourceRowIR> source_rows;
};

struct MessageEntryIR {
  std::string id;
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  MessageParagraphIR headline;
  // Display runs after the initial headline and before Meaning/Action are
  // semantically continuations of the emphasized message headline.
  std::vector<MessageParagraphIR> headline_continuations;
  std::vector<MessageSectionIR> sections;
  // Complete ordered ledger of physical rows assigned to this message. Rows
  // carrying structural separator artifacts are retained separately rather
  // than silently discarded from source ownership.
  std::vector<MessageSourceRowIR> source_rows;
  std::vector<MessageSourceRowIR> suppressed_source_rows;
};

struct MessageCatalogIR {
  std::vector<MessageEntryIR> entries;
};

std::optional<MessageCatalogIR> extract_message_catalog_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    std::string *error = nullptr);
bool verify_message_catalog_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const MessageCatalogIR &catalog, std::string *error = nullptr);
bool same_message_catalog_ir(const MessageCatalogIR &left,
                             const MessageCatalogIR &right);
std::string format_message_catalog_ir(const MessageCatalogIR &catalog);

} // namespace geist::detail
