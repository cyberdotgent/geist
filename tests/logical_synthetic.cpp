// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using geist::detail::AssembledLogicalRecord;
using geist::detail::LogicalWordSourceKind;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << "\n";
    geist_test::record_failure();
    return;
  }
}

TokenWords legacy_assemble(const std::vector<TokenWords> &tokens) {
  TokenWords output;
  std::uint16_t spacing_control = 2;
  const auto remove_pending_space = [&]() {
    if (!output.empty() && output.back() == ' ') {
      output.pop_back();
    }
  };

  for (const auto &token : tokens) {
    auto words = token;
    spacing_control = words.empty() ? 3 : words.front();
    if (!words.empty() && words.front() < 4) {
      words.erase(words.begin());
      if (!output.empty()) {
        if (spacing_control == 1) {
          remove_pending_space();
          if (words.empty()) {
            spacing_control = 2;
          }
        } else if (spacing_control == 0) {
          remove_pending_space();
          spacing_control = 2;
        }
      }
    }
    output.insert(output.end(), words.begin(), words.end());
    if (!words.empty() && words.back() == ' ') {
      spacing_control = 2;
    }
    if (spacing_control != 2) {
      output.push_back(' ');
    }
  }
  if (output.size() > 1 && spacing_control != 2) {
    output.pop_back();
  }
  if (!output.empty() && output.front() != ' ' && output.front() != 'S') {
    for (auto &word : output) {
      if (word == ' ' || word == '=' || word == 0) {
        break;
      }
      word = geist::detail::map_token_word_to_upper_ascii(word);
    }
  }
  return output;
}

void verify_map(const std::vector<TokenWords> &tokens,
                const AssembledLogicalRecord &assembled) {
  require(assembled.sources.size() == assembled.words.size(),
          "assembly source map has the wrong size");
  require(assembled.tokens.size() == tokens.size(),
          "assembly token spans have the wrong size");
  std::size_t previous_end = 0;
  for (std::size_t index = 0; index < assembled.tokens.size(); ++index) {
    const auto &span = assembled.tokens[index];
    require(span.token_index == index, "token span has the wrong index");
    require(span.output_begin <= span.output_end &&
                span.output_end <= assembled.words.size(),
            "token span is outside assembled output");
    require(span.output_begin == previous_end,
            "token spans are not contiguous after boundary adjustment");
    previous_end = span.output_end;
  }
  require(previous_end == assembled.words.size(),
          "token spans do not contain the complete assembled record");
  for (std::size_t index = 0; index < assembled.sources.size(); ++index) {
    const auto &source = assembled.sources[index];
    require(source.token_index < tokens.size(),
            "mapped source token is outside input");
    const auto &span = assembled.tokens[source.token_index];
    require(index >= span.output_begin && index < span.output_end,
            "mapped source word is outside its token span");
    const auto expected_has_control = !tokens[source.token_index].empty() &&
                                      tokens[source.token_index].front() < 4;
    require(source.has_control == expected_has_control &&
                span.has_control == expected_has_control,
            "mapped source has incorrect control-prefix metadata");
    const auto expected_spacing = expected_has_control
                                      ? tokens[source.token_index].front()
                                      : std::uint16_t{3};
    require(source.spacing_control == expected_spacing &&
                span.spacing_control == expected_spacing,
            "mapped source has incorrect effective spacing metadata");
    if (source.kind == LogicalWordSourceKind::inserted_space) {
      require(assembled.words[index] == ' ',
              "inserted source does not map to a space");
      continue;
    }
    require(source.word_index < tokens[source.token_index].size(),
            "mapped source word is outside input token");
    const auto original = tokens[source.token_index][source.word_index];
    const auto rendered = assembled.words[index];
    require(rendered == original ||
                rendered ==
                    geist::detail::map_token_word_to_upper_ascii(original),
            "mapped token word changed outside capitalization");
  }
}

void verify_segment_spans(const std::string &record) {
  const auto spans = geist::detail::split_decoded_markup_segment_spans(record);
  std::vector<std::string> projected;
  for (const auto &span : spans) {
    require(span.output_begin < span.output_end &&
                span.output_end <= record.size(),
            "decoded segment span is outside its record");
    require(
        record.substr(span.output_begin, span.output_end - span.output_begin)
                    .find(span.text) != std::string::npos ||
            span.text.find('?') == std::string::npos,
        "decoded segment text lost its source range");
    projected.push_back(span.text);
  }
  // The legacy API is now a value-only view of the span-preserving splitter.
  require(projected == geist::detail::split_decoded_markup_segments(record),
          "span splitter differs from the legacy segment splitter");
}

