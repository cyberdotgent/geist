#include "geist/detail/control_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace geist::detail {
namespace {

struct WordSpan {
  std::size_t begin = 0;
  std::size_t end = 0;
  // The word projects display-geometry code points (see
  // display_geometry_word); such a word is payload, never opcode or operand.
  bool geometry = false;
};

// Box-drawing code points (U+2500-U+257F) are display geometry emitted from
// the glyph dictionary: table rules, junctions and syntax-diagram rails. They
// are never opcode or operand material. The ASCII projection flattens them to
// '?', so this class is decided on the assembled code points, not on the
// projected spelling.
bool display_geometry_word(std::uint16_t word) {
  return word >= 0x2500 && word <= 0x257F;
}

// Byte-level map of the assembled output marking every byte that projects a
// display-geometry code point.
std::vector<bool> display_geometry_bytes(const AssembledLogicalRecord &assembled,
                                         std::size_t text_size) {
  std::vector<bool> geometry(text_size, false);
  std::size_t byte = 0;
  for (const auto word : assembled.words) {
    const auto width = token_word_ascii_width(word);
    if (display_geometry_word(word)) {
      for (std::size_t at = byte; at < byte + width && at < text_size; ++at)
        geometry[at] = true;
    }
    byte += width;
  }
  return geometry;
}

// Splits [begin, end) into words on whitespace and on every transition between
// display-geometry bytes and other bytes. A control name or operand adjacent
// to a table rule without an intervening decoder space (SRETBL followed by a
// zero-width control token and a horizontal rule; CMITEM followed by a
// vertical rail) therefore ends at the glyph boundary instead of absorbing it.
std::vector<WordSpan> words_in(const std::string &text, std::size_t begin,
                               std::size_t end,
                               const std::vector<bool> &geometry = {}) {
  std::vector<WordSpan> words;
  const auto geometry_at = [&](std::size_t at) {
    return at < geometry.size() && geometry[at];
  };
  auto cursor = begin;
  while (cursor < end) {
    while (cursor < end &&
           std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
      ++cursor;
    }
    if (cursor == end)
      break;
    const auto word_begin = cursor;
    const auto word_geometry = geometry_at(cursor);
    while (cursor < end &&
           std::isspace(static_cast<unsigned char>(text[cursor])) == 0 &&
           geometry_at(cursor) == word_geometry) {
      ++cursor;
    }
    words.push_back({word_begin, cursor, word_geometry});
  }
  return words;
}

bool decimal_word(const std::string &text, const WordSpan &word,
                  std::size_t maximum = 4096) {
  if (word.begin == word.end)
    return false;
  std::size_t value = 0;
  for (auto at = word.begin; at < word.end; ++at) {
    const auto byte = static_cast<unsigned char>(text[at]);
    if (std::isdigit(byte) == 0)
      return false;
    value = value * 10 + static_cast<std::size_t>(text[at] - '0');
    if (value > maximum)
      return false;
  }
  return true;
}

BookControlKind classify(std::string opcode) {
  opcode = ascii_lower(std::move(opcode));
  // Topic-start controls encode the topic number in the opcode itself (for
  // example SH5.0). A lexical word such as "shutdown" at the start of a
  // continuation record is prose, not a topic boundary.  Non-numeric topic
  // ids are promoted afterwards by promote_corroborated_topic_start when the
  // record envelope proves them.
  if (opcode.size() > 2 && opcode.rfind("sh", 0) == 0 &&
      std::isdigit(static_cast<unsigned char>(opcode[2])) != 0)
    return BookControlKind::topic_start;
  // A C control with an empty operand keeps the operand's '.' terminator
  // attached to its opcode word: SC31-711 record 543 reads
  // "ctopicn 91. cparent. cforwardlevel" and PRG1SORT record 119 reads
  // "cparent 1.2. cforwardlevel. cbacklevel". Match the exact control name
  // without that terminator; the byte remains inside the opcode range.
  if (opcode.size() > 2 && opcode.front() == 'c' && opcode.back() == '.')
    opcode.pop_back();
  if (opcode == "ctopicn")
    return BookControlKind::topic_number;
  if (opcode == "cparent")
    return BookControlKind::parent;
  if (opcode == "cforwardlevel")
    return BookControlKind::forward_level;
  if (opcode == "cbacklevel")
    return BookControlKind::back_level;
  if (opcode == "csummary")
    return BookControlKind::summary;
  if (opcode == "chdlevel")
    return BookControlKind::heading_level;
  if (opcode == "csourcefn")
    return BookControlKind::source_file;
  if (opcode == "st")
    return BookControlKind::title;
  if (opcode == "cfont")
    return BookControlKind::font;
  if (opcode == "cselect")
    return BookControlKind::select;
  if (opcode == "c.sp")
    return BookControlKind::spacing;
  if (opcode == "cz")
    return BookControlKind::layout_directive;
  if (opcode.rfind("srtbl", 0) == 0)
    return BookControlKind::table_start;
  // A comma can be attached to the decoded end token at a segment boundary.
  if (opcode.rfind("sretbl", 0) == 0)
    return BookControlKind::table_end;
  if (opcode == "cmenu")
    return BookControlKind::menu_start;
  if (opcode == "cmitem")
    return BookControlKind::menu_item;
  if (opcode == "cemenu")
    return BookControlKind::menu_end;
  if (opcode == "srmsg")
    return BookControlKind::message_start;
  // Remaining SR controls use an identifier-like opcode. Bare prose tokens
  // such as "SR," (from "SR, TP, and STP") must stay text. Known C controls
  // are classified explicitly above; treating every C-prefixed word as a
  // control loses continuations beginning "Consequently" or "cannot".
  if (opcode.size() > 2 && opcode.rfind("sr", 0) == 0 &&
      std::all_of(opcode.begin(), opcode.end(), [](const unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_';
      }))
    return BookControlKind::structural;
  return BookControlKind::text;
}

bool topic_identifier_word(const std::string &text, const WordSpan &word) {
  if (word.end - word.begin <= 2 ||
      ascii_lower(text.substr(word.begin, 2)) != "sh")
    return false;
  return std::all_of(text.begin() + static_cast<std::ptrdiff_t>(word.begin),
                     text.begin() + static_cast<std::ptrdiff_t>(word.end),
                     [](const unsigned char ch) {
                       return std::isalnum(ch) != 0 || ch == '_' ||
                              ch == '-' || ch == '.';
                     });
}

// Topic identifiers are not always numeric (APPENDIX1.9.5, GLOSSARY, BACK_1.7
// are real BookManager topic ids).  A record-leading SH word whose segment
// carries nothing else and that is immediately followed by the CTOPICN
// control is the topic-start control by envelope evidence, independent of
// its spelling.  Without that corroboration the word remains prose.
void promote_corroborated_topic_start(const std::string &text,
                                      std::vector<ControlSegmentIR> &segments) {
  if (segments.size() < 2 || segments[0].kind != BookControlKind::text ||
      segments[1].kind != BookControlKind::topic_number ||
      segments[0].complete.begin != 0)
    return;
  auto &segment = segments[0];
  auto begin = segment.complete.begin;
  while (begin < segment.complete.end &&
         (std::isspace(static_cast<unsigned char>(text[begin])) != 0 ||
          text[begin] == ',' || text[begin] == '?')) {
    ++begin;
  }
  const auto words = words_in(text, begin, segment.complete.end);
  if (words.size() != 1 || !topic_identifier_word(text, words[0]))
    return;
  segment.kind = BookControlKind::topic_start;
  segment.opcode =
      text.substr(words[0].begin, words[0].end - words[0].begin);
  segment.opcode_range = {words[0].begin, words[0].end};
  segment.operand_range = {words[0].end, words[0].end};
  segment.payload_range = {words[0].end, segment.complete.end};
}

// The SCRIPT body controls (`c.cp` keep-together, `c.cc` conditional column,
// `c.pa` page eject) are stored as one whole dictionary token spelling
// `c.` plus the two-letter opcode.  Deciding this on the assembled words of a
// single source token, not on the flattened projection, is what separates the
// control from prose: the decoded string writes the boundary byte before it as
// `?` (the projection of the attach-control word 0x0001, of a bullet glyph and
// of every box word alike), so the string-level segment splitter — which
// requires a space, `=`, `,` or `.` after the opcode — cannot see it at all.
bool script_control_words(const TokenWords &words) {
  if (words.size() < 4 || words[0] != 'c' || words[1] != '.')
    return false;
  return std::all_of(words.begin() + 2, words.end(),
                     [](const std::uint16_t word) {
                       return word >= 'a' && word <= 'z';
                     });
}

// A one-byte dictionary token whose expansion is the attach control plus a
// comma is the control separator the record encoder writes before a body
// control (FA1PLMM0 record 353 token 72 and SC09-138 record 1482 token 47 both
// carry encoded value 2, words {0x0001, ','}; the same separator stands
// between `csummary` and `chdlevel` in FA1PLMM0 record 352).  Hosted
// BookServer prints no comma there.
bool control_separator_words(const TokenWords &words) {
  std::size_t at = 0;
  while (at < words.size() && words[at] == 1)
    ++at;
  return at + 1 == words.size() && words[at] == ',';
}

// The token's own words: the assembled output carries the decoder's inserted
// spacing, so a token's assembled words can start or end with a space run.
OutputRangeIR token_body_word_range(const AssembledLogicalRecord &assembled,
                                    const LogicalTokenSpan &token) {
  auto begin = token.output_begin;
  auto end = token.output_end;
  while (begin < end && assembled.words[begin] == ' ')
    ++begin;
  while (end > begin && assembled.words[end - 1] == ' ')
    --end;
  return {begin, end};
}

TokenWords token_body_words(const AssembledLogicalRecord &assembled,
                            const LogicalTokenSpan &token) {
  const auto range = token_body_word_range(assembled, token);
  return TokenWords(
      assembled.words.begin() + static_cast<std::ptrdiff_t>(range.begin),
      assembled.words.begin() + static_cast<std::ptrdiff_t>(range.end));
}

// A control separator token carries no space of its own, so the flattened
// projection glues its comma to the opcode word in front of it: SC24-5527-02
// record 145 token 128 spells `SRTBLTBLUNIQ37` and token 129 is the separator
// (encoded value 2, words {0x0001, ','}), which the string reads as one word
// `SRTBLTBLUNIQ37,`.  Hosted BookServer serves that table as
// `<a name="TBLTBLUNIQ37">` (DT 19921218151459), with no comma anywhere.  The
// separator keeps its place in the opcode *range* -- it stays structural in
// the ownership ledger exactly as before -- only the opcode spelling drops it.
std::size_t opcode_end_without_separator(const AssembledLogicalRecord &assembled,
                                         const std::string &text,
                                         const WordSpan &word) {
  if (word.end == 0 || word.end > text.size() || text[word.end - 1] != ',')
    return word.end;
  for (const auto &token : assembled.tokens) {
    const auto bytes = decoded_word_range_to_byte_range(
        assembled, token_body_word_range(assembled, token));
    if (bytes.end != word.end || bytes.begin <= word.begin)
      continue;
    if (!control_separator_words(token_body_words(assembled, token)))
      continue;
    return bytes.begin;
  }
  return word.end;
}

// Splits every decoded span that carries a body-control opcode token which the
// string-level splitter left glued to the run before it.  Nothing else about
// the span changes, so a record without such a token keeps its exact spans.
std::vector<DecodedMarkupSegmentSpan>
split_glued_body_controls(const AssembledLogicalRecord &assembled,
                          const std::vector<EncodedLogicalToken> &encoded,
                          const std::string &text,
                          std::vector<DecodedMarkupSegmentSpan> spans) {
  struct Cut {
    std::size_t begin = 0;
    std::size_t opcode_end = 0;
  };
  std::vector<Cut> cuts;
  // The assembled output carries the decoder's inserted spacing, so a token's
  // assembled words can start or end with a space run; the token's own word is
  // what is between them.
  const auto words_of = [&](const LogicalTokenSpan &token) {
    return token_body_words(assembled, token);
  };
  for (std::size_t index = 0; index < assembled.tokens.size(); ++index) {
    const auto &token = assembled.tokens[index];
    if (!script_control_words(words_of(token)))
      continue;
    // Only a two-byte dictionary token is a body control.  A one-byte token
    // that merely spells `c.<xx>` is display-line geometry the row model owns:
    // IBMMMSTR 3.1 record 1244 token 139 (encoded value 31) spells `c.cc` and
    // stands exactly where a display line's length byte stands -- after the
    // `:` that ends `Messages print at run-time when:` and before the
    // three-cell origin run of `1.  An error occurs ...`; OFCUSEOV 1.4
    // records 83 and 87 (value 55) spell `c.cp` at the row break hosted
    // serves as a `<p>` between the numbered steps.  Reading either as a
    // control loses that break.  The genuine controls are two-byte tokens:
    // FA1PLMM0 record 353 token 73 `c.cp` value 49655, GG24-4302-00 record
    // 613 token 19 `c.cc` value 52750, SC33-033 record 241 token 56 `c.cc`
    // value 53126, SC09-138 record 1482 token 48 `c.pa`.
    if (index < encoded.size() && encoded[index].width != 2)
      continue;
    auto begin = token.output_begin;
    if (index != 0 &&
        control_separator_words(words_of(assembled.tokens[index - 1])))
      begin = assembled.tokens[index - 1].output_begin;
    cuts.push_back(
        {decoded_word_range_to_byte_range(assembled, {begin, begin}).begin,
         decoded_word_range_to_byte_range(assembled,
                                          {token.output_end, token.output_end})
             .begin});
  }
  if (cuts.empty())
    return spans;
  // A cut only earns its segment when display material follows the opcode
  // inside the same span.  Where the rest of the span is layout, the control
  // is already the row boundary the display-row model reads, and splitting it
  // out loses that boundary: SH12-565 1.1.3.1 record 37 ends its span with
  // `c.cp` and a 63-cell fill run, and hosted DT 19941206115523 serves a
  // `<p>` between the numbered steps there.
  const auto display_between = [&](std::size_t byte_begin,
                                   std::size_t byte_end) {
    const auto range = decoded_byte_range_to_word_range(
        assembled, {byte_begin, byte_end});
    for (auto word = range.begin; word < range.end && word < text.size();
         ++word) {
      const auto value = assembled.words[word];
      if (value >= 0x21 && value != 0x7F && value < 0x2500)
        return true;
    }
    return false;
  };
  std::vector<DecodedMarkupSegmentSpan> result;
  result.reserve(spans.size() + cuts.size());
  for (auto &span : spans) {
    auto begin = span.output_begin;
    for (const auto &cut : cuts) {
      if (cut.begin <= begin || cut.begin >= span.output_end)
        continue;
      if (!display_between(cut.opcode_end, span.output_end))
        continue;
      result.push_back({begin, cut.begin, text.substr(begin, cut.begin - begin)});
      begin = cut.begin;
    }
    if (begin == span.output_begin) {
      result.push_back(std::move(span));
    } else {
      result.push_back({begin, span.output_end,
                        text.substr(begin, span.output_end - begin)});
    }
  }
  return result;
}

std::size_t fixed_operand_count(BookControlKind kind) {
  switch (kind) {
  case BookControlKind::topic_number:
  case BookControlKind::parent:
  case BookControlKind::forward_level:
  case BookControlKind::back_level:
  case BookControlKind::heading_level:
  case BookControlKind::source_file:
  case BookControlKind::menu_item:
  case BookControlKind::message_start:
    return 1;
  case BookControlKind::summary:
    return 3;
  case BookControlKind::select:
    return 3;
  default:
    return 0;
  }
}

OutputRangeIR word_range_for_bytes(const AssembledLogicalRecord &assembled,
                                   std::size_t byte_begin,
                                   std::size_t byte_end) {
  OutputRangeIR result{assembled.words.size(), assembled.words.size()};
  std::size_t byte = 0;
  for (std::size_t word = 0; word < assembled.words.size(); ++word) {
    const auto width = token_word_ascii_width(assembled.words[word]);
    if (result.begin == assembled.words.size() && byte + width > byte_begin) {
      result.begin = word;
    }
    byte += width;
    if (byte >= byte_end) {
      result.end = word + 1;
      break;
    }
  }
  if (byte_begin == byte_end)
    result.begin = result.end;
  return result;
}

} // namespace

