#pragma once

#include "geist/detail/glossary_ir.hpp"
#include "geist/detail/provenance_ir.hpp"

#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct GlossaryCatalogCellIR {
  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  std::uint16_t word = 0;
  SourceDisposition disposition = SourceDisposition::opaque;
  DisplayRunId run = 0;
  std::size_t row_index = 0;
};

// An exact physical fragment of a glossary definition.  visible_text and the
// optional marker are deliberately kept separate: joining them is a rendering
// decision, and several BOO rows use marker slots as lexical carry.
struct GlossaryDefinitionRowIR {
  std::string visible_text;
  std::optional<MarkerSlotIR> marker;
  std::size_t native_origin = 0;
  PhysicalBreakKind break_before = PhysicalBreakKind::unknown;
  DocumentSourceRowIR source_row;
  DocumentSourceSliceIR source;
  std::vector<GlossaryCatalogCellIR> cells;
};

enum class GlossaryEmbeddedControlKindIR {
  figure_start,
  table_start,
  table_end,
  figure_end,
};

struct GlossaryEmbeddedControlIR {
  GlossaryEmbeddedControlKindIR kind =
      GlossaryEmbeddedControlKindIR::figure_start;
  std::string identifier;
  DocumentSourceSliceIR source;
};

// A semantic cell plus the exact visible source cells which spell it.  Space
// and row-boundary cells remain conserved by physical_rows; they are not
// silently promoted into cell text.
struct GlossaryEmbeddedTableCellIR {
  std::string text;
  std::vector<GlossaryCatalogCellIR> source_cells;
};

struct GlossaryEmbeddedTableRowIR {
  std::vector<GlossaryEmbeddedTableCellIR> cells;
};

// The glossary contains one observed fixed two-column table nested in a
// figure.  This object records both its semantic grid and its lossless physical
// envelope, since compact marker slots can split one lexical value across two
// physical rows (the observed "1-" / "15" boundary).
struct GlossaryEmbeddedTableIR {
  std::vector<GlossaryEmbeddedControlIR> controls;
  std::vector<GlossaryDefinitionRowIR> physical_rows;
  std::vector<GlossaryEmbeddedTableRowIR> rows;
  std::size_t header_rows = 0;
};

struct GlossaryDefinitionIR {
  std::vector<GlossaryDefinitionRowIR> rows;
  std::vector<DocumentSourceSliceIR> structural_sources;
  std::optional<GlossaryEmbeddedTableIR> embedded_table;
};

struct GlossaryEntryIR {
  // term is the verified display boundary. raw_term and source_suffix conserve
  // the complete SRGLS payload, including any fixed-row carry token.
  std::string term;
  std::string raw_term;
  std::string source_suffix;
  DocumentSourceSliceIR term_source;
  GlossaryDefinitionIR definition;
};

struct GlossarySectionIR {
  std::string label;
  DocumentSourceSliceIR marker_source;
  std::vector<GlossaryDefinitionRowIR> label_rows;
};

struct GlossaryCatalogSegmentIR {
  BookControlKind kind = BookControlKind::text;
  std::string opcode;
  bool malformed = false;
  DocumentSourceSliceIR source;
};

// Complete, output-neutral ownership of a glossary topic.  Entries and section
// markers are source ordered, while segments provide the whole-topic
// conservation ledger (including metadata and the one observed embedded
// table/figure envelope).
struct GlossaryCatalogIR {
  std::uint32_t first_logical_record = 0;
  std::uint32_t end_logical_record = 0;
  std::string heading_level;
  GlossaryIntroductionIR introduction;
  std::vector<GlossarySectionIR> sections;
  std::vector<GlossaryEntryIR> entries;
  DocumentSourceSliceIR terminal_source;
  std::vector<GlossaryCatalogSegmentIR> segments;
};

std::optional<GlossaryCatalogIR> extract_glossary_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    std::string* error = nullptr);
bool verify_glossary_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const GlossaryCatalogIR& catalog, std::string* error = nullptr);
std::string format_glossary_catalog_ir(const GlossaryCatalogIR& catalog);

} // namespace geist::detail