void verify_token_ir_contract() {
  geist::detail::LogicalRecordIR record;
  record.logical_record = 7;
  record.payload_range = {100, 103};
  record.tokens = {
      {0, {0x41, 1}, {3, 'a'}, {100, 101}, true, 3},
      {1, {0x8042, 2}, {'b'}, {101, 103}, false, 3},
  };
  std::string error;
  require(geist::detail::verify_token_ir(record, &error),
          "valid token IR failed verification: " + error);
  require(geist::detail::project_token_words(record) ==
              std::vector<TokenWords>({{3, 'a'}, {'b'}}),
          "token-word compatibility projection is incorrect");
  require(geist::detail::project_encoded_tokens(record) ==
              std::vector<geist::detail::EncodedLogicalToken>(
                  {{0x41, 1}, {0x8042, 2}}),
          "encoded-token compatibility projection is incorrect");

  for (auto malformed : {
           [&] {
             auto value = record;
             value.tokens[1].token_index = 2;
             return value;
           }(),
           [&] {
             auto value = record;
             value.tokens[1].byte_range.begin = 102;
             return value;
           }(),
           [&] {
             auto value = record;
             value.tokens[0].spacing_control = 2;
             return value;
           }(),
           [&] {
             auto value = record;
             value.payload_range.end = 104;
             return value;
           }(),
       }) {
    require(!geist::detail::verify_token_ir(malformed, &error) &&
                !error.empty(),
            "malformed token IR did not fail with a diagnostic");
  }
}

