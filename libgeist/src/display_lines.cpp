#include "geist/detail/display_lines.hpp"
#include <sstream>

#include "geist/detail/figure_block_ir.hpp"

#include <algorithm>

namespace geist::detail {

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
    lines.push_back({at, end});
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

// Walks the assembled word sources of one display line, calling `visit(word)`
// for every displayed word and `visit(' ')` for every inter-token space the
// assembler inserted inside the line.
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
      visit(static_cast<std::uint16_t>(' '), no_token);
      pending_space = false;
    }
    started = true;
    visit(words[source.word_index], source.token_index);
  }
}

} // namespace

std::string display_line_text(const DecodedLogicalRecordSource& record,
                              const DisplayLineIR& line) {
  std::string text;
  walk_display_line(record, line,
                    [&](const std::uint16_t word, const std::size_t) {
                      text += figure_display_glyph(word);
                    });
  return text;
}

std::vector<std::size_t> display_line_column_offsets(
    const DecodedLogicalRecordSource& record, const DisplayLineIR& line) {
  std::vector<std::size_t> offsets;
  std::size_t size = 0;
  walk_display_line(record, line,
                    [&](const std::uint16_t word, const std::size_t) {
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
                    [&](const std::uint16_t word, const std::size_t) {
                      columns.push_back(word);
                    });
  return columns;
}

std::vector<DisplayLineCellIR> display_line_cells(
    const DecodedLogicalRecordSource& record, const DisplayLineIR& line) {
  std::vector<DisplayLineCellIR> cells;
  walk_display_line(record, line,
                    [&](const std::uint16_t word, const std::size_t token) {
                      cells.push_back({word, token});
                    });
  return cells;
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

void demote_length_byte_controls(DecodedLogicalRecordSource& record) {
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
  for (auto& segment : record.control_segments) {
    if (segment.kind == BookControlKind::text || segment.opcode.empty())
      continue;
    // Only the object-scope opcodes are demoted here.  The topic-metadata
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
      break;
    default:
      continue;
    }
    if (segment.source_tokens.empty()) continue;
    const auto token = segment.source_tokens.front();
    if (!is_display_line_length_token(record, token)) continue;
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

} // namespace

void demote_display_line_owned_controls(DecodedLogicalRecordSource& record) {
  if (record.control_segments.empty()) return;
  const auto lines = record_display_lines(record);
  if (!lines) return;
  demote_length_byte_controls(record);
  for (std::size_t index = 0; index < record.control_segments.size(); ++index) {
    auto& segment = record.control_segments[index];
    if (segment.source_tokens.empty()) continue;
    if (segment.kind != BookControlKind::structural &&
        segment.kind != BookControlKind::text)
      continue;
    std::size_t first = record.ir.tokens.size();
    for (const auto token : segment.source_tokens)
      if (visible_display_token(record.ir.tokens[token])) {
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
    if (!visible_before) continue;
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
  bound_menu_items_to_display_lines(record);
}

} // namespace geist::detail
