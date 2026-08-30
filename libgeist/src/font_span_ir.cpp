#include "geist/detail/font_span_ir.hpp"

#include "geist/detail/internal.hpp"

#include <cctype>

namespace geist::detail {
namespace {

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

bool parse_decimal(const std::string& word, std::size_t& value) {
  if (word.empty() || word.size() > 9) return false;
  value = 0;
  for (const auto ch : word) {
    if (std::isdigit(static_cast<unsigned char>(ch)) == 0) return false;
    value = value * 10 + static_cast<std::size_t>(ch - '0');
  }
  return true;
}

FontStyleIR style_for_code(const std::string& code) {
  if (code == "1") return FontStyleIR::highlight_1;
  if (code == "2") return FontStyleIR::highlight_2;
  if (code == "3") return FontStyleIR::highlight_3;
  if (code == "5") return FontStyleIR::highlight_5;
  if (code == "6") return FontStyleIR::highlight_6;
  if (code == "7") return FontStyleIR::highlight_7;
  if (code == "8") return FontStyleIR::highlight_8;
  if (code == "9") return FontStyleIR::highlight_9;
  if (code.size() != 1) return FontStyleIR::unknown;
  switch (ascii_lower_char(code[0])) {
  case 'c': return FontStyleIR::citation;
  case 'q': return FontStyleIR::keyword_define;
  case 'x':
  case 'e':
  case '4': return FontStyleIR::example_phrase;
  case 'p': return FontStyleIR::keyword;
  case 'v': return FontStyleIR::variable;
  case 'w': return FontStyleIR::warning;
  case 'g': return FontStyleIR::warning_text;
  case 'r':
  case 'h':
  case 'i':
  case 'j':
  case 'k':
  case 'm': return FontStyleIR::bold_phrase;
  case 'l': return FontStyleIR::italic_phrase;
  default: break;
  }
  return FontStyleIR::unknown;
}

} // namespace

FontStyleIR font_style_for_code(const std::string& code) {
  return style_for_code(code);
}

const char* font_style_name(FontStyleIR style) {
  switch (style) {
  case FontStyleIR::highlight_1:
    return "hp1";
  case FontStyleIR::highlight_2:
    return "hp2";
  case FontStyleIR::highlight_3:
    return "hp3";
  case FontStyleIR::highlight_5:
    return "hp5";
  case FontStyleIR::highlight_6:
    return "hp6";
  case FontStyleIR::highlight_7:
    return "hp7";
  case FontStyleIR::highlight_8:
    return "hp8";
  case FontStyleIR::highlight_9:
    return "hp9";
  case FontStyleIR::keyword_define:
    return "pkdef";
  case FontStyleIR::citation:
    return "cit";
  case FontStyleIR::example_phrase:
    return "xph";
  case FontStyleIR::keyword:
    return "pk";
  case FontStyleIR::variable:
    return "pv";
  case FontStyleIR::bold_phrase:
    return "bold";
  case FontStyleIR::italic_phrase:
    return "italic";
  case FontStyleIR::warning:
    return "warning";
  case FontStyleIR::warning_text:
    return "warningtext";
  case FontStyleIR::unknown:
    break;
  }
  return "unknown";
}

std::optional<FontControlSpansIR>
decode_font_control_spans(const DecodedLogicalRecordSource& record,
                          const ControlSegmentIR& segment,
                          std::string* error) {
  if (segment.kind != BookControlKind::font || segment.malformed) {
    fail(error, "segment is not a well-formed font control");
    return std::nullopt;
  }
  const auto text = token_words_to_ascii(record.assembled.words);
  const auto& range = segment.operand_range;
  if (range.begin > range.end || range.end > text.size()) {
    fail(error, "font operand range is outside the decoded record");
    return std::nullopt;
  }
  std::vector<std::string> words;
  for (std::size_t cursor = range.begin; cursor < range.end;) {
    if (std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
      ++cursor;
      continue;
    }
    auto end = cursor;
    while (end < range.end &&
           std::isspace(static_cast<unsigned char>(text[end])) == 0)
      ++end;
    words.push_back(text.substr(cursor, end - cursor));
    cursor = end;
  }
  // The final triple of a control can carry a trailing `,` separator glued to
  // its style code (`cfont 4 4 R,`); it is neither part of the code nor
  // display text. See geist/detail/font_span_ir.hpp for the hosted evidence
  // on ACPZMST1 FRONT_1.1 and GC28-183 2.2.1. A comma anywhere else stays in
  // its word and fails the operand closed.
  if (!words.empty() && words.back().size() > 1 && words.back().back() == ',')
    words.back().pop_back();
  if (words.empty() || words.size() % 3 != 0) {
    fail(error, "font operand is not a sequence of complete triples");
    return std::nullopt;
  }

  FontControlSpansIR result;
  for (std::size_t index = 0; index < words.size(); index += 3) {
    FontSpanIR span;
    if (!parse_decimal(words[index], span.column) ||
        !parse_decimal(words[index + 1], span.length) || span.length == 0) {
      fail(error, "font operand triple has no decimal geometry");
      return std::nullopt;
    }
    span.code = words[index + 2];
    span.style = style_for_code(span.code);
    result.spans.push_back(std::move(span));
  }

  result.operand_source.logical_record = record.logical_record;
  result.operand_source.segment_index = segment.segment_index;
  const auto word_range = decoded_byte_range_to_word_range(record.assembled, range);
  const auto tokens = source_tokens_intersecting_output(
      record.assembled, word_range.begin, word_range.end);
  if (tokens.empty() || tokens.back() >= record.ir.tokens.size()) {
    fail(error, "font operand has no source token provenance");
    return std::nullopt;
  }
  result.operand_source.token_begin = tokens.front();
  result.operand_source.token_end = tokens.back() + 1;
  result.operand_source.byte_begin =
      record.ir.tokens[tokens.front()].byte_range.begin;
  result.operand_source.byte_end = record.ir.tokens[tokens.back()].byte_range.end;
  if (error != nullptr) error->clear();
  return result;
}

} // namespace geist::detail
