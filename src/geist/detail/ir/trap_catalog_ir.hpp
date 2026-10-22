// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/layout/font_span_ir.hpp"
#include "geist/detail/ir/message_prose_rows.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/container/provenance_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;

// Typed section-label SRMSG catalogs ("trap catalogs"): every `SRMSG <id>`
// entry is followed by font runs whose leading highlighted spans spell the
// entry headline and, for each field, a label ending in `:` (`Description:`,
// `LNM for AIX Response:`, `Action:`). Unlike MessageCatalogIR the label
// vocabulary is not fixed: it is the ordered label sequence that every entry
// of the catalog repeats, discovered from the CFONT span geometry and the
// projected row text of the catalog itself. Entry IDs are the first operand
// word of the SRMSG control (numeric, symbolic, or prefixed).
//
// Source evidence used for every decision (no spelling is consulted):
// - CFONT operand triples (column/length/code) give the label and headline
//   spans; a chain of spans that starts at the catalog origin column and
//   whose members follow each other with single-space gaps is one label.
// - LayoutIR physical rows give marker slots. A marker glyph, a `?`
//   placeholder run, or a dictionary word at the three-space origin is a row
//   boundary; sentence punctuation in a marker slot closes the preceding
//   text; a dictionary word at any other origin is lexical text.
// - OwnershipIR dispositions classify every remaining cell: visible content
//   is text, origin/padding cells are spacing, control operands are skipped,
//   and printable cells outside any physical row are admitted only where
//   the record has no row for them (record-prefix continuations).

enum class TrapCellRoleIR {
  text,
  spacing,
  control,
  placeholder,
  layout_marker,
  punctuation_marker,
  lexical_marker,
  terminal_glyph,
  unrowed_text,
  suppressed,
};

struct TrapSourceCellIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  std::uint16_t word = 0;
  SourceDisposition disposition = SourceDisposition::opaque;
  TrapCellRoleIR role = TrapCellRoleIR::text;
};

inline bool operator==(const TrapSourceCellIR &left,
                       const TrapSourceCellIR &right) {
  return left.logical_record == right.logical_record &&
         left.token_index == right.token_index &&
         left.word_index == right.word_index && left.word == right.word &&
         left.disposition == right.disposition && left.role == right.role;
}

// A highlighted run the source draws *inside* a body, rather than as the
// entry headline or a field label.
//
// SC09-138 F.1 record 2048 line 1 is the whole line
// `   IBM C/370 Diagnosis Guide and Reference`, covered by
// `cfont 3 3 C 7 5 C 13 9 C 23 5 C 29 3 C 33 9 C`: a citation run that opens
// a continuation display line of the `Explanation:` field at the catalog's
// own origin column. SC24-546 A.0 record 996 line 0 draws
// `LISTFILE '* * *'` the same way in XPH. Neither is a label -- the run does
// not end in `:` -- and neither is a headline, because the entry already has
// one.
//
// `begin` and `end` are byte offsets into the owning `TrapTextIR::text`, in
// source order and disjoint, and each range spells exactly the run's own
// display words. The style is retained rather than presented: it is the
// consumer that decides whether a code has a rendering.
struct TrapBodyHighlightIR {
  std::size_t begin = 0;
  std::size_t end = 0;
  FontStyleIR style = FontStyleIR::unknown;
};

inline bool operator==(const TrapBodyHighlightIR &left,
                       const TrapBodyHighlightIR &right) {
  return left.begin == right.begin && left.end == right.end &&
         left.style == right.style;
}

// Source-bound selector range in projected UTF-8 byte coordinates.
struct TrapTextLinkIR {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::string target;
  DocumentSourceSliceIR source;
};

// A `SRTBL<id>` ... `SRETBL` or `SRFIG<id>` ... `SREFIG` envelope drawn
// inside one entry.
//
// The family models the envelope, not a grid. Nothing in the source spells
// column boundaries for these regions: the book draws them by placing words
// at fixed display columns, and hosted BookServer serves them the same way,
// as the entry's own display lines with an `<a name="TBL<id>">` anchor on
// the first one (N2AH1MST 28.0 `IDC3009I`, DT 19910329000100, serves
// `<a name="TBLTBLUNIQ35">   <B>Return</B>   <B>Reason</B> ...`). Claiming a
// TableBlockIR here would be inventing cell boundaries; flowing the region
// into the surrounding field would destroy the alignment that carries its
// meaning. So the region keeps its display lines exactly as the record's own
// framing draws them.
//
// A figure envelope is drawn the same way: hosted N2AH1MST 10.0 `ATB101I`
// (DT 19910329000100) serves the rows between `SRFIGFIGUNIQ20` and `SREFIG`
// as the entry's own display lines with `<a name="FIGFIGUNIQ20">` on the
// first. The region's anchor is the opening opcode without `SR`: `TBL` or
// `FIG` followed by `identifier`.
enum class TrapEmbeddedControlKindIR {
  table_start,
  table_end,
  figure_start,
  figure_end,
};