void verify_control_ir_contract() {
  const std::vector<TokenWords> tokens = {
      {2,   'c', 'f', 'o', 'n', 't', ' ', '3', ' ', '4', ' ', 'C', ' ',
       '8', ' ', '2', ' ', 'C', ' ', '8', '2', '4', '0', ' ', 'C', 'o',
       'n', 'c', 'e', 'n', 't', 'r', 'a', 't', 'o', 'r', '?'},
      {2,   'c', 's', 'e', 'l', 'e', 'c', 't', ' ', '2', '9', ' ', '3', '7',
       ' ', 'H', 'D', 'R', ' ', 'v', 'i', 's', 'i', 'b', 'l', 'e', ','},
      {2, 'S', 'T', ' ', 'T', 'i', 't', 'l', 'e', ' ', 'b', 'o', 'd', 'y'},
      {2, '?', 'S', 'R', 'E', 'T', 'B', 'L', ','},
  };
  const auto assembled =
      geist::detail::assemble_logical_record_with_sources(tokens);
  const auto text = geist::detail::token_words_to_ascii(assembled.words);
  const auto segments = geist::detail::decode_control_segments(12, assembled);
  std::string error;
  require(geist::detail::verify_control_segments(assembled, segments, &error),
          "valid control IR failed verification: " + error);
  require(segments.size() == 4 &&
              segments[0].kind == geist::detail::BookControlKind::font &&
              segments[1].kind == geist::detail::BookControlKind::select &&
              segments[2].kind == geist::detail::BookControlKind::title &&
              segments[3].kind == geist::detail::BookControlKind::table_end,
          "typed control IR classified known controls incorrectly");

  const auto lexical_kind = [](geist::detail::TokenWords words) {
    const auto lexical = geist::detail::assemble_logical_record_with_sources(
        {{std::move(words)}});
    const auto decoded = geist::detail::decode_control_segments(13, lexical);
    return decoded.size() == 1 ? decoded.front().kind
                               : geist::detail::BookControlKind::unknown;
  };
  require(lexical_kind({2, 's', 'h', 'u', 't', 'd', 'o', 'w', 'n', ' ', 'h',
                        'a', 's', ' ', 'b', 'e', 'e', 'n'}) ==
                  geist::detail::BookControlKind::text &&
              lexical_kind({2, 'C', 'o', 'n', 's', 'e', 'q', 'u', 'e', 'n', 't',
                            'l', 'y', ',', ' ', 't', 'h', 'e'}) ==
                  geist::detail::BookControlKind::text &&
              lexical_kind({2, 'S', 'R', ',', ' ', 'T', 'P', ',', ' ', 'a', 'n',
                            'd'}) == geist::detail::BookControlKind::text,
          "lexical SH/C/SR prefixes were mistaken for control opcodes");
  const auto slice = [&](const geist::detail::OutputRangeIR &range) {
    return geist::detail::trim_ascii(
        text.substr(range.begin, range.end - range.begin));
  };
  const auto font_operands = slice(segments[0].operand_range);
  const auto font_payload = slice(segments[0].payload_range);
  require(font_operands == "3 4 C 8 2 C" && font_payload == "8240 Concentrator",
          "digit-leading CFONT split is incorrect: operands='" + font_operands +
              "' payload='" + font_payload + "'");
  require(slice(segments[1].operand_range) == "29 37 HDR" &&
              slice(segments[1].payload_range) == "visible",
          "CSELECT operand/payload ranges are incorrect");
  require(slice(segments[2].operand_range).empty() &&
              slice(segments[2].payload_range) == "Title body",
          "ST payload range is incorrect");
  require(!segments[0].source_tokens.empty() &&
              geist::detail::ascii_lower(
                  geist::detail::format_control_segment_ir(segments[0]))
                      .find("opcode=cfont") != std::string::npos,
          "control IR lost provenance or its stable diagnostic");

  auto malformed = segments;
  malformed[1].payload_range.begin++;
  require(
      !geist::detail::verify_control_segments(assembled, malformed, &error) &&
          !error.empty(),
      "control IR gap did not fail verification");

  const auto spacing = geist::detail::assemble_logical_record_with_sources(
      {{{2, 'c', '.', 's', 'p', ' ', '3', 'p', ' ', 'p', ' ', 'c'}}});
  const auto spacing_segments =
      geist::detail::decode_control_segments(13, spacing);
  const auto spacing_text = geist::detail::token_words_to_ascii(spacing.words);
  const auto directive = geist::detail::assemble_logical_record_with_sources(
      {{{2,   'c', 'z', ' ', 'O', 'F', 'F', ' ', 'E', 'F',
         'I', 'G', 'L', 'I', 'S', 'T', ' ', '0', ' ', '0'}}});
  const auto directive_segments =
      geist::detail::decode_control_segments(14, directive);
  const auto directive_text =
      geist::detail::token_words_to_ascii(directive.words);
  const auto exact_slice = [](const std::string &source,
                              const geist::detail::OutputRangeIR &range) {
    return geist::detail::trim_ascii(
        source.substr(range.begin, range.end - range.begin));
  };
  require(
      spacing_segments.size() == 1 &&
          spacing_segments[0].kind == geist::detail::BookControlKind::spacing &&
          !spacing_segments[0].malformed &&
          exact_slice(spacing_text, spacing_segments[0].operand_range) ==
              "3p p c" &&
          spacing_segments[0].payload_range.begin ==
              spacing_segments[0].payload_range.end &&
          directive_segments.size() == 1 &&
          directive_segments[0].kind ==
              geist::detail::BookControlKind::layout_directive &&
          !directive_segments[0].malformed &&
          exact_slice(directive_text, directive_segments[0].operand_range) ==
              "OFF EFIGLIST 0 0" &&
          directive_segments[0].payload_range.begin ==
              directive_segments[0].payload_range.end,
      "generated-list control operand/payload ranges are incorrect");

  const auto malformed_spacing =
      geist::detail::assemble_logical_record_with_sources(
          {{{2, 'c', '.', 's', 'p', ' ', '4', 'p', ' ', 'p', ' ', 'c'}}});
  const auto malformed_directive =
      geist::detail::assemble_logical_record_with_sources(
          {{{2, 'c', 'z', ' ', 'B', 'R', 'E', 'A', 'K', ' ', '4'}}});
  const auto malformed_spacing_segments =
      geist::detail::decode_control_segments(15, malformed_spacing);
  const auto malformed_directive_segments =
      geist::detail::decode_control_segments(16, malformed_directive);
  require(malformed_spacing_segments.size() == 1 &&
              malformed_spacing_segments[0].malformed &&
              malformed_spacing_segments[0].operand_range.begin ==
                  malformed_spacing_segments[0].operand_range.end &&
              malformed_spacing_segments[0].payload_range.begin <
                  malformed_spacing_segments[0].payload_range.end &&
              malformed_directive_segments.size() == 1 &&
              malformed_directive_segments[0].malformed &&
              malformed_directive_segments[0].operand_range.begin ==
                  malformed_directive_segments[0].operand_range.end &&
              malformed_directive_segments[0].payload_range.begin <
                  malformed_directive_segments[0].payload_range.end,
          "unknown generated-list control operands did not fail closed");
}