OutputRangeIR
decoded_byte_range_to_word_range(const AssembledLogicalRecord &assembled,
                                 const OutputRangeIR &bytes) {
  return word_range_for_bytes(assembled, bytes.begin, bytes.end);
}

OutputRangeIR
decoded_word_range_to_byte_range(const AssembledLogicalRecord &assembled,
                                 const OutputRangeIR &words) {
  const auto bounded_begin = std::min(words.begin, assembled.words.size());
  const auto bounded_end = std::min(words.end, assembled.words.size());
  std::size_t byte = 0;
  OutputRangeIR result;
  for (std::size_t word = 0; word <= bounded_end; ++word) {
    if (word == bounded_begin)
      result.begin = byte;
    if (word == bounded_end) {
      result.end = byte;
      break;
    }
    byte += token_word_ascii_width(assembled.words[word]);
  }
  return result;
}

std::vector<ControlSegmentIR>
decode_control_segments(std::uint32_t logical_record,
                        const AssembledLogicalRecord &assembled,
                        const std::vector<EncodedLogicalToken> &encoded_tokens) {
  const auto text = token_words_to_ascii(assembled.words);
  const auto geometry = display_geometry_bytes(assembled, text.size());
  const auto decoded = split_glued_body_controls(
      assembled, encoded_tokens, text,
      split_decoded_markup_segment_spans(text));
  std::vector<ControlSegmentIR> result;
  result.reserve(decoded.size());
  for (std::size_t index = 0; index < decoded.size(); ++index) {
    const auto &source = decoded[index];
    if (source.output_begin >= source.output_end) {
      continue;
    }
    ControlSegmentIR segment;
    segment.logical_record = logical_record;
    segment.segment_index = result.size();
    segment.complete = {source.output_begin, source.output_end};
    auto begin = source.output_begin;
    while (begin < source.output_end &&
           (std::isspace(static_cast<unsigned char>(text[begin])) != 0 ||
            text[begin] == ',' || text[begin] == '?')) {
      ++begin;
    }
    auto words = words_in(text, begin, source.output_end, geometry);
    // Operand parsing sees only the words before the first geometry word: a
    // control's operands end where display geometry begins, and the geometry
    // itself is payload. A leading geometry word cannot be an opcode (its
    // projection never classifies as a control), so it leaves the segment
    // as text exactly as before.
    words.erase(std::find_if(words.begin(), words.end(),
                             [](const WordSpan &word) { return word.geometry; }),
                words.end());
    if (words.empty()) {
      segment.kind = BookControlKind::text;
      segment.opcode_range = {begin, begin};
      segment.operand_range = {begin, begin};
      segment.payload_range = {begin, source.output_end};
    } else {
      const auto opcode_end =
          opcode_end_without_separator(assembled, text, words[0]);
      segment.opcode = text.substr(words[0].begin, opcode_end - words[0].begin);
      segment.kind = classify(segment.opcode);
      segment.opcode_range = {words[0].begin, words[0].end};
      if (segment.kind == BookControlKind::text) {
        segment.opcode.clear();
        segment.opcode_range = {source.output_begin, source.output_begin};
        segment.operand_range = segment.opcode_range;
        segment.payload_range = {source.output_begin, source.output_end};
      } else {
        auto operand_words = fixed_operand_count(segment.kind);
        if (segment.kind == BookControlKind::font) {
          operand_words = 0;
          for (std::size_t word = 1; word + 2 < words.size(); word += 3) {
            if (!decimal_word(text, words[word]) ||
                !decimal_word(text, words[word + 1])) {
              break;
            }
            operand_words += 3;
          }
          segment.malformed = operand_words == 0;
        } else if (segment.kind == BookControlKind::spacing) {
          segment.malformed =
              words.size() != 4 ||
              ascii_lower(text.substr(words[1].begin,
                                      words[1].end - words[1].begin)) != "3p" ||
              ascii_lower(text.substr(words[2].begin,
                                      words[2].end - words[2].begin)) != "p" ||
              ascii_lower(text.substr(words[3].begin,
                                      words[3].end - words[3].begin)) != "c";
          operand_words = segment.malformed ? 0 : 3;
        } else if (segment.kind == BookControlKind::layout_directive) {
          std::vector<std::string> operands;
          operands.reserve(words.size() - 1);
          for (std::size_t word = 1; word < words.size(); ++word) {
            operands.push_back(ascii_lower(text.substr(
                words[word].begin, words[word].end - words[word].begin)));
          }
          segment.malformed =
              !((operands.size() == 2 && operands[0] == "break" &&
                 operands[1] == "3") ||
                (operands.size() == 2 && operands[0] == "off" &&
                 (operands[1] == "figlist" || operands[1] == "tlist")) ||
                (operands.size() == 4 && operands[0] == "off" &&
                 (operands[1] == "efiglist" || operands[1] == "etlist") &&
                 operands[2] == "0" && operands[3] == "0"));
          operand_words = segment.malformed ? 0 : words.size() - 1;
        } else if (operand_words > words.size() - 1) {
          segment.malformed = true;
          operand_words = words.size() - 1;
        }
        const auto operand_end = operand_words == 0 ? segment.opcode_range.end
                                                    : words[operand_words].end;
        segment.operand_range = {segment.opcode_range.end, operand_end};
        segment.payload_range = {operand_end, source.output_end};
      }
    }
    const auto word_range =
        decoded_byte_range_to_word_range(assembled, segment.complete);
    segment.source_tokens = source_tokens_intersecting_output(
        assembled, word_range.begin, word_range.end);
    result.push_back(std::move(segment));
  }
  promote_corroborated_topic_start(text, result);
  return result;
}