struct TrapEmbeddedControlIR {
  TrapEmbeddedControlKindIR kind = TrapEmbeddedControlKindIR::table_start;
  std::string identifier;
  DocumentSourceSliceIR source;
};

struct TrapEmbeddedRegionIR {
  std::string identifier;
  TrapEmbeddedControlIR start;
  TrapEmbeddedControlIR end;
  // One entry per drawn display line, in source order, with the line's own
  // provenance beside it.
  std::vector<std::string> lines;
  std::vector<DocumentSourceSliceIR> line_sources;
  // Empty, or one entry per line: the runs the `CFONT` directly above the row styles,
  // in byte coordinates of that line, sorted and disjoint. Empty for a row
  // the source does not style or whose spans its cells do not prove.
  std::vector<std::vector<TrapBodyHighlightIR>> line_highlights;
  // Fields already closed when the envelope opened, so a consumer can place
  // the region back where the entry drew it.
  std::size_t after_field = 0;
  std::size_t cell_begin = 0;
  std::size_t cell_end = 0;
};

// One source-backed span of projected UTF-8. Together these spans cover a
// paragraph's `text` exactly. A glyph normally names one ledger cell; a
// collapsed separator space names the source cells between adjacent glyphs.
enum class TrapTextProjectionKindIR {
  source_glyph,
  source_spacing,
  decoded_separator,
};

struct TrapTextProjectionIR {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::size_t cell_begin = 0;
  std::size_t cell_end = 0;
  TrapTextProjectionKindIR kind = TrapTextProjectionKindIR::source_glyph;
};

struct TrapParagraphBoundaryIR {
  DocumentSourceSliceIR source;
  std::size_t cell_begin = 0;
  std::size_t cell_end = 0;
};

struct TrapFlowParagraphIR {
  std::string text;
  std::string display_text;
  std::vector<TrapBodyHighlightIR> highlights;
  std::vector<TrapTextLinkIR> links;
  std::vector<DocumentSourceSliceIR> source_slices;
  std::vector<DocumentSourceRowIR> source_rows;
  std::vector<TrapTextProjectionIR> projection;
  // Every native blank row before this paragraph, in source order. Keeping
  // the vector (rather than a boolean) preserves repeated paragraph controls.
  std::vector<TrapParagraphBoundaryIR> break_before;
  std::size_t cell_begin = 0;
  std::size_t cell_end = 0;
  // Drawn column of the paragraph's first cell on its row, so a paragraph a
  // later control segment opens can be placed against an open native list.
  // Zero where no row places it.
  std::size_t first_column = 0;
};

struct TrapFlowBlockIR;

struct TrapFlowListItemIR {
  DocumentSourceSliceIR marker_source;
  std::size_t marker_cell_begin = 0;
  std::size_t marker_cell_end = 0;
  std::vector<TrapFlowBlockIR> blocks;
};

struct TrapFlowListIR {
  bool ordered = false;
  // Drawn column of the items' markers.
  std::size_t marker_column = 0;
  std::vector<TrapFlowListItemIR> items;
};

// A destination the source names where it stands inside an entry. A bare
// `SRMSG` (no operand, no payload) is one: hosted N2AH1MST 2.0 (DT
// 19910329000100) serves `<a name="MSG">` immediately before the footnote
// that follows it, not at the top of the topic.
struct TrapFlowAnchorIR {
  std::string id;
  DocumentSourceSliceIR source;
};

// An `SRFTN<id>` ... `SREFTN` footnote envelope, placed where the source
// draws it. `id` is the opening opcode without `SR` (the served anchor);
// `start` and `end` are the two controls' own slices. The closer is an end
// marker and names nothing. `blocks` are the footnote's own paragraphs.
struct TrapFlowFootnoteIR {
  std::string id;
  DocumentSourceSliceIR start;
  DocumentSourceSliceIR end;
  std::vector<TrapFlowBlockIR> blocks;
  std::size_t cell_begin = 0;
  std::size_t cell_end = 0;
};