// A book header record carries `ccopyright=` and `csecurity=` as adjacent
// controls, and the value that follows a key in the decoded record belongs to
// that key and no other.  `XWEBDEMO.boo` is the one sample book that leaves
// `ccopyright=` empty and puts its copyright notice after `csecurity=`; its
// own token dictionary holds `csecurity=<a9>IBM` as a single word, which is
// where the two are joined, so the attribution is the book's and not the
// decoder's.  Both orders are pinned here so neither key can drift onto the
// other's value.
void verify_adjacent_copyright_and_security_controls() {
  const TokenWords copyright_notice = {
      0x00a9, 'I', 'B', 'M', ' ', 'C', 'o', 'r', 'p', '.', ' ',
      '1',    '9', '9', '5'};

  const auto header_controls = [&](const TokenWords &copyright_value,
                                   const TokenWords &security_value) {
    const auto keyed = [](const char *key, const TokenWords &value) {
      TokenWords token(key, key + std::char_traits<char>::length(key));
      token.insert(token.end(), value.begin(), value.end());
      return token;
    };
    std::vector<TokenWords> tokens = {
        {1, ','},
        {'c', 't', 'i', 't', 'l', 'e', '=', 'D', 'e', 'm', 'o'},
        {1, ','},
        {'c', 's', 't', 'i', 't', 'l', 'e', '='},
        {1, ','},
        keyed("ccopyright=", copyright_value),
        {' ', ' ', ' '},
        keyed("csecurity=", security_value),
        {1, ','},
        {'c', 'r', 'e', 's', 'm', 'a', 't', '1', '='},
        {1, ','},
        {'c', 'd', 'o', 'c', 'n', 'u', 'm', '=', 'S', 'Y', 'N', 'T', 'H'},
    };
    const auto assembled =
        geist::detail::assemble_logical_record_with_sources(tokens);
    return geist::detail::build_book_properties(
        geist::detail::extract_logical_controls(
            geist::detail::token_words_to_ascii(assembled.words),
            geist::detail::assembled_token_output_offsets(assembled)));
  };

  const auto notice = geist::detail::token_words_to_ascii(copyright_notice);

  const auto security_owned = header_controls({}, copyright_notice);
  require(security_owned.copyright.empty() &&
              security_owned.security == notice,
          "a value written after csecurity= was not attributed to CSECURITY");

  const auto copyright_owned = header_controls(copyright_notice, {});
  require(copyright_owned.copyright == notice &&
              copyright_owned.security.empty(),
          "a value written after ccopyright= was not attributed to CCOPYRIGHT");
}

// One header control ends where the next one's token begins, whatever the
// separator between them renders as.  The books spell that separator three
// ways -- a decoder placeholder (`cversion=1.2 ? csource=...`), a run of
// spaces (`cbldvers=1.3.0  csource=...`, XWEBDEMO), or nothing at all -- and
// none of the three is evidence in itself.  The token boundary underneath all
// of them is, so all three must bound the value identically (issue #80).
void verify_header_control_boundary_separator_spellings() {
  const auto build_version = [](const std::vector<TokenWords>& separator) {
    std::vector<TokenWords> tokens = {
        {1, ','},
        {'c', 'v', 'e', 'r', 's', 'i', 'o', 'n', '=', '1', '.', '2'},
        {1, ','},
        {'c', 'b', 'l', 'd', 'v', 'e', 'r', 's', '=', '1', '.', '3', '.', '0'},
    };
    tokens.insert(tokens.end(), separator.begin(), separator.end());
    tokens.push_back({'c', 's', 'o', 'u', 'r', 'c', 'e', '=', 'S', 'Y', 'N',
                      'T', 'H', 'B', 'K'});
    tokens.push_back({1, ','});
    tokens.push_back({'c', 'd', 'o', 'c', 'n', 'u', 'm', '=', 'S', 'Y', 'N',
                      'T', 'H'});
    const auto assembled =
        geist::detail::assemble_logical_record_with_sources(tokens);
    return geist::detail::build_book_properties(
               geist::detail::extract_logical_controls(
                   geist::detail::token_words_to_ascii(assembled.words),
                   geist::detail::assembled_token_output_offsets(assembled)))
        .build_version;
  };

  // A word the ASCII projection cannot represent, which is what renders as
  // the `?` the old boundary table was spelled with.
  const TokenWords placeholder = {0x2500};
  require(build_version({placeholder}) == "1.3.0",
          "a placeholder-separated csource= was absorbed into CBLDVERS");
  require(build_version({{' ', ' '}}) == "1.3.0",
          "a space-separated csource= was absorbed into CBLDVERS");
  require(build_version({}) == "1.3.0",
          "an unseparated csource= was absorbed into CBLDVERS");
}