bool verify_control_segments(const AssembledLogicalRecord &assembled,
                             const std::vector<ControlSegmentIR> &segments,
                             std::string *error) {
  const auto fail = [&](const std::string &message) {
    if (error != nullptr)
      *error = message;
    return false;
  };
  std::size_t previous_end = 0;
  const auto output_size = token_words_to_ascii(assembled.words).size();
  for (std::size_t index = 0; index < segments.size(); ++index) {
    const auto &segment = segments[index];
    if (segment.segment_index != index)
      return fail("control segment ordinal is not contiguous");
    if (segment.complete.begin >= segment.complete.end ||
        segment.complete.end > output_size ||
        segment.complete.begin < previous_end) {
      return fail("control segment " + std::to_string(index) + " range [" +
                  std::to_string(segment.complete.begin) + "," +
                  std::to_string(segment.complete.end) + ") follows end " +
                  std::to_string(previous_end));
    }
    if (segment.opcode_range.begin < segment.complete.begin ||
        segment.opcode_range.end > segment.operand_range.begin ||
        segment.operand_range.end != segment.payload_range.begin ||
        segment.payload_range.end != segment.complete.end)
      return fail("control segment internal ranges do not partition its tail");
    if (!std::is_sorted(segment.source_tokens.begin(),
                        segment.source_tokens.end()) ||
        std::any_of(
            segment.source_tokens.begin(), segment.source_tokens.end(),
            [&](const auto token) { return token >= assembled.tokens.size(); }))
      return fail("control segment source-token provenance is invalid");
    previous_end = segment.complete.end;
  }
  if (error != nullptr)
    error->clear();
  return true;
}

std::string format_control_segment_ir(const ControlSegmentIR &segment) {
  std::ostringstream out;
  out << "record=" << segment.logical_record
      << " segment=" << segment.segment_index
      << " opcode=" << (segment.opcode.empty() ? "<text>" : segment.opcode)
      << " whole=[" << segment.complete.begin << ',' << segment.complete.end
      << ") operands=[" << segment.operand_range.begin << ','
      << segment.operand_range.end << ") payload=["
      << segment.payload_range.begin << ',' << segment.payload_range.end << ')';
  if (segment.malformed)
    out << " malformed";
  return out.str();
}

} // namespace geist::detail
