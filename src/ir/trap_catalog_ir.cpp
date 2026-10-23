// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/trap_catalog_ir.hpp"

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/row_font_highlights.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/trap_selector_links.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

using CellKey = std::tuple<std::uint32_t, std::size_t, std::size_t>;
using TokenKey = std::pair<std::uint32_t, std::size_t>;

bool fail(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
  return false;
}

const DecodedLogicalRecordSource *
find_record(const std::vector<DecodedLogicalRecordSource> &records,
            std::uint32_t logical_record) {
  const auto found =
      std::find_if(records.begin(), records.end(), [&](const auto &record) {
        return record.logical_record == logical_record;
      });
  return found == records.end() ? nullptr : &*found;
}

std::string range_text(const DecodedLogicalRecordSource &record,
                       const OutputRangeIR &range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size())
    return {};
  return text.substr(range.begin, range.end - range.begin);
}

std::string token_range_text(const DecodedLogicalRecordSource &record,
                             std::size_t begin, std::size_t end) {
  if (begin >= end || end > record.tokens.size()) return {};
  const std::vector<TokenWords> tokens(record.tokens.begin() + begin,
                                       record.tokens.begin() + end);
  return token_words_to_ascii(assemble_logical_record(tokens));
}

std::string collapse(std::string value) {
  return collapse_ascii_whitespace(trim_ascii(std::move(value)));
}

std::string first_word(std::string value) {
  value = trim_ascii(std::move(value));
  const auto end = value.find_first_of(" \t\r\n");
  return value.substr(0, end);
}

DocumentSourceSliceIR token_slice(const DecodedLogicalRecordSource &record,
                                  std::size_t segment_index,
                                  std::size_t token_begin,
                                  std::size_t token_end) {
  DocumentSourceSliceIR slice;
  slice.logical_record = record.logical_record;
  slice.segment_index = segment_index;
  slice.token_begin = token_begin;
  slice.token_end = token_end;
  if (token_begin < token_end && token_end <= record.ir.tokens.size()) {
    slice.byte_begin = record.ir.tokens[token_begin].byte_range.begin;
    slice.byte_end = record.ir.tokens[token_end - 1].byte_range.end;
  }
  return slice;
}

DocumentSourceSliceIR segment_slice(const DecodedLogicalRecordSource &record,
                                    const ControlSegmentIR &segment) {
  if (segment.source_tokens.empty())
    return {record.logical_record, segment.segment_index, 0, 0, 0, 0};
  return token_slice(record, segment.segment_index,
                     segment.source_tokens.front(),
                     segment.source_tokens.back() + 1);
}

bool structural_padding_words(const TokenWords &words) {
  return !words.empty() &&
         std::all_of(words.begin(), words.end(), [](const auto word) {
           return word == ' ' || word == '?' || word == 0x2666 || word < 0x20;
         });
}

bool marker_attached_title_control(const DecodedLogicalRecordSource &record,
                                   const ControlSegmentIR &segment) {
  if ((segment.kind != BookControlKind::text &&
       segment.kind != BookControlKind::structural) ||
      segment.display_text || segment.malformed ||
      segment.source_tokens.empty())
    return false;
  if (segment.kind == BookControlKind::structural &&
      ascii_lower(segment.opcode) != "st|")
    return false;
  const auto token = segment.source_tokens.front();
  if (token + 2 >= record.ir.tokens.size() ||
      record.ir.tokens[token].decoded_words != TokenWords{'S', 'T'} ||
      record.ir.tokens[token + 1].decoded_words != TokenWords{1} ||
      record.ir.tokens[token + 2].decoded_words != TokenWords{'|'})
    return false;
  const auto *line = display_line_of_token(record, token);
  return line != nullptr && token == line->prefix_token + 1 &&
         token + 2 < line->token_end &&
         display_line_of_token(record, token + 1) == line &&
         display_line_of_token(record, token + 2) == line;
}

// The column `token` opens on its drawn row: its first cell's index among
// the row's display cells, which include blanks no token spells. N2AH1MST
// record 2297 line 19 stores ` ` + `U+2502` + a seven-blank run before its
// bullet, and the row draws a blank between the bar and the run, so the
// bullet stands at column 10 like the unrevised bullet of line 15 (a
// ten-blank run) above it.
std::optional<std::size_t> drawn_column(const DecodedLogicalRecordSource &record,
                                        std::size_t token) {
  const auto *line = display_line_of_token(record, token);
  if (line == nullptr)
    return std::nullopt;
  const auto cells = display_line_cells(record, *line);
  for (std::size_t at = 0; at < cells.size(); ++at)
    if (cells[at].token == token)
      return at;
  return std::nullopt;
}

// The revision margin of a trap row is read from the row's own cells
// (`is_display_line_margin_revision_bar`). SC34-425 2.4.21 record 1602
// line 23 is the length byte (token 124), ` ` (token 125, byte 0x0f at
// 0x1653f8), `|` (token 126, byte 0x70 at 0x1653f9), a `U+2500` run, `0`
// (token 128) and a second `U+2500` run, drawn ` | ____ 0 ____...` under
// `cfont 8 1 2`: column 8 is the `0`, and the unrevised sibling 2.4.22
// record 1604 line 49 (`   ____ 0 ____...`) draws its `0` at the same
// column under the same CFONT.
bool margin_revision_bar(const DecodedLogicalRecordSource &record,
                         std::size_t token) {
  return is_display_line_margin_revision_bar(record, token);
}

bool native_list_bullet(const DecodedLogicalRecordSource &record,
                        std::size_t token) {
  return is_display_line_list_bullet(record, token);
}

bool layout_glyph(char ch) {
  return std::string("-<>/\"=()[{").find(ch) != std::string::npos;
}

bool sentence_punctuation(char ch) {
  return std::string(".,:;!?]}").find(ch) != std::string::npos;
}

bool closes_open_delimiter(const std::string &preceding, char closing) {
  char opener = '\0';
  switch (closing) {
  case ')': opener = '('; break;
  case '>': opener = '<'; break;
  case ']': opener = '['; break;
  case '}': opener = '{'; break;
  default: return false;
  }
  std::size_t depth = 0;
  for (const auto ch : preceding) {
    if (ch == opener)
      ++depth;
    else if (ch == closing && depth != 0)
      --depth;
  }
  return depth != 0;
}

struct RowRef {
  const DisplayRunIR *run = nullptr;
  const PhysicalRowIR *row = nullptr;
  std::size_t row_index = 0;
};

struct SourceIndex {
  std::map<CellKey, const OwnedSourceCellIR *> cells;
  // Display-line length bytes, per record. A record payload is a run of
  // `<length byte><that many bytes of tokens>` display lines
  // (doc/boo-spec/logical-controls.adoc, "Display Lines Inside A Record Payload");
  // the length byte draws nothing whatever dictionary word it resolves to.
  // N2AH1MST record 2284 token 0 is the one-byte value 31 that spells `are`
  // and opens the 31-byte line `   IDC0064I text UPDATED IN CARTRIDGE ...`,
  // which the flattened splitter had cut off as a leading text segment.
  // Tokens of a body-control display line. A line whose whole visible content
  // is one `c.<xx>` opcode at the line origin with at most one operand word
  // after it draws nothing (`prose_topic_lines.cpp`, `body_control_line`:
  // hosted prints neither SC33-033 PREFACE.1's `c.cc 4` nor DREICMST
  // 1.5.6.3's bare `c.cp`). N2AH1MST record 261 line 3 is exactly `c.cc 15`,
  // and the flattened splitter left it inside the surrounding text segment.
  std::set<TokenKey> body_control_tokens;
  // Tokens of a display line no physical row covers. LayoutIR builds no row
  // for a footnote block's payload (N2AH1MST record 177 line 40,
  // `    (*) IBM is a trademark of the IBM Corporation.`, inside
  // `SRFTNFTNUNIQ16` ... `SREFTN`), yet the display line is there and hosted
  // 2.0 prints it. Such a line's printable cells are admitted as unrowed
  // text rather than treated as an ownership gap.
  std::set<TokenKey> unrowed_line_tokens;
  // Tokens of a display line that *is* rowed, but which no row covers: the
  // flattened splitter cut a segment boundary inside the line and LayoutIR
  // ended the row there. N2AH1MST record 3152 line 2 is the single line
  // `          characters (no SO or SI characters were located).`, and the
  // splitter cut `SI ...` off as segment 1; hosted 2.0 (DT 19910329000100)
  // serves the whole line. The tail is drawn text, not an ownership gap.
  std::set<TokenKey> split_line_tokens;
  // Length bytes of empty display lines, per record and in token order. An
  // empty display line is the only paragraph break a record spells.
  std::map<std::uint32_t, std::vector<std::size_t>> empty_line_prefixes;
  std::map<TokenKey, RowRef> marker_rows;
  std::map<TokenKey, RowRef> rows_by_token;
  std::map<TokenKey, std::vector<RowRef>> rows_by_segment;
};

SourceIndex index_sources(const std::vector<DecodedLogicalRecordSource> &records,
                          const LayoutIR &layout,
                          const OwnershipIR &ownership) {
  SourceIndex index;
  const auto visible_word = [](const LogicalTokenIR &token) {
    return std::any_of(token.decoded_words.begin(), token.decoded_words.end(),
                       [](const auto word) { return word > ' '; });
  };
  const auto body_control_opcode = [](const LogicalTokenIR &token) {
    std::string text;
    for (const auto word : token.decoded_words) {
      if (word > 0xff)
        return false;
      text.push_back(static_cast<char>(word));
    }
    if (text.size() < 4 || text.compare(0, 2, "c.") != 0)
      return false;
    return std::all_of(text.begin() + 2, text.end(), [](const unsigned char ch) {
      return std::islower(ch) != 0;
    });
  };
  for (const auto &record : records)
    if (const auto lines = record_display_lines(record))
      for (const auto &line : *lines) {
        const auto first = line.prefix_token + 1;
        if (first >= line.token_end)
          index.empty_line_prefixes[record.logical_record].push_back(
              line.prefix_token);
        if (first >= line.token_end || first >= record.ir.tokens.size() ||
            !body_control_opcode(record.ir.tokens[first]))
          continue;
        std::size_t further = 0;
        for (auto token = first + 1;
             token < line.token_end && token < record.ir.tokens.size(); ++token)
          if (visible_word(record.ir.tokens[token]))
            ++further;
        if (further > 1)
          continue;
        for (auto token = first;
             token < line.token_end && token < record.ir.tokens.size(); ++token)
          index.body_control_tokens.emplace(record.logical_record, token);
      }
  for (const auto &cell : ownership.cells)
    index.cells.emplace(
        CellKey{cell.logical_record, cell.token_index, cell.word_index},
        &cell);
  for (const auto &run : layout.runs)
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto &row = run.rows[row_index];
      RowRef ref{&run, &row, row_index};
      if (row.marker)
        index.marker_rows.emplace(
            TokenKey{row.marker->logical_record, row.marker->token_index},
            ref);
      for (auto token = row.token_begin; token < row.token_end; ++token)
        index.rows_by_token.emplace(TokenKey{row.logical_record, token}, ref);
      index.rows_by_segment[{row.logical_record, row.segment_index}]
          .push_back(ref);
    }
  for (const auto &record : records)
    if (const auto lines = record_display_lines(record))
      for (const auto &line : *lines) {
        bool rowed = false;
        for (auto token = line.prefix_token + 1; token < line.token_end; ++token)
          if (index.rows_by_token.count({record.logical_record, token}) != 0)
            rowed = true;
        if (rowed) {
          for (auto token = line.prefix_token + 1; token < line.token_end;
               ++token)
            if (index.rows_by_token.count({record.logical_record, token}) == 0)
              index.split_line_tokens.emplace(record.logical_record, token);
          continue;
        }
        for (auto token = line.prefix_token + 1; token < line.token_end; ++token)
          index.unrowed_line_tokens.emplace(record.logical_record, token);
      }
  return index;
}

// A segment whose every token is a display-line length byte is not a
// control at all: the byte is the row-control slot and only happens to
// resolve to a control-shaped dictionary word. N2AH1MST record 2365 token 0
// is the one-byte value 37 spelling `cfont`, which opens the 37-byte line
// `   IDC0874I FOLLOWING NOT ALPHABETIC - INSUFFICIENT WORK SPACE FOR SORT`
// whose real `cfont` control is token 1.
// The segment's opcode is the display line's length byte: the flattened
// splitter read the byte as a control word and cut a segment there, but the
// byte is the row-control slot and the rest of the segment is the line's
// display text. N2AH1MST record 2400 token 95 is the one-byte value 37
// spelling `cfont`, and tokens 96-120 are the headline
// `   IDC01551I type CACHING STATUS: stat FOR SD X'ss' DEV X'dd''`.
bool segment_opcode_is_display_line_prefix(
    const DecodedLogicalRecordSource &record,
    const ControlSegmentIR &segment) {
  return !segment.source_tokens.empty() &&
         is_display_line_length_token(record, segment.source_tokens.front());
}

bool segment_is_display_line_prefix(const DecodedLogicalRecordSource &record,
                                    const ControlSegmentIR &segment) {
  return !segment.source_tokens.empty() &&
         std::all_of(segment.source_tokens.begin(), segment.source_tokens.end(),
                     [&](const auto token) {
                       return is_display_line_length_token(record, token);
                     });
}

// An `SRMSG` that fills its display line alone and whose only operand is the
// next line's length byte names no message: the flattened splitter read the
// byte's dictionary word as the operand. SC09-138 F.1 record 2061 line 9 is
// the length byte `01` and `SRMSG` (`77` at 0x1091C9); the next byte, `24`
// at 0x1091CA, is line 10's length (36), spelled `;`. Hosted F.1
// (DT 19910321130500) serves `<a name="MSG">` there, the bare-SRMSG anchor.
bool message_operand_is_next_line_length(
    const DecodedLogicalRecordSource &record, const ControlSegmentIR &segment) {
  if (segment.kind != BookControlKind::message_start ||
      segment.source_tokens.empty() ||
      segment.payload_range.begin != segment.payload_range.end ||
      segment.operand_range.begin == segment.operand_range.end)
    return false;
  const auto *own_line =
      display_line_of_token(record, segment.source_tokens.front());
  if (own_line == nullptr ||
      own_line->prefix_token + 1 != segment.source_tokens.front() ||
      own_line->token_end != segment.source_tokens.front() + 1)
    return false;
  const auto words =
      decoded_byte_range_to_word_range(record.assembled, segment.operand_range);
  // The operand's byte range starts at the separator the assembler wrote
  // after the opcode, so it can touch the opcode's own token; only the
  // tokens after the opcode are the operand's words.
  auto tokens =
      source_tokens_intersecting_output(record.assembled, words.begin, words.end);
  tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                              [&](const auto token) {
                                return token <= segment.source_tokens.front();
                              }),
               tokens.end());
  return tokens.size() == 1 && tokens.front() == own_line->token_end &&
         is_display_line_length_token(record, tokens.front());
}

// A control that draws nothing of its own: `c.sp` is a SCRIPT vertical-space
// directive and `cselect` marks a hotspot region of the *following* display
// line (N2AH1MST record 252 line 10 is the whole line `cselect 34 4
// FTNFTNUNIQ17`, and the `(*)` it points at is drawn by line 11). Neither
// contributes a display cell, so both are admitted only after proving they
// own no visible content.
bool nondrawing_control(const SourceIndex &index,
                        const DecodedLogicalRecordSource &record,
                        const ControlSegmentIR &segment) {
  if (segment.kind != BookControlKind::spacing &&
      segment.kind != BookControlKind::select)
    return false;
  for (const auto token : segment.source_tokens)
    for (std::size_t word = 0; word < record.ir.tokens[token].decoded_words.size();
         ++word) {
      const auto cell = index.cells.find({record.logical_record, token, word});
      if (cell != index.cells.end() &&
          cell->second->disposition == SourceDisposition::visible_content)
        return false;
    }
  return true;
}

bool all_space_token(const LogicalTokenIR &token) {
  return !token.decoded_words.empty() &&
         std::all_of(token.decoded_words.begin(), token.decoded_words.end(),
                     [](const auto word) { return word == ' '; });
}

// The source facts that classify a marker slot (doc/boo-spec/markup.adoc "Repeated
// row-control signatures"): `?` placeholder runs, the decoder sentinel
// (value 4), and single layout glyphs are layout; sentence punctuation
// closes the preceding text, except that a punctuation slot opening a
// payload at the three-space origin is the boundary control itself; a
// dictionary word at the three-space origin is the row boundary control
// unless a padding run follows the origin (the word is then the wrapped
// last word of the previous display line); a dictionary word at any other
// origin is lexical text.
TrapCellRoleIR marker_role(const DecodedLogicalRecordSource &record,
                           const PhysicalRowIR &row, bool payload_leading) {
  const auto &marker = *row.marker;
  const auto &text = marker.decoded_text;
  if (text.empty() || marker.encoded_value == 4 ||
      std::all_of(text.begin(), text.end(),
                  [](const char ch) { return ch == '?'; }))
    return TrapCellRoleIR::layout_marker;
  if (text.size() == 1 && layout_glyph(text.front()))
    return TrapCellRoleIR::layout_marker;
  const auto punctuation =
      text.size() == 1 && sentence_punctuation(text.front());
  const auto lexical =
      std::all_of(text.begin(), text.end(), [](const unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_';
      });
  if (!punctuation && !lexical)
    return TrapCellRoleIR::layout_marker;
  const auto textual = punctuation ? TrapCellRoleIR::punctuation_marker
                                   : TrapCellRoleIR::lexical_marker;
  if (row.native_origin != 3)
    return textual;
  if (punctuation && payload_leading)
    return TrapCellRoleIR::layout_marker;
  // Signature: `<slot><3 spaces>` followed directly by the row text is the
  // boundary control. A padding token or a decoder placeholder after the
  // origin means the slot word is the wrapped tail of the previous display
  // line (`send the trap       to`, `segment.   <sep>   Segment`).
  const auto after_origin = marker.token_index + 2;
  if (after_origin < record.ir.tokens.size() &&
      (all_space_token(record.ir.tokens[after_origin]) ||
       structural_padding_words(record.ir.tokens[after_origin].decoded_words) ||
       std::any_of(record.ir.tokens[after_origin].decoded_words.begin(),
                   record.ir.tokens[after_origin].decoded_words.end(),
                   [](const auto word) { return word > 0xff; })))
    return textual;
  // A word slot that opens its own display line is drawn: the row control
  // and the row's first word are the same cell. N2AH1MST record 2585 line 24
  // is `                    12    No prompting was allowed on a PROMPT
  // request.` inside `IDC3900I`, whose slot carries the boundary signature,
  // and hosted 2.0 (DT 19910329000100) serves `<p>` followed by
  // `                    12    No prompting ...` -- the break *and* the
  // number. Only a slot with drawn text in front of it on the same display
  // line is a control alone.
  if (lexical) {
    const auto *line = display_line_of_token(record, marker.token_index);
    if (line != nullptr) {
      bool drawn_before = false;
      for (auto token = line->prefix_token + 1; token < marker.token_index;
           ++token) {
        const auto *words = display_text_words(record, token);
        if (words != nullptr &&
            std::any_of(words->begin(), words->end(),
                        [](const auto word) { return word > ' '; }))
          drawn_before = true;
      }
      if (!drawn_before)
        return textual;
    }
  }
  return TrapCellRoleIR::layout_marker;
}

// A one-word layout glyph that the record's own framing draws between drawn
// cells of one display line. It is a cell of that line, never the slot of a
// row break, because a row break stands only at a display line's length
// byte. Requires decided framing; an unframed record keeps the slot reading.
bool glyph_drawn_mid_line(const DecodedLogicalRecordSource &record,
                          std::size_t token, std::uint16_t value) {
  if (!record_framing_is_decided(record.ir) || value < 0x20 || value > 0x7f ||
      !layout_glyph(static_cast<char>(value)) ||
      record.ir.tokens[token].decoded_words.size() != 1 ||
      is_display_line_length_token(record, token))
    return false;
  const auto *line = display_line_of_token(record, token);
  if (line == nullptr)
    return false;
  const auto drawn = [&](std::size_t other) {
    const auto *words = display_text_words(record, other);
    return words != nullptr &&
           std::any_of(words->begin(), words->end(), [](const auto word) {
             return word > ' ' && word <= 0xff;
           });
  };
  bool before = false, after = false;
  for (auto other = line->prefix_token + 1; other < token; ++other)
    before = before || drawn(other);
  for (auto other = token + 1; other < line->token_end; ++other)
    after = after || drawn(other);
  return before && after;
}

struct Piece {
  std::size_t token = 0;
  std::size_t column = 0;
  std::string text;
  bool glue = false;
  // Reader spacing before this piece (one space per inserted or padding
  // space cell) so CFONT columns can be mapped onto the display text.
  std::string display_gap;
  std::size_t cell_begin = 0;
  std::size_t columns = 0;
  std::vector<std::size_t> words;
  std::vector<std::size_t> cells;
};

struct NativeListMarker {
  std::size_t token = 0;
  std::size_t column = 0;
  std::size_t cell_begin = 0;
  std::size_t cell_end = 0;
  DocumentSourceSliceIR source;
};

using CellPositions = std::map<CellKey, std::size_t>;

// A payload-less selector whose control line is followed by further selector
// lines, waiting for the selector whose payload draws their shared row.
struct QueuedSelector {
  TrapSelectorSource selector;
  DocumentSourceSliceIR source;
};

std::size_t append_unique_cell(std::vector<TrapSourceCellIR> &cells,
                               CellPositions &positions,
                               const TrapSourceCellIR &cell) {
  const auto key = CellKey{cell.logical_record, cell.token_index,
                           cell.word_index};
  const auto inserted = positions.emplace(key, cells.size());
  if (inserted.second) {
    cells.push_back(cell);
    return cells.size() - 1;
  }
  auto &existing = cells[inserted.first->second];
  if (existing.role == TrapCellRoleIR::layout_marker &&
      cell.role != TrapCellRoleIR::layout_marker)
    existing.role = cell.role;
  return inserted.first->second;
}

std::vector<std::size_t> empty_lines_between(const SourceIndex &index,
                                             std::uint32_t record,
                                             std::size_t from_token,
                                             std::size_t to_token) {
  std::vector<std::size_t> result;
  const auto found = index.empty_line_prefixes.find(record);
  if (found == index.empty_line_prefixes.end())
    return result;
  for (const auto prefix : found->second)
    if (prefix > from_token && prefix < to_token)
      result.push_back(prefix);
  return result;
}

bool same_row(const DocumentSourceRowIR &left,
              const DocumentSourceRowIR &right) {
  return left.display_run == right.display_run &&
         left.row_index == right.row_index;
}

void add_unique_row(std::vector<DocumentSourceRowIR> &rows,
                    const DocumentSourceRowIR &row) {
  if (std::none_of(rows.begin(), rows.end(),
                   [&](const auto &candidate) {
                     return same_row(candidate, row);
                   }))
    rows.push_back(row);
}

bool append_paragraph(TrapFlowParagraphIR &out,
                      TrapFlowParagraphIR piece) {
  if (piece.text.empty())
    return true;
  const auto text_base = out.text.size() + (out.text.empty() ? 0 : 1);
  if (!out.text.empty()) {
    out.text.push_back(' ');
    TrapTextProjectionIR separator;
    separator.begin = text_base - 1;
    separator.end = text_base;
    // Segment concatenation inserts this separator. Do not assign the
    // intervening controls to it merely because their ledger cells happen to
    // lie between the adjacent text segments.
    separator.cell_begin = piece.cell_begin;
    separator.cell_end = piece.cell_begin;
    separator.kind = TrapTextProjectionKindIR::decoded_separator;
    out.projection.push_back(separator);
  }
  for (auto projection : piece.projection) {
    projection.begin += text_base;
    projection.end += text_base;
    out.projection.push_back(std::move(projection));
  }
  for (auto highlight : piece.highlights) {
    highlight.begin += text_base;
    highlight.end += text_base;
    out.highlights.push_back(std::move(highlight));
  }
  for (auto link : piece.links) {
    link.begin += text_base;
    link.end += text_base;
    out.links.push_back(std::move(link));
  }
  out.text += piece.text;
  if (!out.display_text.empty() && !piece.display_text.empty())
    out.display_text.push_back(' ');
  out.display_text += piece.display_text;
  out.source_slices.insert(out.source_slices.end(),
                           piece.source_slices.begin(),
                           piece.source_slices.end());
  for (const auto &row : piece.source_rows)
    add_unique_row(out.source_rows, row);
  if (out.cell_begin == out.cell_end)
    out.cell_begin = piece.cell_begin;
  out.cell_end = piece.cell_end;
  return true;
}

// Appends one block a later control segment opened. A record boundary
// splits the segments but not the source's flow, so the block continues
// whatever the flow left open: a paragraph without a blank row before it
// runs on in the last paragraph, and a paragraph or list drawn deeper than
// an open native list's markers belongs to that list's last item, exactly
// as it would inside one segment. A list at the open list's own marker
// column continues its items. N2AH1MST record 492 line 27 opens the bullet
// `Create a new parmlib member ...`, and record 493 line 0 continues it at
// column 14 with `Then ask the operator ...`; hosted 10.0 (DT
// 19910329000100) serves both rows in one bullet.
void append_flow_block(std::vector<TrapFlowBlockIR> &out,
                       TrapFlowBlockIR block) {
  if (!out.empty() &&
      std::holds_alternative<TrapFlowListIR>(out.back().value)) {
    auto &list = std::get<TrapFlowListIR>(out.back().value);
    if (!list.items.empty()) {
      if (const auto *paragraph =
              std::get_if<TrapFlowParagraphIR>(&block.value);
          paragraph != nullptr &&
          paragraph->first_column > list.marker_column) {
        append_flow_block(list.items.back().blocks, std::move(block));
        return;
      }
      if (auto *more = std::get_if<TrapFlowListIR>(&block.value);
          more != nullptr && more->ordered == list.ordered &&
          more->marker_column >= list.marker_column) {
        if (more->marker_column > list.marker_column) {
          append_flow_block(list.items.back().blocks, std::move(block));
          return;
        }
        for (auto &item : more->items)
          list.items.push_back(std::move(item));
        return;
      }
    }
  }
  if (!out.empty() &&
      std::holds_alternative<TrapFlowParagraphIR>(out.back().value) &&
      std::holds_alternative<TrapFlowParagraphIR>(block.value) &&
      std::get<TrapFlowParagraphIR>(block.value).break_before.empty()) {
    append_paragraph(std::get<TrapFlowParagraphIR>(out.back().value),
                     std::move(std::get<TrapFlowParagraphIR>(block.value)));
    return;
  }
  out.push_back(std::move(block));
}

