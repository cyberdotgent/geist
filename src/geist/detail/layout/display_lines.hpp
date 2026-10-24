// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/core/internal.hpp"

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
// display lines (doc/boo-spec/logical-controls.adoc, "Display Lines Inside A Record
// Payload": FA1PLMM0 records 37-38, GG24-4302-00 record 262, ACPZMST1
// record 55, and every record of the swept figure topics).  The length byte
// is below the book's token threshold, so a token reader resolves it as an
// arbitrary one-byte dictionary word; its encoded value is the byte itself,
// not a dictionary reference.  This is the structure the Layout IR sees as a
// width-1 "marker slot" followed by the line's leading space token.
//
// `DisplayLineIR` itself lives in book_ir.hpp beside `LogicalRecordIR`,
// which stores the framing.

// The single re-derivation entry point.  Walks a token list as
// length-prefixed display lines, or declines when a line does not end
// exactly on a token boundary (a length byte at or above the token
// threshold is tokenised as two bytes and breaks the walk).  The record
// decoder uses this to decide whether its plain left-to-right token walk
// already agrees with the record's own line structure.  No consumer should
// call this on a decoded record: read the stored framing instead.
std::optional<std::vector<DisplayLineIR>> token_display_lines(
    const std::vector<LogicalTokenIR>& tokens, std::uint32_t payload_end);

// Computes the record's display-line framing once and stores it on the IR,
// stamping every token with its `TokenFramingRole`.  Called by the record
// decoder (`decode_record_payload_ir`); a test that assembles a synthetic
// record by hand calls it too, so there is exactly one implementation of the
// walk in the library.
void assign_display_line_framing(LogicalRecordIR& record);

// The record's stored display lines, or `nullptr` when the payload does not
// tile into whole display lines.  This is a read of decoder state, not a
// re-parse.
const std::vector<DisplayLineIR>* record_display_lines(
    const LogicalRecordIR& record);
const std::vector<DisplayLineIR>* record_display_lines(
    const DecodedLogicalRecordSource& record);

// True when `token` is a display line's length byte -- structure, never
// display text and never a control opcode, whatever word the dictionary
// spells for it.  False for an unframed record: an undecided framing may not
// be reported as a decided "not a length byte", so ask
// `record_framing_is_decided` where the distinction matters.
bool is_display_line_length_token(const LogicalRecordIR& record,
                                  std::size_t token);
bool is_display_line_length_token(const DecodedLogicalRecordSource& record,
                                  std::size_t token);
inline bool record_framing_is_decided(const LogicalRecordIR& record) {
  return record.display_lines_parse;
}

// The display line `token` belongs to (its length byte included), or
// `nullptr` when the record is unframed or `token` is out of range.
const DisplayLineIR* display_line_of_token(const LogicalRecordIR& record,
                                           std::size_t token);
const DisplayLineIR* display_line_of_token(
    const DecodedLogicalRecordSource& record, std::size_t token);

// A subject-index keyword is metadata only at the native line origin and at
// a surviving control-segment boundary. Indented literal SI remains text.
bool is_subject_index_display_line(const DecodedLogicalRecordSource& record,
                                  const DisplayLineIR& line);

// Segment kinds describe the flattened scanner's speculative interpretation.
// Canonical native framing can prove its apparent opcode is only a line-length
// byte. This does not change the kind: compatibility consumers still use those
// segment boundaries to recover display payload. False when framing is unknown.
bool is_display_line_length_opcode(const DecodedLogicalRecordSource& record,
                                   const ControlSegmentIR& segment);
// The apparent opcode stands after native indentation or other line content,
// not at the instruction origin. Such a word cannot open an instruction.
bool is_display_line_embedded_opcode(const DecodedLogicalRecordSource& record,
                                     const ControlSegmentIR& segment);

// Checked display-text accessor.  Hands back the token's words only when the
// token really is line content; a length byte yields `nullptr` instead of
// its dictionary spelling, so a consumer asking for display text cannot be
// handed structure by accident.  An unframed record hands the words back --
// there is no decided framing to contradict.
const TokenWords* display_text_words(const DecodedLogicalRecordSource& record,
                                     std::size_t token);

// Checks that the stored framing is internally consistent: the lines tile
// the token list in order, every length byte is one byte wide and covers
// exactly its line, and every token's `framing` role matches the lines.
bool verify_display_line_framing(const LogicalRecordIR& record,
                                 std::string* error = nullptr);

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