// Recursive source flow: paragraphs, native lists, exact embedded regions
// (`SRTBL`/`SRFIG` envelopes), footnote envelopes and positioned anchors, in
// source order.
struct TrapFlowBlockIR {
  std::variant<TrapFlowParagraphIR, TrapFlowListIR, TrapEmbeddedRegionIR,
               TrapFlowAnchorIR, TrapFlowFootnoteIR>
      value;
};

struct TrapFlowSequenceIR {
  std::vector<TrapFlowBlockIR> blocks;
};

inline bool operator==(const TrapTextLinkIR& a, const TrapTextLinkIR& b) {
  return a.begin == b.begin && a.end == b.end && a.target == b.target &&
         a.source == b.source;
}

// Projected UTF-8 text with exact provenance. `text` is whitespace-collapsed;
// `display_text` keeps the reader's inter-word spacing so CFONT columns can
// be mapped onto it.
struct TrapTextIR {
  std::string text;
  std::string display_text;
  // Set when an empty display line -- the only paragraph break a record
  // spells -- falls inside this projected text. `TrapLineIR` carries one
  // paragraph, so such a body runs its paragraphs together. That has been
  // the family's behaviour since SC31-711: rejecting it was measured and
  // costs SC31-711 4.1.2, 4.2.2 and 4.3.4, which are typed on `main`.
  // Recorded here, and compared canonically, rather than enforced;
  // representing a multi-paragraph field needs an entry model whose fields
  // can hold blocks, which `ListItemIR` (inline content only) cannot.
  bool paragraph_break = false;
  // Highlighted runs drawn inside this text, ordered and disjoint.
  std::vector<TrapBodyHighlightIR> highlights;
  std::vector<TrapTextLinkIR> links;
  std::vector<DocumentSourceSliceIR> source_slices;
  std::vector<DocumentSourceRowIR> source_rows;
  std::size_t cell_begin = 0;
  std::size_t cell_end = 0;
  // Canonical source structure. `text` and the flat ranges above remain the
  // compatibility projection consumed by the existing DocumentIR lowering;
  // extraction and verification preserve structure here first.
  TrapFlowSequenceIR flow;
};

struct TrapStyledSpanIR {
  std::string text;
  FontSpanIR span;
};

// One headline or field line: the CFONT control that carries its spans and
// the projected text of its payload (including a deferred payload carried by
// the next record's leading text segment).
struct TrapLineIR {
  DocumentSourceSliceIR font_source;
  std::vector<TrapStyledSpanIR> spans;
  std::string spans_text;
  TrapTextIR body;
};

// The whole field line: `line.spans` spell the label, `line.body.text` starts
// with `label_text` and continues with the field text.
// A labelled line outside the catalog vocabulary (a `Note:` paragraph inside
// one entry) keeps its highlight but is not a field of the envelope.
struct TrapFieldIR {
  TrapLineIR line;
  std::string label_text;
  bool in_vocabulary = true;
};

// An `SI <term>` subject-index display line standing inside an entry. It is
// index metadata and draws nothing: SC34-425 2.4.11.1 record 1509 line 25 is
// `SI translators, FLMLPGEN` after entry 599's last field, and hosted
// BookServer (DT 19921112160049) serves nothing for it. `term` is the line's
// words after the keyword; `source` is the whole line.
struct TrapIndexTermIR {
  std::string term;
  DocumentSourceSliceIR source;
};

inline bool operator==(const TrapIndexTermIR &left,
                       const TrapIndexTermIR &right) {
  return left.term == right.term && left.source == right.source;
}

struct TrapEntryIR {
  std::string id;
  std::string operand;
  DocumentSourceSliceIR start_source;
  // Footnote envelopes the entry draws between its `SRMSG` and its headline,
  // in source order; `leading.body.flow` holds nothing else. N2AH1MST record
  // 1936 lines 13-19 draw `FTNFTNUNIQ31` and `FTNFTNUNIQ32` there, and
  // hosted 21.0 (DT 19910329000100) serves them after `<a name="MSG
  // ERB415I">` and before the headline.
  TrapLineIR leading;
  TrapLineIR headline;
  std::vector<TrapFieldIR> fields;
  std::vector<TrapEmbeddedRegionIR> embedded_regions;
  std::vector<TrapIndexTermIR> index_terms;
  // Every payload cell of the entry's segments, in source order, each with
  // exactly one role.
  std::vector<TrapSourceCellIR> cells;
  std::vector<DocumentSourceRowIR> suppressed_rows;
};

