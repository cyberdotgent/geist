// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/container/provenance_ir.hpp"

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

// The source cell a projected glyph came from, and the byte offset in the
// paragraph's `text` of the glyph's first byte.
struct MessageProseGlyphIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  std::size_t offset = 0;
};

struct MessageProseParagraphIR {
  std::string text;
  // Every glyph of `text` with its source cell, in text order. Recorded as
  // the text is projected, never recovered by searching it; cleared, with
  // `glyphs_complete` false, where a piece's projection could not be
  // replayed byte for byte.
  std::vector<MessageProseGlyphIR> glyphs;
  bool glyphs_complete = true;
  std::vector<DocumentSourceRowIR> source_rows;
  // Contiguous projected pieces, split at native framing and hidden controls.
  // An unrowed native continuation retains these byte-backed claims without
  // inventing a compatibility layout row or borrowing a later font run.
  std::vector<DocumentSourceSliceIR> source_slices;
  // A complete terminal CFONT in the preceding framed record can open the
  // first native row here. Its consumer must corroborate every font span
  // against that row before admitting the paragraph.
  std::optional<DocumentSourceSliceIR> leading_font_source;
  std::vector<MessageProseBoundaryTokenIR> opening_boundary;
  bool opened_by_run_start = false;
  // Display-row boundaries inside this paragraph that are line breaks rather
  // than reflow joins: the byte offset in `text` of the single separator
  // space that stands for the boundary, strictly ascending. A boundary is a
  // break when the row after it is opened by a CFONT span at its first drawn
  // cell and either both rows are example rows (every drawn cell under an
  // example or variable span) or the row before it left room for the opening
  // word, measured against the paragraph's widest row.
  std::vector<std::size_t> line_breaks;
};

// Prose between the topic title segment and the first numeric SRMSG segment,
// excluding the title run itself.
struct MessageProseIntroductionIR {
  std::size_t first_run_index = 0;
  std::size_t end_run_index = 0;
  DocumentSourceSliceIR first_catalog_segment;
  std::vector<MessageProseParagraphIR> paragraphs;
};

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

} // namespace geist::detail
