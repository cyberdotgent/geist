#pragma once

#include "geist/detail/font_span_ir.hpp"
#include "geist/detail/message_prose_rows.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/provenance_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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

// Projected text with exact provenance. `text` is whitespace-collapsed;
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
  std::vector<DocumentSourceSliceIR> source_slices;
  std::vector<DocumentSourceRowIR> source_rows;
  std::size_t cell_begin = 0;
  std::size_t cell_end = 0;
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

struct TrapEntryIR {
  std::string id;
  std::string operand;
  DocumentSourceSliceIR start_source;
  TrapLineIR headline;
  std::vector<TrapFieldIR> fields;
  // Every payload cell of the entry's segments, in source order, each with
  // exactly one role.
  std::vector<TrapSourceCellIR> cells;
  std::vector<DocumentSourceRowIR> suppressed_rows;
};

struct TrapAnchorIR {
  std::string id;
  DocumentSourceSliceIR source;
};

struct TrapIntroductionParagraphIR {
  std::string text;
  std::vector<TrapStyledSpanIR> leading_spans;
  std::vector<DocumentSourceSliceIR> source_slices;
  std::vector<DocumentSourceRowIR> source_rows;
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
  std::optional<MessageProseEnvelopeIR> introduction_envelope;
  std::vector<TrapIntroductionParagraphIR> introduction;
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
