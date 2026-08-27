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

std::optional<MessageProseIntroductionIR>
extract_message_prose_introduction_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    std::string* error = nullptr);
std::string format_message_prose_introduction_ir(
    const MessageProseIntroductionIR& introduction);

// Emits one `:p.` GML record per paragraph. Font highlight spans of the run's
// CFONT control are applied only when they cover a whole leading word
// sequence of the paragraph exactly; otherwise the paragraph stays plain.
std::vector<std::string> render_message_prose_introduction_gml(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const MessageProseIntroductionIR& introduction);

// A catalog row that LayoutIR typed as a soft wrap of the previous row of its
// display run and that no control-only spacing boundary separates from it.
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

// A dictionary-word marker slot outside the three-space origin: source-owned
// text that the flattened legacy renderer sometimes drops (`Add correlation
// information`, `received was incorrect`). The row geometry, not the word's
// spelling, identifies it as lexical.
struct MessageProseLexicalMarkerIR {
  DocumentSourceRowIR source_row;
  DocumentSourceSliceIR source;
  MarkerSlotIR marker;
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
