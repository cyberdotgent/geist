#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using geist::detail::AssembledLogicalRecord;
using geist::detail::LogicalWordSourceKind;
using geist::detail::TokenWords;

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

TokenWords legacy_assemble(const std::vector<TokenWords>& tokens) {
  TokenWords output;
  std::uint16_t spacing_control = 2;
  const auto remove_pending_space = [&]() {
    if (!output.empty() && output.back() == ' ') {
      output.pop_back();
    }
  };

  for (const auto& token : tokens) {
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
    for (auto& word : output) {
      if (word == ' ' || word == '=' || word == 0) {
        break;
      }
      word = geist::detail::map_token_word_to_upper_ascii(word);
    }
  }
  return output;
}

void verify_map(const std::vector<TokenWords>& tokens,
                const AssembledLogicalRecord& assembled) {
  require(assembled.sources.size() == assembled.words.size(),
          "assembly source map has the wrong size");
  require(assembled.tokens.size() == tokens.size(),
          "assembly token spans have the wrong size");
  std::size_t previous_end = 0;
  for (std::size_t index = 0; index < assembled.tokens.size(); ++index) {
    const auto& span = assembled.tokens[index];
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
    const auto& source = assembled.sources[index];
    require(source.token_index < tokens.size(),
            "mapped source token is outside input");
    const auto& span = assembled.tokens[source.token_index];
    require(index >= span.output_begin && index < span.output_end,
            "mapped source word is outside its token span");
    const auto expected_has_control =
        !tokens[source.token_index].empty() &&
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

void verify_segment_spans(const std::string& record) {
  const auto spans =
      geist::detail::split_decoded_markup_segment_spans(record);
  std::vector<std::string> projected;
  for (const auto& span : spans) {
    require(span.output_begin < span.output_end &&
                span.output_end <= record.size(),
            "decoded segment span is outside its record");
    require(record.substr(span.output_begin,
                          span.output_end - span.output_begin)
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

} // namespace

int main() {
  verify_token_ir_contract();
  for (const auto& record : {
           std::string("  ST title, cfont 3 5 2     text  "),
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
      {{{'a','b'}}, {{3, 'c', 'd'}}, {{3, 'e'}}});
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
      {'c','s','e','l','e','c','t',' ','3',' ','7',' ','H','D','R'},
      {'a','c','t','i','o','n'},
      {' ',' ',' '},
      {'C','h','a','p','t','e','r'},
  };
  selector.encoded_tokens = {{0x80, 2}, {0x1c, 1}, {0x09, 1}, {0x81, 2}};
  selector.assembled =
      geist::detail::assemble_logical_record_with_sources(selector.tokens);
  const auto selector_record = geist::detail::token_words_to_ascii(
      selector.assembled.words);
  const auto selector_cleaned =
      geist::detail::clean_source_owned_selector_display_markers(
          {selector_record}, {selector});
  require(selector_cleaned.size() == 1 &&
              selector_cleaned[0].find("action") == std::string::npos &&
              selector_cleaned[0].find("Chapter") != std::string::npos,
          "source-owned selector display marker was not removed");
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
  for (const auto* semantic_target :
       {"LNK", "PICIMAGE", "FTNFTNUNIQ1", "FIGLIST1", "TLIST1"}) {
    auto semantic = selector;
    semantic.tokens[0] = {'c','s','e','l','e','c','t',' ','3',' ','7',' '};
    semantic.tokens[0].insert(semantic.tokens[0].end(), semantic_target,
                              semantic_target + std::char_traits<char>::length(
                                                    semantic_target));
    semantic.assembled =
        geist::detail::assemble_logical_record_with_sources(semantic.tokens);
    const auto record =
        geist::detail::token_words_to_ascii(semantic.assembled.words);
    require(geist::detail::clean_source_owned_selector_display_markers(
                {record}, {semantic}) == std::vector<std::string>({record}),
            std::string("semantic selector family activated row cleanup: ") +
                semantic_target);
  }
  auto combined_origin = selector;
  combined_origin.tokens[1] = {'a','c','t','i','o','n',' ',' ',' '};
  combined_origin.tokens.erase(combined_origin.tokens.begin() + 2);
  combined_origin.encoded_tokens.erase(
      combined_origin.encoded_tokens.begin() + 2);
  combined_origin.assembled =
      geist::detail::assemble_logical_record_with_sources(
          combined_origin.tokens);
  const auto combined_record = geist::detail::token_words_to_ascii(
      combined_origin.assembled.words);
  require(geist::detail::clean_source_owned_selector_display_markers(
              {combined_record}, {combined_origin}) ==
              std::vector<std::string>({combined_record}),
          "combined marker/origin token activated selector cleanup");
  auto out_of_range = selector;
  out_of_range.tokens[0] = {
      'c','s','e','l','e','c','t',' ','9','9',' ','7',' ','H','D','R'};
  out_of_range.assembled =
      geist::detail::assemble_logical_record_with_sources(out_of_range.tokens);
  const auto out_of_range_record = geist::detail::token_words_to_ascii(
      out_of_range.assembled.words);
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
  geist::detail::DecodedLogicalRecordSource table_start;
  table_start.logical_record = 8;
  table_start.tokens = {{'S','R','T','B','L','T','E','S','T'}};
  table_start.encoded_tokens = {{0x82, 2}};
  table_start.assembled =
      geist::detail::assemble_logical_record_with_sources(table_start.tokens);
  const auto table_start_record = geist::detail::token_words_to_ascii(
      table_start.assembled.words);
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
      {{2, '?'}, {2, '-', ' ', ' ', ' ', ' '},
       {2, 'o', 'v', 'o', 'b', 'j', 'p', 'r', 'i', 'n', 't', ' ', '|',
        ' ', 'h', 'e', 'a', 'd'}},
  };

  for (const auto& tokens : cases) {
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
              std::all_of(unprefixed.sources.begin(),
                          unprefixed.sources.end(), [](const auto& source) {
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
