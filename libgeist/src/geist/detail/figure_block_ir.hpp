#pragma once

#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/provenance_ir.hpp"
#include "geist/detail/selector_display_ir.hpp"
#include "geist/detail/selector_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace geist::detail {

// Typed figure block: the reusable picture region that a topic body is
// partitioned around.  A BookManager figure is, in source order,
//
//   SRFIG<anchor>          structural start; payload is the ASCII box outline
//   cselect c l PIC<n>     picture selector targeting book resource <n>, or
//   cselect c l LNK <IMAGE> <INTERNET> <> <path> <> <id>   external image
//   "PICTURE <n>" row      decoder placeholder for the picture cell
//   "Figure N. Title" row  caption, optionally soft-wrapped
//   SREFIG                 structural or bare-text end
//
// (GG24-4302-00 5.1.8/8.5.3, XWEBDEMO 1.4.1; hosted BookServer renders the
// box with the image inside it and the caption beneath).  The placeholder and
// caption may share one row (SC09-2417-00 1.2.2), carry a change bar
// ("| Figure  1-4." SC24-5527-02 1.5), open a marker-less text segment
// (GG24-4302-00 4.1.1), or follow a subject-index entry ("SI DCE, file
// system", GG24-395 2.4.4) that hosted output never displays.  The legacy
// renderer suppresses the box and the "PICTURE n" placeholder because the
// image replaces them; this block records those cells as suppressed so the
// composer can prove every source cell inside the region has exactly one
// disposition.  A bare picture selector outside any SRFIG/SRTBL is admitted
// as an anchorless figure.  Everything else (ASCII-art and CFONT-boxed
// figures without a picture, tables inside figures, picture selectors owned
// by a table, several pictures in one region, prose rows inside the region,
// unterminated regions) is declined with a reason and left to other
// families.

enum class FigureTargetKindIR {
  book_resource,
  external_image,
};

enum class FigureCellRoleIR {
  control,               // opcode/operand cells of the region's controls
  boundary,              // SREFIG end marker cells
  placeholder_suppressed, // box outline, PICTURE n, selector display cells
  caption_layout,        // marker/origin/padding cells of the caption rows
  caption_content,       // visible caption cells
  index_term,            // "SI term" subject-index entries (never displayed)
};

struct FigureSourceCellIR {
  std::uint32_t logical_record = 0;
  // Owning control segment; a decoder separator between two segments is
  // attributed to the segment it follows.
  std::size_t segment_index = 0;
  std::size_t token_index = 0;
  std::size_t word_index = 0;
  std::uint16_t word = 0;
  SourceByteRange token_bytes;
  FigureCellRoleIR role = FigureCellRoleIR::placeholder_suppressed;
};

struct FigureSegmentRefIR {
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
};

// Source extent of one figure region.  `begin` is the SRFIG segment (or the
// picture selector for an anchorless figure); `end` is the SREFIG segment
// (or the selector again).  Only the SREFIG opcode belongs to the region: a
// structural SREFIG may carry the following prose as payload rows, which are
// deliberately left unclaimed for the composer's prose family.
struct FigureBlockSpanIR {
  FigureSegmentRefIR begin;
  FigureSegmentRefIR end;
  bool anchored = false;
};

struct FigureCaptionIR {
  std::string text;
  std::vector<DocumentSourceRowIR> rows;
};

struct FigureSourceBlockIR {
  FigureBlockSpanIR span;
  // "FIG4302RSX": the SRFIG opcode without its SR prefix, which is the name
  // hosted BookServer emits as the anchor and the target cross references
  // select.  Empty for an anchorless figure.
  std::string anchor;
  SelectorRefIR selector;
  FigureTargetKindIR target_kind = FigureTargetKindIR::book_resource;
  // Resource id ("9") or the external path ("/bookmgr/monetcoq.jpg").
  std::string target;
  // Decoder placeholder text for the picture cell when present ("PICTURE 9").
  std::string placeholder_text;
  std::optional<FigureCaptionIR> caption;
  // Subject-index entries ("SI DCE, file system") carried inside the region;
  // hosted BookServer never displays them.
  std::vector<std::string> index_terms;
  std::vector<DocumentSourceRowIR> suppressed_rows;
  // Every source cell inside the region, each exactly once.
  std::vector<FigureSourceCellIR> cells;
};

struct FigureBlockDeclineIR {
  FigureSegmentRefIR begin;
  std::optional<FigureSegmentRefIR> end;
  std::string anchor;
  std::string reason;
};

struct FigureBlocksIR {
  std::vector<FigureSourceBlockIR> blocks;
  std::vector<FigureBlockDeclineIR> declined;
};

// `resource_ids` are the book's resource catalog ids (ResourceEntry::id) so a
// PIC<n> selector can be proven to address a stored picture.  An empty set
// declines every book-resource figure.
FigureBlocksIR extract_figure_blocks_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const SelectorCatalogIR &selectors,
    const std::set<std::string> &resource_ids);

// Re-extracts the canonical blocks and checks every claimed cell against the
// ownership ledger: identity, role consistency, uniqueness across blocks,
// and complete coverage of each region.
bool verify_figure_blocks_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const SelectorCatalogIR &selectors,
    const std::set<std::string> &resource_ids, const FigureBlocksIR &blocks,
    std::string *error = nullptr);

std::string format_figure_blocks_ir(const FigureBlocksIR &blocks);

} // namespace geist::detail
