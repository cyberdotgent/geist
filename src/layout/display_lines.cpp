// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/layout/display_lines.hpp"
#include <sstream>

#include "geist/detail/ir/figure_block_ir.hpp"

#include <algorithm>
#include <iterator>

namespace geist::detail {

namespace {
std::optional<std::size_t> opcode_token(
    const DecodedLogicalRecordSource& record, const ControlSegmentIR& segment) {
  if (record_display_lines(record) == nullptr) return std::nullopt;
  if (segment.opcode_range.begin >= segment.opcode_range.end)
    return std::nullopt;
  const auto words = decoded_byte_range_to_word_range(record.assembled,
                                                       segment.opcode_range);
  if (words.begin >= words.end || words.begin >= record.assembled.sources.size())
    return std::nullopt;
  const auto& source = record.assembled.sources[words.begin];
  if (source.kind != LogicalWordSourceKind::token_word)
    return std::nullopt;
  return source.token_index;
}
} // namespace

bool is_display_line_length_opcode(const DecodedLogicalRecordSource& record,
                                   const ControlSegmentIR& segment) {
  const auto token = opcode_token(record, segment);
  return token && is_display_line_length_token(record, *token);
}

bool is_display_line_embedded_opcode(const DecodedLogicalRecordSource& record,
                                     const ControlSegmentIR& segment) {
  const auto token = opcode_token(record, segment);
  if (!token) return false;
  const auto* line = display_line_of_token(record, *token);
  return line != nullptr && *token > line->prefix_token + 1;
}

bool is_subject_index_display_line(const DecodedLogicalRecordSource& record,
                                  const DisplayLineIR& line) {
  const auto keyword = line.prefix_token + 1;
  if (keyword >= line.token_end || keyword >= record.tokens.size() ||
      ascii_lower(token_words_to_ascii(record.tokens[keyword])) != "si")
    return false;
  for (const auto& segment : record.control_segments) {
    if (segment.display_text) continue;
    const auto end =
        std::min(segment.complete.end, segment.complete.begin + 2);
    const auto range = decoded_byte_range_to_word_range(
        record.assembled, {segment.complete.begin, end});
    const auto tokens = source_tokens_intersecting_output(
        record.assembled, range.begin, range.end);
    if (!tokens.empty() && tokens.front() == keyword) return true;
  }
  return false;
}

std::optional<std::vector<DisplayLineIR>> token_display_lines(
    const std::vector<LogicalTokenIR>& tokens,
    const std::uint32_t payload_end) {
  std::vector<DisplayLineIR> lines;
  std::size_t at = 0;
  while (at < tokens.size()) {
    const auto& prefix = tokens[at];
    if (prefix.byte_range.end != prefix.byte_range.begin + 1)
      return std::nullopt;
    const auto line_end = prefix.byte_range.end + prefix.encoded.value;
    auto end = at + 1;
    while (end < tokens.size() && tokens[end].byte_range.end <= line_end) ++end;
    const auto boundary =
        end < tokens.size() ? tokens[end].byte_range.begin : payload_end;
    if (boundary != line_end) return std::nullopt;
    DisplayLineIR line;
    line.prefix_token = at;
    line.token_end = end;
    lines.push_back(line);
    at = end;
  }
  return lines;
}

void assign_display_line_framing(LogicalRecordIR& record) {
  record.display_lines.clear();
  record.display_lines_parse = false;
  for (auto& token : record.tokens)
    token.framing = TokenFramingRole::unframed;
  auto lines = token_display_lines(record.tokens, record.payload_range.end);
  if (!lines) return;
  record.display_lines = std::move(*lines);
  record.display_lines_parse = true;
  for (const auto& line : record.display_lines) {
    record.tokens[line.prefix_token].framing = TokenFramingRole::line_length;
    for (auto token = line.prefix_token + 1; token < line.token_end; ++token)
      record.tokens[token].framing = TokenFramingRole::line_content;
  }
}

const std::vector<DisplayLineIR>* record_display_lines(
    const LogicalRecordIR& record) {
  return record.display_lines_parse ? &record.display_lines : nullptr;
}

const std::vector<DisplayLineIR>* record_display_lines(
    const DecodedLogicalRecordSource& record) {
  return record_display_lines(record.ir);
}

bool is_display_line_length_token(const LogicalRecordIR& record,
                                  const std::size_t token) {
  return token < record.tokens.size() &&
         record.tokens[token].framing == TokenFramingRole::line_length;
}

bool is_display_line_length_token(const DecodedLogicalRecordSource& record,
                                  const std::size_t token) {
  return is_display_line_length_token(record.ir, token);
}

const DisplayLineIR* display_line_of_token(const LogicalRecordIR& record,
                                           const std::size_t token) {
  if (!record.display_lines_parse) return nullptr;
  for (const auto& line : record.display_lines)
    if (token >= line.prefix_token && token < line.token_end) return &line;
  return nullptr;
}

const DisplayLineIR* display_line_of_token(
    const DecodedLogicalRecordSource& record, const std::size_t token) {
  return display_line_of_token(record.ir, token);
}

const TokenWords* display_text_words(const DecodedLogicalRecordSource& record,
                                     const std::size_t token) {
  if (token >= record.ir.tokens.size()) return nullptr;
  if (record.ir.tokens[token].framing == TokenFramingRole::line_length)
    return nullptr;
  return &record.ir.tokens[token].decoded_words;
}

bool verify_display_line_framing(const LogicalRecordIR& record,
                                 std::string* error) {
  const auto fail = [&](const char* message) {
    if (error != nullptr) *error = message;
    return false;
  };
  if (!record.display_lines_parse) {
    if (!record.display_lines.empty())
      return fail("unframed logical record carries display lines");
    for (const auto& token : record.tokens)
      if (token.framing != TokenFramingRole::unframed)
        return fail("unframed logical record stamps a token framing role");
    return true;
  }
  std::size_t at = 0;
  for (const auto& line : record.display_lines) {
    if (line.prefix_token != at || line.token_end <= line.prefix_token ||
        line.token_end > record.tokens.size())
      return fail("display lines do not tile the record token list");
    const auto& prefix = record.tokens[line.prefix_token];
    if (prefix.encoded.width != 1)
      return fail("display line length byte is not one byte wide");
    const auto line_end = prefix.byte_range.end + prefix.encoded.value;
    const auto boundary = line.token_end < record.tokens.size()
                              ? record.tokens[line.token_end].byte_range.begin
                              : record.payload_range.end;
    if (boundary != line_end)
      return fail("display line does not end on a token boundary");
    if (prefix.framing != TokenFramingRole::line_length)
      return fail("display line length byte is not stamped as one");
    for (auto token = line.prefix_token + 1; token < line.token_end; ++token)
      if (record.tokens[token].framing != TokenFramingRole::line_content)
        return fail("display line content token is not stamped as one");
    at = line.token_end;
  }
  if (at != record.tokens.size())
    return fail("display lines do not cover the record token list");
  return true;
}

namespace {

// Walks the assembled word sources of one display line, calling
// `visit(word, token, word_index)` for every displayed word and
// `visit(' ', -1, 0)` for every inter-token space the assembler inserted
// inside the line.
template <typename Visit>
void walk_display_line(const DecodedLogicalRecordSource& record,
                       const DisplayLineIR& line, Visit visit) {
  bool started = false;
  bool pending_space = false;
  constexpr auto no_token = static_cast<std::size_t>(-1);
  for (const auto& source : record.assembled.sources) {
    if (source.kind == LogicalWordSourceKind::inserted_space) {
      if (started) pending_space = true;
      continue;
    }
    if (source.token_index < line.prefix_token) continue;
    if (source.token_index >= line.token_end) break;
    // The checked accessor is what keeps the length byte out: it refuses to
    // hand back the words of a token the framing calls structure, so a
    // caller asking this walk for display text cannot be given the byte's
    // dictionary spelling.
    const auto* words_ptr = display_text_words(record, source.token_index);
    if (words_ptr == nullptr) continue;
    const auto& words = *words_ptr;
    if (source.word_index >= words.size() ||
        (source.word_index == 0 && words[0] < 4))
      continue;
    if (pending_space) {
      visit(static_cast<std::uint16_t>(' '), no_token, std::size_t{0});
      pending_space = false;
    }
    started = true;
    visit(words[source.word_index], source.token_index, source.word_index);
  }
}

} // namespace

std::string display_line_text(const DecodedLogicalRecordSource& record,
                              const DisplayLineIR& line) {
  std::string text;
  walk_display_line(record, line,
                    [&](const std::uint16_t word, const std::size_t,
                        const std::size_t) {
                      text += figure_display_glyph(word);
                    });
  return text;
}

std::vector<std::size_t> display_line_column_offsets(
    const DecodedLogicalRecordSource& record, const DisplayLineIR& line) {
  std::vector<std::size_t> offsets;
  std::size_t size = 0;
  walk_display_line(record, line,
                    [&](const std::uint16_t word, const std::size_t,
                        const std::size_t) {
                      offsets.push_back(size);
                      size += figure_display_glyph(word).size();
                    });
  offsets.push_back(size);
  return offsets;
}

std::vector<std::uint16_t> display_line_columns(
    const DecodedLogicalRecordSource& record, const DisplayLineIR& line) {
  std::vector<std::uint16_t> columns;
  walk_display_line(record, line,
                    [&](const std::uint16_t word, const std::size_t,
                        const std::size_t) {
                      columns.push_back(word);
                    });
  return columns;
}

std::vector<DisplayLineCellIR> display_line_cells(
    const DecodedLogicalRecordSource& record, const DisplayLineIR& line) {
  std::vector<DisplayLineCellIR> cells;
  walk_display_line(record, line,
                    [&](const std::uint16_t word, const std::size_t token,
                        const std::size_t word_index) {
                      cells.push_back({word, token, word_index});
                    });
  return cells;
}

bool is_display_line_margin_revision_bar(
    const DecodedLogicalRecordSource& record, const std::size_t token) {
  if (token == 0 || token >= record.ir.tokens.size() ||
      (record.ir.tokens[token].decoded_words != TokenWords{'|'} &&
       record.ir.tokens[token].decoded_words != TokenWords{0x2502}))
    return false;
  const auto* line = display_line_of_token(record, token);
  if (line == nullptr || token != line->prefix_token + 2 ||
      record.ir.tokens[token - 1].decoded_words != TokenWords{' '})
    return false;
  const auto cells = display_line_cells(record, *line);
  if (cells.size() < 4 || cells[0].token != token - 1 ||
      cells[1].token != token || cells[2].word != ' ')
    return false;
  return std::any_of(cells.begin() + 3, cells.end(),
                     [](const auto& cell) { return cell.word > ' '; });
}

namespace {

// A character a row margin draws as a revision mark: the box-drawing vertical,
// the broken bar U+00A6, or a printable ASCII punctuation character.  Letters
// and digits in cell 1 are row text (markup.adoc, "Fixed selection rows in
// generated lists").  SC34-5559-00 4.1.3 record 990 line 18 stores a blank,
// the two-byte U+00A6 at 0x61C82 and the bullet word; hosted (DT
// 20000125132847) serves `<li> <br> ¦     CL commands: ...`, the bar in the
// margin and the bullet blanked, as for `|`.
bool margin_mark_glyph(const std::uint16_t word) {
  if (word == 0x2502 || word == 0x00A6) return true;
  if (word < 0x21 || word > 0x7e) return false;
  return !((word >= '0' && word <= '9') || (word >= 'A' && word <= 'Z') ||
           (word >= 'a' && word <= 'z'));
}

struct MarginMarkReading {
  std::optional<char32_t> mark;
  std::size_t token = static_cast<std::size_t>(-1);
  MarginMarkPlaceIR place = MarginMarkPlaceIR::none;
};

MarginMarkReading read_margin_mark(const DecodedLogicalRecordSource& record,
                                   const DisplayLineIR& line) {
  MarginMarkReading reading;
  const auto& tokens = record.ir.tokens;
  const auto first = line.prefix_token + 1;
  if (first + 1 >= line.token_end || first + 1 >= tokens.size())
    return reading;
  const auto cells = display_line_cells(record, line);
  if (cells.size() < 3) return reading;
  // A token's displayed words: a leading spacing control (a word below 4) is
  // not drawn.
  const auto displayed = [&](const std::size_t token) {
    auto words = tokens[token].decoded_words;
    if (!words.empty() && words[0] < 4) words.erase(words.begin());
    return words;
  };
  const auto opcode = displayed(first);
  const auto next = displayed(first + 1);
  // The `ST` revision column: the cell directly after the two opcode cells,
  // glued to them (no inserted space), holds a non-blank word.  The cell is
  // a display column, not a token: a bare attach prefix stored as a token of
  // its own may stand between the opcode and the mark.
  constexpr auto inserted = static_cast<std::size_t>(-1);
  if ((opcode == TokenWords{'S', 'T'} || opcode == TokenWords{'s', 't'}) &&
      cells[0].token == first && cells[1].token == first &&
      cells[2].token != inserted && cells[2].token != first &&
      cells[2].word > ' ') {
    reading.mark = static_cast<char32_t>(cells[2].word);
    reading.token = cells[2].token;
    reading.place = MarginMarkPlaceIR::title_column;
    return reading;
  }
  // A body row's margin: one blank cell, the mark, a blank, drawn cells.
  if (cells.size() < 4 || opcode != TokenWords{' '} || next.size() != 1 ||
      !margin_mark_glyph(next[0]) || cells[0].token != first ||
      cells[1].token != first + 1 || cells[2].word != ' ')
    return reading;
  if (!std::any_of(cells.begin() + 3, cells.end(),
                   [](const auto& cell) { return cell.word > ' '; }))
    return reading;
  reading.mark = static_cast<char32_t>(next[0]);
  reading.token = first + 1;
  reading.place = MarginMarkPlaceIR::row_margin;
  return reading;
}

} // namespace

void assign_display_line_margin_marks(DecodedLogicalRecordSource& record) {
  if (!record.ir.display_lines_parse) return;
  for (auto& line : record.ir.display_lines) {
    const auto reading = read_margin_mark(record, line);
    line.margin_mark = reading.mark;
    line.margin_mark_token = reading.token;
    line.margin_mark_place = reading.place;
  }
}

std::optional<char32_t> display_line_margin_mark_at(
    const DecodedLogicalRecordSource& record, const std::size_t token) {
  const auto* line = display_line_of_token(record, token);
  if (line == nullptr || !line->margin_mark || line->margin_mark_token != token)
    return std::nullopt;
  return line->margin_mark;
}

std::optional<char32_t> topic_title_revision_mark(
    const std::vector<DecodedLogicalRecordSource>& records) {
  for (const auto& record : records) {
    if (!record.ir.display_lines_parse) continue;
    for (const auto& line : record.ir.display_lines) {
      const auto first = line.prefix_token + 1;
      if (first >= line.token_end || first >= record.ir.tokens.size()) continue;
      auto words = record.ir.tokens[first].decoded_words;
      if (!words.empty() && words[0] < 4) words.erase(words.begin());
      if (words != TokenWords{'S', 'T'} && words != TokenWords{'s', 't'})
        continue;
      if (line.margin_mark_place == MarginMarkPlaceIR::title_column)
        return line.margin_mark;
      return std::nullopt;
    }
  }
  return std::nullopt;
}

bool verify_display_line_margin_marks(const DecodedLogicalRecordSource& record,
                                      std::string* error) {
  const auto fail = [&](const char* message) {
    if (error != nullptr) *error = message;
    return false;
  };
  for (const auto& line : record.ir.display_lines) {
    const auto reading = read_margin_mark(record, line);
    if (reading.mark != line.margin_mark ||
        reading.token != line.margin_mark_token ||
        reading.place != line.margin_mark_place)
      return fail("display line margin mark disagrees with the line's cells");
    if (line.margin_mark.has_value() !=
        (line.margin_mark_place != MarginMarkPlaceIR::none))
      return fail("display line margin mark has no place");
  }
  return true;
}

bool is_display_line_list_bullet(const DecodedLogicalRecordSource& record,
                                 const std::size_t token) {
  const auto spaces = [](const LogicalTokenIR &candidate) {
    return !candidate.decoded_words.empty() &&
           std::all_of(candidate.decoded_words.begin(),
                       candidate.decoded_words.end(),
                       [](const auto word) { return word == ' '; });
  };
  if (token >= record.ir.tokens.size() ||
      record.ir.tokens[token].decoded_words != TokenWords{0x2666})
    return false;
  const auto *line = display_line_of_token(record, token);
  // The blank in front of the bullet may be the one the assembler inserts
  // after a margin revision bar: SC34-425 2.4.15.1 record 1565 line 33 is
  // ` ` + `|` + `U+2666` + a two-blank run + `Mismatched quotes ...`, the
  // bullet at column 3 (`.x.x...`).
  if (line == nullptr || token <= line->prefix_token + 1 ||
      token + 2 >= line->token_end ||
      (!spaces(record.ir.tokens[token - 1]) &&
       !is_display_line_margin_revision_bar(record, token - 1)) ||
      record.ir.tokens[token + 1].decoded_words != TokenWords{' ', ' '})
    return false;
  // The leading run is blank, except for a revision bar in its margin: a
  // one-cell blank, then a width-one `U+2502` or `|`. N2AH1MST record 599
  // lines 9-10 and 24-34 are ` ` + `U+2502` + 17 blanks + `U+2666` + two
  // blanks + the keyword, and hosted 10.0 (DT 19910329000100) serves them
  // as ` |                  °   TPADD`, the same bullet rows as the
  // unrevised lists around them.
  auto first = line->prefix_token + 1;
  if (first + 1 < token &&
      record.ir.tokens[first].decoded_words == TokenWords{' '} &&
      (record.ir.tokens[first + 1].decoded_words == TokenWords{0x2502} ||
       record.ir.tokens[first + 1].decoded_words == TokenWords{'|'}))
    first += 2;
  for (auto before = first; before < token; ++before)
    if (!spaces(record.ir.tokens[before]))
      return false;
  for (auto after = token + 2; after < line->token_end; ++after)
    if (std::any_of(record.ir.tokens[after].decoded_words.begin(),
                    record.ir.tokens[after].decoded_words.end(),
                    [](const auto word) { return word > ' '; }))
      return true;
  return false;
}

std::string format_display_line_ir(const DecodedLogicalRecordSource& record,
                                   const DisplayLineIR& line,
                                   const std::size_t index) {
  const auto& prefix = record.ir.tokens[line.prefix_token];
  std::string classes;
  for (const auto word : display_line_columns(record, line)) {
    if (word == ' ')
      classes.push_back('.');
    else if (word >= 0x2500 && word <= 0x25ff)
      classes.push_back('B');
    else if (word == 0xffff)
      classes.push_back('?');
    else
      classes.push_back('x');
  }
  std::ostringstream out;
  out << "line=" << index << " prefix_token=" << line.prefix_token
      << " length=" << prefix.encoded.value << " tokens=["
      << line.prefix_token + 1 << ',' << line.token_end << ") cols="
      << classes.size() << " class='" << classes << "' text='"
      << display_line_text(record, line) << '\'';
  return out.str();
}

namespace {

bool visible_display_token(const LogicalTokenIR& token) {
  for (const auto word : token.decoded_words)
    if (word != ' ') return true;
  return false;
}

} // namespace

namespace {

// ASCII spelling of one token's own words, with the leading spacing control
// and any surrounding space run removed.
std::string token_word_text(const LogicalTokenIR& token) {
  std::string text;
  for (const auto word : token.decoded_words) {
    if (word < 4) continue;
    if (word > 0x7F) return {};
    text.push_back(static_cast<char>(word));
  }
  const auto begin = text.find_first_not_of(' ');
  if (begin == std::string::npos) return {};
  return text.substr(begin, text.find_last_not_of(' ') + 1 - begin);
}

void demote_length_byte_controls(DecodedLogicalRecordSource& record,
                                 bool carried_selection) {
  // A display line's length byte is a length and nothing else: whatever word
  // the dictionary happens to spell for that byte, the reader never displays
  // it and it never opens a control.  Where the byte's spelling is itself a
  // control name the segment decoder reads it as one and the rest of the
  // line becomes that control's payload -- FA1PLMM0 record 477 byte 0x3f00f
  // is 0x39 = 57, spelled `cmitem`, and opens the 57-byte display line
  // `                    LTAB=(10,00,...),          *` inside the drawn
  // figure FIGFIGUNIQ9.  Genuine controls always sit at `prefix_token + 1`
  // (FA1PLMM0 record 471: every one of `ctopicn`, `cparent`, `csummary`,
  // `SRHDRPWRGEN`, `ST`, `SI`, `cfont` opens its line one token after that
  // line's length byte).
  for (std::size_t index = 0; index < record.control_segments.size();
       ++index) {
    auto& segment = record.control_segments[index];
    if (segment.kind == BookControlKind::text || segment.opcode.empty())
      continue;
    // The line a `cselect` with an empty payload selects is display text
    // whatever its length byte spells: qbkaco03 FIGURES record 42 line 51
    // opens with a byte spelled `csummary` under `cselect 3 72
    // FIGFIGUNIQ36`, record 43 line 0 with one spelled `CSUMMARY` after a
    // record-ending `cselect` (`carried_selection`), and dfha3b01 FIGURES
    // spells such a byte `cbacklevel`.
    const auto& previous =
        index == 0 ? nullptr : &record.control_segments[index - 1];
    const auto selected_line =
        (index == 0 && carried_selection) ||
        (previous != nullptr && previous->kind == BookControlKind::select &&
         previous->payload_range.begin == previous->payload_range.end);
    // Object-scope and selector opcodes are demoted here. The topic-metadata
    // and font opcodes are length bytes just as often, but they are also
    // read by the message and trap families, which render a segment's
    // payload as text and would print the byte's spelling; the prose stream
    // already withdraws those (prose_topic_stream.cpp, "Where a record's
    // display lines parse ...").
    switch (segment.kind) {
    case BookControlKind::menu_start:
    case BookControlKind::menu_item:
    case BookControlKind::menu_end:
    case BookControlKind::table_start:
    case BookControlKind::table_end:
    case BookControlKind::message_start:
    case BookControlKind::select:
      break;
    default:
      if (!selected_line) continue;
    }
    if (segment.source_tokens.empty()) continue;
    const auto token = segment.source_tokens.front();
    if (!is_display_line_length_opcode(record, segment)) continue;
    // Only when the length byte is what spells the opcode.  A segment that
    // merely starts at the byte before its own opcode keeps its control.
    if (ascii_lower(token_word_text(record.ir.tokens[token])) !=
        ascii_lower(segment.opcode))
      continue;
    segment.kind = BookControlKind::text;
    segment.display_text = true;
    segment.opcode.clear();
    segment.opcode_range = {segment.complete.begin, segment.complete.begin};
    segment.operand_range = segment.opcode_range;
    segment.payload_range = {segment.complete.begin, segment.complete.end};
  }
}

// A compiled menu item's label is bounded by the item's own display line.
// The flattened decoded string carries no mark at that boundary, so the
// splitter gets it wrong in both directions and the label the menu pass
// reconstructs no longer equals the catalogue title the item names:
//
//   * it runs past the line end and picks up the next line's length byte.
//     SC26-457 record 543 display line 18 is exactly
//     `cmitem 3.14.2.8 ... Catalog:  Example 8` (tokens 136-147); token 148
//     is line 19's length byte, and this book's dictionary spells the byte
//     35 as `----------`, so the label arrives with a rule glued to it that
//     hosted never prints.
//   * it stops short of the line end, because a word inside the label is
//     spelled like a control and the splitter opened a segment on it.
//     SC24-5527-02 record 592 display line 2 is
//     `cmitem 6.3.7 Create an APPLY List from Two SRVAPPS Tables`
//     (tokens 34-49) and arrives as two segments; the demotion above has
//     already established that `SRVAPPS` is display text, because a genuine
//     control opens its own display line and this one does not.
//
// Both are the same correction: the item's extent is the framing's, not the
// flattened string's.  A record with no decided framing, or an item whose
// label already ends exactly at its line end, keeps its previous reading.
void bound_menu_items_to_display_lines(DecodedLogicalRecordSource& record) {
  const auto text_size = token_words_to_ascii(record.assembled.words).size();
  const auto byte_of_token = [&](const std::size_t token) {
    if (token >= record.assembled.tokens.size()) return text_size;
    const auto begin = record.assembled.tokens[token].output_begin;
    return decoded_word_range_to_byte_range(record.assembled, {begin, begin})
        .begin;
  };
  const auto retoken = [&](ControlSegmentIR& segment) {
    const auto words =
        decoded_byte_range_to_word_range(record.assembled, segment.complete);
    segment.source_tokens =
        source_tokens_intersecting_output(record.assembled, words.begin,
                                          words.end);
  };
  std::vector<ControlSegmentIR> bounded;
  bounded.reserve(record.control_segments.size());
  for (std::size_t index = 0; index < record.control_segments.size();
       ++index) {
    auto segment = record.control_segments[index];
    const auto* line =
        segment.kind == BookControlKind::menu_item &&
                !segment.source_tokens.empty()
            ? display_line_of_token(record, segment.source_tokens.front())
            : nullptr;
    // An opcode standing on the length byte itself is no control at all; the
    // demotion above withdraws those, and one that survives is not bounded
    // here.
    if (line == nullptr || segment.source_tokens.front() == line->prefix_token) {
      segment.segment_index = bounded.size();
      bounded.push_back(std::move(segment));
      continue;
    }
    const auto line_end = byte_of_token(line->token_end);
    // Everything the splitter left on this line after the item belongs to the
    // item's label.  Only display text is absorbed: a segment the demotion
    // left as a control opens its own line and cannot be inside this one.
    while (index + 1 < record.control_segments.size()) {
      const auto& next = record.control_segments[index + 1];
      if (next.kind != BookControlKind::text || next.source_tokens.empty() ||
          display_line_of_token(record, next.source_tokens.front()) != line)
        break;
      segment.complete.end = next.complete.end;
      ++index;
    }
    if (line_end > segment.payload_range.begin &&
        line_end < segment.complete.end)
      segment.complete.end = line_end;
    segment.payload_range.end = segment.complete.end;
    retoken(segment);
    segment.segment_index = bounded.size();
    bounded.push_back(std::move(segment));
  }
  record.control_segments = std::move(bounded);
}

// A `cselect` payload is the display line after the control's own line, and
// that line can hold a word spelled like a control.  The splitter opens a
// segment on it, the demotion above proves it display text (a genuine
// control opens its own display line), and the selected line then ends at the
// word: the selection's columns reach past the row it was left with.
// SG24-5257-00 FIGURES record 16 line 4 is `   28.  Defining SRDS with
// IDCAMS   4.1.3` under `cselect 3 38 FIGTSTA6`; the splitter cut it before
// `SRDS`, leaving 17 of the 41 cells the selection names.  Hosted (DT
// 19981216162732) links the whole `28.  Defining SRDS with IDCAMS   4.1.3`.
// The same correction as for a menu item: the text segments that open on the
// selector's last payload line are that line's display text and belong to
// the selector's payload.  A selector that ends its record with an empty
// payload selects the next record's first line (`carried_selection`), whose
// leading text segment is then that payload: qbkaqx00 FIGURES record 17 ends
// `cselect 3 59 FIGCSIG` and record 18 line 0 is `   23.  Current Signature
// of Service Program SRV1   2.1.4.1`.
//
// An `SR<id>` anchor's payload is the rest of its display line in the same
// way: the metadata-envelope `SRLEN <name>` names its anchor by that line.
// SC24-5780-01 3.146 record 1261 line 6 is `SRLEN SI (Macro)`, which the
// splitter cut before `SI`; hosted (DT 19961023165238) serves
// `<a name="LEN SI (Macro)">` and nothing of it as text.  An `SRMSG` row is
// read the same way: the reader forms every `SR` row's anchor from the row
// text after `SR`, verbatim, and dknm3a02 3.1906 record 2192 line 6 is
// `SRMSG SRTM 01`, cut before `SRTM`.
void rejoin_selected_line_display_text(DecodedLogicalRecordSource& record,
                                       bool carried_selection) {
  std::vector<ControlSegmentIR> joined;
  joined.reserve(record.control_segments.size());
  for (std::size_t index = 0; index < record.control_segments.size();
       ++index) {
    auto segment = record.control_segments[index];
    const auto selected =
        segment.kind == BookControlKind::select ||
        segment.kind == BookControlKind::message_start ||
        (segment.kind == BookControlKind::structural &&
         segment.opcode.size() > 2 &&
         ascii_lower(segment.opcode).rfind("sr", 0) == 0) ||
        (carried_selection && index == 0 &&
         segment.kind == BookControlKind::text && !segment.display_text);
    const auto* line =
        selected && !segment.source_tokens.empty()
            ? display_line_of_token(record, segment.source_tokens.back())
            : nullptr;
    bool grown = false;
    while (line != nullptr && index + 1 < record.control_segments.size()) {
      const auto& next = record.control_segments[index + 1];
      if (!next.display_text || next.kind != BookControlKind::text ||
          next.source_tokens.empty() ||
          display_line_of_token(record, next.source_tokens.front()) != line ||
          next.source_tokens.front() == line->prefix_token + 1)
        break;
      segment.complete.end = next.complete.end;
      ++index;
      grown = true;
    }
    if (grown && segment.kind == BookControlKind::message_start &&
        segment.operand_range.begin == segment.operand_range.end) {
      // The message identity is what the splitter cut off: the row's words
      // become the control's operand.
      segment.operand_range.end = segment.complete.end;
      segment.payload_range = {segment.complete.end, segment.complete.end};
      segment.malformed = false;
    }
    if (grown) {
      segment.payload_range.end = segment.complete.end;
      const auto words =
          decoded_byte_range_to_word_range(record.assembled, segment.complete);
      segment.source_tokens = source_tokens_intersecting_output(
          record.assembled, words.begin, words.end);
    }
    segment.segment_index = joined.size();
    joined.push_back(std::move(segment));
  }
  record.control_segments = std::move(joined);
}

} // namespace

const ControlSegmentIR* carried_control_segment(
    const DecodedLogicalRecordSource& record) {
  auto at = record.control_segments.rbegin();
  if (at == record.control_segments.rend()) return nullptr;
  while (std::next(at) != record.control_segments.rend() &&
         at->kind == BookControlKind::font &&
         at->payload_range.begin == at->payload_range.end)
    ++at;
  return &*at;
}

bool ends_with_carried_selection(const DecodedLogicalRecordSource& record) {
  const auto* carried = carried_control_segment(record);
  return carried != nullptr && carried->kind == BookControlKind::select &&
         carried->payload_range.begin == carried->payload_range.end;
}

void demote_display_line_owned_controls(DecodedLogicalRecordSource& record,
                                        bool carried_selection) {
  if (record.control_segments.empty()) return;
  const auto lines = record_display_lines(record);
  if (!lines) return;
  demote_length_byte_controls(record, carried_selection);
  for (std::size_t index = 0; index < record.control_segments.size(); ++index) {
    auto& segment = record.control_segments[index];
    if (segment.source_tokens.empty()) continue;
    // Publication identifiers such as SH26-7000 can resemble a topic-start
    // instruction. Only an opcode at the native origin starts a topic; a
    // source-proven indented identifier is display payload, even when the
    // flattened segment also includes its preceding length byte.
    if (segment.kind == BookControlKind::topic_start &&
        is_display_line_embedded_opcode(record, segment)) {
      segment.kind = BookControlKind::text;
      segment.display_text = true;
      segment.opcode.clear();
      segment.opcode_range = {segment.complete.begin, segment.complete.begin};
      segment.operand_range = segment.opcode_range;
      segment.payload_range = segment.complete;
      continue;
    }
    const bool index_keyword =
        segment.kind == BookControlKind::text &&
        ascii_lower(token_word_text(
            record.ir.tokens[segment.source_tokens.front()])) == "si";
    // A layout directive is read the same way: `cz` opens its own display
    // line, so a `CZ` word after visible cells is prose (ha5cam00
    // GLOSSARY.2 record 259 line 0 `... character sets have a CZ prefix.`).
    if (segment.kind != BookControlKind::structural &&
        segment.kind != BookControlKind::title &&
        segment.kind != BookControlKind::text &&
        segment.kind != BookControlKind::layout_directive)
      continue;
    // A line's length byte draws nothing whatever its spelling, so it is
    // never the segment's first visible token: dfhqap4a 10.0 record 1678
    // opens on line 0's byte (spelled U+251C) and the splitter's `SRL` is
    // the language-code cell after the line's rail.
    std::size_t first = record.ir.tokens.size();
    for (const auto token : segment.source_tokens)
      if (visible_display_token(record.ir.tokens[token]) &&
          !is_display_line_length_token(record, token)) {
        first = token;
        break;
      }
    if (first >= record.ir.tokens.size()) continue;
    const DisplayLineIR* line = nullptr;
    for (const auto& candidate : *lines)
      if (first > candidate.prefix_token && first < candidate.token_end)
        line = &candidate;
    if (line == nullptr) continue;
    bool visible_before = false;
    for (auto token = line->prefix_token + 1; token < first; ++token)
      if (visible_display_token(record.ir.tokens[token])) visible_before = true;
    // The `ST` title and `SI` index keyword get the stricter reading an opcode gets
    // everywhere else in a record: it must stand at the *first* cell of its
    // display line, one token after that line's length byte.  Every genuine
    // control does (FA1PLMM0 record 471 opens each of `ctopicn`, `cparent`,
    // `csummary`, `SRHDRPWRGEN`, `ST`, `SI` and `cfont` exactly there), and
    // `ST` is short enough and common enough as display text that the weaker
    // "nothing visible in front of it" test does not separate the two:
    // SH12-565 3.1.4.1 record 184 line 23 is the definition term `   ST` of a
    // two-column list, styled by the `cfont 3 2 P` on the line above, and
    // SC24-546 D.0 record 1141 lines 5, 9 and 11 are the assembler STORE
    // instructions `              ST    R4,EVSIZE`. An indented `SI` is
    // likewise a literal shift-in term (SC24-546 B.2 records 1038/1041),
    // not subject-index metadata. Hosted serves the STORE examples as
    // display text (DT 19941206115523 and DT 19940323131240).
    if ((segment.kind == BookControlKind::title || index_keyword) &&
        first != line->prefix_token + 1)
      visible_before = true;
    // A control's opcode is the first token of its own display line: the
    // encoder writes the line's length byte and then the opcode, in every
    // control the corpus stores.  A word that stands *after* the line's
    // origin run is therefore display text at a column, whatever it is
    // spelled like -- the same reading as the rule above, taken off the
    // record's own framing instead of off a visible neighbour, because a row
    // whose first word happens to be control-shaped has no visible neighbour
    // to prove it with.
    //
    // Corpus-wide this withdraws exactly nineteen segments, in six books, and
    // every one of them is a source-file, member or keyword name standing at
    // the left margin of a code sample: SC34-425 `2.5.6` records 1759/1760/
    // 1762 `SRC1`/`SRC2`/`SRC3` (the `SRC1   FLMTYPE EXTEND=SRC2` rows of
    // `2.5.6`'s project definition), SH12-565 records 403/814 `SRVMODE` and
    // 328/796 `SRVPREF`, PRG1SORT records 528/529 `SRCFILE`/`SRCMBR`,
    // SC24-5527-02 records 572/605/606/613/627
    // `SRVREQT`/`SRVAPPS`/`SRVBLDS`/`SRVRECS`, SC24-5520-00 records 102/103
    // `SRCH` and SC24-546 record 962 `SRRCMIT`.  Hosted BookServer serves
    // every one of them as display text and puts no anchor on any:
    // `<samp>SRC1</samp>   <samp>FLMTYPE</samp>` (SC34-4254-03 DT
    // 19921112160049 `2.5.6`), `SRVPREF is a common prefix ...` (SH12-5657-04
    // DT 19941206115523 `4.7.2`) and `<samp>SRCH</samp> <samp>ID</samp>
    // <samp>EQ,A+2,CC,5</samp>` (SC24-5520-00 DT 19911011135123 `1.1.13`).
    // None of them is a control the reader knows either: the
    // controls this corpus stores are `SRHDR`/`SRFIG`/`SRTBL`/`SRGLS`/
    // `SRLIS`/`SRMSG`/`SRSPT`/`SRFTN` anchors and their closers, and each of
    // those opens its own display line.  Losing them cost the row its origin
    // run and its first word, which is why a `CFONT` operand then landed
    // several columns past the cells the row had left.
    //
    // Only a segment that claims an opcode is withdrawn this way.  A text
    // segment already carries no opcode, and rewriting its ranges here would
    // move boundaries the splitter owns for no gain.
    if (!visible_before &&
        (segment.opcode.empty() || first == line->prefix_token + 1))
      continue;
    // The boundary itself stays where the flattened string put it.  Giving
    // the demoted segment the whole gap back to the previous segment's end
    // was measured and reverted: it costs 60 topics, because that gap also
    // carries padding the previous segment's own model relies on.  The one
    // character the split really consumed -- the separator it fired on -- is
    // reclaimed exactly and only where it is display text, by
    // `reclaim_split_separators` in control_ir.cpp.
    segment.kind = BookControlKind::text;
    segment.display_text = true;
    segment.opcode.clear();
    segment.opcode_range = {segment.complete.begin, segment.complete.begin};
    segment.operand_range = segment.opcode_range;
    segment.payload_range = {segment.complete.begin, segment.complete.end};
  }
  rejoin_selected_line_display_text(record, carried_selection);
  bound_menu_items_to_display_lines(record);
}

} // namespace geist::detail
