// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/glossary_entry_styles.hpp"

#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/layout/font_span_ir.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <utility>

namespace geist::detail {
namespace {

using LineKey = std::pair<const DecodedLogicalRecordSource *,
                          const DisplayLineIR *>;

// The reader's test for a drawn text line: the first token after the line's
// length byte opens on a blank.
bool text_line(const DecodedLogicalRecordSource &record,
               const DisplayLineIR &line) {
  const auto first = line.prefix_token + 1;
  return first < line.token_end && first < record.ir.tokens.size() &&
         !record.ir.tokens[first].decoded_words.empty() &&
         record.ir.tokens[first].decoded_words.front() == ' ';
}

const DecodedLogicalRecordSource *
find_record(const std::vector<DecodedLogicalRecordSource> &records,
            std::uint32_t logical_record) {
  const auto found = std::find_if(
      records.begin(), records.end(), [&](const auto &record) {
        return record.logical_record == logical_record;
      });
  return found == records.end() ? nullptr : &*found;
}

// The display line a `CFONT` styles: the first text line after the
// control's own line, in its record or the next record's first.
std::optional<LineKey>
font_target(const std::vector<DecodedLogicalRecordSource> &records,
            const DecodedLogicalRecordSource &record,
            const ControlSegmentIR &segment) {
  if (segment.source_tokens.empty())
    return std::nullopt;
  const auto *own = display_line_of_token(record, segment.source_tokens.front());
  if (own == nullptr)
    return std::nullopt;
  const auto &lines = record.ir.display_lines;
  for (auto index = static_cast<std::size_t>(own - lines.data()) + 1;
       index < lines.size(); ++index)
    if (text_line(record, lines[index]))
      return LineKey{&record, &lines[index]};
  const auto *next = find_record(records, record.logical_record + 1);
  if (next == nullptr || !record_framing_is_decided(next->ir))
    return std::nullopt;
  for (const auto &line : next->ir.display_lines)
    if (text_line(*next, line))
      return LineKey{next, &line};
  return std::nullopt;
}

bool blank_cell(std::uint16_t word) { return word <= ' ' || word == 0x00A0; }

struct LineWord {
  std::size_t begin = 0; // display column
  std::size_t end = 0;
  std::string text;
  // UTF-8 byte offset of every column of the word, plus its end.
  std::vector<std::size_t> offsets;
};

std::vector<LineWord> line_words(const DecodedLogicalRecordSource &record,
                                 const DisplayLineIR &line) {
  const auto columns = display_line_columns(record, line);
  std::vector<LineWord> words;
  for (std::size_t column = 0; column < columns.size();) {
    if (blank_cell(columns[column])) {
      ++column;
      continue;
    }
    LineWord word;
    word.begin = column;
    TokenWords glyphs;
    while (column < columns.size() && !blank_cell(columns[column])) {
      word.offsets.push_back(token_word_ascii_width(columns[column]) +
                             (word.offsets.empty() ? 0 : word.offsets.back()));
      glyphs.push_back(columns[column]);
      ++column;
    }
    word.offsets.insert(word.offsets.begin(), 0);
    word.end = column;
    word.text = token_words_to_ascii(glyphs);
    words.push_back(std::move(word));
  }
  return words;
}

// Byte ranges of the blank-separated words of `text`.
std::vector<std::pair<std::size_t, std::size_t>>
text_words(const std::string &text) {
  std::vector<std::pair<std::size_t, std::size_t>> words;
  for (std::size_t at = 0; at < text.size();) {
    if (text[at] == ' ') {
      ++at;
      continue;
    }
    const auto begin = at;
    while (at < text.size() && text[at] != ' ')
      ++at;
    words.emplace_back(begin, at);
  }
  return words;
}

struct StyledWord {
  std::size_t bytes = 0; // styled prefix of the word, in bytes
  FontStyleIR style = FontStyleIR::unknown;
  DocumentSourceSliceIR source;
};

} // namespace

GlossaryEntryStylesIR
glossary_entry_styles(const std::vector<DecodedLogicalRecordSource> &records,
                      const std::vector<GlossaryStyleSegmentIR> &segments,
                      const std::string &term, const std::string &delimiter,
                      const std::string &prose) {
  // The entry's drawn lines and the spans each `CFONT` places on its line.
  std::vector<LineKey> lines;
  std::map<LineKey, std::vector<std::pair<FontSpanIR, DocumentSourceSliceIR>>>
      spans;
  for (const auto &item : segments) {
    if (item.record == nullptr || item.segment == nullptr)
      return {};
    const auto &record = *item.record;
    const auto &segment = *item.segment;
    if (segment.kind != BookControlKind::font &&
        segment.kind != BookControlKind::text)
      continue;
    if (!record_framing_is_decided(record.ir))
      return {};
    for (const auto token : segment.source_tokens) {
      const auto *line = display_line_of_token(record, token);
      if (line == nullptr || !text_line(record, *line))
        continue;
      const LineKey key{&record, line};
      if (std::find(lines.begin(), lines.end(), key) == lines.end())
        lines.push_back(key);
    }
    if (segment.kind != BookControlKind::font)
      continue;
    const auto font = decode_font_control_spans(record, segment);
    const auto target = font_target(records, record, segment);
    if (!font || !target)
      return {};
    for (const auto &span : font->spans)
      spans[*target].push_back({span, font->operand_source});
  }
  for (const auto &[key, ignored] : spans)
    if (std::find(lines.begin(), lines.end(), key) == lines.end())
      return {};

  // Every drawn word of the entry, in order, with the style a triple proves.
  std::vector<std::string> drawn;
  std::map<std::size_t, StyledWord> styled;
  for (const auto &key : lines) {
    const auto words = line_words(*key.first, *key.second);
    const auto found = spans.find(key);
    if (found != spans.end()) {
      for (const auto &[span, source] : found->second) {
        if (span.length == 0 || span.style == FontStyleIR::unknown ||
            span.style == FontStyleIR::underscore)
          continue;
        const auto end = span.column + span.length;
        auto first = std::find_if(words.begin(), words.end(),
                                  [&](const auto &word) {
                                    return word.begin == span.column;
                                  });
        if (first == words.end())
          return {};
        for (auto word = first; word != words.end() && word->begin < end;
             ++word) {
          auto columns = word->end - word->begin;
          if (end < word->end) {
            // The span closes inside the word: only before the punctuation
            // the word ends with.
            for (auto column = end; column < word->end; ++column) {
              const auto ch = word->text[word->offsets[column - word->begin]];
              if (std::isalnum(static_cast<unsigned char>(ch)) != 0 ||
                  static_cast<unsigned char>(ch) >= 0x80)
                return {};
            }
            columns = end - word->begin;
          }
          const auto index =
              drawn.size() + static_cast<std::size_t>(word - words.begin());
          if (styled.count(index) != 0)
            return {};
          styled[index] = {word->offsets[columns], span.style, source};
        }
        const auto last = std::find_if(words.begin(), words.end(),
                                       [&](const auto &word) {
                                         return word.begin < end &&
                                                end <= word.end;
                                       });
        if (last == words.end())
          return {};
      }
    }
    for (const auto &word : words)
      drawn.push_back(word.text);
  }

  // The first words must spell the term with its delimiter glued to the
  // last one; only then do the styles name the term's words.
  const auto term_words = text_words(term);
  const auto prose_words = text_words(prose);
  if (term_words.empty() || drawn.size() < term_words.size())
    return {};
  for (std::size_t index = 0; index < term_words.size(); ++index) {
    auto expected = term.substr(term_words[index].first,
                                term_words[index].second -
                                    term_words[index].first);
    if (index + 1 == term_words.size())
      expected += delimiter;
    if (drawn[index] != expected)
      return {};
  }
  // The definition's styles stand only where the remaining words spell the
  // definition exactly; otherwise it stays plain (a cz BREAK row, a bullet
  // the catalog composes differently).
  auto definition_proven =
      drawn.size() == term_words.size() + prose_words.size();
  for (std::size_t index = 0; definition_proven && index < prose_words.size();
       ++index)
    definition_proven =
        drawn[term_words.size() + index] ==
        prose.substr(prose_words[index].first,
                     prose_words[index].second - prose_words[index].first);

  GlossaryEntryStylesIR result;
  for (const auto &[index, word] : styled) {
    if (index < term_words.size()) {
      const auto [begin, end] = term_words[index];
      // A style over the delimiter is not the term's alone.
      if (word.bytes > end - begin)
        return {};
      result.term.push_back(
          {begin, begin + word.bytes, word.style, word.source});
      continue;
    }
    if (!definition_proven)
      continue;
    const auto begin = prose_words[index - term_words.size()].first;
    result.definition.push_back(
        {begin, begin + word.bytes, word.style, word.source});
  }
  return result;
}

} // namespace geist::detail
