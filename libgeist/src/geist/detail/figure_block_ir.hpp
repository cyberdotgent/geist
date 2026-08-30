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
// as an anchorless figure.
//
// A region without a picture selector is an ASCII/CFONT-drawn figure whose
// body hosted BookServer shows verbatim inside its <pre> (FA1PLMM0
// PREFACE.3, ACPZMST1 1.2.5, GG24-4302-00 3.3.4, SC09-138 1.3.1, SH20-918
// FRONT_1.3).  Its geometry comes from the display-line structure of the
// logical record itself: a record payload is a sequence of
//
//   <length byte> <that many bytes of tokens>
//
// display lines (FA1PLMM0 record 37: byte 0x0b before the box top, 0x05
// before the empty box row, 0x15 before each 21-byte CFONT prose row;
// GG24-4302-00 record 262: 0x05, 0x1a, 0x3a before the RMF report rows).
// The length byte is what the Layout IR sees as a width-1 "marker slot"
// whose dictionary spelling is meaningless ('call', 'command', box
// junctions, space runs).  The tokens of a line, with the decoder's
// inter-token spaces, are the hosted display columns exactly; box-drawing
// words render as hosted does ('_' for U+2500, '|' for U+2502 and the
// bottom/side junctions, blank for the top corners).  Such a region is
// admitted as a preformatted figure: its lines become body lines, a
// "Figure N. Title" line (with wrapped continuation lines) becomes the
// caption, blank lines around the body and rule-only frame lines (which
// hosted shows as empty lines: SC09-138 1.3.1, GC23-046 6.2) are spacing,
// and CFONT/spacing controls between lines are control cells.
//
// Everything else (tables inside figures, picture selectors owned by a
// table, several pictures in one region, selectors or prose after the
// caption inside a drawn figure, misaligned line prefixes, unterminated
// regions) is declined with a reason and left to other families.

enum class FigureTargetKindIR {
  book_resource,
  external_image,
};

enum class FigureBodyKindIR {
  picture,      // image resource replaces the drawn placeholder
  preformatted, // ASCII/CFONT-drawn body reproduced line for line
};

enum class FigureCellRoleIR {
  control,               // opcode/operand cells of the region's controls
  boundary,              // SREFIG end marker cells
  placeholder_suppressed, // box outline, PICTURE n, selector display cells
  caption_layout,        // marker/origin/padding cells of the caption rows
  caption_content,       // visible caption cells
  index_term,            // "SI term" subject-index entries (never displayed)
  line_prefix,           // length byte opening a display line
  body_content,          // visible word of a preformatted body line
  body_layout,           // space / box-drawing word of a body line
  spacing,               // blank or frame-rule lines around the body
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

// One display line of a preformatted figure body.  `prefix_token` is the
// record-local length-byte token; the line's content tokens are
// [prefix_token + 1, token_end).  `text` is the hosted display line (UTF-8;
// decoder-inserted spaces included).  `rows` are the Layout IR rows that
// intersect the line, for provenance only: the Layout IR splits drawn lines
// by prose rules and its rows do not delimit them.
struct FigurePreformattedLineIR {
  std::uint32_t logical_record = 0;
  std::size_t prefix_token = 0;
  std::size_t token_end = 0;
  std::string text;
  std::vector<DocumentSourceRowIR> rows;
};

struct FigureSourceBlockIR {
  FigureBlockSpanIR span;
  // "FIG4302RSX": the SRFIG opcode without its SR prefix, which is the name
  // hosted BookServer emits as the anchor and the target cross references
  // select.  Empty for an anchorless figure.
  std::string anchor;
  FigureBodyKindIR body_kind = FigureBodyKindIR::picture;
  // Picture figures only.
  SelectorRefIR selector;
  FigureTargetKindIR target_kind = FigureTargetKindIR::book_resource;
  // Resource id ("9") or the external path ("/bookmgr/monetcoq.jpg").
  std::string target;
  // Preformatted figures only: the body lines in display order (interior
  // blank lines included, surrounding spacing excluded).
  std::vector<FigurePreformattedLineIR> lines;
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

// A selector target that names a book picture resource: `PIC` followed by
// decimal digits only (`PIC69`).  Shared with the fixed-layout region block,
// which meets the same selector inside an SRTBL envelope.
bool figure_picture_target(const std::string &target);

// The resource catalog id a picture target addresses (`PIC69` -> `69`), or
// an empty string when `target` is not a picture target.
std::string figure_picture_resource(const std::string &target);

// The decoder placeholder words the compiler wrote into the display bytes
// where the picture goes (`69` -> `PICTURE 69`).  Hosted BookServer replaces
// exactly these words with the `<img>` and uses them as its `alt` text.
std::string figure_picture_placeholder(const std::string &resource);

// Hosted BookServer display of one decoded token word inside a preformatted
// figure line (UTF-8).  Box-drawing words follow the hosted <pre> output;
// arrows keep their glyph (hosted's per-book display tables turn them into
// substitution bytes).
std::string figure_display_glyph(std::uint16_t word);

std::string format_figure_blocks_ir(const FigureBlocksIR &blocks);

} // namespace geist::detail