void append_flow(TrapFlowSequenceIR &out, TrapFlowSequenceIR piece) {
  for (auto &block : piece.blocks)
    append_flow_block(out.blocks, std::move(block));
}

bool add_highlights(TrapFlowSequenceIR &flow,
                    const std::vector<TrapBodyHighlightIR> &ranges) {
  for (const auto &range : ranges) {
    bool placed = false;
    std::size_t base = 0;
    const auto place = [&](const auto &self,
                           std::vector<TrapFlowBlockIR> &blocks) -> void {
      for (auto &block : blocks) {
        if (placed)
          return;
        if (std::holds_alternative<TrapFlowParagraphIR>(block.value)) {
          auto &paragraph = std::get<TrapFlowParagraphIR>(block.value);
          const auto limit = base + paragraph.text.size();
          if (range.begin >= base && range.end <= limit) {
            auto local = range;
            local.begin -= base;
            local.end -= base;
            paragraph.highlights.push_back(std::move(local));
            placed = true;
            return;
          }
          base = limit + 1;
        } else if (std::holds_alternative<TrapFlowListIR>(block.value)) {
          for (auto &item : std::get<TrapFlowListIR>(block.value).items)
            self(self, item.blocks);
        } else if (std::holds_alternative<TrapFlowFootnoteIR>(block.value)) {
          self(self, std::get<TrapFlowFootnoteIR>(block.value).blocks);
        }
      }
    };
    place(place, flow.blocks);
    if (!placed)
      return false;
  }
  return true;
}

TrapFlowParagraphIR *first_paragraph(TrapFlowSequenceIR &flow) {
  const auto find = [&](const auto &self,
                        std::vector<TrapFlowBlockIR> &blocks)
      -> TrapFlowParagraphIR * {
    for (auto &block : blocks) {
      if (std::holds_alternative<TrapFlowParagraphIR>(block.value))
        return &std::get<TrapFlowParagraphIR>(block.value);
      if (std::holds_alternative<TrapFlowListIR>(block.value))
        for (auto &item : std::get<TrapFlowListIR>(block.value).items)
          if (auto *paragraph = self(self, item.blocks);
              paragraph != nullptr)
            return paragraph;
      if (std::holds_alternative<TrapFlowFootnoteIR>(block.value))
        if (auto *paragraph =
                self(self, std::get<TrapFlowFootnoteIR>(block.value).blocks);
            paragraph != nullptr)
          return paragraph;
    }
    return nullptr;
  };
  return find(find, flow.blocks);
}

// Projects one decoded segment payload into text, ledgering every payload
// cell with exactly one role.
// `highlighted_columns` is the display width the segment's own CFONT spans
// reach from the first span's column. A layout glyph inside that width is
// highlighted display text, not a marker slot: N2AH1MST record 197 line 5 is
// `   AHL040 NOT A LEGAL FORM OF THE MACRO. CHECK THE MF=` and its control
// `cfont … 51 3 2` highlights `MF=` through column 54, so the closing `=`
// (its own one-byte token at the segment end) is the third character of a
// highlighted word. Hosted serves `<B>MF=</B>`.
bool walk_payload(const DecodedLogicalRecordSource &record,
                  const ControlSegmentIR &segment, const SourceIndex &index,
                  TrapTextIR &out, std::vector<TrapSourceCellIR> &cells,
                  CellPositions &cell_positions,
                  std::string *error, bool operand_tail = false,
                  std::size_t highlighted_columns = 0,
                  bool whole_segment = false,
                  const std::vector<std::pair<std::size_t, std::size_t>>
                      *highlight_ranges = nullptr,
                  const std::vector<QueuedSelector> *queued = nullptr,
                  const FontControlSpansIR *row_font = nullptr,
                  const std::string *open_context = nullptr) {
  // A revision bar the row's own CFONT styles is a drawn cell of the row,
  // not its margin; any span reaching column 1 keeps every bar of the
  // payload in the text, where the row's column checks then decide.
  const auto font_claims_margin =
      row_font != nullptr &&
      std::any_of(row_font->spans.begin(), row_font->spans.end(),
                  [](const auto &span) {
                    return span.length != 0 && span.column <= 1 &&
                           span.column + span.length > 1;
                  });
  // True when one CFONT span of the segment covers `[from, to)` whole, in the
  // payload's own display coordinates.
  const auto highlight_covers = [&](const std::size_t from,
                                    const std::size_t to) {
    if (highlight_ranges == nullptr)
      return false;
    return std::any_of(highlight_ranges->begin(), highlight_ranges->end(),
                       [&](const auto &range) {
                         return from >= range.first && to <= range.second;
                       });
  };
  auto range = decoded_byte_range_to_word_range(
      record.assembled, whole_segment ? segment.complete : segment.payload_range);
  // The flattened splitter cuts its segments at `, <control>`, so an
  // attach-control comma that ends a drawn row in front of a control line
  // falls between two segments. The record's framing says it is drawn on
  // the row the payload ends on, and it is that row's text: SC34-425
  // APPENDIX1.5.3 record 2827 line 11 ends `Chapter 20` + token 147 (`02`
  // at 0x1C6B1F, the attach control and `,`) before line 12's
  // `cfont 32 2 C ...`, and hosted (DT 19921112160049) serves `Chapter 20,`.
  // A token outside the payload's last display line, a length byte (SC09-138
  // F.1 record 2061 token 60 is line 5's length, spelled `,`) or a token of
  // another segment ends the extension.
  if (!segment.source_tokens.empty() && record_framing_is_decided(record.ir)) {
    const auto payload_tokens =
        source_tokens_intersecting_output(record.assembled, range.begin, range.end);
    const auto last = segment.source_tokens.back();
    const auto *line = !payload_tokens.empty() && payload_tokens.back() == last
                           ? display_line_of_token(record, last)
                           : nullptr;
    for (auto token = last + 1; line != nullptr && token < line->token_end;
         ++token) {
      if (is_display_line_length_token(record, token) ||
          std::any_of(record.control_segments.begin(),
                      record.control_segments.end(), [&](const auto &other) {
                        return std::find(other.source_tokens.begin(),
                                         other.source_tokens.end(),
                                         token) != other.source_tokens.end();
                      }))
        break;
      const auto span = std::find_if(
          record.assembled.tokens.begin(), record.assembled.tokens.end(),
          [&](const auto &candidate) { return candidate.token_index == token; });
      if (span == record.assembled.tokens.end())
        break;
      range.end = std::max(range.end, span->output_end);
    }
  }
  // Every selector this payload's row carries, in source order: the queued
  // selectors whose control lines preceded this one, then its own.
  std::vector<QueuedSelector> selectors;
  std::set<std::pair<std::size_t, std::size_t>> selected_glyphs;
  if (!whole_segment && segment.kind == BookControlKind::select) {
    auto own = read_trap_selector_source(record, segment, error);
    if (!own) return false;
    if (queued != nullptr)
      for (const auto &waiting : *queued) {
        if (waiting.source.logical_record != record.logical_record ||
            waiting.selector.row_prefix_token != own->row_prefix_token)
          return fail(error, "trap queued selector does not share its row");
        selectors.push_back(waiting);
      }
    selectors.push_back({std::move(*own), segment_slice(record, segment)});
    for (const auto &selector : selectors)
      for (const auto &cell : selector.selector.cells)
        if (cell.word != ' ' &&
            !selected_glyphs.emplace(cell.token, cell.word_index).second)
          return fail(error, "trap selectors on one row overlap");
  } else if (queued != nullptr && !queued->empty()) {
    // Selectors that closed the record before mark this record's first row,
    // and this payload must open on that row.
    const auto first_payload_token = [&]() -> std::optional<std::size_t> {
      const auto tokens = source_tokens_intersecting_output(
          record.assembled, range.begin, range.end);
      if (tokens.empty())
        return std::nullopt;
      return tokens.front();
    }();
    const auto *first_line =
        first_payload_token ? display_line_of_token(record, *first_payload_token)
                            : nullptr;
    // Or selectors before a `cfont` line mark the row directly after that
    // font control's own line, in the same record.
    const auto *font_line =
        segment.kind == BookControlKind::font && !segment.source_tokens.empty()
            ? display_line_of_token(record, segment.source_tokens.front())
            : nullptr;
    for (const auto &waiting : *queued) {
      const auto carried = segment.segment_index == 0 &&
                           waiting.source.logical_record + 1 ==
                               record.logical_record &&
                           first_line != nullptr &&
                           waiting.selector.row_prefix_token ==
                               first_line->prefix_token;
      const auto styled = font_line != nullptr &&
                          waiting.source.logical_record ==
                              record.logical_record &&
                          waiting.selector.row_prefix_token ==
                              font_line->token_end;
      if ((!carried && !styled) ||
          waiting.selector.target_record != record.logical_record)
        return fail(error, "trap queued selector has no selector row");
      selectors.push_back(waiting);
    }
    for (const auto &selector : selectors)
      for (const auto &cell : selector.selector.cells)
        if (cell.word != ' ' &&
            !selected_glyphs.emplace(cell.token, cell.word_index).second)
          return fail(error, "trap selectors on one row overlap");
  }
  std::vector<Piece> pieces;
  std::vector<NativeListMarker> list_markers;
  std::string display;
  std::string gap;
  bool pending_space = false;
  bool row_seen = false;
  std::optional<std::size_t> first_token;
  std::optional<std::size_t> last_token;
  std::vector<DocumentSourceRowIR> rows;
  const auto add_row = [&](const RowRef &ref) {
    const DocumentSourceRowIR row{ref.run->id, ref.row_index};
    if (rows.empty() || rows.back().display_run != row.display_run ||
        rows.back().row_index != row.row_index)
      if (std::none_of(rows.begin(), rows.end(), [&](const auto &item) {
            return item.display_run == row.display_run &&
                   item.row_index == row.row_index;
          }))
        rows.push_back(row);
  };
  // Display width of the pieces emitted so far, in the same accumulation the
  // final `display` string uses, so a CFONT column can be tested mid-walk.
  std::size_t running = 0;
  const auto emit_char = [&](std::size_t token, std::size_t ordinal,
                             std::uint16_t word, bool glue) {
    if (pieces.empty() || pieces.back().token != token) {
      Piece piece;
      piece.token = token;
      if (const auto column = drawn_column(record, token))
        piece.column = *column + ordinal;
      else if (const auto *line = display_line_of_token(record, token);
               line != nullptr) {
        for (auto before = line->prefix_token + 1; before < token; ++before)
          piece.column += record.ir.tokens[before].decoded_words.size();
        piece.column += ordinal;
      }
      piece.glue = glue || (!pending_space && !pieces.empty());
      piece.display_gap = piece.glue ? std::string{} : gap;
      piece.cell_begin = cells.size();
      if (!pieces.empty() && !piece.glue)
        running += piece.display_gap.size();
      pieces.push_back(std::move(piece));
    }
    pending_space = false;
    gap.clear();
    pieces.back().text += token_words_to_ascii({word});
    pieces.back().words.push_back(ordinal);
    pieces.back().cells.push_back(cells.size());
    ++pieces.back().columns;
    ++running;
  };
  // A record can open inside a native list row. Its first text segment then
  // begins at the row's first word, and the blank run and bullet in front of
  // it lie in no segment payload. N2AH1MST record 1484 opens with a
  // ten-blank run, `U+2666` and a two-blank run before `Not the first record
  // in the member`, the second bullet of the list whose first item
  // `Missing` closes record 1483. The bullet is that row's marker all the
  // same.
  if (!whole_segment && segment.kind == BookControlKind::text &&
      segment.segment_index == 0) {
    std::optional<std::size_t> first;
    for (auto output = range.begin;
         output < range.end && output < record.assembled.sources.size();
         ++output)
      if (record.assembled.sources[output].kind !=
          LogicalWordSourceKind::inserted_space) {
        first = record.assembled.sources[output].token_index;
        break;
      }
    const auto *line =
        first ? display_line_of_token(record, *first) : nullptr;
    for (auto token = line != nullptr ? line->prefix_token + 1 : 0;
         line != nullptr && token < *first; ++token) {
      if (!native_list_bullet(record, token))
        continue;
      const auto column = drawn_column(record, token);
      if (!column)
        return fail(error, "trap native list marker is not drawn on its row");
      const auto owned = index.cells.find({record.logical_record, token, 0});
      TrapSourceCellIR cell;
      cell.logical_record = record.logical_record;
      cell.token_index = token;
      cell.word_index = 0;
      cell.word = record.ir.tokens[token].decoded_words.front();
      cell.disposition = owned == index.cells.end()
                             ? SourceDisposition::opaque
                             : owned->second->disposition;
      cell.role = TrapCellRoleIR::layout_marker;
      const auto at = append_unique_cell(cells, cell_positions, cell);
      list_markers.push_back(
          {token, *column, at, at + 1,
           token_slice(record, segment.segment_index, token, token + 1)});
      pending_space = true;
      if (gap.empty())
        gap.push_back(' ');
    }
  }
  for (std::size_t output = range.begin;
       output < range.end && output < record.assembled.sources.size();
       ++output) {
    const auto &source = record.assembled.sources[output];
    if (source.kind == LogicalWordSourceKind::inserted_space) {
      pending_space = true;
      gap.push_back(' ');
      continue;
    }
    const auto token = source.token_index;
    const auto word = source.word_index;
    if (token >= record.ir.tokens.size() ||
        word >= record.ir.tokens[token].decoded_words.size())
      return fail(error, "trap payload word has no token provenance");
    const auto value = record.ir.tokens[token].decoded_words[word];
    const auto is_list_marker = word == 0 && native_list_bullet(record, token);
    if (!first_token)
      first_token = token;
    if (last_token && *last_token != token) {
      const auto boundaries = empty_lines_between(
          index, record.logical_record, *last_token, token);
      if (!boundaries.empty())
        out.paragraph_break = true;
      for (const auto prefix : boundaries) {
        const auto already = std::find_if(
            cells.begin(), cells.end(), [&](const auto &cell) {
              return cell.logical_record == record.logical_record &&
                     cell.token_index == prefix;
            });
        if (already != cells.end())
          continue;
        for (std::size_t boundary_word = 0;
             boundary_word <
             record.ir.tokens[prefix].decoded_words.size(); ++boundary_word) {
          const auto owned = index.cells.find(
              {record.logical_record, prefix, boundary_word});
          cells.push_back(
              {record.logical_record, prefix, boundary_word,
               record.ir.tokens[prefix].decoded_words[boundary_word],
               owned == index.cells.end() ? SourceDisposition::opaque
                                          : owned->second->disposition,
               TrapCellRoleIR::layout_marker});
        }
      }
    }
    last_token = token;
    const auto covering = index.rows_by_token.find({record.logical_record, token});
    if (covering != index.rows_by_token.end()) {
      add_row(covering->second);
      row_seen = true;
    }
    const auto owned =
        index.cells.find({record.logical_record, token, word});
    const auto disposition = owned == index.cells.end()
                                 ? SourceDisposition::opaque
                                 : owned->second->disposition;
    TrapSourceCellIR cell;
    cell.logical_record = record.logical_record;
    cell.token_index = token;
    cell.word_index = word;
    cell.word = value;
    cell.disposition = disposition;
    const auto marker = index.marker_rows.find({record.logical_record, token});
    if (is_list_marker) {
      const auto *line = display_line_of_token(record, token);
      if (line == nullptr)
        return fail(error, "trap native list marker has no display row");
      const auto column = drawn_column(record, token);
      if (!column)
        return fail(error, "trap native list marker is not drawn on its row");
      cell.role = TrapCellRoleIR::layout_marker;
      list_markers.push_back(
          {token, *column, cells.size(), cells.size() + 1,
           token_slice(record, segment.segment_index, token, token + 1)});
      pending_space = true;
      if (gap.empty())
        gap.push_back(' ');
    } else if (word == 0 && !font_claims_margin &&
               margin_revision_bar(record, token)) {
      // The row's revision margin draws no text of the entry; the CFONT
      // columns count across it (see `margin_revision_bar`).
      cell.role = TrapCellRoleIR::layout_marker;
      pending_space = true;
      if (gap.empty())
        gap.push_back(' ');
    } else if (is_display_line_length_token(record, token) ||
        index.body_control_tokens.count({record.logical_record, token}) != 0) {
      // The display line's length byte is the row-control slot, always and
      // only; whatever word it resolves to it draws nothing. Nor does a
      // body-control line.
      cell.role = TrapCellRoleIR::layout_marker;
      pending_space = true;
      if (gap.empty())
        gap.push_back(' ');
    } else if (selected_glyphs.count({token, word}) != 0 &&
               value >= 0x20 && value <= 0xff && value != ' ') {
      // Native selector columns prove these glyphs are drawn, including a
      // quote that compatibility geometry mistakes for a closing marker.
      cell.role = TrapCellRoleIR::text;
      emit_char(token, word, value, false);
    } else if (marker != index.marker_rows.end() &&
               glyph_drawn_mid_line(record, token, value)) {
      // A layout glyph with drawn cells after it on its own display line is
      // a cell of that line, whatever row break LayoutIR guessed there.
      // SC34-425 APPENDIX1.5.3 record 2941 line 34 is `   non-editable
      // etc.)  which is incompatible ...` (`)` = `22` at 0x1D0551, then a
      // one-cell blank and `which`); hosted (DT 19921112160049) serves
      // `etc.)  which`.
      add_row(marker->second);
      cell.role = TrapCellRoleIR::text;
      emit_char(token, word, value, false);
    } else if (marker != index.marker_rows.end()) {
      add_row(marker->second);
      cell.role = marker_role(record, *marker->second.row, pieces.empty());
      // A LayoutIR marker slot inside a column the segment's own CFONT
      // highlights is display text, not a row control: N2AH1MST record 2171
      // line 10 is `   ICT1033 CRYPTOGRAPHY CIPHER FUNCTION FAILED -  CODE
      // xx` and its `cfont ... 47 1 2 ...` highlights the `-` at column 47,
      // which hosted serves as `<B>-</B>`. The display line's length byte is
      // the only true row-control slot, and this token is not one.
      //
      // The slot's width is not the evidence; the CFONT column is. N2AH1MST
      // record 2445 line 45 is the whole line `cfont 10 6 3` and record 2446
      // line 0 is `          number    The number of the record update.`:
      // the slot word `number` carries the three-space origin signature of a
      // row control, and the control before it highlights exactly its six
      // columns. Hosted 2.0 (DT 19910329000100) serves
      // `<B><I>number</B></I>    The number of the record update.`. The slot
      // is admitted only when the highlight covers it whole.
      if (cell.role == TrapCellRoleIR::layout_marker &&
          value >= 0x20 && value <= 0xff && value != ' ' &&
          running + (record.ir.tokens[token].decoded_words.size() - word) <=
              highlighted_columns) {
        cell.role = TrapCellRoleIR::text;
        emit_char(token, word, value, false);
      } else if (cell.role == TrapCellRoleIR::punctuation_marker) {
        // A punctuation slot that one CFONT span covers whole is display text
        // the row draws at its own column, not a sentence stop LayoutIR glues
        // to the word in front of it. N2AH1MST record 418 line 10 is
        // ` | ASB029I ASCHPMxx :  LINE num {CLASSADD | CLASSDEL} STATEMENT
        // IGNORED.  NO` under `cfont 3 7 2 11 6 2 17 2 V 20 1 2 ...`: the `:`
        // has a span of its own at column 20 and the line draws a space at
        // column 19, and hosted 2.0 (DT 19910329000100) serves
        // `<B>ASCHPM</B><var>xx</var> <B>:</B>  <B>LINE</B>`. Only a span
        // that covers the slot whole is evidence; the extent between two
        // spans is not.
        const auto at = running + (pending_space ? gap.size() : 0);
        const auto width =
            record.ir.tokens[token].decoded_words.size() - word;
        if (value >= 0x20 && value <= 0xff && value != ' ' &&
            highlight_covers(at, at + width)) {
          cell.role = TrapCellRoleIR::text;
          emit_char(token, word, value, false);
        } else if (value <= 0xff) {
          emit_char(token, word, value, true);
        }
      } else if (cell.role == TrapCellRoleIR::lexical_marker) {
        if (value <= 0xff && value >= 0x20)
          emit_char(token, word, value, false);
      } else {
        pending_space = true;
        if (gap.empty())
          gap.push_back(' ');
      }
    } else if (disposition == SourceDisposition::control_operand) {
      cell.role = TrapCellRoleIR::control;
    } else if (value < 0x20) {
      cell.role = TrapCellRoleIR::spacing;
    } else if (value == ' ') {
      cell.role = TrapCellRoleIR::spacing;
      pending_space = true;
      gap.push_back(' ');
    } else if (value > 0xff ||
               structural_padding_words(record.ir.tokens[token].decoded_words)) {
      // A cell one CFONT span covers whole is display text, whatever its
      // token looks like on its own. N2AH1MST record 2113 line 1 is
      // `   ICP050D SHOULD jjobname WRITE TO LEVEL xxx IOCDS? REPLY 'YES',
      // 'NO', OR` and its `cfont ... 46 6 2 ...` covers columns 46-51, the
      // last of which is the `?`. Read alone that token is a placeholder run
      // -- the shape the decoder also uses for an unmapped word -- but the
      // span states it is drawn, and hosted 2.0 (DT 19910329000100) serves
      // `<B>IOCDS?</B>`.
      const auto at = running + (pending_space ? gap.size() : 0);
      const auto width = record.ir.tokens[token].decoded_words.size() - word;
      if (value >= 0x20 && value <= 0xff && value != ' ' &&
          highlight_covers(at, at + width)) {
        cell.role = TrapCellRoleIR::text;
        emit_char(token, word, value, false);
      } else {
        cell.role = TrapCellRoleIR::placeholder;
        pending_space = true;
        if (gap.empty())
          gap.push_back(' ');
      }
    } else if (disposition == SourceDisposition::visible_content) {
      cell.role = TrapCellRoleIR::text;
      emit_char(token, word, value, false);
    } else if (disposition == SourceDisposition::layout_origin ||
               disposition == SourceDisposition::layout_padding) {
      cell.role = TrapCellRoleIR::spacing;
      pending_space = true;
      gap.push_back(' ');
    } else if (disposition == SourceDisposition::marker_slot) {
      cell.role = TrapCellRoleIR::layout_marker;
      pending_space = true;
      if (gap.empty())
        gap.push_back(' ');
    } else {
      // LayoutIR loses positioned provenance for a record prefix (the
      // continuation of a control whose payload ran past the record end), for
      // the gap between two physical rows of one payload (a sentence stop
      // before a wide padding run), and for the SRMSG operand tail. An
      // unowned printable cell anywhere else is not explained.
      if (covering != index.rows_by_token.end() ||
          (!operand_tail && segment.segment_index != 0 && !row_seen &&
           index.unrowed_line_tokens.count({record.logical_record, token}) == 0 &&
           index.split_line_tokens.count({record.logical_record, token}) == 0))
        return fail(error, "trap payload has an unowned cell outside a record "
                           "prefix or row gap at LR" +
                               std::to_string(record.logical_record) +
                               " token " + std::to_string(token));
      cell.role = TrapCellRoleIR::unrowed_text;
      emit_char(token, word, value, false);
    }
    const auto prospective_index = cells.size();
    const auto cell_index = append_unique_cell(cells, cell_positions, cell);
    if (cell_index != prospective_index) {
      for (auto &piece : pieces)
        for (auto &piece_cell : piece.cells)
          if (piece_cell == prospective_index)
            piece_cell = cell_index;
      for (auto &marker : list_markers)
        if (marker.cell_begin == prospective_index) {
          marker.cell_begin = cell_index;
          marker.cell_end = cell_index + 1;
        }
    }
  }
  // A single layout glyph that closes the segment, or that a padding run of
  // two or more spaces follows, is the marker slot of a display line whose
  // origin token the following control or padding swallowed, unless it
  // closes a delimiter that the preceding text of the same segment left
  // open. Such glyphs are conserved as terminal_glyph cells. The text the
  // payload continues counts as preceding text: a field can open its
  // bracket in an earlier segment (SC34-425 APPENDIX1.5.3 `(See Chapter 10,
  // ...` before a `cselect` line, closed by `data sets)` two rows later).
  std::string preceding = open_context != nullptr ? *open_context : "";
  std::vector<Piece> kept;
  // Display offsets of the pieces before any of them is withheld, so a CFONT
  // column can be compared against them.
  std::vector<std::size_t> piece_end;
  {
    std::size_t columns = 0;
    for (const auto &piece : pieces) {
      if (columns != 0 && !piece.glue)
        columns += piece.display_gap.size();
      columns += piece.columns;
      piece_end.push_back(columns);
    }
  }
  for (std::size_t index = 0; index < pieces.size(); ++index) {
    const auto &piece = pieces[index];
    const auto &token = record.ir.tokens[piece.token];
    const auto next = piece.token + 1;
    const auto followed_by_padding =
        index + 1 == pieces.size() ||
        (next < record.ir.tokens.size() &&
         all_space_token(record.ir.tokens[next]) &&
         record.ir.tokens[next].decoded_words.size() >= 2);
    const auto inside_highlight =
        out.display_text.empty() && piece_end[index] <= highlighted_columns;
    const auto inside_selector = std::any_of(piece.words.begin(), piece.words.end(),
        [&](auto word) { return selected_glyphs.count({piece.token, word}) != 0; });
    if (!inside_highlight && !inside_selector && token.encoded.width == 1 &&
        piece.text.size() == 1 &&
        layout_glyph(piece.text.front()) && followed_by_padding &&
        !closes_open_delimiter(preceding, piece.text.front())) {
      for (auto &cell : cells)
        if (cell.logical_record == record.logical_record &&
            cell.token_index == piece.token &&
            cell.role == TrapCellRoleIR::text)
          cell.role = TrapCellRoleIR::terminal_glyph;
      continue;
    }
    preceding += piece.text;
    kept.push_back(piece);
  }
  pieces = std::move(kept);
  std::string text;
  std::map<std::pair<std::size_t, std::size_t>, std::size_t> glyph_offsets;
  bool pending_gap = false;
  display.clear();
  for (const auto &piece : pieces) {
    if (!text.empty() && !piece.glue) pending_gap = true;
    for (const auto ordinal : piece.words) {
      const auto value = record.tokens[piece.token][ordinal];
      if (value == ' ') { pending_gap = true; continue; }
      if (pending_gap && !text.empty()) text.push_back(' ');
      pending_gap = false;
      glyph_offsets[{piece.token, ordinal}] = text.size();
      text += token_words_to_ascii({value});
    }
    if (!display.empty() && !piece.glue)
      display += piece.display_gap;
    display += piece.text;
  }
  text = collapse(std::move(text));
  std::vector<TrapTextLinkIR> local_links;
  for (const auto &selector : selectors) {
    auto local_link = project_trap_selector_link(selector.selector, text,
                                                 glyph_offsets, selector.source,
                                                 error);
    if (!local_link) return false;
    auto link = *local_link;
    const auto base = out.text.size() + (out.text.empty() ? 0 : 1);
    link.begin += base;
    link.end += base;
    out.links.push_back(std::move(link));
    local_links.push_back(std::move(*local_link));
  }
  if (!out.text.empty() && !text.empty())
    out.text.push_back(' ');
  out.text += text;
  display = trim_ascii(std::move(display));
  if (!out.display_text.empty() && !display.empty())
    out.display_text.push_back(' ');
  out.display_text += display;
  if (first_token && last_token)
    out.source_slices.push_back(token_slice(record, segment.segment_index,
                                            *first_token, *last_token + 1));
  out.source_rows.insert(out.source_rows.end(), rows.begin(), rows.end());

  // Build the canonical paragraph-local projection from the same kept source
  // pieces. This is intentionally done before any later segment is appended:
  // byte coordinates, source cells and blank-row controls therefore stay
  // local to the paragraph they actually name.
  TrapFlowSequenceIR flow;
  TrapFlowParagraphIR paragraph;
  struct ListFrame {
    std::size_t column = 0;
    TrapFlowListIR list;
    TrapFlowListItemIR item;
    bool has_item = false;
  };
  std::vector<ListFrame> lists;
  std::optional<std::size_t> paragraph_first_token;
  std::optional<std::size_t> paragraph_last_token;
  std::optional<std::size_t> previous_event_token;
  const auto destination = [&]() -> std::vector<TrapFlowBlockIR> & {
    return lists.empty() ? flow.blocks : lists.back().item.blocks;
  };
  const auto finish_paragraph = [&]() {
    if (!paragraph_first_token || !paragraph_last_token)
      return;
    paragraph.display_text = trim_ascii(std::move(paragraph.display_text));
    paragraph.cell_end = paragraph.projection.back().cell_end;
    paragraph.source_slices.push_back(token_slice(
        record, segment.segment_index, *paragraph_first_token,
        *paragraph_last_token + 1));
    destination().push_back({std::move(paragraph)});
    paragraph = TrapFlowParagraphIR{};
    paragraph_first_token.reset();
    paragraph_last_token.reset();
  };
  const auto finish_item = [&]() {
    finish_paragraph();
    if (!lists.empty() && lists.back().has_item) {
      lists.back().list.items.push_back(std::move(lists.back().item));
      lists.back().item = TrapFlowListItemIR{};
      lists.back().has_item = false;
    }
  };
  const auto close_list = [&]() {
    finish_item();
    auto list = std::move(lists.back().list);
    lists.pop_back();
    auto block = TrapFlowBlockIR{std::move(list)};
    if (lists.empty())
      flow.blocks.push_back(std::move(block));
    else
      lists.back().item.blocks.push_back(std::move(block));
  };
  std::vector<TrapParagraphBoundaryIR> pending_boundaries;
  const auto collect_boundaries = [&](const std::size_t from,
                                      const std::size_t to) {
    for (const auto prefix :
         empty_lines_between(index, record.logical_record, from, to)) {
        std::size_t cell_begin = cells.size(), cell_end = cells.size();
        for (std::size_t cell = 0; cell < cells.size(); ++cell)
          if (cells[cell].logical_record == record.logical_record &&
              cells[cell].token_index == prefix) {
            cell_begin = std::min(cell_begin, cell);
            cell_end = std::max(cell_end == cells.size() ? cell + 1 : cell_end,
                                cell + 1);
          }
        if (cell_begin == cells.size())
          return false;
        pending_boundaries.push_back(
            {token_slice(record, segment.segment_index, prefix, prefix + 1),
             cell_begin, cell_end});
    }
    return true;
  };
  const auto begin_item = [&](const NativeListMarker &marker) {
    lists.back().item = TrapFlowListItemIR{};
    lists.back().item.marker_source = marker.source;
    lists.back().item.marker_cell_begin = marker.cell_begin;
    lists.back().item.marker_cell_end = marker.cell_end;
    lists.back().has_item = true;
  };
  const auto accept_marker = [&](const NativeListMarker &marker) {
    finish_paragraph();
    if (lists.empty()) {
      ListFrame frame;
      frame.column = marker.column;
      frame.list.marker_column = marker.column;
      lists.push_back(std::move(frame));
      begin_item(marker);
      return true;
    }
    if (marker.column > lists.back().column) {
      if (!lists.back().has_item)
        return false;
      ListFrame frame;
      frame.column = marker.column;
      frame.list.marker_column = marker.column;
      lists.push_back(std::move(frame));
      begin_item(marker);
      return true;
    }
    while (!lists.empty() && marker.column < lists.back().column)
      close_list();
    if (lists.empty() || marker.column != lists.back().column)
      return false;
    finish_item();
    begin_item(marker);
    return true;
  };
  const auto accept_piece = [&](const Piece &piece) {
    if (!paragraph_first_token)
      paragraph_first_token = piece.token;
    paragraph_last_token = piece.token;
    if (paragraph.text.empty()) {
      if (piece.cells.empty())
        return false;
      paragraph.first_column = piece.column;
      paragraph.cell_begin = piece.cells.front();
      paragraph.break_before = std::move(pending_boundaries);
      pending_boundaries.clear();
    }
    if (!paragraph.text.empty() && !piece.glue) {
      const auto begin = paragraph.text.size();
      paragraph.text.push_back(' ');
      const auto next_cell = piece.cells.empty() ? paragraph.cell_end
                                                 : piece.cells.front();
      const auto previous_cell = paragraph.projection.empty()
                                     ? paragraph.cell_begin
                                     : paragraph.projection.back().cell_end;
      paragraph.projection.push_back(
          {begin, begin + 1, previous_cell, next_cell,
           previous_cell == next_cell
               ? TrapTextProjectionKindIR::decoded_separator
               : TrapTextProjectionKindIR::source_spacing});
    }
    if (!paragraph.display_text.empty() && !piece.glue)
      paragraph.display_text += piece.display_gap;
    paragraph.display_text += piece.text;
    for (std::size_t word_index = 0; word_index < piece.words.size(); ++word_index) {
      const auto ordinal = piece.words[word_index];
      const auto value = record.tokens[piece.token][ordinal];
      if (value == ' ')
        continue;
      const auto glyph = token_words_to_ascii({value});
      const auto begin = paragraph.text.size();
      paragraph.text += glyph;
      const auto cell = piece.cells[word_index];
      paragraph.projection.push_back(
          {begin, begin + glyph.size(), cell, cell + 1,
           TrapTextProjectionKindIR::source_glyph});
    }
    const auto covering = index.rows_by_token.find({record.logical_record,
                                                     piece.token});
    if (covering != index.rows_by_token.end())
      add_unique_row(paragraph.source_rows,
                     {covering->second.run->id, covering->second.row_index});
    return true;
  };
  std::size_t piece_index = 0, marker_index = 0;
  while (piece_index < pieces.size() || marker_index < list_markers.size()) {
    const auto marker_next = marker_index < list_markers.size() &&
        (piece_index == pieces.size() ||
         list_markers[marker_index].token < pieces[piece_index].token);
    const auto token = marker_next ? list_markers[marker_index].token
                                   : pieces[piece_index].token;
    if (previous_event_token) {
      const auto prior_boundary_count = pending_boundaries.size();
      if (!collect_boundaries(*previous_event_token, token))
        return fail(error, "trap paragraph boundary has no ledger cell");
      if (pending_boundaries.size() != prior_boundary_count) {
        finish_paragraph();
        if (!marker_next)
          // A blank row does not itself end a list item. Native indentation
          // does: a following paragraph deeper than the marker remains in
          // the current item, while a row at or left of the marker returns to
          // the containing flow.
          while (!lists.empty() &&
                 pieces[piece_index].column <= lists.back().column)
            close_list();
      }
    }
    if (marker_next) {
      if (!accept_marker(list_markers[marker_index++]))
        return fail(error, "trap native list indentation is inconsistent");
    } else if (!accept_piece(pieces[piece_index++])) {
      return fail(error, "trap paragraph has no source text cell");
    }
    previous_event_token = token;
  }
  finish_paragraph();
  while (!lists.empty())
    close_list();
  for (const auto &local_link : local_links) {
    bool placed = false;
    std::size_t base = 0;
    const auto place = [&](const auto &self,
                           std::vector<TrapFlowBlockIR> &blocks) -> void {
      for (auto &block : blocks) {
        if (placed)
          return;
        if (std::holds_alternative<TrapFlowParagraphIR>(block.value)) {
          auto &candidate = std::get<TrapFlowParagraphIR>(block.value);
          const auto limit = base + candidate.text.size();
          if (local_link.begin >= base && local_link.end <= limit) {
            auto link = local_link;
            link.begin -= base;
            link.end -= base;
            candidate.links.push_back(std::move(link));
            placed = true;
            return;
          }
          base = limit + 1;
        } else if (std::holds_alternative<TrapFlowListIR>(block.value)) {
          for (auto &item : std::get<TrapFlowListIR>(block.value).items)
            self(self, item.blocks);
        } else if (std::holds_alternative<TrapFlowFootnoteIR>(block.value)) {
          self(self, std::get<TrapFlowFootnoteIR>(block.value).blocks);
        }
      }
    };
    place(place, flow.blocks);
    if (!placed)
      return fail(error, "trap selector crosses a paragraph boundary");
  }
  append_flow(out.flow, std::move(flow));
  return true;
}


