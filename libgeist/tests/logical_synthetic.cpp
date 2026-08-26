#include "geist/detail/internal.hpp"

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
    require(span.output_begin <= previous_end,
            "token span lost an assembler-owned boundary adjustment");
    previous_end = span.output_end;
  }
  for (std::size_t index = 0; index < assembled.sources.size(); ++index) {
    const auto& source = assembled.sources[index];
    require(source.token_index < tokens.size(),
            "mapped source token is outside input");
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

} // namespace

int main() {
  const std::vector<std::vector<TokenWords>> cases = {
      {},
      {{}},
      {{2}},
      {{2, 'a', 'b'}},
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

  return 0;
}
