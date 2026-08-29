#pragma once

#include "geist/detail/internal.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

// Display lines of a logical record.
//
// A record payload is a sequence of
//
//   <length byte> <that many bytes of token references>
//
// display lines (Format/logical-controls.md, "Display Lines Inside A Record
// Payload": FA1PLMM0 records 37-38, GG24-4302-00 record 262, ACPZMST1
// record 55, and every record of the swept figure topics).  The length byte
// is below the book's token threshold, so a token reader resolves it as an
// arbitrary one-byte dictionary word; its encoded value is the byte itself,
// not a dictionary reference.  This is the structure the Layout IR sees as a
// width-1 "marker slot" followed by the line's leading space token.
struct DisplayLineIR {
  std::size_t prefix_token = 0;  // the length byte
  std::size_t token_end = 0;     // exclusive end of the line's tokens
};

// Walks a token list as length-prefixed display lines, or declines when a
// line does not end exactly on a token boundary (a length byte at or above
// the token threshold is tokenised as two bytes and breaks the walk).  The
// record decoder uses this to decide whether its plain left-to-right token
// walk already agrees with the record's own line structure.
std::optional<std::vector<DisplayLineIR>> token_display_lines(
    const std::vector<LogicalTokenIR>& tokens, std::uint32_t payload_end);

// Parses the record payload into display lines, or declines when a line does
// not end exactly on a token boundary.
std::optional<std::vector<DisplayLineIR>> record_display_lines(
    const DecodedLogicalRecordSource& record);

// Hosted display text of a line's tokens: token words in order with the
// decoder's inter-token spaces, spacing prefixes dropped, the inserted space
// that precedes the next line's length byte dropped.  Box-drawing words are
// rendered as hosted BookServer renders them
// (`figure_display_glyph`).
std::string display_line_text(const DecodedLogicalRecordSource& record,
                              const DisplayLineIR& line);

// The line's words per display column, in the same accumulation as
// `display_line_text` (one entry per rendered column; a word that renders to
// several bytes still occupies one column).  Column 0 is the first cell of
// the line.
std::vector<std::uint16_t> display_line_columns(
    const DecodedLogicalRecordSource& record, const DisplayLineIR& line);

// One entry per display column: the word and the record-local token it came
// from.  `token` is `static_cast<std::size_t>(-1)` for a space the assembler
// inserted between two tokens.
struct DisplayLineCellIR {
  std::uint16_t word = 0;
  std::size_t token = static_cast<std::size_t>(-1);
};

std::vector<DisplayLineCellIR> display_line_cells(
    const DecodedLogicalRecordSource& record, const DisplayLineIR& line);

// Display-line corroboration of the control-shaped words the flattened
// decoded string splits a segment on.
//
// `decode_control_segments` works on the assembled string, where a
// control-shaped word after a marker starts a new segment and any
// identifier-shaped word beginning `SR` classifies as a structural control.
// The record's display lines disprove some of those splits: a word with
// another displayed word in front of it *on its own display line* stands
// inside a row, so it is that row's display text and not a control.
//
// Marks such a segment `display_text` (and demotes a structural one to
// `text`).  The segment boundary itself stays -- the flattened string really
// did split there -- so a consumer must read the segment's payload as body
// text in place.
//
// Evidence, over the 34 fixtures: of the 14,392 structural segments 9,138
// open their display line, 4,987 open it and carry text after, 61 sit in a
// record whose lines do not parse, and ~200 have a displayed word in front of
// them.  Every one of those ~200 is prose (`SRVAPPS`, `SRVBLDS`, `SREPLACE`,
// `SREF`, `SRCVPAC`, `SRPI`, `SRC1`, ...); no `SREFIG`, `SRFIG*`, `SRGLS`,
// `SRHDR*`, `SRLIS*`, `SRLEN`, `SRTBL` or `SRFTN*` anchor is among them.
// Worked example: DREICMST record 430 display line [195,205) reads
// `       the command is saved in the SRC.`, and the flattened string split
// `SRC.` off as its own segment; hosted 2.8.3 (DT 19911219125856) prints the
// abbreviation.  SH12-565 record 282 line 31 is the list item
// `   °   SRCVPAC` of `LOGMODE / RUSIZES / PSNDPAC / SRCVPAC / SSNDPAC.`,
// all five of which hosted 4.3.5 (DT 19941206115523) serves.
void demote_display_line_owned_controls(DecodedLogicalRecordSource& record);

} // namespace geist::detail