// The record-local token the segment's payload opens on, or nothing when the
// payload is empty.
std::optional<std::size_t>
payload_first_token(const DecodedLogicalRecordSource &record,
                    const ControlSegmentIR &segment) {
  const auto range =
      decoded_byte_range_to_word_range(record.assembled, segment.payload_range);
  for (auto output = range.begin;
       output < range.end && output < record.assembled.sources.size();
       ++output) {
    const auto &source = record.assembled.sources[output];
    if (source.kind == LogicalWordSourceKind::inserted_space)
      continue;
    return source.token_index;
  }
  return std::nullopt;
}

std::string control_identifier(const std::string &opcode,
                               const std::string &prefix) {
  if (opcode.size() <= prefix.size() ||
      !ascii_starts_with_case_insensitive(opcode, prefix))
    return {};
  return opcode.substr(prefix.size());
}

// The control segment a record-local token belongs to, or the record's own
// segment count when no segment claims it.
std::vector<std::size_t>
segment_of_token(const DecodedLogicalRecordSource &record) {
  std::vector<std::size_t> owner(record.ir.tokens.size(),
                                 record.control_segments.size());
  for (const auto &segment : record.control_segments)
    for (const auto token : segment.source_tokens)
      if (token < owner.size() &&
          owner[token] == record.control_segments.size())
        owner[token] = segment.segment_index;
  return owner;
}

// The `CFONT` a region's markup line carries: the control that owns the
// line's first content token, when that control is a font control. The row
// drawn directly below is the one its spans name.
std::optional<FontControlSpansIR>
region_markup_font(const DecodedLogicalRecordSource &record,
                   const DisplayLineIR &line) {
  const auto first = line.prefix_token + 1;
  if (first >= line.token_end)
    return std::nullopt;
  for (const auto &segment : record.control_segments) {
    if (std::find(segment.source_tokens.begin(), segment.source_tokens.end(),
                  first) == segment.source_tokens.end())
      continue;
    if (segment.kind != BookControlKind::font)
      return std::nullopt;
    return decode_font_control_spans(record, segment);
  }
  return std::nullopt;
}

// Maps one drawn region row's `CFONT` spans onto its projected text
// (`display_row_font_highlights`, the rule every verbatim region shares).
// N2AH1MST record 533 line 17 is ` |          ACTIVE LU'S ...` under
// `cfont 12 6 2 19 4 2 ...`, and hosted BookServer (DT 19910329000100) serves
// `<B>ACTIVE</B> <B>LU'S</B>` at exactly those columns.
std::vector<TrapBodyHighlightIR>
region_row_highlights(const std::vector<DisplayLineCellIR> &cells,
                      const std::vector<std::size_t> &offsets,
                      const FontControlSpansIR &font) {
  std::vector<TrapBodyHighlightIR> highlights;
  for (const auto &run : display_row_font_highlights(cells, offsets, font))
    highlights.push_back({run.begin, run.end, run.style});
  return highlights;
}

// A revision bar in a drawn region row: the row opens with a one-cell blank
// and a width-one `U+2502` or `|` at column 1, followed by a blank or the
// row's end, with no other box-drawing cell anywhere in the row and no span
// styling the bar. It is the reader's revision margin, the same mark the
// prose and fixed-table rows already leave out of their text (a one-space
// origin followed by the bar). N2AH1MST record 533 lines 17-21 and record
// 534 lines 0-2 are ` |` + the figure row. A box drawn into the region
// keeps its rules: packet `1.3` stores its left rule at column 3 beside a
// matching right rule.
bool region_revision_bar(const std::vector<DisplayLineCellIR> &cells,
                         const std::vector<TrapBodyHighlightIR> &highlights,
                         const std::vector<std::size_t> &offsets) {
  if (cells.size() < 2 || cells[0].word != ' ' ||
      (cells[1].word != 0x2502 && cells[1].word != '|') ||
      (cells.size() > 2 && cells[2].word != ' '))
    return false;
  for (std::size_t column = 2; column < cells.size(); ++column)
    if (cells[column].word >= 0x2500 && cells[column].word <= 0x257f)
      return false;
  for (const auto &highlight : highlights)
    if (highlight.begin < offsets[2] && highlight.end > offsets[1])
      return false;
  return true;
}

// Projects the display lines an embedded `SRTBL` ... `SRETBL` envelope draws.
//
// The bounds are given in the records' own token coordinates: `start_token`
// is the first token of the opening control's payload (inclusive) and
// `end_token` the first source token of the closing control (exclusive). A
// display line is the region's when every one of its tokens lies between
// them; a line that holds tokens on both sides of a bound is not decidable
// here and the whole family declines rather than guess where the envelope's
// drawing begins.
bool project_embedded_region(const std::vector<DecodedLogicalRecordSource> &records,
                             const SourceIndex &index,
                             std::uint32_t start_record,
                             std::size_t start_token,
                             std::uint32_t end_record, std::size_t end_token,
                             TrapEmbeddedRegionIR &region,
                             std::vector<TrapSourceCellIR> &cells,
                             CellPositions &cell_positions,
                             std::string *error,
                             std::vector<TrapIndexTermIR> *index_terms =
                                 nullptr) {
  const auto identity = region.identifier.empty() ? std::string{"SRTBL"}
                                                  : region.identifier;
  // The font control of the markup line just walked, waiting for the drawn
  // row directly below it. Any other line in between drops it.
  std::optional<FontControlSpansIR> pending_font;
  for (auto logical = start_record; logical <= end_record; ++logical) {
    const auto *record = find_record(records, logical);
    if (record == nullptr)
      return fail(error, "trap table envelope names a missing record: " +
                             identity);
    const auto *lines = record_display_lines(*record);
    if (lines == nullptr)
      return fail(error,
                  "trap table envelope record has no display-line framing: " +
                      identity);
    const auto owner = segment_of_token(*record);
    for (const auto &line : *lines) {
      const auto lower =
          logical == start_record ? start_token : std::size_t{0};
      const auto upper = logical == end_record
                             ? end_token
                             : std::numeric_limits<std::size_t>::max();
      const auto in_region = [&](const std::size_t token) {
        return (logical != start_record || token >= lower) &&
               (logical != end_record || token < upper);
      };
      // The line's length byte is structure, never drawing, so the bounds
      // are decided on the line's content tokens; a line that draws nothing
      // is placed by its own length byte.
      std::size_t inside = 0;
      std::size_t outside = 0;
      for (auto token = line.prefix_token + 1; token < line.token_end; ++token)
        (in_region(token) ? inside : outside)++;
      if (inside == 0 && outside == 0)
        inside = in_region(line.prefix_token) ? 1 : 0;
      if (inside == 0) {
        pending_font.reset();
        continue;
      }
      if (outside != 0)
        return fail(error,
                    "trap table envelope boundary falls inside a display "
                    "line: " +
                        identity);
      // Where the caller asks for them, `SI` lines are index terms of the
      // region: they draw nothing, and their cells are control.
      if (index_terms != nullptr &&
          is_subject_index_display_line(*record, line)) {
        const auto keyword = line.prefix_token + 1;
        auto term = collapse(display_line_text(*record, line));
        const auto keyword_text = token_words_to_ascii(record->tokens[keyword]);
        term = term.compare(0, keyword_text.size(), keyword_text) == 0
                   ? collapse(term.substr(keyword_text.size()))
                   : std::string{};
        if (term.empty())
          return fail(error, "trap region subject index line names no term: " +
                                 identity);
        for (auto token = line.prefix_token;
             token < line.token_end && token < record->ir.tokens.size();
             ++token) {
          const auto &words = record->ir.tokens[token].decoded_words;
          for (std::size_t word = 0; word < words.size(); ++word) {
            const auto owned = index.cells.find({logical, token, word});
            append_unique_cell(
                cells, cell_positions,
                {logical, token, word, words[word],
                 owned == index.cells.end() ? SourceDisposition::opaque
                                            : owned->second->disposition,
                 token == line.prefix_token ? TrapCellRoleIR::layout_marker
                                            : TrapCellRoleIR::control});
          }
        }
        index_terms->push_back(
            {term, token_slice(*record, owner[keyword], keyword,
                               line.token_end)});
        pending_font.reset();
        continue;
      }
      // A display line whose printable cells are all control operands is the
      // region's own markup -- the `CFONT` that highlights the row below it
      // -- and draws nothing. Its cells are still ledgered; no line is.
      bool printable = false;
      bool drawn = false;
      for (auto token = line.prefix_token + 1;
           token < line.token_end && token < record->ir.tokens.size();
           ++token) {
        if (is_display_line_length_token(*record, token))
          continue;
        const auto &words = record->ir.tokens[token].decoded_words;
        for (std::size_t word = 0; word < words.size(); ++word) {
          if (words[word] <= ' ')
            continue;
          printable = true;
          const auto owned = index.cells.find({logical, token, word});
          if (owned == index.cells.end() ||
              owned->second->disposition != SourceDisposition::control_operand)
            drawn = true;
        }
      }
      bool ledgered = false;
      for (auto token = line.prefix_token;
           token < line.token_end && token < record->ir.tokens.size();
           ++token) {
        if (!in_region(token))
          continue;
        const auto &words = record->ir.tokens[token].decoded_words;
        for (std::size_t word = 0; word < words.size(); ++word) {
          ledgered = true;
          const auto owned = index.cells.find({logical, token, word});
          TrapSourceCellIR cell;
          cell.logical_record = logical;
          cell.token_index = token;
          cell.word_index = word;
          cell.word = words[word];
          cell.disposition = owned == index.cells.end()
                                 ? SourceDisposition::opaque
                                 : owned->second->disposition;
          if (is_display_line_length_token(*record, token))
            cell.role = TrapCellRoleIR::layout_marker;
          else if (cell.disposition == SourceDisposition::control_operand)
            cell.role = TrapCellRoleIR::control;
          else if (cell.word == ' ' || cell.word < 0x20)
            cell.role = TrapCellRoleIR::spacing;
          else if (cell.word > 0xff)
            cell.role = TrapCellRoleIR::placeholder;
          else if (cell.disposition == SourceDisposition::visible_content)
            cell.role = TrapCellRoleIR::text;
          else if (cell.disposition == SourceDisposition::layout_origin ||
                   cell.disposition == SourceDisposition::layout_padding)
            cell.role = TrapCellRoleIR::spacing;
          else if (cell.disposition == SourceDisposition::marker_slot)
            cell.role = TrapCellRoleIR::layout_marker;
          else
            // The region is projected from the record's own display-line
            // framing, so a cell LayoutIR left unpositioned is still drawn
            // and still ledgered; it is not an ownership gap here.
            cell.role = TrapCellRoleIR::unrowed_text;
          append_unique_cell(cells, cell_positions, cell);
        }
      }
      if (!ledgered) {
        pending_font.reset();
        continue;
      }
      if (printable && !drawn) {
        pending_font = region_markup_font(*record, line);
        continue;
      }
      const auto row_cells = display_line_cells(*record, line);
      const auto offsets = display_line_column_offsets(*record, line);
      auto text = display_line_text(*record, line);
      std::vector<TrapBodyHighlightIR> highlights;
      if (pending_font)
        highlights = region_row_highlights(row_cells, offsets, *pending_font);
      pending_font.reset();
      if (region_revision_bar(row_cells, highlights, offsets)) {
        const auto bar_bytes = offsets[2] - offsets[1];
        text.replace(offsets[1], bar_bytes, " ");
        for (auto &highlight : highlights) {
          highlight.begin = highlight.begin - bar_bytes + 1;
          highlight.end = highlight.end - bar_bytes + 1;
        }
      }
      while (!text.empty() && text.back() == ' ')
        text.pop_back();
      region.lines.push_back(std::move(text));
      region.line_highlights.push_back(std::move(highlights));
      region.line_sources.push_back(
          token_slice(*record, owner[line.prefix_token], line.prefix_token,
                      line.token_end));
    }
  }
  while (!region.lines.empty() && region.lines.front().empty()) {
    region.lines.erase(region.lines.begin());
    region.line_highlights.erase(region.line_highlights.begin());
    region.line_sources.erase(region.line_sources.begin());
  }
  while (!region.lines.empty() && region.lines.back().empty()) {
    region.lines.pop_back();
    region.line_highlights.pop_back();
    region.line_sources.pop_back();
  }
  if (region.lines.empty())
    return fail(error, "trap table envelope draws no display line: " + identity);
  return true;
}

// The ordinary payload projector emits Unicode words through U+00FF as
// UTF-8. Map those encoded byte offsets back to their single display columns;
// malformed or unsupported text cannot corroborate a font span.
std::optional<std::vector<std::size_t>> projected_column_offsets(
    const std::string& text) {
  std::vector<std::size_t> offsets{0};
  for (std::size_t at = 0; at < text.size();) {
    const auto byte = static_cast<unsigned char>(text[at]);
    if (byte < 0x80) ++at;
    else if ((byte == 0xc2 || byte == 0xc3) && at + 1 < text.size() &&
             (static_cast<unsigned char>(text[at + 1]) & 0xc0) == 0x80)
      at += 2;
    else
      return std::nullopt;
    offsets.push_back(at);
  }
  return offsets;
}

// Leading run of CFONT spans that opens the row, verified against the row's
// own display text: the run starts at `origin_column`, and between two
// consecutive spans the display text holds nothing but spaces. Nothing about
// the gap width is assumed. N2AH1MST record 2280 line 12 (`cfont 3 8 2 12 7 2
// 19 3 V` over `IDC0014I LASTCC=cde`) abuts two spans with no gap at all
// because the word is `LASTCC=cde`; record 305 line 26 (`cfont … 20 6 2 28 5
// 2 …` over `AMA133I CHECKSUM ERROR.  NO-GO SWITCH SET`) leaves two spaces
// after the sentence stop. Both are the same fact — the gap is spacing — and
// the display text is the evidence for it.
//
// The longest valid opening run is returned; a span the display text
// disproves ends the run rather than failing, so a highlighted word later in
// a field's text stays out of the label.
std::vector<FontSpanIR> map_leading_chain(const FontControlSpansIR &spans,
                                          std::size_t origin_column,
                                          const std::string &display,
                                          std::vector<TrapStyledSpanIR> &out,
                                          std::string &joined) {
  out.clear();
  joined.clear();
  const auto offsets = projected_column_offsets(display);
  if (!offsets) return {};
  const auto columns = offsets->size() - 1;
  auto sorted = spans.spans;
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const auto &left, const auto &right) {
                     return left.column < right.column;
                   });
  std::vector<FontSpanIR> chain;
  std::size_t previous_end = 0;
  for (const auto &span : sorted) {
    if (span.length == 0 || span.column < origin_column)
      break;
    const auto offset = span.column - origin_column;
    auto end = offset + span.length;
    if (offset >= columns)
      break;
    // The span may reach past the segment's own payload: where the flattened
    // splitter fired on a `{0x0001, ','}` boundary token the separator glyph
    // it consumed is outside the payload, so the row text stops one character
    // short of the column the CFONT covers. N2AH1MST record 258 line 5 is
    // `   AHL146I ... DURING THE CCW SCAN,` with `cfont ... 67 5 2` over
    // `SCAN,`, and record 258 segment 2 ends at the comma. The span is then
    // clipped to what the payload retained rather than disproved; the lost
    // separator is the residual recorded in `demote_display_line_owned_
    // controls`, which legacy loses too.
    if (end > columns)
      end = columns;
    if (chain.empty()) {
      if (offset != 0)
        break;
    } else {
      if (offset < previous_end)
        break;
      const auto gap = display.substr((*offsets)[previous_end],
                                     (*offsets)[offset] - (*offsets)[previous_end]);
      if (!std::all_of(gap.begin(), gap.end(),
                       [](const char ch) { return ch == ' '; }))
        break;
    }
    const auto text = display.substr((*offsets)[offset],
                                     (*offsets)[end] - (*offsets)[offset]);
    if (text.empty() || text.front() == ' ' || text.back() == ' ')
      break;
    if (!joined.empty() && offset != previous_end)
      joined.push_back(' ');
    joined += text;
    out.push_back({text, span});
    chain.push_back(span);
    previous_end = end;
  }
  if (chain.empty()) {
    out.clear();
    joined.clear();
  }
  return chain;
}