// A `?` inside a control's value and a `?` spelling the separator in front of
// the next control's key project to the very same character, so the rendered
// string cannot tell them apart -- and neither can asking whether the run is
// written against the preceding word, because a separator comma is written
// against its word exactly as a title's question mark is.  What does tell them
// apart is the display-line framing: the separator is the *length byte* of the
// next line, and the record decoder has already labelled it
// `TokenFramingRole::line_length` (issue #93).
void verify_question_mark_title_survives_line_length_separator() {
  using geist::detail::TokenFramingRole;

  // `separator` is the token index that stands for the next display line's
  // length byte; every other token is that line's content.
  const auto title = [](const std::vector<TokenWords>& tokens,
                        std::size_t separator, bool frame) {
    const auto assembled =
        geist::detail::assemble_logical_record_with_sources(tokens);
    const auto offsets =
        geist::detail::assembled_token_output_offsets(assembled);
    std::vector<TokenFramingRole> framing;
    if (frame) {
      framing.assign(offsets.size(), TokenFramingRole::line_content);
      framing[separator] = TokenFramingRole::line_length;
    }
    return geist::detail::build_book_properties(
               geist::detail::extract_logical_controls(
                   geist::detail::token_words_to_ascii(assembled.words),
                   offsets, framing))
        .title;
  };

  // `dsnwnj10.boo`: the title's own last word ends in a question mark, and the
  // separator that follows it renders as `(`.
  const std::vector<TokenWords> content_question_mark = {
      {'c', 't', 'i', 't', 'l', 'e', '=', 'W', 'h', 'a', 't', '\'', 's'},
      {'N', 'e', 'w', '?'},
      {'('},
      {'c', 'd', 'o', 'c', 'n', 'u', 'm', '=', 'S', 'Y', 'N', 'T', 'H'},
  };
  require(title(content_question_mark, 2, true) == "What's New?",
          "a title's own trailing question mark was read as a separator");

  // `qbka8202.boo`: the separator is a whole token of undecodable words, and
  // renders as the same question marks.
  const std::vector<TokenWords> separator_question_marks = {
      {'c', 't', 'i', 't', 'l', 'e', '=', 'C', 'o', 'n', 't', 'r', 'o', 'l'},
      {'R', 'e', 'f', 'e', 'r', 'e', 'n', 'c', 'e'},
      {0x250c, 0x2500, '*'},
      {'c', 'd', 'o', 'c', 'n', 'u', 'm', '=', 'S', 'Y', 'N', 'T', 'H'},
  };
  require(title(separator_question_marks, 2, true) == "Control Reference",
          "a placeholder separator was read as part of the title");

  // `b1bw1a00.boo`: the separator is a comma, written hard against the last
  // word of the value with no space between them.
  const std::vector<TokenWords> separator_comma = {
      {'c', 't', 'i', 't', 'l', 'e', '=', 'L', 'P', 'S'},
      {'1', '.', '0'},
      {1, ','},
      {'c', 'd', 'o', 'c', 'n', 'u', 'm', '=', 'S', 'Y', 'N', 'T', 'H'},
  };
  require(title(separator_comma, 2, true) == "LPS 1.0",
          "a separator comma was read as part of the title");

  // A record whose payload does not tile into display lines carries no
  // framing, and the older test -- does the run hold a letter or a digit --
  // still has to bound the value.
  require(title(separator_comma, 2, false) == "LPS 1.0",
          "an unframed record lost its separator-comma boundary");
  require(title(separator_question_marks, 2, false) == "Control Reference",
          "an unframed record lost its placeholder boundary");

  // Nothing says a length byte has to decode to punctuation.  `dsnwnj10.boo`
  // spells one `:H4`, which holds a letter and a digit, so the older test
  // reads it as text and hands `cversion=` the value `1.2 :H4`.  Only the
  // framing can bound this one.
  const std::vector<TokenWords> separator_spells_a_word = {
      {'c', 't', 'i', 't', 'l', 'e', '=', 'W', 'h', 'a', 't', '\'', 's'},
      {'N', 'e', 'w', '?'},
      {':', 'H', '4'},
      {'c', 'd', 'o', 'c', 'n', 'u', 'm', '=', 'S', 'Y', 'N', 'T', 'H'},
  };
  require(title(separator_spells_a_word, 2, true) == "What's New?",
          "a separator that spells a word was read as part of the title");
}

} // namespace

