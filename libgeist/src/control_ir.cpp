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
};

std::vector<WordSpan> words_in(const std::string &text, std::size_t begin,
                               std::size_t end) {
  std::vector<WordSpan> words;
  auto cursor = begin;
  while (cursor < end) {
    while (cursor < end &&
           std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
      ++cursor;
    }
    if (cursor == end)
      break;
    const auto word_begin = cursor;
    while (cursor < end &&
           std::isspace(static_cast<unsigned char>(text[cursor])) == 0) {
      ++cursor;
    }
    words.push_back({word_begin, cursor});
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
  // continuation record is prose, not a topic boundary.
  if (opcode.size() > 2 && opcode.rfind("sh", 0) == 0 &&
      std::isdigit(static_cast<unsigned char>(opcode[2])) != 0)
    return BookControlKind::topic_start;
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
    const auto width = token_words_to_ascii({assembled.words[word]}).size();
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
    byte += token_words_to_ascii({assembled.words[word]}).size();
  }
  return result;
}

std::vector<ControlSegmentIR>
decode_control_segments(std::uint32_t logical_record,
                        const AssembledLogicalRecord &assembled) {
  const auto text = token_words_to_ascii(assembled.words);
  const auto decoded = split_decoded_markup_segment_spans(text);
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
    const auto words = words_in(text, begin, source.output_end);
    if (words.empty()) {
      segment.kind = BookControlKind::text;
      segment.opcode_range = {begin, begin};
      segment.operand_range = {begin, begin};
      segment.payload_range = {begin, source.output_end};
    } else {
      segment.opcode =
          text.substr(words[0].begin, words[0].end - words[0].begin);
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