// Corroborates a mapped chain against the display line it is supposed to
// cover.
//
// `map_leading_chain` maps a CFONT column onto projected text by assuming the
// text begins at the chain's anchor column, which the record does not always
// state. SC34-425 APPENDIX1.5.3 record 2540 line 9 is
// ` | User Response:  Contact the project administrator ...`: a `|` at
// column 1 opens the row and the `cfont 3 4 2 8 9 2` covers `User` at
// column 3, but the projected text begins at the `|`, so the mapping slides
// two columns left and spells `| Us`. A CFONT column is an index into its
// display line's own cells, so the line settles it: the chain is admitted
// only where every span spells exactly the cells its columns name.
bool chain_matches_display_columns(const DecodedLogicalRecordSource &record,
                                   const std::size_t token,
                                   const std::vector<TrapStyledSpanIR> &spans) {
  const auto *line = display_line_of_token(record, token);
  if (line == nullptr)
    return false;
  const auto text = display_line_text(record, *line);
  const auto offsets = display_line_column_offsets(record, *line);
  if (offsets.empty())
    return false;
  const auto columns = offsets.size() - 1;
  for (const auto &span : spans) {
    auto end = span.span.column + span.span.length;
    if (span.span.column >= columns)
      return false;
    if (end > columns)
      end = columns;
    if (offsets[end] < offsets[span.span.column] ||
        offsets[end] > text.size())
      return false;
    if (text.compare(offsets[span.span.column],
                     offsets[end] - offsets[span.span.column], span.text) != 0)
      return false;
  }
  return !spans.empty();
}

// The byte ranges the chain's own spans occupy in `joined`, offset by `base`.
// `map_leading_chain` builds `joined` by concatenating the span texts and
// inserting a single space wherever the columns leave a gap, so the geometry
// alone reproduces the offsets. Empty when it does not reproduce `joined`
// exactly, so the caller fails closed rather than place a highlight over
// characters it has not proved belong to the run.
std::vector<TrapBodyHighlightIR>
chain_highlight_ranges(const std::vector<TrapStyledSpanIR> &spans,
                       const std::string &joined, const std::size_t base) {
  std::vector<TrapBodyHighlightIR> ranges;
  std::size_t at = 0;
  std::size_t previous_end_column = 0;
  for (std::size_t index = 0; index < spans.size(); ++index) {
    const auto &span = spans[index];
    if (index != 0 && span.span.column != previous_end_column) {
      if (at >= joined.size() || joined[at] != ' ')
        return {};
      ++at;
    }
    if (span.text.empty() ||
        joined.compare(at, span.text.size(), span.text) != 0)
      return {};
    ranges.push_back(
        {base + at, base + at + span.text.size(), span.span.style});
    at += span.text.size();
    const auto offsets = projected_column_offsets(span.text);
    if (!offsets) return {};
    previous_end_column = span.span.column + offsets->size() - 1;
  }
  if (at != joined.size())
    return {};
  return ranges;
}

bool starts_with_word(const std::string &text, const std::string &prefix) {
  return !prefix.empty() && text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0 &&
         (text.size() == prefix.size() || text[prefix.size()] == ' ');
}

// Every styled run lies inside its own row, in order, and covers drawn text.
bool region_highlights_fit(const TrapEmbeddedRegionIR &region) {
  if (region.line_highlights.empty())
    return true;
  if (region.line_highlights.size() != region.lines.size())
    return false;
  for (std::size_t line = 0; line < region.lines.size(); ++line) {
    std::size_t at = 0;
    for (const auto &highlight : region.line_highlights[line]) {
      if (highlight.begin < at || highlight.begin >= highlight.end ||
          highlight.end > region.lines[line].size() ||
          region.lines[line][highlight.begin] == ' ' ||
          region.lines[line][highlight.end - 1] == ' ')
        return false;
      at = highlight.end;
    }
  }
  return true;
}

bool same_region(const TrapEmbeddedRegionIR &left,
                 const TrapEmbeddedRegionIR &right) {
  return left.identifier == right.identifier &&
         left.start.kind == right.start.kind &&
         left.start.identifier == right.start.identifier &&
         left.start.source == right.start.source &&
         left.end.kind == right.end.kind &&
         left.end.identifier == right.end.identifier &&
         left.end.source == right.end.source && left.lines == right.lines &&
         left.line_sources == right.line_sources &&
         left.line_highlights == right.line_highlights &&
         left.after_field == right.after_field &&
         left.cell_begin == right.cell_begin && left.cell_end == right.cell_end;
}

bool same_flow(const TrapFlowSequenceIR &left, const TrapFlowSequenceIR &right);

bool same_paragraph(const TrapFlowParagraphIR &left,
                    const TrapFlowParagraphIR &right) {
  const auto rows_equal = [](const auto &a, const auto &b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), same_row);
  };
  if (left.text != right.text || left.display_text != right.display_text ||
      left.highlights != right.highlights || left.links != right.links ||
      left.source_slices != right.source_slices ||
      !rows_equal(left.source_rows, right.source_rows) ||
      left.cell_begin != right.cell_begin || left.cell_end != right.cell_end ||
      left.projection.size() != right.projection.size() ||
      left.break_before.size() != right.break_before.size() ||
      left.first_column != right.first_column)
    return false;
  for (std::size_t index = 0; index < left.projection.size(); ++index) {
    const auto &a = left.projection[index];
    const auto &b = right.projection[index];
    if (a.begin != b.begin || a.end != b.end ||
        a.cell_begin != b.cell_begin || a.cell_end != b.cell_end ||
        a.kind != b.kind)
      return false;
  }
  for (std::size_t index = 0; index < left.break_before.size(); ++index) {
    const auto &a = left.break_before[index];
    const auto &b = right.break_before[index];
    if (!(a.source == b.source) || a.cell_begin != b.cell_begin ||
        a.cell_end != b.cell_end)
      return false;
  }
  return true;
}

bool same_block(const TrapFlowBlockIR &left, const TrapFlowBlockIR &right) {
  if (left.value.index() != right.value.index())
    return false;
  if (std::holds_alternative<TrapFlowParagraphIR>(left.value))
    return same_paragraph(std::get<TrapFlowParagraphIR>(left.value),
                          std::get<TrapFlowParagraphIR>(right.value));
  if (std::holds_alternative<TrapEmbeddedRegionIR>(left.value))
    return same_region(std::get<TrapEmbeddedRegionIR>(left.value),
                       std::get<TrapEmbeddedRegionIR>(right.value));
  if (std::holds_alternative<TrapFlowAnchorIR>(left.value)) {
    const auto &a = std::get<TrapFlowAnchorIR>(left.value);
    const auto &b = std::get<TrapFlowAnchorIR>(right.value);
    return a.id == b.id && a.source == b.source;
  }
  if (std::holds_alternative<TrapFlowFootnoteIR>(left.value)) {
    const auto &a = std::get<TrapFlowFootnoteIR>(left.value);
    const auto &b = std::get<TrapFlowFootnoteIR>(right.value);
    if (a.id != b.id || !(a.start == b.start) || !(a.end == b.end) ||
        a.cell_begin != b.cell_begin || a.cell_end != b.cell_end ||
        a.blocks.size() != b.blocks.size())
      return false;
    for (std::size_t block = 0; block < a.blocks.size(); ++block)
      if (!same_block(a.blocks[block], b.blocks[block]))
        return false;
    return true;
  }
  const auto &a = std::get<TrapFlowListIR>(left.value);
  const auto &b = std::get<TrapFlowListIR>(right.value);
  if (a.ordered != b.ordered || a.marker_column != b.marker_column ||
      a.items.size() != b.items.size())
    return false;
  for (std::size_t item = 0; item < a.items.size(); ++item) {
    if (!(a.items[item].marker_source == b.items[item].marker_source) ||
        a.items[item].marker_cell_begin != b.items[item].marker_cell_begin ||
        a.items[item].marker_cell_end != b.items[item].marker_cell_end ||
        a.items[item].blocks.size() != b.items[item].blocks.size())
      return false;
    for (std::size_t block = 0; block < a.items[item].blocks.size(); ++block)
      if (!same_block(a.items[item].blocks[block],
                      b.items[item].blocks[block]))
        return false;
  }
  return true;
}

bool same_flow(const TrapFlowSequenceIR &left, const TrapFlowSequenceIR &right) {
  if (left.blocks.size() != right.blocks.size())
    return false;
  for (std::size_t index = 0; index < left.blocks.size(); ++index)
    if (!same_block(left.blocks[index], right.blocks[index]))
      return false;
  return true;
}

bool validate_flow(const TrapFlowSequenceIR &flow,
                   const std::vector<TrapSourceCellIR> &cells,
                   const std::string &identity, std::string *error,
                   std::string *flat_text = nullptr) {
  std::set<std::size_t> claimed_cells;
  const auto claim = [&](const std::size_t begin, const std::size_t end) {
    for (auto cell = begin; cell < end; ++cell)
      if (!claimed_cells.insert(cell).second)
        return false;
    return true;
  };
  const auto validate_blocks = [&](const auto &self,
                                   const std::vector<TrapFlowBlockIR> &blocks,
                                   std::string &flat) -> bool {
    for (const auto &block : blocks) {
      if (std::holds_alternative<TrapFlowParagraphIR>(block.value)) {
        const auto &paragraph = std::get<TrapFlowParagraphIR>(block.value);
        if (paragraph.text.empty() || paragraph.source_slices.empty() ||
            paragraph.cell_begin > paragraph.cell_end ||
            paragraph.cell_end > cells.size())
          return fail(error, "trap flow paragraph has invalid provenance: " +
                                 identity);
        for (const auto &boundary : paragraph.break_before) {
          if (boundary.cell_begin >= boundary.cell_end ||
              boundary.cell_end > cells.size())
            return fail(error,
                        "trap flow paragraph boundary range is invalid: " +
                            identity);
          if (cells[boundary.cell_begin].logical_record !=
                  boundary.source.logical_record ||
              cells[boundary.cell_begin].token_index !=
                  boundary.source.token_begin)
            return fail(error,
                        "trap flow paragraph boundary source differs: " +
                            identity);
          if (!claim(boundary.cell_begin, boundary.cell_end))
            return fail(error,
                        "trap flow paragraph boundary cells overlap: " +
                            identity);
        }
        std::size_t at = 0;
        for (const auto &projection : paragraph.projection) {
          if (projection.begin != at || projection.begin >= projection.end ||
              projection.end > paragraph.text.size() ||
              projection.cell_begin > projection.cell_end ||
              projection.cell_begin < paragraph.cell_begin ||
              projection.cell_end > paragraph.cell_end ||
              (projection.kind != TrapTextProjectionKindIR::decoded_separator &&
               projection.cell_begin == projection.cell_end) ||
              (projection.kind != TrapTextProjectionKindIR::decoded_separator &&
               !claim(projection.cell_begin, projection.cell_end)))
            return fail(error,
                        "trap flow paragraph projection is not exact: " +
                            identity);
          at = projection.end;
        }
        if (at != paragraph.text.size())
          return fail(error, "trap flow paragraph has unprojected text: " +
                                 identity);
        for (const auto &highlight : paragraph.highlights)
          if (highlight.begin >= highlight.end ||
              highlight.end > paragraph.text.size())
            return fail(error, "trap flow highlight is outside its paragraph: " +
                                   identity);
        for (const auto &link : paragraph.links)
          if (link.begin >= link.end || link.end > paragraph.text.size() ||
              link.target.empty())
            return fail(error, "trap flow link is outside its paragraph: " +
                                   identity);
        if (!flat.empty())
          flat.push_back(' ');
        flat += paragraph.text;
      } else if (std::holds_alternative<TrapEmbeddedRegionIR>(block.value)) {
        const auto &region = std::get<TrapEmbeddedRegionIR>(block.value);
        if (region.cell_begin > region.cell_end ||
            region.cell_end > cells.size())
          return fail(error, "trap flow embedded region overlaps source cells: " +
                                 identity);
      } else if (std::holds_alternative<TrapFlowAnchorIR>(block.value)) {
        const auto &anchor = std::get<TrapFlowAnchorIR>(block.value);
        if (anchor.id.empty() || anchor.source.token_begin >= anchor.source.token_end)
          return fail(error, "trap flow anchor has no source control: " +
                                 identity);
      } else if (std::holds_alternative<TrapFlowFootnoteIR>(block.value)) {
        const auto &footnote = std::get<TrapFlowFootnoteIR>(block.value);
        if (footnote.id.empty() ||
            footnote.start.token_begin >= footnote.start.token_end ||
            footnote.end.token_begin >= footnote.end.token_end ||
            std::make_pair(footnote.end.logical_record, footnote.end.token_begin) <=
                std::make_pair(footnote.start.logical_record,
                               footnote.start.token_begin) ||
            footnote.cell_begin > footnote.cell_end ||
            footnote.cell_end > cells.size() || footnote.blocks.empty())
          return fail(error, "trap flow footnote envelope is not source-proven: " +
                                 identity);
        for (const auto &inner : footnote.blocks)
          if (!std::holds_alternative<TrapFlowParagraphIR>(inner.value))
            return fail(error, "trap flow footnote holds a non-paragraph: " +
                                   identity);
        if (!self(self, footnote.blocks, flat))
          return false;
      } else {
        const auto &list = std::get<TrapFlowListIR>(block.value);
        if (list.items.empty())
          return fail(error, "trap flow list has no items: " + identity);
        for (const auto &item : list.items) {
          if (item.marker_cell_begin >= item.marker_cell_end ||
              item.marker_cell_end > cells.size() ||
              cells[item.marker_cell_begin].logical_record !=
                  item.marker_source.logical_record ||
              cells[item.marker_cell_begin].token_index !=
                  item.marker_source.token_begin ||
              !claim(item.marker_cell_begin, item.marker_cell_end))
            return fail(error,
                        "trap flow list marker is not source-proven: " +
                            identity);
          if (item.blocks.empty() || !self(self, item.blocks, flat))
            return false;
        }
      }
    }
    return true;
  };
  std::string flat;
  if (!validate_blocks(validate_blocks, flow.blocks, flat))
    return false;
  if (flat_text != nullptr)
    *flat_text = std::move(flat);
  return true;
}

struct SegmentRef {
  const DecodedLogicalRecordSource *record = nullptr;
  const ControlSegmentIR *segment = nullptr;
};

bool same_text(const TrapTextIR &left, const TrapTextIR &right) {
  const auto rows_equal = [](const auto &a, const auto &b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](const auto &x, const auto &y) {
                        return x.display_run == y.display_run &&
                               x.row_index == y.row_index;
                      });
  };
  return left.text == right.text && left.display_text == right.display_text &&
         left.paragraph_break == right.paragraph_break &&
         left.highlights == right.highlights &&
         left.links == right.links &&
         left.source_slices == right.source_slices &&
         rows_equal(left.source_rows, right.source_rows) &&
         left.cell_begin == right.cell_begin && left.cell_end == right.cell_end &&
         same_flow(left.flow, right.flow);
}

bool same_spans(const std::vector<TrapStyledSpanIR> &left,
                const std::vector<TrapStyledSpanIR> &right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](const auto &a, const auto &b) {
                      return a.text == b.text &&
                             a.span.column == b.span.column &&
                             a.span.length == b.span.length &&
                             a.span.code == b.span.code &&
                             a.span.style == b.span.style;
                    });
}

bool same_line(const TrapLineIR &left, const TrapLineIR &right) {
  return left.font_source == right.font_source &&
         same_spans(left.spans, right.spans) &&
         left.spans_text == right.spans_text && same_text(left.body, right.body);
}

// Reads the native list shape of one introduction paragraph from its own
// rows and splits it into list items where its rows draw bullets.
//
// The paragraph's first glyph gives its drawn column. Where that glyph's
// display line opens with a list bullet, the paragraph opens an item. A
// compact list draws further bullet rows with no empty line between them,
// so the prose reads them as one paragraph: SC34-425 2.4.21 record 1601
// lines 31-35 are four ` | °   ...` rows, and hosted DT 19921112160049
// serves four bullets between one pair of `<p>` marks. Each further bullet
// starts an item at the first glyph it is drawn in front of, placed through
// the paragraph's exact glyph provenance. Bullets of one paragraph stand in
// one column. A bullet after the paragraph's own first text, a bullet with
// no glyph after it on its line, or a split the provenance cannot place
// declines. `pieces` receives the items in order and `bases` each item's
// byte offset in the paragraph's text.
// The `CFONT` runs drawn inside one introduction prose paragraph, as byte
// ranges over its text. A row the source styles is a row of a font display
// run: the `CFONT` segment is the row's own control, and its columns are
// indices into the row's display cells. Each span is placed through the
// cells it covers and the glyph the prose projected from each of them, never
// by searching the text. SC34-425 2.4.10.1 record 1490 styles `not` with
// `cfont 70 3 1` in the middle of `... A separator period is not`, and
// record 1494 draws `cfont 3 8 P` over `@@FLMLIS`; hosted (DT
// 19921112160049) serves `<I>not</I>` and `<kbd>@@FLMLIS</kbd>`. A row whose
// spans the cells do not all prove (`display_row_font_highlights`), or whose
// span covers a cell the paragraph did not project, stays unstyled; so does
// a paragraph without exact glyph provenance.
std::vector<TrapBodyHighlightIR> introduction_row_highlights(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const MessageProseParagraphIR &paragraph) {
  std::vector<TrapBodyHighlightIR> result;
  if (!paragraph.glyphs_complete)
    return result;
  std::map<CellKey, std::size_t> glyphs;
  for (const auto &glyph : paragraph.glyphs)
    glyphs[{glyph.logical_record, glyph.token_index, glyph.word_index}] =
        glyph.offset;
  for (const auto &source_row : paragraph.source_rows) {
    const auto run = std::find_if(
        layout.runs.begin(), layout.runs.end(), [&](const auto &candidate) {
          return candidate.id == source_row.display_run;
        });
    if (run == layout.runs.end() ||
        run->control_kind != BookControlKind::font ||
        source_row.row_index >= run->rows.size())
      continue;
    const auto &row = run->rows[source_row.row_index];
    const auto *record = find_record(records, row.logical_record);
    if (record == nullptr ||
        row.segment_index >= record->control_segments.size())
      continue;
    const auto font = decode_font_control_spans(
        *record, record->control_segments[row.segment_index]);
    if (!font)
      continue;
    const DisplayLineIR *line = nullptr;
    for (auto token = row.token_begin; token < row.token_end && !line; ++token)
      line = display_line_of_token(*record, token);
    // The spans name the row directly below the `cfont` line. The segment's
    // payload runs on over later rows of the same display run, which the
    // `cfont` does not style (SC31-711 4.1.3 record 104 line 28, hosted
    // plain).
    const auto &segment = record->control_segments[row.segment_index];
    const auto *font_line =
        segment.source_tokens.empty()
            ? nullptr
            : display_line_of_token(*record, segment.source_tokens.front());
    if (line == nullptr || font_line == nullptr || line != font_line + 1)
      continue;
    const auto cells = display_line_cells(*record, *line);
    const auto offsets = display_line_column_offsets(*record, *line);
    const auto text = display_line_text(*record, *line);
    if (display_row_font_highlights(cells, offsets, *font).empty())
      continue;
    std::vector<FontSpanIR> spans;
    for (const auto &span : font->spans)
      if (span.length != 0)
        spans.push_back(span);
    std::sort(spans.begin(), spans.end(),
              [](const auto &left, const auto &right) {
                return left.column < right.column;
              });
    std::vector<TrapBodyHighlightIR> mapped;
    bool proven = true;
    for (const auto &span : spans) {
      std::optional<std::size_t> begin;
      std::size_t end = 0;
      std::string drawn;
      for (auto column = span.column;
           proven && column < span.column + span.length; ++column) {
        const auto &cell = cells[column];
        if (cell.word <= ' ')
          continue;
        const auto glyph =
            text.substr(offsets[column], offsets[column + 1] - offsets[column]);
        const auto found =
            glyphs.find({record->logical_record, cell.token, cell.word_index});
        if (cell.token < row.token_begin || cell.token >= row.token_end ||
            found == glyphs.end() || glyph.empty() ||
            paragraph.text.compare(found->second, glyph.size(), glyph) != 0 ||
            (begin && found->second < end)) {
          proven = false;
          break;
        }
        if (!begin)
          begin = found->second;
        end = found->second + glyph.size();
        drawn += glyph;
      }
      if (!proven || !begin) {
        proven = false;
        break;
      }
      // Between the run's own glyphs the text holds only separator spaces.
      std::string spelled;
      for (const auto ch : paragraph.text.substr(*begin, end - *begin))
        if (ch != ' ')
          spelled.push_back(ch);
      if (spelled != drawn) {
        proven = false;
        break;
      }
      mapped.push_back({*begin, end, span.style});
    }
    if (proven)
      result.insert(result.end(), mapped.begin(), mapped.end());
  }
  std::sort(result.begin(), result.end(),
            [](const auto &left, const auto &right) {
              return left.begin < right.begin;
            });
  for (std::size_t at = 1; at < result.size(); ++at)
    if (result[at].begin < result[at - 1].end)
      return {};
  return result;
}

bool split_introduction_list_items(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const MessageProseParagraphIR &paragraph,
    TrapIntroductionParagraphIR item,
    std::vector<TrapIntroductionParagraphIR> &pieces,
    std::vector<std::size_t> &bases, std::string *error) {
  struct Cell {
    const DecodedLogicalRecordSource *record = nullptr;
    std::size_t token = 0;
  };
  struct Bullet {
    Cell bullet;
    Cell glyph;
  };
  std::optional<Cell> first_glyph;
  std::vector<Bullet> bullets;
  for (const auto &slice : paragraph.source_slices) {
    const auto *record = find_record(records, slice.logical_record);
    if (record == nullptr)
      continue;
    for (auto token = slice.token_begin;
         token < slice.token_end && token < record->ir.tokens.size();
         ++token) {
      if (is_display_line_list_bullet(*record, token)) {
        if (!bullets.empty() && bullets.back().glyph.record == nullptr)
          return fail(error, "trap introduction list bullet has no drawn text");
        if (bullets.empty() && first_glyph)
          return fail(error, "trap introduction paragraph draws a list bullet "
                             "inside its text at LR" +
                                 std::to_string(record->logical_record) +
                                 " token " + std::to_string(token));
        bullets.push_back({{record, token}, {}});
        continue;
      }
      if (is_display_line_length_token(*record, token) ||
          is_display_line_margin_revision_bar(*record, token))
        continue;
      const auto &words = record->ir.tokens[token].decoded_words;
      if (!std::any_of(words.begin(), words.end(), [](const auto word) {
            return word > ' ' && word <= 0xff;
          }))
        continue;
      if (!first_glyph)
        first_glyph = Cell{record, token};
      if (!bullets.empty() && bullets.back().glyph.record == nullptr)
        bullets.back().glyph = {record, token};
    }
  }
  if (!bullets.empty() && bullets.back().glyph.record == nullptr)
    return fail(error, "trap introduction list bullet has no drawn text");
  const auto no_shape = [&] {
    if (!bullets.empty())
      return fail(error, "trap introduction list bullet does not open its "
                         "paragraph's first row");
    pieces.push_back(std::move(item));
    bases.push_back(0);
    return true;
  };
  if (!first_glyph)
    return no_shape();
  // The paragraph's text must open with that glyph's own word, or the rows
  // are not the ones the text came from.
  const auto glyph = collapse(
      token_words_to_ascii(first_glyph->record->tokens[first_glyph->token]));
  const auto column = drawn_column(*first_glyph->record, first_glyph->token);
  if (glyph.empty() || !column ||
      item.text.compare(0, glyph.size(), glyph) != 0)
    return no_shape();
  item.first_column = *column;
  if (bullets.empty()) {
    pieces.push_back(std::move(item));
    bases.push_back(0);
    return true;
  }
  // Every bullet stands in front of its glyph on the glyph's own line, in
  // the first bullet's column.
  std::optional<std::size_t> marker_column;
  for (const auto &bullet : bullets) {
    const auto bullet_column =
        drawn_column(*bullet.bullet.record, bullet.bullet.token);
    const auto glyph_column =
        drawn_column(*bullet.glyph.record, bullet.glyph.token);
    if (bullet.bullet.record != bullet.glyph.record || !bullet_column ||
        !glyph_column ||
        display_line_of_token(*bullet.bullet.record, bullet.bullet.token) !=
            display_line_of_token(*bullet.glyph.record, bullet.glyph.token) ||
        *bullet_column >= *glyph_column ||
        (marker_column && *marker_column != *bullet_column))
      return fail(error, "trap introduction list bullet does not open its "
                         "paragraph's first row");
    marker_column = *bullet_column;
  }
  if (bullets.front().glyph.record != first_glyph->record ||
      bullets.front().glyph.token != first_glyph->token)
    return fail(error, "trap introduction list bullet does not open its "
                       "paragraph's first row");
  // Each further bullet's item begins at its glyph's first projected byte.
  std::vector<std::size_t> begins{0};
  for (std::size_t at = 1; at < bullets.size(); ++at) {
    if (!paragraph.glyphs_complete)
      return fail(error, "trap introduction compact list has no exact glyph "
                         "provenance");
    const auto &cell = bullets[at].glyph;
    std::optional<std::size_t> offset;
    for (const auto &projected : paragraph.glyphs)
      if (projected.logical_record == cell.record->logical_record &&
          projected.token_index == cell.token &&
          (!offset || projected.offset < *offset))
        offset = projected.offset;
    if (!offset || *offset <= begins.back() || *offset >= item.text.size() ||
        item.text[*offset - 1] != ' ')
      return fail(error, "trap introduction compact list item cannot be "
                         "placed in its paragraph");
    begins.push_back(*offset);
  }
  // Leading highlighted spans belong to the first item's own text.
  if (!item.leading_spans.empty() && begins.size() > 1) {
    std::size_t joined = 0;
    for (const auto &span : item.leading_spans)
      joined += span.text.size() + (joined == 0 ? 0 : 1);
    if (joined + 1 > begins[1])
      return fail(error, "trap introduction highlight crosses a list item");
  }
  // Token order of the bullets, to share the paragraph's slices and rows
  // among the items.
  const auto position = [](std::uint32_t record, std::size_t token) {
    return std::make_pair(record, token);
  };
  const auto item_of = [&](std::uint32_t record, std::size_t token) {
    std::size_t owner = 0;
    for (std::size_t at = 1; at < bullets.size(); ++at)
      if (position(record, token) >=
          position(bullets[at].bullet.record->logical_record,
                   bullets[at].bullet.token))
        owner = at;
    return owner;
  };
  for (std::size_t at = 0; at < bullets.size(); ++at) {
    TrapIntroductionParagraphIR piece;
    const auto begin = begins[at];
    const auto end =
        at + 1 < begins.size() ? begins[at + 1] - 1 : item.text.size();
    piece.text = item.text.substr(begin, end - begin);
    // Row line breaks inside this item move with its text. The separator
    // that ends an item is the bullet row's own boundary, which the list
    // states; a break recorded there is not kept twice.
    for (const auto offset : item.line_breaks)
      if (offset > begin && offset < end)
        piece.line_breaks.push_back(offset - begin);
    if (at == 0)
      piece.leading_spans = item.leading_spans;
    piece.list_marker = token_slice(
        *bullets[at].bullet.record,
        segment_of_token(*bullets[at].bullet.record)[bullets[at].bullet.token],
        bullets[at].bullet.token, bullets[at].bullet.token + 1);
    piece.marker_column = *marker_column;
    piece.first_column =
        drawn_column(*bullets[at].glyph.record, bullets[at].glyph.token);
    pieces.push_back(std::move(piece));
    bases.push_back(begin);
  }
  if (bullets.size() == 1) {
    pieces.front().source_slices = item.source_slices;
    pieces.front().source_rows = item.source_rows;
    return true;
  }
  // The paragraph's prose slices, cut at the bullets; slices the paragraph
  // gained beyond them (a deferred font) stay with the first item.
  for (std::size_t index = 0; index < item.source_slices.size(); ++index) {
    const auto &slice = item.source_slices[index];
    const auto *record = find_record(records, slice.logical_record);
    if (index >= paragraph.source_slices.size() || record == nullptr ||
        slice.token_begin >= slice.token_end) {
      pieces.front().source_slices.push_back(slice);
      continue;
    }
    auto from = slice.token_begin;
    while (from < slice.token_end) {
      const auto owner = item_of(slice.logical_record, from);
      auto to = from + 1;
      while (to < slice.token_end && item_of(slice.logical_record, to) == owner)
        ++to;
      pieces[owner].source_slices.push_back(
          token_slice(*record, slice.segment_index, from, to));
      from = to;
    }
  }
  for (const auto &row : item.source_rows) {
    std::size_t owner = 0;
    const auto run = std::find_if(
        layout.runs.begin(), layout.runs.end(),
        [&](const auto &candidate) { return candidate.id == row.display_run; });
    if (run != layout.runs.end() && row.row_index < run->rows.size())
      owner = item_of(run->rows[row.row_index].logical_record,
                      run->rows[row.row_index].token_begin);
    pieces[owner].source_rows.push_back(row);
  }
  return true;
}