// The byte offset into `display_line_text(record, line)` of every display
// column of that line, plus a final end offset -- so the result holds one
// more entry than the line has columns.
//
// A consumer holding a *column* range (a `cselect` names its cross reference
// that way) has to find the bytes that range covers, and a word that renders
// to several bytes still occupies one column, so the map cannot be recovered
// from the flattened row.  Built by the same walk as `display_line_text` so
// the two cannot diverge.
std::vector<std::size_t> display_line_column_offsets(
    const DecodedLogicalRecordSource& record, const DisplayLineIR& line);

// One entry per display column: the word, the record-local token it came from
// and the word's ordinal inside that token.  `token` is
// `static_cast<std::size_t>(-1)` for a space the assembler inserted between two
// tokens, and `word_index` is then meaningless.
struct DisplayLineCellIR {
  std::uint16_t word = 0;
  std::size_t token = static_cast<std::size_t>(-1);
  std::size_t word_index = 0;
};

std::vector<DisplayLineCellIR> display_line_cells(
    const DecodedLogicalRecordSource& record, const DisplayLineIR& line);

// True when `token` is the revision bar in its display line's margin, decided
// from the line's own cells: a one-word blank token at column 0, `token`
// itself (the single word `U+2502` or `|`) at column 1, a blank cell at
// column 2, and at least one drawn cell after it.  The bar is the reader's
// revision margin in front of the row; the row's CFONT/CSELECT columns keep
// counting across it.  Whether a span claims the bar as styled text is the
// caller's question.  False for an unframed record.
bool is_display_line_margin_revision_bar(
    const DecodedLogicalRecordSource& record, std::size_t token);

// Decides every display line's revision mark from its own cells and stores it
// on the line (`DisplayLineIR::margin_mark`).  Called once by the record
// decoder after the record is assembled; a test that assembles a record by
// hand calls it too.  Two stored shapes carry a mark:
//
//  * a body row's margin: a one-cell blank token at cell 0, a single-word
//    token at cell 1 holding `U+2502` or a printable ASCII punctuation
//    character (`|`, `#`, `+`, `:`, ...), a blank cell 2 and at least one
//    drawn cell after it (markup.adoc, "The three-column left margin").  A
//    letter or digit there is text, never a mark;
//  * the `ST` revision column: the line's first token is the two-word opcode
//    `ST` and the cell glued directly after it is not blank
//    (logical-controls.adoc, the title line).
//
// No-op for an unframed record.
void assign_display_line_margin_marks(DecodedLogicalRecordSource& record);

// The stored mark of the display line `token` belongs to, when `token` is
// that mark's own token; `std::nullopt` otherwise.
std::optional<char32_t> display_line_margin_mark_at(
    const DecodedLogicalRecordSource& record, std::size_t token);

// Checks that every stored mark agrees with the line's cells: recomputes the
// marks of a copy and compares.
bool verify_display_line_margin_marks(const DecodedLogicalRecordSource& record,
                                      std::string* error = nullptr);

// True when `token` is a native list bullet on its display line: the single
// word `U+2666` after a blank run (or a blank token behind a margin revision
// bar), followed by a two-blank token and at least one drawn cell. The
// bullet is the row's list marker, never display text. False for an
// unframed record.
bool is_display_line_list_bullet(const DecodedLogicalRecordSource& record,
                                 std::size_t token);

// One trace line per display line: index, the length byte's token and encoded
// value, the line's token range, the hosted display text, and a per-column
// class string (`.` space, `B` box drawing `U+2500`-`U+25FF`, `?` the
// decoder's unmapped word `U+FFFF`, `x` any other word).  The class string is
// what makes a placeholder site decidable: it separates a drawn rule from a
// placeholder glued into a text run without reading the flattened string.
std::string format_display_line_ir(const DecodedLogicalRecordSource& record,
                                   const DisplayLineIR& line,
                                   std::size_t index);

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
// Marks such a segment `display_text` (and demotes a structural or `ST` title
// one to `text`).  The segment boundary itself stays -- the flattened string
// really did split there -- so a consumer must read the segment's payload as
// body text in place.
//
// The `ST` title and `SI` index keyword use the stricter form of the rule: each
// must stand at the first cell of its display line, one token after the
// line's length byte, which every genuine control of a record does.  `ST` is
// too short and too common as display text for "nothing visible in front of
// it" to separate the two -- an indented definition term (`   ST`) and an
// assembler STORE instruction both open no line but have only padding before
// them.
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
