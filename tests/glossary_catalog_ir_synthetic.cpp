#include "geist/detail/ir/glossary_catalog_ir.hpp"
#include "test_failures.hpp"
#include "geist/detail/core/internal.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    geist_test::record_failure();
    return;
  }
}


std::optional<geist::detail::GlossaryCatalogIR>
extract(const geist::detail::LogicalDecodeContext &context, std::uint32_t first,
        std::uint32_t end, std::string *error = nullptr) {
  const auto sources =
      geist::detail::decode_logical_record_sources(context, first, end);
  const auto layout = geist::detail::extract_layout_ir(sources);
  std::string inner;
  const auto ownership =
      geist::detail::build_verified_ownership_ir(sources, layout, &inner);
  if (!ownership) {
    if (error != nullptr)
      *error = "source layout/ownership is not canonical: " + inner;
    return std::nullopt;
  }
  return geist::detail::extract_glossary_catalog_ir(sources, layout, *ownership,
                                                    error);
}

} // namespace

int main() {
  {
    geist::detail::DecodedLogicalRecordSource record;
    record.logical_record = 9;
    record.tokens = {{'x'},
                     {' ', ' ', ' '},
                     {3, 'W', 'h', 'a', 't', '?'},
                     {'?', '?', '?'},
                     {3, 'N', 'e', 'x', 't'}};
    record.assembled =
        geist::detail::assemble_logical_record_with_sources(record.tokens);
    geist::detail::PhysicalRowIR row;
    row.logical_record = 9;
    row.token_begin = 0;
    row.token_end = record.tokens.size();
    std::vector<geist::detail::GlossaryCatalogCellIR> cells;
    const auto add_token = [&](std::size_t token,
                               geist::detail::SourceDisposition disposition) {
      const auto first = record.tokens[token].front() < 4 ? 1u : 0u;
      for (auto word = first; word < record.tokens[token].size(); ++word)
        cells.push_back(
            {9, token, word, record.tokens[token][word], disposition, 1, 0});
    };
    add_token(0, geist::detail::SourceDisposition::marker_slot);
    add_token(1, geist::detail::SourceDisposition::layout_origin);
    add_token(2, geist::detail::SourceDisposition::visible_content);
    add_token(3, geist::detail::SourceDisposition::layout_padding);
    add_token(4, geist::detail::SourceDisposition::visible_content);
    std::string projection_error;
    const auto projected = geist::detail::project_glossary_semantic_row_text(
        record, row, cells, &projection_error);
    require(projected && *projected == "What? Next",
            "exact row projection removed a lexical question mark or retained "
            "question padding");

    auto relabeled = cells;
    for (auto &cell : relabeled)
      if (cell.token_index == 3)
        cell.disposition = geist::detail::SourceDisposition::visible_content;
    const auto padding_made_visible =
        geist::detail::project_glossary_semantic_row_text(record, row,
                                                          relabeled);
    require(padding_made_visible &&
                padding_made_visible->find("???") != std::string::npos,
            "row projection ignored exact padding-token disposition");

    relabeled = cells;
    const auto lexical_question =
        std::find_if(relabeled.begin(), relabeled.end(), [](const auto &cell) {
          return cell.token_index == 2 && cell.word == '?';
        });
    require(lexical_question != relabeled.end(),
            "synthetic lexical question cell is absent");
    lexical_question->disposition =
        geist::detail::SourceDisposition::layout_padding;
    require(!geist::detail::project_glossary_semantic_row_text(record, row,
                                                               relabeled),
            "row projection admitted mixed token dispositions");
  }

  std::cout << "glossary catalog IR synthetic tests passed\n";
  return 0;
}