// The heading a bare `SRMSG` opens between two groups of entries
// (`TrapGroupHeadingIR`), or nothing where the source does not draw one.
// `at` is the bare `SRMSG`, `next_entry` the next entry's `SRMSG`; every
// segment between them must be a `CFONT`, a drawn row, an `SI` line or a
// display-line length byte. The line directly after the `SRMSG`'s own line
// is a `CFONT` line, the row under it is styled in full, and every later
// drawn row opens with a styled run. SC09-138 F.1 record 2061 lines 9-25 and
// record 2062 lines 0-6 are that shape; hosted F.1 (DT 19910321130500)
// serves the heading row in italics after `<a name="MSG">` and the rows
// after it in bold, each on its own line, before `MSG EDCK001`. SC24-546
// A.0's plain row `The following are the REXX error messages:` after a bare
// `SRMSG` is not this shape and stays entry text.
std::optional<TrapGroupHeadingIR>
read_group_heading(const std::vector<DecodedLogicalRecordSource> &records,
                   const SourceIndex &index,
                   const std::vector<SegmentRef> &ordered, std::size_t at,
                   std::size_t next_entry,
                   std::vector<TrapSourceCellIR> &cells,
                   CellPositions &cell_positions) {
  if (next_entry >= ordered.size() || at + 1 >= next_entry)
    return std::nullopt;
  const auto &bare = ordered[at];
  const auto &record = *bare.record;
  const auto &segment = *bare.segment;
  const auto *lines = record_display_lines(record);
  if (lines == nullptr || segment.source_tokens.empty())
    return std::nullopt;
  const auto *own_line = display_line_of_token(record, segment.source_tokens.front());
  if (own_line == nullptr ||
      own_line->prefix_token + 1 != segment.source_tokens.front() ||
      own_line->token_end != segment.source_tokens.front() + 1)
    return std::nullopt;
  const auto own_index = static_cast<std::size_t>(own_line - lines->data());
  if (own_index + 2 >= lines->size())
    return std::nullopt;
  const auto &font_line = (*lines)[own_index + 1];
  const auto &heading_row = (*lines)[own_index + 2];
  const auto &first = ordered[at + 1];
  if (first.record != &record || first.segment->kind != BookControlKind::font ||
      first.segment->malformed || first.segment->source_tokens.empty() ||
      first.segment->source_tokens.front() != font_line.prefix_token + 1)
    return std::nullopt;
  for (auto other = at + 1; other < next_entry; ++other) {
    const auto &ref = ordered[other];
    const auto &candidate = *ref.segment;
    if (segment_is_display_line_prefix(*ref.record, candidate))
      continue;
    if (candidate.kind == BookControlKind::font && !candidate.malformed)
      continue;
    if (candidate.kind == BookControlKind::text ||
        candidate.kind == BookControlKind::unknown)
      continue;
    const auto *line =
        candidate.source_tokens.empty()
            ? nullptr
            : display_line_of_token(*ref.record, candidate.source_tokens.front());
    if (line != nullptr &&
        line->prefix_token + 1 == candidate.source_tokens.front() &&
        is_subject_index_display_line(*ref.record, *line))
      continue;
    return std::nullopt;
  }
  const auto &next = ordered[next_entry];
  if (next.segment->source_tokens.empty())
    return std::nullopt;
  TrapEmbeddedRegionIR region;
  std::vector<TrapIndexTermIR> terms;
  std::vector<TrapSourceCellIR> claimed;
  CellPositions claimed_positions;
  if (!project_embedded_region(records, index, record.logical_record,
                               font_line.prefix_token,
                               next.record->logical_record,
                               next.segment->source_tokens.front(), region,
                               claimed, claimed_positions, nullptr, &terms))
    return std::nullopt;
  if (region.lines.empty() || region.line_highlights.size() != region.lines.size() ||
      region.line_sources.front().logical_record != record.logical_record ||
      region.line_sources.front().token_begin != heading_row.prefix_token)
    return std::nullopt;
  // The heading row is styled in full.
  {
    const auto &text = region.lines.front();
    const auto &runs = region.line_highlights.front();
    if (runs.empty())
      return std::nullopt;
    std::size_t covered = 0;
    for (std::size_t byte = 0; byte < text.size(); ++byte) {
      if (text[byte] == ' ')
        continue;
      if (!std::any_of(runs.begin(), runs.end(), [&](const auto &run) {
            return byte >= run.begin && byte < run.end;
          }))
        return std::nullopt;
      ++covered;
    }
    if (covered == 0)
      return std::nullopt;
  }
  // Every later drawn row opens with a styled run.
  for (std::size_t line = 1; line < region.lines.size(); ++line) {
    const auto &text = region.lines[line];
    const auto first_glyph = text.find_first_not_of(' ');
    if (first_glyph == std::string::npos)
      continue;
    const auto &runs = region.line_highlights[line];
    if (runs.empty() || runs.front().begin != first_glyph)
      return std::nullopt;
  }
  TrapGroupHeadingIR heading;
  heading.anchor = {"MSG", segment_slice(record, segment)};
  heading.heading.lines = {region.lines.front()};
  heading.heading.line_sources = {region.line_sources.front()};
  heading.heading.line_highlights = {region.line_highlights.front()};
  std::size_t rest = 1;
  while (rest < region.lines.size() && region.lines[rest].empty())
    ++rest;
  heading.rows.lines.assign(region.lines.begin() + rest, region.lines.end());
  heading.rows.line_sources.assign(region.line_sources.begin() + rest,
                                   region.line_sources.end());
  heading.rows.line_highlights.assign(region.line_highlights.begin() + rest,
                                      region.line_highlights.end());
  heading.index_terms = std::move(terms);
  heading.cell_begin = cells.size();
  for (const auto &cell : claimed)
    append_unique_cell(cells, cell_positions, cell);
  heading.cell_end = cells.size();
  heading.heading.cell_begin = heading.rows.cell_begin = heading.cell_begin;
  heading.heading.cell_end = heading.rows.cell_end = heading.cell_end;
  return heading;
}

} // namespace