int main() {
  verify_token_ir_contract();
  verify_control_ir_contract();
  verify_adjacent_copyright_and_security_controls();
  verify_header_control_boundary_separator_spellings();
  verify_question_mark_title_survives_line_length_separator();
  for (const auto &record :
       {std::string("  ST title, cfont 3 5 2     text  "),
        std::string("alpha???????????????????? cselect 3 5 target text"),
        std::string("SRMSG 12, cfont 3 2 2   12"),
        std::string(75, ' ') + " cfont 3 4 2 fixed"}) {
    verify_segment_spans(record);
  }
  const std::string exact_record = "  ST title cfont 3 4 2 text  ";
  const auto exact_spans =
      geist::detail::split_decoded_markup_segment_spans(exact_record);
  require(exact_spans.size() == 2 && exact_spans[0].output_begin == 2 &&
              exact_spans[0].output_end == 10 &&
              exact_spans[0].text == "ST title" &&
              exact_spans[1].output_begin == 11 &&
              exact_spans[1].output_end == exact_record.size() - 2 &&
              exact_spans[1].text == "cfont 3 4 2 text",
          "decoded segment half-open offsets are not exact");

  const auto intersection = geist::detail::assemble_logical_record_with_sources(
      {{{'a', 'b'}}, {{3, 'c', 'd'}}, {{3, 'e'}}});
  require(geist::detail::output_spans_intersect(1, 3, 2, 4) &&
              !geist::detail::output_spans_intersect(1, 2, 2, 4),
          "half-open output-span intersection is incorrect");
  const auto intersecting = geist::detail::source_tokens_intersecting_output(
      intersection, intersection.tokens[1].output_begin,
      intersection.tokens[2].output_end);
  require(intersecting == std::vector<std::size_t>({1, 2}),
          "segment/token intersection lost exact token ownership");

  geist::detail::DecodedLogicalRecordSource selector;
  selector.logical_record = 9;
  selector.tokens = {
      {'c', 's', 'e', 'l', 'e', 'c', 't', ' ', '3', ' ', '7', ' ', 'H', 'D',
       'R'},
      {'a', 'c', 't', 'i', 'o', 'n'},
      {' ', ' ', ' '},
      {'C', 'h', 'a', 'p', 't', 'e', 'r'},
  };
  selector.encoded_tokens = {{0x80, 2}, {0x1c, 1}, {0x09, 1}, {0x81, 2}};
  selector.assembled =
      geist::detail::assemble_logical_record_with_sources(selector.tokens);
  const auto refresh_typed_source = [](auto &source) {
    source.ir.logical_record = source.logical_record;
    source.ir.tokens.clear();
    std::uint32_t byte = 0;
    for (std::size_t index = 0; index < source.tokens.size(); ++index) {
      const auto encoded = source.encoded_tokens[index];
      source.ir.tokens.push_back({index,
                                  encoded,
                                  source.tokens[index],
                                  {byte, byte + encoded.width},
                                  false,
                                  3});
      byte += encoded.width;
    }
    source.ir.payload_range = {0, byte};
    geist::detail::assign_display_line_framing(source.ir);
    source.control_segments = geist::detail::decode_control_segments(
        source.logical_record, source.assembled);
  };
  refresh_typed_source(selector);
  const auto selector_record =
      geist::detail::token_words_to_ascii(selector.assembled.words);
  const auto selector_cleaned =
      geist::detail::clean_source_owned_selector_display_markers(
          {selector_record}, {selector});
  require(selector_cleaned.size() == 1 &&
              selector_cleaned[0].find("action") == std::string::npos &&
              selector_cleaned[0].find("Chapter") != std::string::npos,
          "source-owned selector display marker was not removed");
  std::string selector_error;
  const auto selector_ir =
      geist::detail::extract_selector_catalog_ir({selector}, &selector_error);
  require(
      selector_ir && selector_ir->selectors.size() == 1 &&
          selector_ir->selectors.front().target == "HDR" &&
          selector_ir->selectors.front().column == 3 &&
          selector_ir->selectors.front().length == 7 &&
          !selector_ir->selectors.front().source_tokens.empty() &&
          selector_ir->selectors.front().source_byte_ranges.size() ==
              selector_ir->selectors.front().source_tokens.size() &&
          selector_ir->selectors.front().display_marker_slot &&
          selector_ir->selectors.front().display_marker_slot->decoded_text ==
              "action",
      selector_error.empty() ? "selector did not enter typed IR"
                             : selector_error.c_str());
  require(selector_ir && geist::detail::verify_selector_catalog_ir(
                             {selector}, *selector_ir, &selector_error),
          selector_error.empty() ? "selector IR verification failed"
                                 : selector_error.c_str());
  require(selector_ir && geist::detail::format_selector_catalog_ir(*selector_ir)
                                 .find("marker='action'") != std::string::npos,
          "selector IR trace omitted marker provenance");
  if (selector_ir) {
    auto mutated = *selector_ir;
    mutated.selectors.front().length = 6;
    require(!geist::detail::verify_selector_catalog_ir({selector}, mutated),
            "selector IR verifier admitted a mutated display span");
    mutated = *selector_ir;
    mutated.selectors.front().source_byte_ranges.front().end++;
    require(!geist::detail::verify_selector_catalog_ir({selector}, mutated),
            "selector IR verifier admitted mutated byte provenance");
  }
  auto utf8_prefix_selector = selector;
  utf8_prefix_selector.tokens.insert(
      utf8_prefix_selector.tokens.begin(),
      {'S', 'T', ' ', 'n', 'a', 0x00e9, 'v', 'e', ','});
  utf8_prefix_selector.encoded_tokens.insert(
      utf8_prefix_selector.encoded_tokens.begin(), {0x84, 2});
  utf8_prefix_selector.assembled =
      geist::detail::assemble_logical_record_with_sources(
          utf8_prefix_selector.tokens);
  refresh_typed_source(utf8_prefix_selector);
  std::string utf8_error;
  const auto utf8_ir = geist::detail::extract_selector_catalog_ir(
      {utf8_prefix_selector}, &utf8_error);
  require(utf8_ir && utf8_ir->selectors.size() == 1 &&
              utf8_ir->selectors.front().display_marker_slot &&
              utf8_ir->selectors.front().display_marker_slot->token_index == 2,
          "UTF-8 text before CSELECT corrupted word-coordinate provenance: " +
              utf8_error);
  auto signed_selector = selector;
  signed_selector.tokens[0] = {'c', 's', 'e', 'l', 'e', 'c', 't', ' ',
                               '+', '3', ' ', '7', ' ', 'H', 'D', 'R'};
  signed_selector.assembled =
      geist::detail::assemble_logical_record_with_sources(
          signed_selector.tokens);
  refresh_typed_source(signed_selector);
  const auto signed_ir =
      geist::detail::extract_selector_catalog_ir({signed_selector});
  require(signed_ir && signed_ir->selectors.size() == 1 &&
              !signed_ir->selectors.front().canonical_operands &&
              !signed_ir->selectors.front().rejection_reason.empty(),
          "signed selector operand was not retained as rejected typed IR");
  auto dictionary_selector = selector;
  dictionary_selector.encoded_tokens[1].width = 2;
  require(geist::detail::clean_source_owned_selector_display_markers(
              {selector_record}, {dictionary_selector}) ==
              std::vector<std::string>({selector_record}),
          "two-byte selector display word was treated as a marker");
  auto two_byte_origin = selector;
  two_byte_origin.encoded_tokens[2].width = 2;
  require(geist::detail::clean_source_owned_selector_display_markers(
              {selector_record}, {two_byte_origin}) ==
              std::vector<std::string>({selector_record}),
          "two-byte selector origin token established row ownership");
  auto malformed_selector = selector;
  malformed_selector.assembled.tokens.resize(2);
  require(geist::detail::clean_source_owned_selector_display_markers(
              {selector_record}, {malformed_selector}) ==
              std::vector<std::string>({selector_record}),
          "truncated token-span provenance did not fail closed");
  auto prose_record = selector_record;
  prose_record.replace(0, 7, "ordinary");
  require(geist::detail::clean_source_owned_selector_display_markers(
              {prose_record}, {selector}) ==
              std::vector<std::string>({prose_record}),
          "non-selector prose activated selector marker cleanup");
  for (const auto *semantic_target :
       {"LNK", "PICIMAGE", "FTNFTNUNIQ1", "FIGLIST1", "TLIST1"}) {
    auto semantic = selector;
    semantic.tokens[0] = {'c', 's', 'e', 'l', 'e', 'c',
                          't', ' ', '3', ' ', '7', ' '};
    semantic.tokens[0].insert(
        semantic.tokens[0].end(), semantic_target,
        semantic_target + std::char_traits<char>::length(semantic_target));
    semantic.assembled =
        geist::detail::assemble_logical_record_with_sources(semantic.tokens);
    refresh_typed_source(semantic);
    const auto record =
        geist::detail::token_words_to_ascii(semantic.assembled.words);
    require(geist::detail::clean_source_owned_selector_display_markers(
                {record}, {semantic}) == std::vector<std::string>({record}),
            std::string("semantic selector family activated row cleanup: ") +
                semantic_target);
  }
  auto combined_origin = selector;
  combined_origin.tokens[1] = {'a', 'c', 't', 'i', 'o', 'n', ' ', ' ', ' '};
  combined_origin.tokens.erase(combined_origin.tokens.begin() + 2);
  combined_origin.encoded_tokens.erase(combined_origin.encoded_tokens.begin() +
                                       2);
  combined_origin.assembled =
      geist::detail::assemble_logical_record_with_sources(
          combined_origin.tokens);
  refresh_typed_source(combined_origin);
  const auto combined_record =
      geist::detail::token_words_to_ascii(combined_origin.assembled.words);
  require(geist::detail::clean_source_owned_selector_display_markers(
              {combined_record}, {combined_origin}) ==
              std::vector<std::string>({combined_record}),
          "combined marker/origin token activated selector cleanup");
  auto out_of_range = selector;
  out_of_range.tokens[0] = {'c', 's', 'e', 'l', 'e', 'c', 't', ' ',
                            '9', '9', ' ', '7', ' ', 'H', 'D', 'R'};
  out_of_range.assembled =
      geist::detail::assemble_logical_record_with_sources(out_of_range.tokens);
  refresh_typed_source(out_of_range);
  const auto out_of_range_record =
      geist::detail::token_words_to_ascii(out_of_range.assembled.words);
  require(geist::detail::clean_source_owned_selector_display_markers(
              {out_of_range_record}, {out_of_range}) ==
              std::vector<std::string>({out_of_range_record}),
          "out-of-range selector activated source-row cleanup");
  const auto ambiguous_record =
      selector_record + " cselect 3 7 OTHER action    Chapter";
  require(geist::detail::clean_source_owned_selector_display_markers(
              {ambiguous_record}, {selector}) ==
              std::vector<std::string>({ambiguous_record}),
          "ambiguous selector/source pairing did not fail closed");
  auto mismatched_operand_record = selector_record;
  const auto column_at =
      geist::detail::ascii_lower(mismatched_operand_record).find("cselect 3 7");
  require(column_at != std::string::npos,
          "selector fixture omitted its operand prefix");
  mismatched_operand_record.replace(column_at, 11, "cselect 4 7");
  require(geist::detail::clean_source_owned_selector_display_markers(
              {mismatched_operand_record}, {selector}) ==
              std::vector<std::string>({mismatched_operand_record}),
          "same-count selector with mismatched operands did not fail closed");
  require(geist::detail::clean_source_owned_selector_display_markers(
              {selector_record, selector_record}, {selector}) ==
              std::vector<std::string>({selector_record, selector_record}),
          "record/source cardinality mismatch did not fail closed");
  geist::detail::DecodedLogicalRecordSource table_start;
  table_start.logical_record = 8;
  table_start.tokens = {{'S', 'R', 'T', 'B', 'L', 'T', 'E', 'S', 'T'}};
  table_start.encoded_tokens = {{0x82, 2}};
  table_start.assembled =
      geist::detail::assemble_logical_record_with_sources(table_start.tokens);
  refresh_typed_source(table_start);
  const auto table_start_record =
      geist::detail::token_words_to_ascii(table_start.assembled.words);
  require(geist::detail::clean_source_owned_selector_display_markers(
              {table_start_record, selector_record}, {table_start, selector}) ==
              std::vector<std::string>({table_start_record, selector_record}),
          "selector projection crossed active table ownership");
  const std::vector<std::vector<TokenWords>> cases = {
      {},
      {{}},
      {{2}},
      {{2, 'a', 'b'}},
      {{'a', 'b'}},
      {{3, 'a'}, {3, 'b'}},
      {{3, 'a'}, {1, 'b'}},
      {{3, 'a'}, {0, 'b'}},
      {{3, 'a'}, {1}},
      {{2, 'a', ' '}, {3, 'b'}},
      {{2, 'S', 'T', ' '}, {3, 'b'}},
      {{2, 'a', '='}, {3, 'b'}},
      {{2, 0x00e9, ' '}, {3, 'x'}},
      {{2, '?'},
       {2, '-', ' ', ' ', ' ', ' '},
       {2, 'o', 'v', 'o', 'b', 'j', 'p', 'r', 'i', 'n', 't', ' ', '|', ' ', 'h',
        'e', 'a', 'd'}},
  };

  for (const auto &tokens : cases) {
    const auto assembled =
        geist::detail::assemble_logical_record_with_sources(tokens);
    require(assembled.words == legacy_assemble(tokens),
            "mapped assembly differs from the legacy algorithm");
    require(geist::detail::assemble_logical_record(tokens) == assembled.words,
            "compatibility wrapper differs from mapped assembly");
    verify_map(tokens, assembled);
  }

  const auto control_only = geist::detail::assemble_logical_record_with_sources(
      {{2, 'A', ' '}, {1}, {3, 'B'}});
  require(control_only.tokens[1].control_only,
          "control-only token was not retained in provenance");
  require(control_only.tokens[1].has_control &&
              control_only.tokens[1].spacing_control == 1 &&
              control_only.tokens[1].output_begin ==
                  control_only.tokens[1].output_end,
          "control-only token has incorrect span or spacing metadata");

  const auto unprefixed =
      geist::detail::assemble_logical_record_with_sources({{'a', 'b'}});
  require(!unprefixed.tokens.front().has_control &&
              unprefixed.tokens.front().spacing_control == 3 &&
              std::all_of(unprefixed.sources.begin(), unprefixed.sources.end(),
                          [](const auto &source) {
                            return !source.has_control &&
                                   source.spacing_control == 3;
                          }),
          "unprefixed token did not use default spacing metadata");
  require(unprefixed.words == TokenWords({'A', 'B'}),
          "capitalization changed while correcting provenance metadata");
  require(unprefixed.sources[0].word_index == 0 &&
              unprefixed.sources[1].word_index == 1,
          "capitalization changed token-word provenance");

  const auto literal_space =
      geist::detail::assemble_logical_record_with_sources({{2, 'A', ' '}});
  require(literal_space.words.back() == ' ' &&
              literal_space.sources.back().kind ==
                  LogicalWordSourceKind::token_word &&
              literal_space.sources.back().word_index == 2,
          "literal token space was classified as an inserted space");

  return 0;
}
