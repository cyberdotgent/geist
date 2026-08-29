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

// The `U+2666` list bullet the reader draws in front of a list item's text.
constexpr std::uint16_t list_bullet_word = 0x2666;

// Display-line corroboration of the `SR<id>` structural controls.
//
// A word that begins with `SR` and is otherwise identifier-shaped is taken
// for a structural control by the flattened-string classifier, which cannot
// see that some of them are ordinary prose.  A display line proves it: a
// list bullet in front of the word on its own line makes the word that
// list item's display text, and hosted BookServer prints it.
//
// SH12-565 record 282 display line 31 is `<length byte> <three-cell origin>
// <U+2666> <two-cell gap> SRCVPAC`, one of the five items of the list
// `LOGMODE / RUSIZES / PSNDPAC / SRCVPAC / SSNDPAC.`, and hosted 4.3.5 (DT
// 19941206115523) serves all five as `   °   <name>`; the classifier had
// been swallowing the fourth as an anchor.  Record 702 (`SRCVPAC`) and
// record 339 (`SRVPREF`) repeat it in the same book, as does SC24-5527-02's
// `SRVAPPS` in eight records.  A real anchor never stands behind a bullet:
// of the 14,392 structural segments in the 34 fixtures only these 11 do.
void demote_bullet_owned_structural_controls(
    DecodedLogicalRecordSource& record);

} // namespace geist::detail