std::optional<TrapCatalogIR>
extract_trap_catalog_ir(const std::vector<DecodedLogicalRecordSource> &records,
                        const LayoutIR &layout, const OwnershipIR &ownership,
                        const std::string &toc_title, std::string *error) {
  const auto reject = [&](std::string message) {
    fail(error, std::move(message));
    return std::optional<TrapCatalogIR>{};
  };
  if (records.empty() || !ownership.conflicts.empty())
    return reject("source ownership is unavailable or conflicted");
  for (std::size_t index = 1; index < records.size(); ++index)
    if (records[index].logical_record != records[index - 1].logical_record + 1)
      return reject("trap catalog logical-record envelope is not contiguous");

  std::vector<SegmentRef> ordered;
  for (const auto &record : records)
    for (const auto &segment : record.control_segments)
      ordered.push_back({&record, &segment});
  const auto is_entry_start = [](const SegmentRef &ref) {
    return ref.segment->kind == BookControlKind::message_start &&
           !first_word(range_text(*ref.record, ref.segment->operand_range))
                .empty() &&
           !message_operand_is_next_line_length(*ref.record, *ref.segment);
  };
  std::vector<std::size_t> starts;
  for (std::size_t index = 0; index < ordered.size(); ++index)
    if (is_entry_start(ordered[index]))
      starts.push_back(index);
  if (starts.empty())
    return reject("source has no SRMSG entry");

  const auto index = index_sources(records, layout, ownership);
  TrapCatalogIR catalog;
  CellPositions introduction_cell_positions;
  catalog.first_logical_record = records.front().logical_record;
  catalog.end_logical_record = records.back().logical_record + 1;

  // Topic header: metadata, title, named anchors, index terms, and the
  // introduction prose.
  //
  // An `SRTBL<id>` ... `SRETBL` envelope can be drawn in the introduction as
  // well as inside an entry (SC09-138 `F.1`/`H.0`, SC24-546 `A.0`,
  // SC34-425 `2.4.32`/`2.4.33`). It is the same shape and is modelled the
  // same way: the region keeps the display lines the record frames, and the
  // prose either side of it is a separate envelope, so
  // `extract_message_prose_paragraphs_ir` never sees the drawing.
  struct HeaderRegion {
    TrapEmbeddedRegionIR region;
    std::uint32_t start_record = 0;
    std::size_t start_token = 0;
    // The `SRTBL` segment the prose before the region ends at, and the token
    // the prose after it resumes from.
    std::uint32_t open_record = 0;
    std::size_t open_segment = 0;
    std::uint32_t resume_record = 0;
    std::size_t resume_token = 0;
  };
  std::vector<HeaderRegion> header_regions;
  std::optional<HeaderRegion> open_header_region;
  std::optional<SegmentRef> title;
  for (std::size_t index_in_order = 0; index_in_order < starts.front();
       ++index_in_order) {
    const auto &ref = ordered[index_in_order];
    const auto &segment = *ref.segment;
    if (segment_is_display_line_prefix(*ref.record, segment) ||
        nondrawing_control(index, *ref.record, segment))
      continue;
    // `SRFIG<id>` ... `SREFIG` is drawn in the introduction the same way an
    // entry draws it: SC34-425 2.4.32 record 1641 line 36 onwards opens
    // `SRFIGADAIN`, and hosted DT 19921112160049 serves the boxed rows and
    // the `Figure 88. ADAIN Example` caption as drawn lines under
    // `<a name="FIGADAIN">`.
    const auto header_structural =
        !segment_opcode_is_display_line_prefix(*ref.record, segment) &&
        !segment.source_tokens.empty() &&
        segment.kind == BookControlKind::structural && !segment.malformed;
    const auto header_lower = ascii_lower(segment.opcode);
    const auto header_figure_start = header_structural &&
                                     header_lower.rfind("srfig", 0) == 0 &&
                                     header_lower.size() > 5;
    const auto header_figure_end = header_structural && header_lower == "srefig";
    if (open_header_region) {
      // Everything between the two controls is the region's own drawing.
      const auto figure = open_header_region->region.start.kind ==
                          TrapEmbeddedControlKindIR::figure_start;
      const auto table_end =
          !segment_opcode_is_display_line_prefix(*ref.record, segment) &&
          segment.kind == BookControlKind::table_end;
      if (figure ? table_end : header_figure_end)
        return reject("trap header closes an envelope it did not open: " +
                      segment.opcode);
      if (figure ? !header_figure_end : !table_end)
        continue;
      if (segment.source_tokens.empty())
        return reject("trap header table envelope end has no source token: " +
                      open_header_region->region.identifier);
      auto opened = *std::move(open_header_region);
      open_header_region.reset();
      opened.region.end = {figure ? TrapEmbeddedControlKindIR::figure_end
                                  : TrapEmbeddedControlKindIR::table_end,
                           {},
                           segment_slice(*ref.record, segment)};
      if (!project_embedded_region(records, index, opened.start_record,
                                   opened.start_token,
                                   ref.record->logical_record,
                                   segment.source_tokens.front(),
                                   opened.region, catalog.introduction_cells,
                                   introduction_cell_positions,
                                   error))
        return std::nullopt;
      // The closing control's own payload is introduction prose again -- the
      // flattened splitter gives `SRETBL` the display lines that follow it --
      // so the prose after the envelope resumes at that payload where there
      // is one, and otherwise at the token after the control.
      const auto resume_payload = payload_first_token(*ref.record, segment);
      opened.resume_record = ref.record->logical_record;
      opened.resume_token =
          resume_payload ? *resume_payload : segment.source_tokens.back() + 1;
      header_regions.push_back(std::move(opened));
      continue;
    }
    if (marker_attached_title_control(*ref.record, segment)) {
      if (title)
        return reject("trap catalog has multiple title segments");
      title = ref;
      continue;
    }
    if (header_figure_end)
      return reject("trap catalog header closes a figure envelope it did not "
                    "open");
    if (header_figure_start) {
      HeaderRegion opened;
      opened.start_record = ref.record->logical_record;
      const auto payload = payload_first_token(*ref.record, segment);
      opened.start_token = payload ? *payload : segment.source_tokens.back() + 1;
      opened.region.identifier = control_identifier(segment.opcode, "srfig");
      opened.region.start = {TrapEmbeddedControlKindIR::figure_start,
                             opened.region.identifier,
                             segment_slice(*ref.record, segment)};
      opened.open_record = ref.record->logical_record;
      opened.open_segment = segment.segment_index;
      open_header_region = std::move(opened);
      continue;
    }
    const auto operand_text = collapse(range_text(*ref.record, segment.operand_range));
    switch (segment.kind) {
    case BookControlKind::topic_start:
      if (segment.opcode.size() > 2)
        catalog.raw_topic_id = segment.opcode.substr(2);
      break;
    case BookControlKind::heading_level: {
      auto value = operand_text;
      if (!value.empty() && value.front() == ':')
        value.erase(value.begin());
      catalog.heading_level = value;
      break;
    }
    case BookControlKind::topic_number:
    case BookControlKind::parent:
    case BookControlKind::forward_level:
    case BookControlKind::back_level:
    case BookControlKind::summary:
    case BookControlKind::source_file:
      break;
    case BookControlKind::title:
      if (title)
        return reject("trap catalog has multiple title segments");
      title = ref;
      break;
    case BookControlKind::message_start:
      catalog.anchors.push_back({"MSG", segment_slice(*ref.record, segment)});
      break;
    case BookControlKind::structural: {
      const auto lower = ascii_lower(segment.opcode);
      bool reserved = false;
      for (const auto* prefix : {"srfig", "srefig", "srtbl", "sretbl",
                                 "srftn", "sreftn", "srmsg", "srlist",
                                 "srelist", "srlblbox", "srelblbox"})
        reserved = reserved || lower.rfind(prefix, 0) == 0;
      if (lower.rfind("sr", 0) == 0 && lower.size() > 2 && !reserved &&
          !segment.malformed &&
          !is_display_line_length_opcode(*ref.record, segment) &&
          !is_display_line_embedded_opcode(*ref.record, segment)) {
        auto id = segment.opcode.substr(2);
        if (!title) {
          // Metadata extends an SR anchor's identity; body payload remains
          // text. Native bounds exclude the next line's dictionary prefix.
          auto name = collapse(range_text(*ref.record, segment.complete));
          const auto words = decoded_byte_range_to_word_range(
              ref.record->assembled, segment.opcode_range);
          const auto tokens = source_tokens_intersecting_output(
              ref.record->assembled, words.begin, words.end);
          if (!tokens.empty())
            if (const auto* line = display_line_of_token(*ref.record, tokens.front()))
              name = collapse(display_line_text(*ref.record, *line));
          if (ascii_starts_with_case_insensitive(name, "SR")) id = name.substr(2);
        }
        catalog.anchors.push_back({std::move(id), segment_slice(*ref.record, segment)});
      }
      break;
    }
    case BookControlKind::table_start: {
      if (segment.source_tokens.empty() ||
          segment_opcode_is_display_line_prefix(*ref.record, segment))
        break;
      HeaderRegion opened;
      opened.start_record = ref.record->logical_record;
      // As in an entry: the drawing starts at the opening control's own
      // payload where it has one, otherwise at the token after the control.
      const auto payload = payload_first_token(*ref.record, segment);
      opened.start_token = payload ? *payload : segment.source_tokens.back() + 1;
      opened.region.identifier = control_identifier(segment.opcode, "srtbl");
      opened.region.start = {TrapEmbeddedControlKindIR::table_start,
                             opened.region.identifier,
                             segment_slice(*ref.record, segment)};
      opened.open_record = ref.record->logical_record;
      opened.open_segment = segment.segment_index;
      open_header_region = std::move(opened);
      break;
    }
    case BookControlKind::table_end:
      return reject("trap catalog header closes a table envelope it did not "
                    "open: " +
                    segment.opcode);
    case BookControlKind::text:
    case BookControlKind::font:
    case BookControlKind::spacing:
    // `cselect` marks a hotspot region of the display lines that follow it,
    // and the flattened splitter gives it their text as its payload; in the
    // header that text is introduction prose, which
    // `extract_message_prose_paragraphs_ir` reads from the display rows.
    case BookControlKind::select:
      break;
    default:
      return reject("trap catalog header contains an unsupported control: " +
                    segment.opcode);
    }
  }
  if (open_header_region)
    return reject("trap catalog header table envelope is not closed: " +
                  open_header_region->region.identifier);
  if (!title || catalog.raw_topic_id.empty() ||
      catalog.heading_level.size() != 2 ||
      (catalog.heading_level.front() != 'H' &&
       catalog.heading_level.front() != 'h') ||
      catalog.heading_level.back() < '1' || catalog.heading_level.back() > '6')
    return reject("trap catalog metadata/title envelope is incomplete");
  const auto attached_title =
      marker_attached_title_control(*title->record, *title->segment);
  const RowRef *title_ref = nullptr;
  const std::vector<RowRef> *title_row_refs = nullptr;
  std::size_t title_begin_token = 0;
  std::size_t title_end_token = 0;
  std::string title_row_text;
  if (attached_title) {
    const auto control_token = title->segment->source_tokens.front();
    const auto *line = display_line_of_token(*title->record, control_token);
    if (line == nullptr || control_token + 3 >= line->token_end)
      return reject("trap catalog attached title row is empty");
    title_begin_token = control_token + 3;
    title_end_token = line->token_end;
    title_row_text = collapse(token_range_text(
        *title->record, title_begin_token, title_end_token));
  } else {
    const auto title_rows = index.rows_by_segment.find(
        {title->record->logical_record, title->segment->segment_index});
    if (title_rows == index.rows_by_segment.end() ||
        title_rows->second.empty() ||
        title_rows->second.front().row_index != 0)
      return reject("trap catalog title has no leading physical row");
    title_ref = &title_rows->second.front();
    title_row_refs = &title_rows->second;
    if (title_ref->row->marker)
      return reject("trap catalog title row carries a marker slot");
    title_begin_token = title_ref->row->token_begin;
    title_end_token = title_ref->row->token_end;
    title_row_text = collapse(title_ref->row->visible_text);
  }
  if (title_row_text.empty())
    return reject("trap catalog title row is empty");
  const auto expected_title = collapse(toc_title);
  if (!expected_title.empty() && title_row_text != expected_title &&
      ascii_lower(title_row_text) == ascii_lower(expected_title)) {
    // The source title row is authoritative for spelling and case. The topic
    // catalogue independently corroborates its extent, so a case-only
    // difference does not make the native row ambiguous.
    catalog.title = title_row_text;
  } else if (attached_title && !expected_title.empty() &&
             title_row_text != expected_title) {
    return reject("trap catalog attached title row differs from the contents "
                  "title: [" +
                  title_row_text + "] vs [" + expected_title + "]");
  } else if (!expected_title.empty() && title_row_text != expected_title) {
    // The heading and the first introduction sentence share one physical
    // row; the contents title decides where the heading ends. Accumulate
    // the title segment's visible cells token by token until they spell the
    // title.
    //
    // The heading can also outrun its first row: SC09-138 `H.0` draws
    // `Appendix H` and `C/370 Compiler Return Codes and Messages` as two
    // rows of the same title segment, joined by the `.` LayoutIR reads as
    // the second row's marker slot. So the walk runs to the end of the
    // segment's last row, and a marker slot inside it counts as the glyph it
    // draws -- hosted serves `Appendix H.  C/370 ...` as one heading.
    auto title_walk_end = title_ref->row->token_end;
    for (const auto &row : *title_row_refs)
      if (row.row->logical_record == title->record->logical_record &&
          row.row->token_end > title_walk_end)
        title_walk_end = row.row->token_end;
    std::string accumulated;
    bool pending_space = false;
    std::optional<std::size_t> split;
    for (auto token = title_ref->row->token_begin;
         token < title_walk_end && token < title->record->ir.tokens.size() &&
         !split;
         ++token) {
      const auto &words = title->record->ir.tokens[token].decoded_words;
      for (std::size_t word = 0; word < words.size(); ++word) {
        const auto cell =
            index.cells.find({title->record->logical_record, token, word});
        if (cell != index.cells.end() &&
            (cell->second->disposition == SourceDisposition::visible_content ||
             cell->second->disposition == SourceDisposition::marker_slot) &&
            words[word] <= 0xff && words[word] >= 0x20 && words[word] != ' ') {
          if (pending_space && !accumulated.empty())
            accumulated.push_back(' ');
          pending_space = false;
          accumulated += token_words_to_ascii({words[word]});
        } else {
          pending_space = true;
        }
      }
      pending_space = true;
      // Inter-token spacing of a title row is not provenance-typed here
      // (hyphenated words split across tokens); compare the glyph sequence.
      const auto without_spaces = [](std::string value) {
        value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
        return value;
      };
      if (without_spaces(accumulated) == without_spaces(expected_title))
        split = token + 1;
    }
    if (!split)
      return reject("trap catalog title row does not begin with the contents "
                    "title: [" +
                    title_row_text + "] vs [" + expected_title +
                    "] accumulated [" + accumulated + "]");
    title_end_token = *split;
    catalog.title = expected_title;
  } else {
    catalog.title = title_row_text;
  }
  catalog.title_source =
      token_slice(*title->record, title->segment->segment_index,
                  title_begin_token, title_end_token);
  if (title_ref != nullptr)
    catalog.title_row = {title_ref->run->id, title_ref->row_index};

  // Entries.
  std::optional<std::size_t> origin_column;
  // The column the catalog's own field labels start at. SC31-711's trap
  // catalogs indent headline and labels alike (both `cfont 3 …`), but that
  // is a property of one catalog, not of the family: N2AH1MST 4.0 writes its
  // headline at column 3 (`cfont 3 7 2 11 8 2 …` over
  // `AMA100I AMASPZAP PROCESSING COMPLETED`) and every field label at column
  // 10 (`cfont 10 12 2` over `Explanation:`). The label column is therefore
  // discovered from the first labelled line of the catalog and then required
  // of every later label, exactly as the label vocabulary is.
  std::optional<std::size_t> label_column;
  for (std::size_t entry_index = 0; entry_index < starts.size();
       ++entry_index) {
    const auto begin = starts[entry_index];
    const auto end = entry_index + 1 < starts.size() ? starts[entry_index + 1]
                                                      : ordered.size();
    const auto &start = ordered[begin];
    TrapEntryIR entry;
    CellPositions entry_cell_positions;
    entry.operand =
        collapse(range_text(*start.record, start.segment->operand_range));
    entry.id = first_word(entry.operand);
    entry.start_source = segment_slice(*start.record, *start.segment);
    {
      // The SRMSG payload carries the operand tail (a symbolic name after
      // the numeric ID) followed by the entry separator placeholder run. Both
      // are suppressed from the entry text; the tail completes the operand.
      TrapTextIR separator;
      std::vector<TrapSourceCellIR> separator_cells;
      CellPositions separator_cell_positions;
      if (!walk_payload(*start.record, *start.segment, index, separator,
                        separator_cells, separator_cell_positions, error,
                        true))
        return std::nullopt;
      for (auto &cell : separator_cells) {
        cell.role = TrapCellRoleIR::suppressed;
        append_unique_cell(entry.cells, entry_cell_positions, cell);
      }
      const auto tail = collapse(separator.text);
      if (!tail.empty())
        entry.operand += " " + tail;
      entry.suppressed_rows = separator.source_rows;
    }
    bool have_headline = false;
    TrapLineIR *current = nullptr;
    // The embedded `SRTBL` envelope currently open, with the record and the
    // token its drawing starts at.
    struct OpenRegion {
      TrapEmbeddedRegionIR region;
      std::uint32_t record = 0;
      std::size_t token = 0;
    };
    std::optional<OpenRegion> open_region;
    // The `SRFTN<id>` footnote envelope currently open: the line whose flow
    // holds it and the block's index in that flow.
    TrapLineIR *footnote_line = nullptr;
    std::size_t footnote_block = 0;
    const auto open_footnote = [&]() -> TrapFlowFootnoteIR & {
      return std::get<TrapFlowFootnoteIR>(
          footnote_line->body.flow.blocks[footnote_block].value);
    };
    // Payload-less selectors queued on consecutive control lines, waiting
    // for the selector whose payload draws the row they all mark.
    std::vector<QueuedSelector> queued_selectors;
    // Cell index the entry's text resumes at after an embedded region has
    // claimed a run of the ledger, so the region does not read as a gap in
    // the interrupted field's own cells.
    std::size_t resume_cell = 0;
    // Set once a bare `SRMSG` stands inside the entry. Plain rows may follow
    // it as entry text: SC24-546 A.0 record 972 line 0, `The following are
    // the REXX error messages:`, which hosted A.0 (DT 19940323131240) serves
    // inside `<a name="MSG">` after the last field of DMSREX255T. A
    // `cfont`-styled row may not: SC09-138 F.1 record 2061 draws a
    // `cfont 3 8 K ...` heading row and `Message Format:` rows after one,
    // which hosted F.1 (DT 19910321130500) serves as an italic heading and
    // bold rows, one per line, between two groups of entries. Where the rows
    // prove that heading (`read_group_heading`) it closes the entry; any
    // other styled row there would run into the field before it, so it
    // declines.
    bool after_bare_anchor = false;
    // The drawn rows a subject-index segment runs on into, walked as the
    // next pass of the loop over the same segment (see the SI branch).
    std::optional<ControlSegmentIR> subject_index_rest;
    bool walk_subject_index_rest = false;
    std::vector<TrapParagraphBoundaryIR> pending_boundaries;
    const auto queue_boundary = [&](TrapParagraphBoundaryIR boundary) {
      if (std::none_of(pending_boundaries.begin(), pending_boundaries.end(),
                       [&](const auto &existing) {
                         return existing.source == boundary.source &&
                                existing.cell_begin == boundary.cell_begin &&
                                existing.cell_end == boundary.cell_end;
                       }))
        pending_boundaries.push_back(std::move(boundary));
    };
    for (auto at = begin + 1; at < end; ++at) {
      const auto &ref = ordered[at];
      const auto walking_subject_index_rest = walk_subject_index_rest;
      walk_subject_index_rest = false;
      const auto &segment =
          walking_subject_index_rest ? *subject_index_rest : *ref.segment;
      const auto ledger_empty_lines = [&] {
        const auto empty = index.empty_line_prefixes.find(
            ref.record->logical_record);
        if (empty != index.empty_line_prefixes.end())
          for (const auto token : segment.source_tokens)
            if (std::find(empty->second.begin(), empty->second.end(), token) !=
                empty->second.end()) {
              const auto already = std::find_if(
                  entry.cells.begin(), entry.cells.end(), [&](const auto &cell) {
                    return cell.logical_record == ref.record->logical_record &&
                           cell.token_index == token;
                  });
              std::size_t cell_begin = 0, cell_end = 0;
              if (already != entry.cells.end()) {
                cell_begin =
                    static_cast<std::size_t>(already - entry.cells.begin());
                cell_end = cell_begin;
                while (cell_end < entry.cells.size() &&
                       entry.cells[cell_end].logical_record ==
                           ref.record->logical_record &&
                       entry.cells[cell_end].token_index == token)
                  ++cell_end;
              } else {
                cell_begin = entry.cells.size();
                for (std::size_t word = 0;
                     word < ref.record->ir.tokens[token].decoded_words.size();
                     ++word) {
                  const auto owned = index.cells.find(
                      {ref.record->logical_record, token, word});
                  append_unique_cell(
                      entry.cells, entry_cell_positions,
                      {ref.record->logical_record, token, word,
                       ref.record->ir.tokens[token].decoded_words[word],
                       owned == index.cells.end()
                           ? SourceDisposition::opaque
                           : owned->second->disposition,
                       TrapCellRoleIR::layout_marker});
                }
                cell_end = entry.cells.size();
              }
              queue_boundary(
                  {token_slice(*ref.record, segment.segment_index, token,
                               token + 1),
                   cell_begin, cell_end});
              if (current != nullptr)
                current->body.cell_end = entry.cells.size();
            }
      };
      if (segment_is_display_line_prefix(*ref.record, segment) ||
          nondrawing_control(index, *ref.record, segment)) {
        if (!segment_is_display_line_prefix(*ref.record, segment) &&
            segment.kind == BookControlKind::select && current != nullptr &&
            !open_region) {
          std::string selector_error;
          auto selector = read_trap_selector_source(
              *ref.record, segment, &selector_error,
              find_record(records, ref.record->logical_record + 1));
          // A selector whose row cannot be found is not dropped.
          if (!selector)
            return reject("trap entry selector rejected: " + entry.id + ": " +
                          selector_error);
          // A selector that closes its record marks the next record's first
          // row, as in the introduction: N2AH1MST record 1886 ends with line
          // 41 `cselect 46 4 FTNFTNUNIQ32`, record 1887 line 0 draws
          // `01F0   Function not supported by VM. (*) partition is not`, and
          // hosted 21.0 (DT 19910329000100) links that `(*)`.
          if (selector && (selector->queued || selector->target_record !=
                                                   ref.record->logical_record))
            queued_selectors.push_back(
                {std::move(*selector), segment_slice(*ref.record, segment)});
        }
        ledger_empty_lines();
        continue;
      }
      // Selectors queued from the record before mark this record's first
      // row, which its leading text segment draws.
      const auto carried_selectors =
          !queued_selectors.empty() && segment.segment_index == 0 &&
          (segment.kind == BookControlKind::text ||
           segment.kind == BookControlKind::unknown) &&
          std::all_of(queued_selectors.begin(), queued_selectors.end(),
                      [&](const auto &waiting) {
                        return waiting.selector.target_record ==
                                   ref.record->logical_record &&
                               waiting.source.logical_record + 1 ==
                                   ref.record->logical_record;
                      });
      if (!queued_selectors.empty() &&
          segment.kind != BookControlKind::select &&
          segment.kind != BookControlKind::font && !carried_selectors)
        return reject("trap queued selector has no drawn row: " + entry.id);
      const auto opcode_is_line_prefix =
          segment_opcode_is_display_line_prefix(*ref.record, segment);
      const auto lower_opcode = ascii_lower(segment.opcode);
      // A subject-index line (`SI <term>`) is index metadata, not entry
      // text: SC34-425 2.4.11.1 record 1509 line 25 is
      // `SI translators, FLMLPGEN` after the last field of entry 599, and
      // hosted BookServer (DT 19921112160049) serves nothing for it. The
      // entry keeps it as an index term and ledgers its cells as control,
      // so it never runs into the field it follows. The flattened splitter
      // may hand the segment the display lines after it; those rows are
      // entry text again, walked from the next line's length byte as a
      // second pass over the segment.
      if (!opcode_is_line_prefix && !segment.source_tokens.empty()) {
        const auto *si_line =
            display_line_of_token(*ref.record, segment.source_tokens.front());
        if (si_line != nullptr &&
            si_line->prefix_token + 1 == segment.source_tokens.front() &&
            is_subject_index_display_line(*ref.record, *si_line)) {
          // The segment can end on the next line's length byte, which is
          // structure whatever its dictionary word: SC09-138 F.1 record 2061
          // token 216 (`0B` at 0x109283, line 18's length 11) spells a
          // U+2500 rule run, and line 18 opens the next `SI`.
          const auto runs_on = std::any_of(
              segment.source_tokens.begin(), segment.source_tokens.end(),
              [&](const auto token) {
                return token >= si_line->token_end &&
                       !is_display_line_length_token(*ref.record, token);
              });
          // A segment that runs on draws rows after the SI line, and they are
          // entry text where the source puts them: SC34-425 APPENDIX1.5.3
          // record 2845 segment 3 is `SI modes, SHR` (line 11) and the bullet
          // row `   °   Free the data set so that it can be allocated in SHR
          // mode.` (line 12), which hosted (DT 19921112160049) serves as the
          // second item of FLM80020's `User Response:` list. The rest starts
          // at the next line's length byte and keeps the segment's index.
          if (runs_on) {
            const auto rest_token = si_line->token_end;
            const auto span = std::find_if(
                ref.record->assembled.tokens.begin(),
                ref.record->assembled.tokens.end(),
                [&](const auto &token) { return token.token_index == rest_token; });
            if (span == ref.record->assembled.tokens.end() ||
                !is_display_line_length_token(*ref.record, rest_token))
              return reject("trap entry subject index line carries text after "
                            "it: " +
                            entry.id);
            const auto rest_byte = decoded_word_range_to_byte_range(
                                       ref.record->assembled,
                                       {span->output_begin, span->output_begin})
                                       .begin;
            ControlSegmentIR rest = segment;
            rest.kind = BookControlKind::text;
            rest.opcode.clear();
            rest.complete.begin = rest_byte;
            rest.opcode_range = {rest_byte, rest_byte};
            rest.operand_range = {rest_byte, rest_byte};
            rest.payload_range = {rest_byte, segment.complete.end};
            rest.source_tokens.erase(
                std::remove_if(rest.source_tokens.begin(),
                               rest.source_tokens.end(),
                               [&](const auto token) { return token < rest_token; }),
                rest.source_tokens.end());
            if (rest.source_tokens.empty() ||
                rest.source_tokens.front() != rest_token)
              return reject("trap entry subject index line carries text after "
                            "it: " +
                            entry.id);
            subject_index_rest = std::move(rest);
          }
          const auto keyword = si_line->prefix_token + 1;
          // The drawn line, keyword dropped: the record assembler's own
          // sentence casing would rewrite the term's first word.
          auto term = collapse(display_line_text(*ref.record, *si_line));
          const auto keyword_text =
              token_words_to_ascii(ref.record->tokens[keyword]);
          term = term.compare(0, keyword_text.size(), keyword_text) == 0
                     ? collapse(term.substr(keyword_text.size()))
                     : std::string{};
          if (term.empty())
            return reject("trap entry subject index line names no term: " +
                          entry.id);
          for (auto token = keyword; token < si_line->token_end; ++token) {
            const auto &line_words = ref.record->ir.tokens[token].decoded_words;
            for (std::size_t word = 0; word < line_words.size(); ++word) {
              const auto owned =
                  index.cells.find({ref.record->logical_record, token, word});
              append_unique_cell(
                  entry.cells, entry_cell_positions,
                  {ref.record->logical_record, token, word, line_words[word],
                   owned == index.cells.end() ? SourceDisposition::opaque
                                              : owned->second->disposition,
                   TrapCellRoleIR::control});
            }
          }
          // The text after the line resumes past the term's ledger run, as
          // it does after an embedded region.
          resume_cell = entry.cells.size();
          entry.index_terms.push_back(
              {term, token_slice(*ref.record, segment.segment_index, keyword,
                                 si_line->token_end)});
          if (runs_on) {
            walk_subject_index_rest = true;
            --at;
          }
          continue;
        }
      }
      const auto structural_control =
          !opcode_is_line_prefix && !segment.source_tokens.empty() &&
          segment.kind == BookControlKind::structural && !segment.malformed;
      const auto figure_start = structural_control &&
                                lower_opcode.rfind("srfig", 0) == 0 &&
                                lower_opcode.size() > 5;
      const auto figure_end = structural_control && lower_opcode == "srefig";
      const auto footnote_start = structural_control &&
                                  lower_opcode.rfind("srftn", 0) == 0 &&
                                  lower_opcode.size() > 5;
      const auto footnote_end = structural_control && lower_opcode == "sreftn";
      const auto region_was_open = open_region.has_value();
      if (current != nullptr && !segment.source_tokens.empty() &&
          !current->body.source_slices.empty() &&
          current->body.source_slices.back().logical_record ==
              ref.record->logical_record) {
        const auto prefixes = empty_lines_between(
            index, ref.record->logical_record,
            current->body.source_slices.back().token_end - 1,
            segment.source_tokens.front());
        const auto owners = segment_of_token(*ref.record);
        for (const auto prefix : prefixes) {
          const auto already = std::find_if(
              entry.cells.begin(), entry.cells.end(), [&](const auto &cell) {
                return cell.logical_record == ref.record->logical_record &&
                       cell.token_index == prefix;
              });
          std::size_t cell_begin = 0, cell_end = 0;
          if (already != entry.cells.end()) {
            cell_begin = static_cast<std::size_t>(already - entry.cells.begin());
            cell_end = cell_begin;
            while (cell_end < entry.cells.size() &&
                   entry.cells[cell_end].logical_record ==
                       ref.record->logical_record &&
                   entry.cells[cell_end].token_index == prefix)
              ++cell_end;
          } else {
            cell_begin = entry.cells.size();
            for (std::size_t word = 0;
                 word < ref.record->ir.tokens[prefix].decoded_words.size();
                 ++word) {
              const auto owned = index.cells.find(
                  {ref.record->logical_record, prefix, word});
              append_unique_cell(
                  entry.cells, entry_cell_positions,
                  {ref.record->logical_record, prefix, word,
                   ref.record->ir.tokens[prefix].decoded_words[word],
                   owned == index.cells.end() ? SourceDisposition::opaque
                                              : owned->second->disposition,
                   TrapCellRoleIR::layout_marker});
            }
            cell_end = entry.cells.size();
          }
          if (cell_begin == cell_end)
            return reject("trap paragraph boundary has no source cell: " +
                          entry.id);
          queue_boundary(
              {token_slice(*ref.record, owners[prefix], prefix, prefix + 1),
               cell_begin, cell_end});
          current->body.cell_end = entry.cells.size();
        }
      }
      // `SRTBL<id>` opens a drawn table or figure envelope inside the entry
      // and `SRETBL` closes it. Everything between the two is the region's
      // own drawing, kept as the display lines the record frames rather than
      // flowed into the field the envelope interrupts.
      if ((!opcode_is_line_prefix && !segment.source_tokens.empty() &&
           segment.kind == BookControlKind::table_start) ||
          figure_start) {
        if (open_region)
          return reject("trap entry nests a table envelope: " + entry.id);
        if (current == nullptr)
          return reject("trap entry table envelope precedes its headline: " +
                        entry.id);
        if (footnote_line != nullptr)
          return reject("trap entry draws an envelope inside a footnote: " +
                        entry.id);
        OpenRegion opened;
        opened.record = ref.record->logical_record;
        // The envelope's drawing starts at the opening control's own
        // payload where it has one -- the flattened splitter keeps the
        // region's first rows inside that segment -- and otherwise at the
        // token after the control.
        const auto payload = payload_first_token(*ref.record, segment);
        opened.token = payload ? *payload : segment.source_tokens.back() + 1;
        opened.region.identifier =
            control_identifier(segment.opcode, figure_start ? "srfig" : "srtbl");
        opened.region.start = {figure_start
                                   ? TrapEmbeddedControlKindIR::figure_start
                                   : TrapEmbeddedControlKindIR::table_start,
                               opened.region.identifier,
                               segment_slice(*ref.record, segment)};
        opened.region.after_field = entry.fields.size();
        opened.region.cell_begin = entry.cells.size();
        open_region = std::move(opened);
        continue;
      }
      if (open_region) {
        const auto figure =
            open_region->region.start.kind ==
            TrapEmbeddedControlKindIR::figure_start;
        const auto table_end = !opcode_is_line_prefix &&
                               segment.kind == BookControlKind::table_end;
        if (figure ? table_end : figure_end)
          return reject("trap entry closes an envelope it did not open: " +
                        entry.id + " " + segment.opcode);
        if (figure ? !figure_end : !table_end)
          continue;
        if (segment.source_tokens.empty())
          return reject("trap table envelope end has no source token: " +
                        entry.id);
        auto &region = open_region->region;
        region.end = {figure ? TrapEmbeddedControlKindIR::figure_end
                             : TrapEmbeddedControlKindIR::table_end,
                      {},
                      segment_slice(*ref.record, segment)};
        if (!project_embedded_region(records, index, open_region->record,
                                     open_region->token,
                                     ref.record->logical_record,
                                     segment.source_tokens.front(), region,
                                     entry.cells, entry_cell_positions, error))
          return std::nullopt;
        region.cell_end = entry.cells.size();
        resume_cell = region.cell_end;
        current->body.flow.blocks.push_back({region});
        entry.embedded_regions.push_back(std::move(region));
        open_region.reset();
        // The closing control's own payload is entry text again: N2AH1MST
        // record 1574 segment 1 reads `SRETBL              Also in the
        // message text:`. Fall through to the text branches below.
      }
      if (!opcode_is_line_prefix &&
          segment.kind == BookControlKind::message_start &&
          segment.payload_range.begin == segment.payload_range.end &&
          (collapse(range_text(*ref.record, segment.operand_range)).empty() ||
           message_operand_is_next_line_length(*ref.record, segment))) {
        // An `SRMSG` with neither operand nor payload is a bare anchor:
        // N2AH1MST record 177 line 38 is the whole one-byte line `SRMSG`,
        // and hosted 2.0 (DT 19910329000100) serves it as `<a name="MSG">`
        // immediately before the footnote anchor. It draws nothing, and it
        // stands where the source puts it.
        if (current == nullptr) {
          catalog.anchors.push_back(
              {"MSG", segment_slice(*ref.record, segment)});
        } else {
          if (footnote_line != nullptr)
            return reject("trap entry names a destination inside a footnote: " +
                          entry.id);
          // A styled heading opening the next group of entries closes the
          // entry: it claims every row up to the next entry's `SRMSG`.
          if (queued_selectors.empty())
            if (auto heading =
                    read_group_heading(records, index, ordered, at, end,
                                       entry.cells, entry_cell_positions)) {
              entry.closing_heading = std::move(*heading);
              break;
            }
          current->body.flow.blocks.push_back(
              {TrapFlowAnchorIR{"MSG", segment_slice(*ref.record, segment)}});
          after_bare_anchor = true;
        }
        continue;
      }
      if (figure_end && !region_was_open)
        return reject("trap entry closes a figure it did not open: " +
                      entry.id);
      // `SRFTN<id>` ... `SREFTN` is a footnote drawn where it stands: hosted
      // N2AH1MST 10.0 (DT 19910329000100) serves `<a name="FTNFTNUNIQ19">
      // <hr><h5> ... </h5></a>` between two native list rows of `ATB348I`.
      // The opener's payload is the footnote's own text; the closer names
      // nothing and its payload is entry text again.
      if (footnote_start) {
        // Before the headline the footnote stands between the entry's
        // `SRMSG` and its first row: N2AH1MST record 1936 lines 13-19, served
        // by hosted 21.0 (DT 19910329000100) after `<a name="MSG ERB415I">`.
        // It opens on the entry's leading line, which holds footnotes only;
        // its closer hands the entry back to the headline.
        if (current == nullptr && !have_headline && footnote_line == nullptr) {
          current = &entry.leading;
          current->body.cell_begin = current->body.flow.blocks.empty()
                                         ? entry.cells.size()
                                         : current->body.cell_begin;
          current->body.cell_end = entry.cells.size();
        }
        if (current == nullptr || footnote_line != nullptr)
          return reject("trap entry footnote envelope is misplaced: " +
                        entry.id + " " + segment.opcode);
        TrapFlowFootnoteIR footnote;
        footnote.id = segment.opcode.substr(2);
        footnote.start = segment_slice(*ref.record, segment);
        footnote.cell_begin = entry.cells.size();
        footnote.cell_end = footnote.cell_begin;
        current->body.flow.blocks.push_back({std::move(footnote)});
        footnote_line = current;
        footnote_block = current->body.flow.blocks.size() - 1;
      } else if (footnote_end) {
        if (footnote_line == nullptr || footnote_line != current)
          return reject("trap entry closes a footnote it did not open: " +
                        entry.id);
        auto &footnote = open_footnote();
        if (footnote.blocks.empty())
          return reject("trap entry footnote has no text: " + entry.id);
        footnote.end = segment_slice(*ref.record, segment);
        footnote.cell_end = entry.cells.size();
        footnote_line = nullptr;
      }
      if (!opcode_is_line_prefix && segment.kind == BookControlKind::font) {
        std::string font_error;
        const auto spans =
            decode_font_control_spans(*ref.record, segment, &font_error);
        if (!spans)
          return reject("trap entry font control rejected: " + entry.id +
                        ": " + font_error);
        TrapLineIR line;
        line.font_source = segment_slice(*ref.record, segment);
        line.body.cell_begin = entry.cells.size();
        std::size_t highlighted_columns = 0;
        // Each span's own columns, relative to the leftmost one, so a walk
        // can ask whether a span covers a cell rather than only whether the
        // cell lies somewhere in the highlighted extent.
        std::vector<std::pair<std::size_t, std::size_t>> highlight_ranges;
        {
          std::size_t first_column = 0;
          bool have_first = false;
          for (const auto &span : spans->spans)
            if (span.length != 0 && (!have_first || span.column < first_column)) {
              first_column = span.column;
              have_first = true;
            }
          for (const auto &span : spans->spans)
            if (have_first && span.length != 0) {
              highlighted_columns = std::max(
                  highlighted_columns, span.column + span.length - first_column);
              if (span.column >= first_column)
                highlight_ranges.push_back(
                    {span.column - first_column,
                     span.column - first_column + span.length});
            }
        }
        // Selectors waiting before the font line mark the row it styles.
        // When that row opens the next record, they travel with the font's
        // deferred payload below: SC09-138 F.1 record 2050 ends with
        // `cselect 64 13 HDRFETCH` and `cfont 36 5 X 65 5 E`, and record
        // 2051 line 0 is the row both mark.
        const auto selectors_deferred =
            !queued_selectors.empty() &&
            std::all_of(queued_selectors.begin(), queued_selectors.end(),
                        [&](const auto &waiting) {
                          return waiting.source.logical_record ==
                                     ref.record->logical_record &&
                                 waiting.selector.target_record ==
                                     ref.record->logical_record + 1;
                        });
        if (!walk_payload(*ref.record, segment, index, line.body, entry.cells,
                          entry_cell_positions, error, false,
                          highlighted_columns, false, &highlight_ranges,
                          queued_selectors.empty() || selectors_deferred
                              ? nullptr
                              : &queued_selectors,
                          &*spans))
          return std::nullopt;
        if (!selectors_deferred)
          queued_selectors.clear();
        // A font control whose payload is empty at the record end carries its
        // text in the next record's leading text segment. That segment can
        // open on the row's own length byte when the byte happens to spell
        // a control word: N2AH1MST record 658 line 0 is the length byte 37
        // (`25` at 0x492F1, spelling `cfont`) and then the headline
        // `   AVM007I SUBSYSTEM ssid ...` that record 657's closing
        // `cfont 3 7 2 11 9 2 ...` styles.
        const auto next_record_row =
            line.body.text.empty() && at + 1 < end &&
            segment.segment_index + 1 == ref.record->control_segments.size() &&
            ordered[at + 1].record->logical_record ==
                ref.record->logical_record + 1 &&
            ordered[at + 1].segment->segment_index == 0;
        const auto next_record_prefixed =
            next_record_row &&
            segment_opcode_is_display_line_prefix(*ordered[at + 1].record,
                                                  *ordered[at + 1].segment);
        if (next_record_row &&
            (next_record_prefixed ||
             ordered[at + 1].segment->kind == BookControlKind::text ||
             ordered[at + 1].segment->kind == BookControlKind::unknown)) {
          ++at;
          // The CFONT's own columns travel with its deferred payload: the
          // control ends one record and its row opens the next.
          if (!walk_payload(*ordered[at].record, *ordered[at].segment, index,
                            line.body, entry.cells, entry_cell_positions, error,
                            false, highlighted_columns, next_record_prefixed,
                            &highlight_ranges,
                            selectors_deferred ? &queued_selectors : nullptr,
                            &*spans))
            return std::nullopt;
          queued_selectors.clear();
        } else if (line.body.text.empty() && at + 1 < end &&
                   ordered[at + 1].record == ref.record &&
                   segment_opcode_is_display_line_prefix(
                       *ref.record, *ordered[at + 1].segment)) {
          // Same fact one segment earlier: a CFONT whose span list fills the
          // whole segment leaves an empty payload, and the flattened splitter
          // then cut the row's own text off at its display-line length byte.
          // N2AH1MST record 2400 segment 2 is `cfont 3 9 2 13 4 V ...` with
          // an empty payload and segment 3 opens on token 95, the one-byte
          // value 37 that spells `cfont` and is line 7's length byte; tokens
          // 96-120 are the headline text.
          ++at;
          if (!walk_payload(*ordered[at].record, *ordered[at].segment, index,
                            line.body, entry.cells, entry_cell_positions, error,
                            false, highlighted_columns, true,
                            &highlight_ranges, nullptr, &*spans))
            return std::nullopt;
        }
        line.body.cell_end = entry.cells.size();
        if (after_bare_anchor && footnote_line == nullptr &&
            !line.body.text.empty())
          return reject("trap entry draws a styled row after a bare SRMSG: " +
                        entry.id);
        if (!pending_boundaries.empty()) {
          auto *paragraph = first_paragraph(line.body.flow);
          if (paragraph != nullptr) {
            paragraph->break_before.insert(paragraph->break_before.begin(),
                                           pending_boundaries.begin(),
                                           pending_boundaries.end());
            paragraph->cell_begin = pending_boundaries.back().cell_end;
            line.body.paragraph_break = true;
            pending_boundaries.clear();
          }
        }
        const auto front_column = spans->spans.empty()
                                      ? std::size_t{0}
                                      : spans->spans.front().column;
        std::vector<FontSpanIR> chain;
        if (!have_headline) {
          chain = map_leading_chain(
              *spans, origin_column ? *origin_column : front_column,
              line.body.display_text, line.spans, line.spans_text);
        } else {
          // A field label is the highlighted run that opens the row and ends
          // in `:`. Look for it at the catalog's label column first, then at
          // the headline origin.
          const auto anchor = label_column ? *label_column : front_column;
          std::vector<TrapStyledSpanIR> candidate_spans;
          std::string candidate_text;
          auto candidate =
              map_leading_chain(*spans, anchor, line.body.display_text,
                                candidate_spans, candidate_text);
          // The label ends at its `:` when the CFONT goes on to style the
          // value: the label stands two blanks before its value, so a span
          // that ends in `:` and is followed by a gap of two or more columns
          // closes the label. N2AH1MST record 1464 line 16 is
          // `cfont 10 12 2 24 7 V 39 11 V` over
          // `          Explanation:  ctcname module module-name ...`, and
          // hosted 16.0 (DT 19910329000100) serves
          // `<B>Explanation:</B>  <var>ctcname</var>` as the field of
          // `CBR9915I`. A one-blank gap joins the words of one label
          // (`System Action:`), so it never closes one.
          if (!candidate.empty() && !candidate_text.empty() &&
              candidate_text.back() != ':') {
            for (std::size_t at = 0; at + 1 < candidate.size(); ++at) {
              const auto &here = candidate_spans[at];
              const auto next_column = candidate[at + 1].column;
              const auto here_end = candidate[at].column + candidate[at].length;
              // The label is drawn in one style and its value opens in
              // another. A headline continuation row such as N2AH1MST 2.0's
              // `prm:  text` (`V`, `2`, `V`) or a cited title such as 26.0's
              // `Catalog Administration:  Access ...` (all `C`) is not a
              // label, and hosted serves neither as one.
              const auto one_style = std::all_of(
                  candidate.begin(), candidate.begin() + at + 1,
                  [&](const auto &span) {
                    return span.code == candidate.front().code;
                  });
              if (here.text.empty() || here.text.back() != ':')
                continue;
              if (next_column < here_end + 2 || !one_style ||
                  candidate[at + 1].code == candidate.front().code)
                break;
              candidate.resize(at + 1);
              candidate_spans.resize(at + 1);
              candidate_text.clear();
              for (std::size_t kept = 0; kept < candidate.size(); ++kept) {
                if (kept != 0 &&
                    candidate[kept].column !=
                        candidate[kept - 1].column + candidate[kept - 1].length)
                  candidate_text.push_back(' ');
                candidate_text += candidate_spans[kept].text;
              }
              break;
            }
          }
          if (!candidate.empty() && !candidate_text.empty() &&
              candidate_text.back() == ':') {
            chain = std::move(candidate);
            line.spans = std::move(candidate_spans);
            line.spans_text = std::move(candidate_text);
          } else if (origin_column && anchor != *origin_column) {
            chain = map_leading_chain(*spans, *origin_column,
                                      line.body.display_text, line.spans,
                                      line.spans_text);
          } else {
            chain = std::move(candidate);
            line.spans = std::move(candidate_spans);
            line.spans_text = std::move(candidate_text);
          }
          // A continuation row drawn deeper than both the catalogue's origin
          // and its label column still carries the run its CFONT opens it
          // with. N2AH1MST 2.0 record 151 line 11 is `cfont 10 3 V 13 1 2
          // 16 4 V` over `          prm:  text`, the second row of the
          // `ADY001I` headline, and line 13 is `cfont 14 4 2 19 2 2 ...`
          // over `              THIS IS AN INVALID DAE VERB`; hosted
          // (DT 19910329000100) serves `<var>prm</var><B>:</B>  <var>text
          // </var>` and `<B>THIS</B> <B>IS</B> ...`. The run is read from the
          // row's first drawn column and admitted only where the row's own
          // cells spell every span; any other row stays unstyled text as
          // before. A run ending in `:` is left alone: at a column no label
          // has used, nothing says whether it is a label.
          if (chain.empty() && !spans->spans.empty()) {
            const DecodedLogicalRecordSource *drawn_record = nullptr;
            std::size_t drawn_token = 0;
            for (auto cell = line.body.cell_begin;
                 cell < line.body.cell_end && drawn_record == nullptr; ++cell)
              if (entry.cells[cell].role == TrapCellRoleIR::text ||
                  entry.cells[cell].role == TrapCellRoleIR::unrowed_text ||
                  entry.cells[cell].role == TrapCellRoleIR::lexical_marker ||
                  entry.cells[cell].role ==
                      TrapCellRoleIR::punctuation_marker) {
                drawn_record =
                    find_record(records, entry.cells[cell].logical_record);
                drawn_token = entry.cells[cell].token_index;
              }
            // The run is read from the column of the row's first drawn
            // glyph, where the projected text begins, so a span opens the
            // chain only if it starts on that very cell. N2AH1MST 16.0
            // record 928 line 0 is `cfont 36 1 V ...` over a row whose first
            // glyph, `orientation`, stands at cell 24 and whose cell 36 is
            // the standalone `o`; hosted serves `orientation <var>o</var>`,
            // and mapping from the span's own column put it on the `o` of
            // `orientation`, which spells the same glyph.
            const auto first_drawn =
                drawn_record == nullptr
                    ? std::optional<std::size_t>{}
                    : drawn_column(*drawn_record, drawn_token);
            std::vector<TrapStyledSpanIR> drawn_spans;
            std::string drawn_text;
            std::vector<FontSpanIR> drawn;
            if (first_drawn)
              drawn = map_leading_chain(*spans, *first_drawn,
                                        line.body.display_text, drawn_spans,
                                        drawn_text);
            auto proven = !drawn.empty() && !drawn_text.empty() &&
                          drawn_text.back() != ':' &&
                          line.body.text.compare(0, drawn_text.size(),
                                                 drawn_text) == 0 &&
                          drawn_record != nullptr &&
                          chain_matches_display_columns(
                              *drawn_record, drawn_token, drawn_spans);
            // Every span of the row also begins and ends on a drawn glyph
            // that meets a blank, the row's edge or the next span, so a
            // `cfont` that claims the revision bar (`| Us`) styles nothing.
            if (proven) {
              const auto *row_line =
                  display_line_of_token(*drawn_record, drawn_token);
              proven = row_line != nullptr &&
                       !display_row_font_highlights(
                            display_line_cells(*drawn_record, *row_line),
                            display_line_column_offsets(*drawn_record,
                                                        *row_line),
                            *spans)
                            .empty();
            }
            if (proven) {
              const auto ranges =
                  chain_highlight_ranges(drawn_spans, drawn_text, 0);
              auto flow = line.body.flow;
              proven = !ranges.empty() && add_highlights(flow, ranges);
            }
            if (proven) {
              chain = std::move(drawn);
              line.spans = std::move(drawn_spans);
              line.spans_text = std::move(drawn_text);
            }
          }
        }
        // A row that opens with a revision margin maps its CFONT from the
        // first word the walk kept, which the record does not state is the
        // span's column; the row's own cells must corroborate every span the
        // mapping claims, for the headline and the labels as for a run.
        // SC34-425 APPENDIX1.5.3 record 2540 line 9 (` | User Response:`
        // under `cfont 3 4 2 8 9 2`) is the guard: columns 3 and 8 are the
        // cells `User` and `Response:`, never `| Us`.
        if (!chain.empty()) {
          for (auto cell = line.body.cell_begin; cell < line.body.cell_end;
               ++cell) {
            const auto role = entry.cells[cell].role;
            if (role != TrapCellRoleIR::text &&
                role != TrapCellRoleIR::unrowed_text &&
                role != TrapCellRoleIR::lexical_marker &&
                role != TrapCellRoleIR::punctuation_marker)
              continue;
            const auto *drawn =
                find_record(records, entry.cells[cell].logical_record);
            const auto *row =
                drawn == nullptr
                    ? nullptr
                    : display_line_of_token(*drawn,
                                            entry.cells[cell].token_index);
            if (row != nullptr &&
                margin_revision_bar(*drawn, row->prefix_token + 2) &&
                !chain_matches_display_columns(
                    *drawn, entry.cells[cell].token_index, line.spans))
              return reject("trap entry highlighted run does not cover the "
                            "columns it names: " +
                            entry.id + " [" + line.spans_text + "]");
            break;
          }
        }
        if (!chain.empty() && !have_headline) {
          if (!origin_column)
            origin_column = chain.front().column;
          // A headline may legitimately end in `:` (N2AH1MST record 2315
          // line 24, `IDC498D ACCESS REQUESTED TO text VOL=SER=volser: REPLY
          // Y OR N text is:`). Where the catalog's label column is known and
          // differs from the headline origin the column decides; only where
          // the two coincide is the trailing colon still evidence of a label.
          // A run that opens with the entry's own id is the headline whatever
          // it ends with: N2AH1MST record 757 line 13 is the catalogue's
          // first headline, `CBP100I VALIDATE PROCESSING ASSUMED.  INVALID
          // INPUT PARAMETER STRING:`, before any label has fixed the label
          // column, and hosted 15.0 (DT 19910329000100) serves it as the
          // headline of `CBP100I`.
          if (!line.spans_text.empty() && line.spans_text.back() == ':' &&
              !starts_with_word(line.spans_text, entry.id) &&
              (!origin_column || !label_column ||
               *label_column == *origin_column))
            return reject("trap entry starts with a field label: " + entry.id);
          if (collapse(line.body.text) != collapse(line.spans_text))
            return reject("trap entry headline is not fully highlighted: " +
                          entry.id + " [" + line.body.text + "]");
          entry.headline = std::move(line);
          have_headline = true;
          current = &entry.headline;
        } else if (!chain.empty() && !line.spans_text.empty() &&
                   line.spans_text.back() == ':') {
          if (!starts_with_word(line.body.text, line.spans_text))
            return reject("trap field text does not begin with its label: " +
                          entry.id);
          if (!label_column)
            label_column = chain.front().column;
          TrapFieldIR field;
          field.label_text = line.spans_text;
          field.line = std::move(line);
          entry.fields.push_back(std::move(field));
          current = &entry.fields.back().line;
        } else {
          if (current == nullptr)
            return reject("trap entry text precedes its headline: " + entry.id);
          auto &body = current->body;
          std::vector<TrapBodyHighlightIR> highlights;
          if (!chain.empty()) {
            // The entry already has its headline and the run does not end in
            // `:`, so this is neither: it is a highlighted run the source
            // draws inside the field body, opening a continuation display
            // line at the column the labels use. SC09-138 F.1 record 2048
            // line 1 is the whole line
            // `   IBM C/370 Diagnosis Guide and Reference` under
            // `cfont 3 3 C ... 33 9 C`, and hosted BookServer
            // (SC09-1384-00 F.1, DT 19940715102756) serves it inside the
            // `Explanation:` paragraph, not as a row of its own. The run
            // stays a run: its columns are recorded over the body text and
            // no presentation is claimed here.
            // A plain prefix, not a whole-word one: the CFONT columns state
            // the run's own end, and a sentence stop the columns leave out
            // stays outside it. SC09-138 F.1 record 2051 line 22 is
            // `cfont 3 5 C 9 3 C 13 9 C` over line 23
            // `   Guide and Reference.`, whose final `.` sits at column 22,
            // one past the last highlighted column.
            if (line.body.text.compare(0, line.spans_text.size(),
                                       line.spans_text) != 0)
              return reject("trap entry highlighted run is neither headline "
                            "nor label: " +
                            entry.id + " [" + line.spans_text + "]");
            // The run's own display line decides whether the columns really
            // spell it; a mapping that slid is not evidence of anything.
            const auto *drawn = static_cast<const DecodedLogicalRecordSource *>(
                nullptr);
            std::size_t drawn_token = 0;
            // A row LayoutIR left unrowed (a record continuation: SC34-425
            // APPENDIX1.5.3 record 2548 line 0, `   dddddddd in group
            // bbbbbbbb.`, styled by record 2547's closing `cfont 3 8 1 21 8
            // 1`) is still a drawn display line, and its cells decide.
            for (auto cell = line.body.cell_begin;
                 cell < line.body.cell_end && drawn == nullptr; ++cell)
              if (entry.cells[cell].role == TrapCellRoleIR::text ||
                  entry.cells[cell].role == TrapCellRoleIR::unrowed_text ||
                  entry.cells[cell].role == TrapCellRoleIR::lexical_marker ||
                  entry.cells[cell].role ==
                      TrapCellRoleIR::punctuation_marker) {
                drawn = find_record(records, entry.cells[cell].logical_record);
                drawn_token = entry.cells[cell].token_index;
              }
            if (drawn == nullptr ||
                !chain_matches_display_columns(*drawn, drawn_token, line.spans))
              return reject("trap entry highlighted run does not cover the "
                            "columns it names: " +
                            entry.id + " [" + line.spans_text + "]");
            const auto base = body.text.empty() || line.body.text.empty()
                                  ? body.text.size()
                                  : body.text.size() + 1;
            highlights =
                chain_highlight_ranges(line.spans, line.spans_text, base);
            if (highlights.empty())
              return reject("trap entry highlighted run does not spell its "
                            "own display words: " +
                            entry.id + " [" + line.spans_text + "]");
          }
          if (!highlights.empty()) {
            auto local = highlights;
            const auto base = body.text.empty() || line.body.text.empty()
                                  ? body.text.size()
                                  : body.text.size() + 1;
            for (auto &highlight : local) {
              highlight.begin -= base;
              highlight.end -= base;
            }
            if (!add_highlights(line.body.flow, local))
              return reject("trap entry highlighted run crosses a paragraph: " +
                            entry.id);
          }
          body.highlights.insert(body.highlights.end(), highlights.begin(),
                                 highlights.end());
          for (auto link : line.body.links) {
            const auto base = body.text.empty() || line.body.text.empty()
                                  ? body.text.size()
                                  : body.text.size() + 1;
            link.begin += base;
            link.end += base;
            body.links.push_back(std::move(link));
          }
          append_flow(body.flow, std::move(line.body.flow));
          if (!body.text.empty() && !line.body.text.empty())
            body.text.push_back(' ');
          body.text += line.body.text;
          if (!body.display_text.empty() && !line.body.display_text.empty())
            body.display_text.push_back(' ');
          body.display_text += line.body.display_text;
          body.source_slices.insert(body.source_slices.end(),
                                    line.body.source_slices.begin(),
                                    line.body.source_slices.end());
          body.source_rows.insert(body.source_rows.end(),
                                  line.body.source_rows.begin(),
                                  line.body.source_rows.end());
          body.cell_end = line.body.cell_end;
        }
      } else if (opcode_is_line_prefix ||
                 segment.kind == BookControlKind::text ||
                 segment.kind == BookControlKind::unknown ||
                 segment.kind == BookControlKind::select ||
                 segment.kind == BookControlKind::table_end ||
                 segment.kind == BookControlKind::structural) {
        // A structural `SR<id>` inside an entry names the topic a second
        // time.  Its payload is entry text and keeps its disposition below;
        // only the name is recorded, because the entry already carries the
        // anchor its own `SRMSG` states.
        // Figure and footnote envelope controls are placed above; their
        // closers are end markers and name nothing.
        if (!opcode_is_line_prefix &&
            segment.kind == BookControlKind::structural &&
            ascii_starts_with_case_insensitive(segment.opcode, "SR") &&
            segment.opcode.size() > 2 && !figure_start && !figure_end &&
            !footnote_start && !footnote_end)
          catalog.entry_named_destinations.push_back(segment.opcode.substr(2));
        TrapTextIR piece;
        piece.cell_begin = entry.cells.size();
        if (!walk_payload(*ref.record, segment, index, piece, entry.cells,
                          entry_cell_positions, error, false, 0,
                          opcode_is_line_prefix, nullptr,
                          segment.kind == BookControlKind::select ||
                                  carried_selectors
                              ? &queued_selectors
                              : nullptr,
                          nullptr,
                          current != nullptr && footnote_line == nullptr
                              ? &current->body.text
                              : nullptr))
          return std::nullopt;
        queued_selectors.clear();
        piece.cell_end = entry.cells.size();
        if (!pending_boundaries.empty()) {
          auto *paragraph = first_paragraph(piece.flow);
          if (paragraph != nullptr) {
            paragraph->break_before.insert(paragraph->break_before.begin(),
                                           pending_boundaries.begin(),
                                           pending_boundaries.end());
            paragraph->cell_begin = pending_boundaries.back().cell_end;
            piece.paragraph_break = true;
            pending_boundaries.clear();
          }
        }
        if (current == &entry.leading && footnote_line == nullptr) {
          // The closer of a footnote that stands before the headline. Its
          // payload would be drawn text ahead of the headline, which no
          // entry is observed to carry; only another footnote or the
          // headline itself may follow.
          if (!piece.text.empty())
            return reject("trap entry text precedes its headline: " +
                          entry.id);
          current->body.cell_end = piece.cell_end;
          current = nullptr;
          continue;
        }
        if (current == nullptr) {
          // A placeholder-only record prefix (separator plus `?` run) before
          // the headline carries no text; its cells stay suppressed.
          if (!piece.text.empty())
            return reject("trap entry text precedes its headline: " + entry.id);
          for (auto cell = piece.cell_begin; cell < piece.cell_end; ++cell)
            entry.cells[cell].role = TrapCellRoleIR::suppressed;
          entry.suppressed_rows.insert(entry.suppressed_rows.end(),
                                       piece.source_rows.begin(),
                                       piece.source_rows.end());
          continue;
        }
        auto &body = current->body;
        if (body.cell_end != piece.cell_begin &&
            !(resume_cell != 0 && resume_cell == piece.cell_begin))
          return reject("trap entry continuation is not contiguous: " +
                        entry.id);
        resume_cell = 0;
        if (piece.paragraph_break)
          body.paragraph_break = true;
        const auto link_base = body.text.size() +
            (!body.text.empty() && !piece.text.empty() ? 1 : 0);
        for (auto link : piece.links) {
          link.begin += link_base;
          link.end += link_base;
          body.links.push_back(std::move(link));
        }
        if (footnote_line != nullptr) {
          if (footnote_line != current)
            return reject("trap entry footnote crosses a field: " + entry.id);
          auto &footnote = open_footnote();
          TrapFlowSequenceIR inner{std::move(footnote.blocks)};
          append_flow(inner, std::move(piece.flow));
          footnote.blocks = std::move(inner.blocks);
          footnote.cell_end = piece.cell_end;
        } else {
          append_flow(body.flow, std::move(piece.flow));
        }
        if (!body.text.empty() && !piece.text.empty())
          body.text.push_back(' ');
        body.text += piece.text;
        if (!body.display_text.empty() && !piece.display_text.empty())
          body.display_text.push_back(' ');
        body.display_text += piece.display_text;
        body.source_slices.insert(body.source_slices.end(),
                                  piece.source_slices.begin(),
                                  piece.source_slices.end());
        body.source_rows.insert(body.source_rows.end(),
                                piece.source_rows.begin(),
                                piece.source_rows.end());
        body.cell_end = piece.cell_end;
      } else {
        return reject("trap entry contains an unsupported control: " +
                      entry.id + " " + segment.opcode);
      }
    }
    if (open_region)
      return reject("trap entry table envelope is not closed: " + entry.id);
    if (footnote_line != nullptr)
      return reject("trap entry footnote envelope is not closed: " + entry.id);
    if (!queued_selectors.empty())
      return reject("trap queued selector has no drawn row: " + entry.id);
    if (!have_headline)
      return reject("trap entry has no highlighted headline: " + entry.id);
    if (!starts_with_word(entry.headline.body.text, entry.id))
      return reject("trap entry headline does not begin with its ID: " +
                    entry.id + " [" + entry.headline.body.text + "]");
    if (entry.fields.empty()) {
      // Two messages can share one set of fields: the first entry is its
      // headline alone, and the next SRMSG follows on the very next display
      // line, drawing no rule row of its own. SC09-138 G.0 record 2071
      // line 13 is `   EDC0762 10 A text deck marked fetchable was not
      // specified` and line 14 `SRMSG EDC0763 40`, whose headline and
      // `Explanation:` follow; hosted G.0 (DT 19910321130500) serves
      // `<a name="MSG EDC0763 40">` straight after EDC0762's headline, with
      // no `<hr>` and no `<p>`. H.0 record 2103 lines 27-28 (EDC0132,
      // EDC0133) are the same shape. Anything else drawn in the entry, or
      // any line between it and the next SRMSG, declines.
      const auto *next =
          entry_index + 1 < starts.size() ? &ordered[end] : nullptr;
      const auto &slices = entry.headline.body.source_slices;
      const auto *next_line =
          next == nullptr || next->segment->source_tokens.empty()
              ? nullptr
              : display_line_of_token(*next->record,
                                      next->segment->source_tokens.front());
      // The next SRMSG's payload is the operand tail on its own line (G.0's
      // severity, `SRMSG EDC0763 40`) and at most the next line's length
      // byte (H.0 record 2110 token 221, `2D`, spelling `@`, after `SRMSG
      // EDC0150`); a payload that draws on a further line is that entry's
      // rule row.
      const auto next_draws_no_rule = [&] {
        const auto words = decoded_byte_range_to_word_range(
            next->record->assembled, next->segment->payload_range);
        const auto tokens = source_tokens_intersecting_output(
            next->record->assembled, words.begin, words.end);
        return std::all_of(tokens.begin(), tokens.end(), [&](const auto token) {
          return token < next_line->token_end ||
                 is_display_line_length_token(*next->record, token);
        });
      };
      const auto shares_next_fields =
          next != nullptr && current == &entry.headline &&
          footnote_line == nullptr && pending_boundaries.empty() &&
          entry.leading.body.flow.blocks.empty() &&
          entry.embedded_regions.empty() && entry.index_terms.empty() &&
          entry.headline.body.links.empty() &&
          entry.headline.body.flow.blocks.size() == 1 &&
          std::holds_alternative<TrapFlowParagraphIR>(
              entry.headline.body.flow.blocks.front().value) &&
          !slices.empty() && next_line != nullptr &&
          next->record->logical_record == slices.back().logical_record &&
          next_line->prefix_token == slices.back().token_end &&
          next_line->prefix_token + 1 ==
              next->segment->source_tokens.front() &&
          next_draws_no_rule();
      if (!shares_next_fields)
        return reject("trap entry has no labelled field: " + entry.id);
    }
    catalog.entries.push_back(std::move(entry));
  }
  // A headline-only entry shares the fields of the entry after it, which
  // must carry them.
  for (std::size_t at = 0; at < catalog.entries.size(); ++at)
    if (catalog.entries[at].fields.empty() &&
        (at + 1 == catalog.entries.size() ||
         catalog.entries[at + 1].fields.empty()))
      return reject("trap entry has no labelled field: " +
                    catalog.entries[at].id);
  catalog.origin_column = origin_column.value_or(0);
  // The vocabulary is the ordered label sequence every entry repeats. A
  // labelled line that some entry lacks (an entry-local `Note:`) stays a
  // highlighted continuation of the entry rather than a field.
  // A headline-only entry carries no labels of its own; the entry whose
  // fields it shares is held to the vocabulary instead.
  const auto first_labelled = std::find_if(
      catalog.entries.begin(), catalog.entries.end(),
      [](const auto &entry) { return !entry.fields.empty(); });
  for (const auto &field : first_labelled->fields) {
    const auto everywhere = std::all_of(
        catalog.entries.begin(), catalog.entries.end(), [&](const auto &entry) {
          return entry.fields.empty() ||
                 std::any_of(entry.fields.begin(), entry.fields.end(),
                             [&](const auto &candidate) {
                               return candidate.label_text == field.label_text;
                             });
        });
    if (everywhere)
      catalog.label_vocabulary.push_back(field.label_text);
  }
  if (catalog.label_vocabulary.empty())
    return reject("trap catalog has no label repeated by every entry");
  for (auto &entry : catalog.entries) {
    if (entry.fields.empty())
      continue;
    std::vector<std::string> labels;
    for (std::size_t at = 0; at < entry.fields.size(); ++at) {
      auto &field = entry.fields[at];
      field.in_vocabulary =
          std::find(catalog.label_vocabulary.begin(),
                    catalog.label_vocabulary.end(),
                    field.label_text) != catalog.label_vocabulary.end();
      // A field may restate the label of the field directly before it:
      // N2AH1MST AVM035I draws `System Action:` twice in a row (record 669
      // lines 29 and 41), and hosted 11.0 (DT 19910329000100) serves both
      // as labelled fields. The vocabulary still counts the label once.
      if (field.in_vocabulary &&
          (at == 0 || entry.fields[at - 1].label_text != field.label_text))
        labels.push_back(field.label_text);
    }
    if (labels != catalog.label_vocabulary)
      return reject("trap entry does not repeat the catalog label vocabulary: " +
                    entry.id);
  }

  // Introduction prose between the title row and the first entry.
  MessageProseEnvelopeIR envelope;
  envelope.begin_record = title->record->logical_record;
  envelope.begin_token = title_end_token;
  envelope.catalog_record = ordered[starts.front()].record->logical_record;
  envelope.catalog_segment = ordered[starts.front()].segment->segment_index;
  catalog.introduction_envelope = envelope;

  // One prose envelope per piece of the introduction. An embedded envelope
  // ends the piece before it at its own opening control and starts the piece
  // after it at the token following its closing control, so the drawing is
  // never offered to the prose model.
  {
    auto piece_record = envelope.begin_record;
    auto piece_token = envelope.begin_token;
    for (const auto &region : header_regions) {
      MessageProseEnvelopeIR piece;
      piece.begin_record = piece_record;
      piece.begin_token = piece_token;
      piece.catalog_record = region.open_record;
      piece.catalog_segment = region.open_segment;
      catalog.introduction_prose_envelopes.push_back(piece);
      piece_record = region.resume_record;
      piece_token = region.resume_token;
    }
    auto tail = envelope;
    tail.begin_record = piece_record;
    tail.begin_token = piece_token;
    catalog.introduction_prose_envelopes.push_back(tail);
  }

  for (std::size_t piece = 0;
       piece < catalog.introduction_prose_envelopes.size(); ++piece) {
  // Each introduction item of this piece: the prose paragraph it came from
  // and its byte offset in that paragraph's text.
  std::vector<std::pair<std::size_t, std::size_t>> item_sources;
  const auto first_item = catalog.introduction.size();
  const auto &envelope = catalog.introduction_prose_envelopes[piece];
  const auto *piece_catalog_record = find_record(records, envelope.catalog_record);
  if (piece_catalog_record == nullptr ||
      envelope.catalog_segment >=
          piece_catalog_record->control_segments.size() ||
      piece_catalog_record->control_segments[envelope.catalog_segment]
          .source_tokens.empty())
    return reject("trap catalog introduction envelope coordinates are invalid");
  const auto piece_end_token =
      piece_catalog_record->control_segments[envelope.catalog_segment]
          .source_tokens.front();
  bool visible_introduction = false;
  for (const auto &record : records) {
    if (record.logical_record < envelope.begin_record ||
        record.logical_record > envelope.catalog_record)
      continue;
    const auto begin =
        record.logical_record == envelope.begin_record ? envelope.begin_token : 0;
    const auto end =
        record.logical_record == envelope.catalog_record
            ? piece_end_token
            : record.ir.tokens.size();
    for (auto token = begin; token < end && token < record.ir.tokens.size();
         ++token)
      for (std::size_t word = 0;
           word < record.ir.tokens[token].decoded_words.size(); ++word) {
        const auto cell =
            index.cells.find({record.logical_record, token, word});
        if (cell != index.cells.end() &&
            cell->second->disposition == SourceDisposition::visible_content)
          visible_introduction = true;
      }
  }
  std::string prose_error;
  const auto prose = extract_message_prose_paragraphs_ir(
      records, layout, ownership, envelope, &prose_error);
  if (!prose && visible_introduction)
    return reject("trap catalog introduction rejected: " + prose_error);
  if (prose) {
    for (const auto &paragraph : prose->paragraphs) {
      TrapIntroductionParagraphIR item;
      item.text = paragraph.text;
      item.line_breaks = paragraph.line_breaks;
      item.source_slices = paragraph.source_slices;
      item.source_rows = paragraph.source_rows;
      // Deferred font provenance is explicit, not borrowed from a later row.
      if (paragraph.leading_font_source) {
        const auto& source = *paragraph.leading_font_source;
        const auto* record = find_record(records, source.logical_record);
        if (record == nullptr || source.segment_index >= record->control_segments.size() ||
            paragraph.source_slices.empty())
          return reject("deferred introduction font has no source");
        const auto font = decode_font_control_spans(
            *record, record->control_segments[source.segment_index]);
        const auto& row_source = paragraph.source_slices.front();
        const auto* row_record = find_record(records, row_source.logical_record);
        const auto* line = row_record == nullptr ? nullptr :
            display_line_of_token(*row_record, row_source.token_begin);
        if (!font || font->spans.empty() || line == nullptr)
          return reject("deferred introduction font has no native target row");
        const auto offsets = display_line_column_offsets(*row_record, *line);
        // Bound before mapping, including before any column+length sum.
        // Only styles this lowering preserves may promote an opaque row.
        for (const auto& span : font->spans)
          if ((span.style != FontStyleIR::highlight_1 &&
               span.style != FontStyleIR::highlight_2 &&
               span.style != FontStyleIR::highlight_3 &&
               span.style != FontStyleIR::bold_phrase &&
               span.style != FontStyleIR::italic_phrase) || offsets.empty() ||
              span.column >= offsets.size() - 1 ||
              span.length > offsets.size() - 1 - span.column)
            return reject("deferred introduction font has unsupported geometry or style");
        std::string joined;
        std::vector<TrapStyledSpanIR> mapped;
        // Entry headlines and introduction headings can have different
        // origins. The target row, not the catalogue's entry origin, proves
        // this font's coordinates.
        const auto chain = map_leading_chain(*font, font->spans.front().column,
                                              item.text, mapped, joined);
        if (chain.size() != font->spans.size() ||
            !chain_matches_display_columns(*row_record, row_source.token_begin, mapped))
          return reject("deferred introduction font does not match its source columns");
        item.leading_spans = std::move(mapped);
        item.source_slices.push_back(source);
      } else if (!paragraph.source_rows.empty()) {
        const auto &first = paragraph.source_rows.front();
        const auto run = std::find_if(
            layout.runs.begin(), layout.runs.end(), [&](const auto &candidate) {
              return candidate.id == first.display_run;
            });
        if (run != layout.runs.end() && first.row_index < run->rows.size() &&
            run->control_kind == BookControlKind::font) {
          const auto &row = run->rows[first.row_index];
          const auto *record = find_record(records, row.logical_record);
          if (record != nullptr &&
              row.segment_index < record->control_segments.size()) {
            const auto spans = decode_font_control_spans(
                *record, record->control_segments[row.segment_index]);
            if (spans) {
              std::string joined;
              std::vector<TrapStyledSpanIR> mapped;
              if (!map_leading_chain(*spans, catalog.origin_column, item.text,
                                     mapped, joined)
                       .empty())
                item.leading_spans = std::move(mapped);
            }
          }
        }
      }
      std::vector<TrapIntroductionParagraphIR> pieces;
      std::vector<std::size_t> bases;
      std::string list_error;
      if (!split_introduction_list_items(records, layout, paragraph,
                                         std::move(item), pieces, bases,
                                         &list_error))
        return reject(list_error);
      for (std::size_t at = 0; at < pieces.size(); ++at) {
        catalog.introduction.push_back(std::move(pieces[at]));
        item_sources.push_back(
            {static_cast<std::size_t>(&paragraph - prose->paragraphs.data()),
             bases[at]});
      }
    }
  }
  // `CFONT` runs inside the piece's prose rows, each moved to the item whose
  // text holds it. The first row's leading run is the item's
  // `leading_spans`; a run inside it, or one that crosses two list items,
  // is not placed again.
  if (prose) {
    for (std::size_t source = 0; source < prose->paragraphs.size(); ++source)
      for (const auto &range : introduction_row_highlights(
               records, layout, prose->paragraphs[source]))
        for (std::size_t at = 0; at < item_sources.size(); ++at) {
          const auto &[owner, base] = item_sources[at];
          auto &item = catalog.introduction[first_item + at];
          if (owner != source || range.begin < base ||
              range.end > base + item.text.size())
            continue;
          std::size_t lead = 0;
          for (const auto &span : item.leading_spans)
            lead += span.text.size() + (lead == 0 ? 0 : 1);
          if (range.begin - base >= lead)
            item.highlights.push_back(
                {range.begin - base, range.end - base, range.style});
          break;
        }
  }
  // `cselect` hotspots of this piece. Each marks cells of the display line
  // after it (queued selectors share one), exactly as in an entry; the
  // range is placed through the source cells the prose projected, never by
  // searching the text. SC34-425 2.4.32 record 1641 lines 30-32 are
  // `cselect 3 8 TBLADINFMT`, `cselect 50 11 FIGADAIN` and
  // `   Table 18 for a description of the ADAIN format.  Figure 88 shows a
  // sample`; hosted DT 19921112160049 links `Table 18` to `#TBLADINFMT`.
  // A hotspot that marks no projected prose declines.
  {
    for (const auto &record : records) {
      if (record.logical_record < envelope.begin_record ||
          record.logical_record > envelope.catalog_record)
        continue;
      const auto begin = record.logical_record == envelope.begin_record
                             ? envelope.begin_token
                             : std::size_t{0};
      const auto end = record.logical_record == envelope.catalog_record
                           ? piece_end_token
                           : record.ir.tokens.size();
      for (const auto &segment : record.control_segments) {
        if (segment.kind != BookControlKind::select ||
            segment.source_tokens.empty() ||
            segment.source_tokens.front() < begin ||
            segment.source_tokens.front() >= end)
          continue;
        std::string selector_error;
        const auto selector = read_trap_selector_source(
            record, segment, &selector_error,
            find_record(records, record.logical_record + 1));
        if (!selector)
          return reject("trap introduction selector rejected: " +
                        selector_error);
        const auto first_cell = std::find_if(
            selector->cells.begin(), selector->cells.end(),
            [](const auto &cell) {
              return cell.word != ' ' &&
                     cell.token != static_cast<std::size_t>(-1);
            });
        std::optional<std::size_t> owner;
        for (std::size_t at = 0;
             prose && first_cell != selector->cells.end() &&
             at < prose->paragraphs.size() && !owner;
             ++at) {
          const auto &glyphs = prose->paragraphs[at].glyphs;
          if (std::any_of(glyphs.begin(), glyphs.end(), [&](const auto &g) {
                return g.logical_record == selector->target_record &&
                       g.token_index == first_cell->token &&
                       g.word_index == first_cell->word_index;
              }))
            owner = at;
        }
        if (!owner)
          return reject("trap introduction selector marks no projected "
                        "prose: " +
                        selector->target);
        const auto &source_paragraph = prose->paragraphs[*owner];
        if (!source_paragraph.glyphs_complete)
          return reject("trap introduction selector paragraph has no exact "
                        "glyph provenance: " +
                        selector->target);
        TrapGlyphOffsets offsets;
        for (const auto &glyph : source_paragraph.glyphs)
          if (glyph.logical_record == selector->target_record)
            offsets[{glyph.token_index, glyph.word_index}] = glyph.offset;
        auto link = project_trap_selector_link(
            *selector, source_paragraph.text, offsets,
            segment_slice(record, segment), &selector_error);
        if (!link)
          return reject("trap introduction selector rejected: " +
                        selector_error);
        // The item of that paragraph the range falls in; a range that
        // crosses two list items is not placed.
        std::optional<std::size_t> item;
        for (std::size_t at = 0; at < item_sources.size(); ++at) {
          const auto &[source, base] = item_sources[at];
          const auto &text = catalog.introduction[first_item + at].text;
          if (source == *owner && link->begin >= base &&
              link->end <= base + text.size())
            item = first_item + at;
        }
        if (!item)
          return reject("trap introduction selector crosses list items: " +
                        selector->target);
        const auto base = item_sources[*item - first_item].second;
        link->begin -= base;
        link->end -= base;
        catalog.introduction[*item].links.push_back(*link);
      }
    }
    for (auto at = first_item; at < catalog.introduction.size(); ++at) {
      auto &links = catalog.introduction[at].links;
      std::sort(links.begin(), links.end(),
                [](const auto &left, const auto &right) {
                  return left.begin < right.begin;
                });
      for (std::size_t link = 1; link < links.size(); ++link)
        if (links[link].begin < links[link - 1].end)
          return reject("trap introduction selectors overlap");
    }
  }
  // The envelope this piece ends at is drawn where the prose stops.
  if (piece < header_regions.size()) {
    auto region = header_regions[piece].region;
    region.after_field = catalog.introduction.size();
    catalog.introduction_regions.push_back(std::move(region));
  }
  }
  // Native lists of the introduction. Bullets of one list stand in one
  // column; a paragraph deeper than that column continues the open item,
  // and one at or left of it ends the list. An introduction region ends a
  // list too: the drawing is not a block of an item. A bullet at another
  // column while a list is open would be a nested list, which this model
  // does not state, so it declines.
  {
    std::optional<std::size_t> list_column;
    std::size_t next_region = 0;
    for (std::size_t at = 0; at < catalog.introduction.size(); ++at) {
      while (next_region < catalog.introduction_regions.size() &&
             catalog.introduction_regions[next_region].after_field <= at) {
        list_column.reset();
        ++next_region;
      }
      auto &paragraph = catalog.introduction[at];
      if (paragraph.list_marker) {
        if (list_column && *list_column != paragraph.marker_column)
          return reject("trap introduction list bullets stand in different "
                        "columns");
        list_column = paragraph.marker_column;
      } else if (list_column) {
        if (!paragraph.first_column)
          return reject("trap introduction paragraph after a list item has "
                        "no drawn column");
        if (*paragraph.first_column > *list_column)
          paragraph.list_continuation = true;
        else
          list_column.reset();
      }
    }
  }
  if (error != nullptr)
    error->clear();
  return catalog;
}

