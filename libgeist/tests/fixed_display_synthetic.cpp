#include "geist/detail/fixed_display.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

} // namespace

int main() {
  using geist::detail::FixedDisplayFragment;
  using geist::detail::reconstruct_fixed_display_rows;

  // A heading and its first data row share a logical record. The description
  // continues on physical rows in that record and again after the
  // logical-record boundary. A new key in the latter record owns another row.
  const std::vector<FixedDisplayFragment> fragments = {
      {19, 4, 2, 3, "Directory", true},
      {19, 5, 0, 28, "Type of Files", false},
      {19, 9, 1, 3, "/usr/lpp/lnm", true},
      {19, 10, 4, 28, "Root directory", false},
      {19, 11, 0, 28, "of the product", true},
      {20, 0, 3, 28, "subtree", true},
      {20, 2, 0, 3, "/usr/lpp/lnm/gifs", true},
      {20, 3, 0, 28, "GIF files", false},
  };
  const auto rows = reconstruct_fixed_display_rows(fragments);
  require(rows.size() == 5, "cross-record fixed rows were not reconstructed");
  require(rows[0].text.substr(3, 9) == "Directory" &&
              rows[0].text.substr(28, 13) == "Type of Files",
          "heading cells lost their display columns");
  require(rows[1].text.substr(3, 12) == "/usr/lpp/lnm" &&
              rows[1].text.substr(28, 14) == "Root directory",
          "key/value cells lost their row ownership");
  require(rows[2].text.substr(28) == "of the product" &&
              rows[3].text.substr(28) == "subtree",
          "value-only physical continuations lost their columns");
  require(rows[1].sources[28].logical_record == 19 &&
              rows[1].sources[28].token_index == 10 &&
              rows[1].sources[28].word_index == 4,
          "value source provenance was not retained");
  require(rows[4].sources[3].logical_record == 20 &&
              rows[4].sources[28].token_index == 3,
          "new-row source provenance was not retained");
  require(!rows[0].source_present[0] && rows[0].source_present[3],
          "synthetic column padding was classified as source text");

  const auto sparse = reconstruct_fixed_display_rows(
      {{7, 1, 0, 18, "Description", false}});
  require(sparse.size() == 1 && sparse[0].text.size() == 29 &&
              sparse[0].text.substr(18) == "Description",
          "a leading continuation fragment did not create a sparse row");

  auto overlap_rejected = false;
  try {
    (void)reconstruct_fixed_display_rows(
        {{1, 0, 0, 3, "alpha", true},
         {1, 1, 0, 5, "omega", false}});
  } catch (const std::invalid_argument&) {
    overlap_rejected = true;
  }
  require(overlap_rejected, "conflicting source fragments were accepted");

  using geist::detail::FixedFormPhysicalRow;
  using geist::detail::FixedFormPhysicalRowKind;
  const auto form_rows = geist::detail::aggregate_fixed_form_rows(
      {
          {FixedFormPhysicalRowKind::border, {}},
          {FixedFormPhysicalRowKind::row_start,
           {"Which mode was AIX NetView/6000 operating", "[ ] Read"}},
          {FixedFormPhysicalRowKind::continuation,
           {"in at the time of the problem?", "[ ] Read-Write"}},
          {FixedFormPhysicalRowKind::spacer, {}},
          {FixedFormPhysicalRowKind::row_start,
           {"Number of objects in the OVw database", {}}},
          {FixedFormPhysicalRowKind::continuation,
           {"(use the command ovobjprint | head)", {}}},
          {FixedFormPhysicalRowKind::row_start,
           {"Number of objects to hold in ovwdb", {}}},
          {FixedFormPhysicalRowKind::continuation, {"cache", {}}},
          {FixedFormPhysicalRowKind::row_start,
           {"Number of seconds between storing", {}}},
          {FixedFormPhysicalRowKind::continuation,
           {"data to the GTMD database", {}}},
          {FixedFormPhysicalRowKind::border, {}},
      },
      2);
  require(form_rows.size() == 4,
          "physical form ownership did not produce four logical rows");
  require(form_rows[0][0] ==
              "Which mode was AIX NetView/6000 operating<br>in at the time of "
              "the problem?" &&
              form_rows[0][1] == "[ ] Read<br>[ ] Read-Write",
          "simultaneous field/value continuations changed columns");
  require(form_rows[1][0] ==
              "Number of objects in the OVw database<br>(use the command "
              "ovobjprint | head)",
          "literal form pipe was treated as grid structure");
  require(form_rows[2][0] ==
              "Number of objects to hold in ovwdb<br>cache" &&
              form_rows[3][0] ==
                  "Number of seconds between storing<br>data to the GTMD "
                  "database",
          "single-column form continuations lost row ownership");

  const auto leading_continuation =
      geist::detail::aggregate_fixed_form_rows(
          {{FixedFormPhysicalRowKind::continuation, {{}, "continued"}}}, 2);
  require(leading_continuation.size() == 1 &&
              leading_continuation[0][1] == "continued",
          "leading physical continuation was discarded");

  auto bad_form_grid_rejected = false;
  try {
    (void)geist::detail::aggregate_fixed_form_rows(
        {{FixedFormPhysicalRowKind::row_start, {"only one"}}}, 2);
  } catch (const std::invalid_argument&) {
    bad_form_grid_rejected = true;
  }
  require(bad_form_grid_rejected,
          "mismatched fixed form geometry was accepted");

  auto punctuation = geist::detail::assemble_fixed_display_row(
      {"Value (positive)    )    next row"});
  std::vector<bool> styled(punctuation.text.size(), false);
  const auto lexical_close = punctuation.text.find(')');
  const auto marker_close = punctuation.text.find(')', lexical_close + 1);
  styled[lexical_close] = true;
  geist::detail::blank_fixed_display_marker_fields(punctuation, false,
                                                   styled);
  require(punctuation.text[lexical_close] == ')',
          "CFONT-owned lexical parenthesis was blanked");
  require(punctuation.text[marker_close] == ' ',
          "unstyled structural parenthesis was preserved");

  return 0;
}
