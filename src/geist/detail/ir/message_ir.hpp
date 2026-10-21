// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/container/provenance_ir.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace geist::detail {

enum class MessageSectionKind {
  meaning,
  action,
};

enum class MessageSectionBoundaryShapeIR {
  normal_row,
  record_prefix,
  pre_message_start,
};

using MessageSourceRowIR = std::pair<DisplayRunId, std::size_t>;

enum class MessageMarkerDispositionIR {
  absent,
  lexical_prefix,
  opaque_continuation_suffix,
  closing_delimiter_bridge,
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
  std::vector<DocumentSourceSliceIR> leading_source_slices;
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
         left.leading_source_slices == right.leading_source_slices &&
         left.trailing_source_slices == right.trailing_source_slices &&
         left.text == right.text;
}

struct MessageParagraphIR {
  std::string text;
  bool recovered_unformatted_segment = false;
  std::vector<MessageSourceRowIR> source_rows;
  std::vector<std::pair<std::uint32_t, std::size_t>> source_segments;
  std::vector<DocumentSourceSliceIR> source_slices;
  // Compact tokens suppressed from recovered opaque fields because their
  // source geometry identifies them as boundary controls. Keeping the exact
  // token evidence makes that suppression independently auditable.
  std::vector<MessageTerminalLayoutTokenIR> suppressed_layout_tokens;
  // Exact row-by-row semantic projection. Marker disposition records why a
  // compact source slot contributes text or remains layout evidence.
  std::vector<MessageSemanticRowIR> semantic_rows;
};

// Typed source boundary discovered before message paragraphs are assembled.
// Label and payload provenance are disjoint: the renderer never has to search
// recovered prose for "Meaning" or "Action".
struct MessageSectionBoundaryIR {
  MessageSectionKind kind = MessageSectionKind::meaning;
  std::size_t owner_entry = 0;
  MessageSectionBoundaryShapeIR shape =
      MessageSectionBoundaryShapeIR::normal_row;
  DocumentSourceSliceIR label_source;
  std::optional<MessageSourceRowIR> label_source_row;
  std::vector<DocumentSourceSliceIR> payload_source_slices;
  std::vector<MessageSourceRowIR> payload_source_rows;
};

struct MessageSectionIR {
  MessageSectionKind kind = MessageSectionKind::meaning;
  DisplayRunId run = 0;
  std::size_t row = 0;
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  bool recovered_record_continuation = false;
  std::optional<std::size_t> boundary_index;
  std::vector<MessageSourceRowIR> label_source_rows;
  std::vector<DocumentSourceSliceIR> label_source_slices;
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

// A record's first display line that the compatibility layout leaves without
// a physical row.  Its length byte and its origin run both spell blanks, so
// the record's leading text segment opens on the line's first drawn word and
// no marker/origin pair is left inside the segment to start a row.  The
// record's own display-line framing still proves the row: SC31-711 record 231
// line 0 is the length byte 0x12 (token 0, eighteen blanks), the 13-blank
// origin run (token 1) and `SNMP Trap    Verify that AIX NetView/6000 is
// running properly.` (tokens [2,17)); hosted BookServer prints it as its own
// row at column 13 (5.0, DT 19941010174546).
//
// A paragraph built from one carries a single semantic row whose source row
// is `{message_record_prefix_run, index}`: `index` addresses
// `MessageCatalogIR::record_prefix_rows`.  Layout run IDs start at 1, so run
// 0 never names a physical row.
inline constexpr DisplayRunId message_record_prefix_run = 0;

// One drawn cell of a record-prefix row, positioned exactly as ownership
// positions a physical row's cells: columns count decoded words from the
// origin run's first word.
struct MessageRecordPrefixCellIR {
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  std::uint16_t word = 0;
  std::size_t display_column = 0;
};

inline bool operator==(const MessageRecordPrefixCellIR &left,
                       const MessageRecordPrefixCellIR &right) {
  return left.token_index == right.token_index &&
         left.word_index == right.word_index && left.word == right.word &&
         left.display_column == right.display_column;
}

struct MessageRecordPrefixRowIR {
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  // The line's length byte: the record's first token.
  std::size_t prefix_token = 0;
  EncodedLogicalToken prefix_encoded;
  SourceByteRange prefix_bytes;
  // The blank origin run that places the row on the display.
  std::size_t origin_token = 0;
  std::size_t origin_column = 0;
  // The drawn tokens, from the first word to the line's framing end.
  DocumentSourceSliceIR content;
  std::vector<MessageRecordPrefixCellIR> cells;
  // The row as drawn from its origin column, interior spacing kept.
  std::string visible_text;
};

inline bool operator==(const MessageRecordPrefixRowIR &left,
                       const MessageRecordPrefixRowIR &right) {
  return left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.prefix_token == right.prefix_token &&
         left.prefix_encoded == right.prefix_encoded &&
         left.prefix_bytes.begin == right.prefix_bytes.begin &&
         left.prefix_bytes.end == right.prefix_bytes.end &&
         left.origin_token == right.origin_token &&
         left.origin_column == right.origin_column &&
         left.content == right.content && left.cells == right.cells &&
         left.visible_text == right.visible_text;
}

struct MessageCatalogIR {
  std::vector<MessageSectionBoundaryIR> boundaries;
  std::vector<MessageEntryIR> entries;
  std::vector<MessageRecordPrefixRowIR> record_prefix_rows;
};

// The record-prefix row a semantic row addresses, or null for a physical row.
const MessageRecordPrefixRowIR *
message_record_prefix_row(const MessageCatalogIR &catalog,
                          const MessageSourceRowIR &source);

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