bool same_trap_catalog_ir(const TrapCatalogIR &left,
                          const TrapCatalogIR &right) {
  if (left.first_logical_record != right.first_logical_record ||
      left.end_logical_record != right.end_logical_record ||
      left.raw_topic_id != right.raw_topic_id ||
      left.heading_level != right.heading_level || left.title != right.title ||
      !(left.title_source == right.title_source) ||
      left.title_row.display_run != right.title_row.display_run ||
      left.title_row.row_index != right.title_row.row_index ||
      left.anchors.size() != right.anchors.size() ||
      left.entry_named_destinations != right.entry_named_destinations ||
      left.introduction_envelope != right.introduction_envelope ||
      left.introduction_prose_envelopes != right.introduction_prose_envelopes ||
      left.introduction_regions.size() != right.introduction_regions.size() ||
      left.introduction_cells != right.introduction_cells ||
      left.introduction.size() != right.introduction.size() ||
      left.origin_column != right.origin_column ||
      left.label_vocabulary != right.label_vocabulary ||
      left.entries.size() != right.entries.size())
    return false;
  for (std::size_t index = 0; index < left.anchors.size(); ++index)
    if (left.anchors[index].id != right.anchors[index].id ||
        !(left.anchors[index].source == right.anchors[index].source))
      return false;
  for (std::size_t index = 0; index < left.introduction_regions.size();
       ++index) {
    if (!same_region(left.introduction_regions[index],
                     right.introduction_regions[index]))
      return false;
  }
  for (std::size_t index = 0; index < left.introduction.size(); ++index) {
    const auto &a = left.introduction[index];
    const auto &b = right.introduction[index];
    if (a.text != b.text || !same_spans(a.leading_spans, b.leading_spans) ||
        a.highlights != b.highlights || a.line_breaks != b.line_breaks ||
        a.source_slices != b.source_slices ||
        a.source_rows.size() != b.source_rows.size() || a.links != b.links ||
        !(a.list_marker == b.list_marker) ||
        a.marker_column != b.marker_column ||
        a.first_column != b.first_column ||
        a.list_continuation != b.list_continuation)
      return false;
    for (std::size_t row = 0; row < a.source_rows.size(); ++row)
      if (a.source_rows[row].display_run != b.source_rows[row].display_run ||
          a.source_rows[row].row_index != b.source_rows[row].row_index)
        return false;
  }
  for (std::size_t index = 0; index < left.entries.size(); ++index) {
    const auto &a = left.entries[index];
    const auto &b = right.entries[index];
    if (a.id != b.id || a.operand != b.operand ||
        !(a.start_source == b.start_source) ||
        !same_line(a.leading, b.leading) ||
        !same_line(a.headline, b.headline) || a.fields.size() != b.fields.size() ||
        a.cells != b.cells || a.suppressed_rows.size() != b.suppressed_rows.size() ||
        a.embedded_regions.size() != b.embedded_regions.size() ||
        a.index_terms != b.index_terms ||
        a.closing_heading.has_value() != b.closing_heading.has_value())
      return false;
    if (a.closing_heading) {
      const auto &x = *a.closing_heading;
      const auto &y = *b.closing_heading;
      if (x.anchor.id != y.anchor.id || !(x.anchor.source == y.anchor.source) ||
          !same_region(x.heading, y.heading) || !same_region(x.rows, y.rows) ||
          x.index_terms != y.index_terms || x.cell_begin != y.cell_begin ||
          x.cell_end != y.cell_end)
        return false;
    }
    for (std::size_t region = 0; region < a.embedded_regions.size(); ++region) {
      if (!same_region(a.embedded_regions[region], b.embedded_regions[region]))
        return false;
    }
    for (std::size_t field = 0; field < a.fields.size(); ++field)
      if (a.fields[field].label_text != b.fields[field].label_text ||
          a.fields[field].in_vocabulary != b.fields[field].in_vocabulary ||
          !same_line(a.fields[field].line, b.fields[field].line))
        return false;
    for (std::size_t row = 0; row < a.suppressed_rows.size(); ++row)
      if (a.suppressed_rows[row].display_run !=
              b.suppressed_rows[row].display_run ||
          a.suppressed_rows[row].row_index != b.suppressed_rows[row].row_index)
        return false;
  }
  return true;
}