struct TrapAnchorIR {
  std::string id;
  DocumentSourceSliceIR source;
};

// One introduction paragraph, with the native list structure its own first
// display line states. A paragraph whose first row opens with a list bullet
// (`is_display_line_list_bullet`) opens an item: `list_marker` is the
// bullet's token and `marker_column` its drawn cell. A later paragraph whose
// first glyph stands deeper than the open list's bullets is a further block
// of the item (`list_continuation`); one at or left of them ends the list.
// SC34-425 2.4.10.1 records 1489-1490 draw `   °   When an uncommented ...`
// followed by `       If the member name is enclosed ...`, and hosted
// BookServer (DT 19921112160049) serves the second paragraph under the
// first bullet, at the bullet's text column.
struct TrapIntroductionParagraphIR {
  std::string text;
  // Byte offsets in `text` of the separator spaces that are line breaks
  // between two display rows of this paragraph, strictly ascending
  // (`MessageProseParagraphIR::line_breaks`).
  std::vector<std::size_t> line_breaks;
  std::vector<TrapStyledSpanIR> leading_spans;
  // `CFONT` runs drawn inside the paragraph's rows, as byte ranges over
  // `text`, ordered and disjoint, and never inside the `leading_spans`
  // prefix. Each is placed through the source cells the prose projected,
  // never by searching the text.
  std::vector<TrapBodyHighlightIR> highlights;
  std::vector<DocumentSourceSliceIR> source_slices;
  std::vector<DocumentSourceRowIR> source_rows;
  // Selector ranges over `text`, ordered and disjoint (`cselect` rows of the
  // introduction, resolved as an entry's are).
  std::vector<TrapTextLinkIR> links;
  std::optional<DocumentSourceSliceIR> list_marker;
  std::size_t marker_column = 0;
  // Drawn column of the paragraph's first glyph, where its rows prove one.
  std::optional<std::size_t> first_column;
  bool list_continuation = false;
};

struct TrapCatalogIR {
  std::uint32_t first_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  std::string raw_topic_id;
  std::string heading_level;
  std::string title;
  DocumentSourceSliceIR title_source;
  DocumentSourceRowIR title_row;
  std::vector<TrapAnchorIR> anchors;
  // A second name the source gives the whole topic from inside an entry:
  // N2AH1MST record 385 spells `SRMSG AMD083I` and `SRSPTE083I` side by side,
  // and the book's own change summary links to `SPTE083I`.  Hosted serves
  // both names; the entry's own anchor is `MSG AMD083I`, so these are
  // destinations for the topic rather than places inside it.
  std::vector<std::string> entry_named_destinations;
  // The whole span between the title row and the first entry.
  std::optional<MessageProseEnvelopeIR> introduction_envelope;
  // The prose pieces of that span, in source order. An `SRTBL` ... `SRETBL`
  // envelope drawn in the introduction interrupts the prose, so the pieces
  // either side of it are separate envelopes and the drawing is never
  // offered to `extract_message_prose_paragraphs_ir`. Without such an
  // envelope there is exactly one piece and it equals
  // `introduction_envelope`.
  std::vector<MessageProseEnvelopeIR> introduction_prose_envelopes;
  std::vector<TrapIntroductionParagraphIR> introduction;
  // Envelopes the introduction draws, in source order. `after_field` counts
  // the introduction paragraphs already closed when the envelope opened, so
  // a consumer can place the region back where the topic drew it.
  std::vector<TrapEmbeddedRegionIR> introduction_regions;
  std::vector<TrapSourceCellIR> introduction_cells;
  std::size_t origin_column = 0;
  std::vector<std::string> label_vocabulary;
  std::vector<TrapEntryIR> entries;
};

// `toc_title` is the book's own contents title for the topic. When the
// title row continues with introduction prose on the same physical row, the
// contents title is the only source evidence for where the heading ends;
// an empty hint takes the whole first title row as the heading.
std::optional<TrapCatalogIR>
extract_trap_catalog_ir(const std::vector<DecodedLogicalRecordSource> &records,
                        const LayoutIR &layout, const OwnershipIR &ownership,
                        const std::string &toc_title,
                        std::string *error = nullptr);
// Re-extracts canonically using the catalog's own title as the hint.
bool verify_trap_catalog_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const TrapCatalogIR &catalog, std::string *error = nullptr);
bool same_trap_catalog_ir(const TrapCatalogIR &left,
                          const TrapCatalogIR &right);
std::string format_trap_catalog_ir(const TrapCatalogIR &catalog);

} // namespace geist::detail
