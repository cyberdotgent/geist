#pragma once

#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/provenance_ir.hpp"

#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;

// Typed paragraph evidence for flattened prose rows inside numeric SRMSG
// catalogs that still render through the legacy GML path (trap catalogs such
// as SC31-711 `4.1.3` and `4.4`). Two source facts drive every decision:
//
// - LayoutIR classifies a row started by a `?` placeholder run or by a
//   control-free record continuation as `soft_wrap`; such a row continues the
//   paragraph of the previous row in its display run.
// - A control-only spacing token (one decoded word `< 4`, no text) that is
//   followed by non-visible geometry (a marker slot, padding, or another
//   control-only token) is a hard paragraph boundary. The same token followed
//   by visible text is an ordinary spacing control and is not a boundary.
//
// No decision consults decoded spelling or sentence punctuation.

// A control-only spacing token that proves a paragraph boundary.
struct MessageProseBoundaryTokenIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::uint16_t spacing_control = 0;
  SourceByteRange bytes;
};

struct MessageProseParagraphIR {
  std::string text;
  std::vector<DocumentSourceRowIR> source_rows;
  // Contiguous token spans (one per logical record) projected into `text`.
  std::vector<DocumentSourceSliceIR> source_slices;
  std::vector<MessageProseBoundaryTokenIR> opening_boundary;
  bool opened_by_run_start = false;
};

// Prose between the topic title segment and the first numeric SRMSG segment,
// excluding the title run itself.
struct MessageProseIntroductionIR {
  std::size_t first_run_index = 0;
  std::size_t end_run_index = 0;
  DocumentSourceSliceIR first_catalog_segment;
  std::vector<MessageProseParagraphIR> paragraphs;
};

// Discovers the envelope itself: the display runs between the title run and
// the first numeric SRMSG segment. This no longer feeds any legacy-GML
// projection -- its only corpus hit, SC31-711 4.1.3, now renders through
// `TrapCatalogIR`, which builds the same envelope from the catalog's own
// entry starts and passes it to `extract_message_prose_paragraphs_ir`. It is
// retained as the layout-derived cross-check of that envelope.
std::optional<MessageProseIntroductionIR>
extract_message_prose_introduction_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    std::string* error = nullptr);

// Explicit prose envelope: the tokens from `begin_token` of `begin_record`
// up to (excluding) the first source token of `catalog_segment` in
// `catalog_record`. The display run and the decoded segment that contain the
// envelope start may straddle it at their start (a title segment whose
// payload continues with prose); every other run/segment must lie inside.
// Structural controls inside the envelope (index entries) contribute only
// their payload text; their operand cells are control-owned.
struct MessageProseEnvelopeIR {
  std::uint32_t begin_record = 0;
  std::size_t begin_token = 0;
  std::uint32_t catalog_record = 0;
  std::size_t catalog_segment = 0;
};

inline bool operator==(const MessageProseEnvelopeIR& left,
                       const MessageProseEnvelopeIR& right) {
  return left.begin_record == right.begin_record &&
         left.begin_token == right.begin_token &&
         left.catalog_record == right.catalog_record &&
         left.catalog_segment == right.catalog_segment;
}

inline bool operator!=(const MessageProseEnvelopeIR& left,
                       const MessageProseEnvelopeIR& right) {
  return !(left == right);
}

std::optional<MessageProseIntroductionIR> extract_message_prose_paragraphs_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const MessageProseEnvelopeIR& envelope, std::string* error = nullptr);
std::string format_message_prose_introduction_ir(
    const MessageProseIntroductionIR& introduction);

