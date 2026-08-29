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

// Parses the record payload into display lines, or declines when a line does
// not end exactly on a token boundary (a length byte at or above the token
// threshold is tokenised as two bytes and breaks the walk).
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

} // namespace geist::detail