bool verify_trap_catalog_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const TrapCatalogIR &catalog, std::string *error) {
  // Structural invariants that do not depend on the canonical extraction.
  for (const auto &entry : catalog.entries) {
    std::set<CellKey> seen;
    for (const auto &cell : entry.cells) {
      if (!seen.insert({cell.logical_record, cell.token_index, cell.word_index})
               .second)
        return fail(error, "trap entry ledgers one source cell twice: " +
                               entry.id + " LR" +
                               std::to_string(cell.logical_record) +
                               " token " + std::to_string(cell.token_index) +
                               " word " + std::to_string(cell.word_index));
      const auto *record = find_record(records, cell.logical_record);
      if (record == nullptr || cell.token_index >= record->ir.tokens.size() ||
          cell.word_index >=
              record->ir.tokens[cell.token_index].decoded_words.size() ||
          record->ir.tokens[cell.token_index].decoded_words[cell.word_index] !=
              cell.word)
        return fail(error, "trap entry ledger cell does not match its source: " +
                               entry.id);
    }
    const auto check_line = [&](const TrapLineIR &line, const char *what) {
      if (line.body.cell_begin > line.body.cell_end ||
          line.body.cell_end > entry.cells.size())
        return fail(error, std::string("trap ") + what +
                               " cell range is invalid: " + entry.id);
      std::string flat;
      if (!validate_flow(line.body.flow, entry.cells, entry.id, error, &flat))
        return false;
      if (flat != line.body.text)
        return fail(error, std::string("trap ") + what +
                               " flow differs from flat text: " + entry.id);
      return true;
    };
    if (!entry.leading.body.flow.blocks.empty()) {
      if (!check_line(entry.leading, "leading footnote"))
        return false;
      if (!std::all_of(entry.leading.body.flow.blocks.begin(),
                       entry.leading.body.flow.blocks.end(),
                       [](const auto &block) {
                         return std::holds_alternative<TrapFlowFootnoteIR>(
                             block.value);
                       }))
        return fail(error, "trap entry draws more than footnotes before its "
                           "headline: " + entry.id);
    }
    if (!check_line(entry.headline, "headline"))
      return false;
    if (!starts_with_word(entry.headline.body.text, entry.id))
      return fail(error, "trap headline does not begin with its ID: " + entry.id);
    for (const auto &region : entry.embedded_regions) {
      if (region.cell_begin > region.cell_end ||
          region.cell_end > entry.cells.size())
        return fail(error, "trap embedded region cell range is invalid: " +
                               entry.id);
      if (region.lines.empty() ||
          region.lines.size() != region.line_sources.size())
        return fail(error, "trap embedded region has no drawn line: " +
                               entry.id);
      if (!region_highlights_fit(region))
        return fail(error, "trap embedded region styles text it does not "
                           "draw: " + entry.id);
      if (region.after_field > entry.fields.size())
        return fail(error, "trap embedded region names a missing field: " +
                               entry.id);
    }
    if (entry.closing_heading) {
      const auto &heading = *entry.closing_heading;
      if (heading.cell_begin > heading.cell_end ||
          heading.cell_end > entry.cells.size() || heading.anchor.id != "MSG")
        return fail(error, "trap group heading cell range is invalid: " +
                               entry.id);
      if (heading.heading.lines.size() != 1 ||
          heading.heading.line_sources.size() != 1 ||
          heading.rows.lines.size() != heading.rows.line_sources.size() ||
          !region_highlights_fit(heading.heading) ||
          !region_highlights_fit(heading.rows))
        return fail(error, "trap group heading styles text it does not "
                           "draw: " + entry.id);
    }
    for (const auto &field : entry.fields) {
      if (!check_line(field.line, "field"))
        return false;
      if (field.label_text.empty() || field.label_text.back() != ':' ||
          field.label_text != field.line.spans_text ||
          !starts_with_word(field.line.body.text, field.label_text))
        return fail(error, "trap field label is not source-proven: " + entry.id);
    }
  }
  {
    std::set<CellKey> seen;
    for (const auto &cell : catalog.introduction_cells) {
      if (!seen.insert({cell.logical_record, cell.token_index, cell.word_index})
               .second)
        return fail(error,
                    "trap catalog introduction ledgers one source cell twice");
      const auto *record = find_record(records, cell.logical_record);
      if (record == nullptr || cell.token_index >= record->ir.tokens.size() ||
          cell.word_index >=
              record->ir.tokens[cell.token_index].decoded_words.size() ||
          record->ir.tokens[cell.token_index].decoded_words[cell.word_index] !=
              cell.word)
        return fail(error, "trap catalog introduction ledger cell does not "
                           "match its source");
    }
  }
  for (const auto &region : catalog.introduction_regions) {
    if (region.cell_begin > region.cell_end ||
        region.cell_end > catalog.introduction_cells.size())
      return fail(error,
                  "trap introduction region cell range is invalid: " +
                      region.identifier);
    if (region.lines.empty() ||
        region.lines.size() != region.line_sources.size())
      return fail(error, "trap introduction region has no drawn line: " +
                             region.identifier);
    if (!region_highlights_fit(region))
      return fail(error, "trap introduction region styles text it does not "
                         "draw: " + region.identifier);
    if (region.after_field > catalog.introduction.size())
      return fail(error, "trap introduction region names a missing paragraph: " +
                             region.identifier);
  }
  if (catalog.introduction_regions.size() + 1 !=
      catalog.introduction_prose_envelopes.size())
    return fail(error, "trap catalog introduction pieces do not bracket its "
                       "drawn envelopes");
  for (const auto &paragraph : catalog.introduction) {
    std::size_t previous = 0;
    for (const auto offset : paragraph.line_breaks)
      if (offset <= previous || offset + 1 >= paragraph.text.size() ||
          paragraph.text[offset] != ' ')
        return fail(error, "trap introduction line break does not name a "
                           "separator of its paragraph");
      else
        previous = offset;
  }
  const auto canonical = extract_trap_catalog_ir(records, layout, ownership,
                                                 catalog.title, error);
  if (!canonical)
    return false;
  if (!same_trap_catalog_ir(*canonical, catalog))
    return fail(error, "trap catalog differs from canonical extraction");
  if (error != nullptr)
    error->clear();
  return true;
}

std::string format_trap_catalog_ir(const TrapCatalogIR &catalog) {
  std::ostringstream out;
  out << "trap_catalog id=" << catalog.raw_topic_id
      << " heading=" << catalog.heading_level << " title='" << catalog.title
      << "' origin=" << catalog.origin_column << " labels=[";
  for (std::size_t index = 0; index < catalog.label_vocabulary.size(); ++index)
    out << (index == 0 ? "" : "|") << catalog.label_vocabulary[index];
  out << "] anchors=" << catalog.anchors.size()
      << " named=" << catalog.entry_named_destinations.size()
      << " introduction=" << catalog.introduction.size()
      << " entries=" << catalog.entries.size() << '\n';
  for (const auto &paragraph : catalog.introduction) {
    out << "introduction spans=" << paragraph.leading_spans.size()
        << " rows=" << paragraph.source_rows.size();
    if (!paragraph.highlights.empty())
      out << " highlights=" << paragraph.highlights.size();
    if (!paragraph.line_breaks.empty()) {
      out << " breaks=";
      for (std::size_t index = 0; index < paragraph.line_breaks.size(); ++index)
        out << (index == 0 ? "" : ",") << paragraph.line_breaks[index];
    }
    out << " text='" << paragraph.text << "'\n";
  }
  for (const auto &entry : catalog.entries) {
    out << "entry id='" << entry.id << "' operand='" << entry.operand
        << "' source=" << entry.start_source.logical_record << ':'
        << entry.start_source.segment_index << " cells=" << entry.cells.size()
        << " suppressed_rows=" << entry.suppressed_rows.size()
        << " headline='" << entry.headline.body.text << "'";
    if (!entry.headline.body.highlights.empty())
      out << " headline_highlights=" << entry.headline.body.highlights.size();
    for (const auto &field : entry.fields) {
      out << " field='" << field.label_text << "' text='"
          << field.line.body.text << "'";
      for (const auto &highlight : field.line.body.highlights)
        out << " highlight=" << highlight.begin << ':' << highlight.end << ':'
            << font_style_name(highlight.style);
    }
    for (const auto &region : entry.embedded_regions)
      out << " table='" << region.identifier
          << "' after_field=" << region.after_field
          << " lines=" << region.lines.size();
    if (entry.closing_heading)
      out << " group_heading='" << collapse(entry.closing_heading->heading.lines.front())
          << "' heading_rows=" << entry.closing_heading->rows.lines.size()
          << " heading_terms=" << entry.closing_heading->index_terms.size();
    out << '\n';
  }
  return out.str();
}

} // namespace geist::detail