// A catalog row that LayoutIR typed as a soft wrap of the previous row of its
// display run, that no control-only spacing boundary separates from it, and
// whose first visible content is not a list-item prefix. A row that opens
// with the decoder-separator sentinel (the bullet glyph BookServer renders as
// `°`; N2AH1MST 22.0 LR1985 tokens 0-4 `(` 10 spaces sentinel 2 spaces
// `Decrease`) or with an ordinal (a digit-only token, an attached `.`, a
// space; N2AH1MST 23.0 LR2009 tokens 2-5 `1` `.` ` ` `During`) starts its own
// block even though the record geometry continues the run.
struct MessageProseRowJoinIR {
  DocumentSourceRowIR source_row;
  DocumentSourceSliceIR source;
  PhysicalRowStartKind start = PhysicalRowStartKind::placeholder_wrap;
  std::optional<MarkerSlotIR> marker;
  std::string previous_visible_text;
  std::string visible_text;
};

std::optional<std::vector<MessageProseRowJoinIR>>
extract_message_prose_row_joins_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    std::string* error = nullptr);
std::string format_message_prose_row_join_ir(const MessageProseRowJoinIR& join);

// Rejoins legacy GML records that were split at a typed soft wrap. A join is
// applied only when one record starts with the wrapped row's visible text and
// the immediately preceding prose record ends with the previous row's visible
// text; every other candidate is left untouched. Returns the number of joins.
std::size_t project_message_prose_row_joins_gml(
    std::vector<std::string>& rendered,
    const std::vector<MessageProseRowJoinIR>& joins);

// The trailing fill run of a display line: the all-space token between a
// hanging marker word and the origin run of the next display line.
struct MessageProseLineFillIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t width = 0;
  SourceByteRange bytes;
};

// A dictionary-word marker slot that is the hanging last word of the
// previous display line: source-owned text that the flattened legacy renderer
// sometimes drops (`Add correlation information`, `received was incorrect`).
// The row geometry, not the word's spelling, identifies it as lexical: a
// hanging word is followed by the line's trailing fill run and then the
// origin run of the next line, two consecutive space-run tokens
// (SC31-711 LR106 `correlation` 6-space fill 3-space origin `information`;
// N2AH1MST LR1555 bytes 0x93fb5 `5a 0c 10`: `the`, 7-space fill, 14-space
// origin, `system`). A width-1 slot followed by a single origin run is the
// row-boundary control of the next line, whatever its decoded spelling
// (N2AH1MST LR1555 bytes 0x93f2c `1c 0d`: `access`, 10-space origin,
// `lookaside`; hosted renders `virtual lookaside`), exactly like the layout
// glyphs `( ) - / =` in the same slot. Slots without the fill run are not
// typed as lexical (fail closed).
struct MessageProseLexicalMarkerIR {
  DocumentSourceRowIR source_row;
  DocumentSourceSliceIR source;
  MarkerSlotIR marker;
  MessageProseLineFillIR line_fill;
  std::string previous_visible_text;
  std::string visible_text;
};

std::vector<MessageProseLexicalMarkerIR> extract_message_prose_lexical_markers_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout);
std::string format_message_prose_lexical_marker_ir(
    const MessageProseLexicalMarkerIR& marker);

// Reinserts a dropped lexical marker only where one prose record carries the
// previous row's visible text immediately followed by this row's visible
// text with a single space. Returns the number of restorations.
std::size_t project_message_prose_lexical_markers_gml(
    std::vector<std::string>& rendered,
    const std::vector<MessageProseLexicalMarkerIR>& markers);

// Layout/Ownership built once for one topic plus both typed projections.
// Introduction extraction is fail-closed and independent of the joins: a
// rejected introduction leaves the legacy erase behaviour in place while the
// joins can still apply, and vice versa.
struct MessageProseSourceIR {
  LayoutIR layout;
  OwnershipIR ownership;
  std::optional<MessageProseIntroductionIR> introduction;
  std::string introduction_rejection;
  std::vector<MessageProseRowJoinIR> joins;
  std::vector<MessageProseLexicalMarkerIR> lexical_markers;
};

std::optional<MessageProseSourceIR> build_message_prose_source_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::string* error = nullptr);

} // namespace geist::detail
