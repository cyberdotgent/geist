// A picture placed inside a sentence is not a figure (issue #65).
//
// `cselect <column> <length> PIC<n>` names the display columns of the line it
// precedes, and the compiler writes the words `PICTURE <n>` into exactly those
// columns.  Two shapes use the same control:
//
//   cselect 0 10 PIC18        cselect 13 10 PIC17
//   PICTURE 18                Click on the PICTURE 17 button.
//   Figure 7. Demo Picture
//
// The left one is a captioned block figure: once the placeholder columns are
// taken out, the line shows nothing and the region is the picture plus its
// caption.  The right one is a sentence with an image in it: hosted BookServer
// serves `Click on the <a href="picture-17?mode=zoom"><img ...></a> button.`
// (SG24-2047-00 4.1.1, DT 19971218054640), so the region is no figure at all
// and the figure family hands it to the prose family by name.
//
// Everything here is synthetic: the tests build `DecodedLogicalRecordSource`
// values by hand and open no book.

#include "geist/detail/control_ir.hpp"
#include "geist/detail/display_lines.hpp"
#include "geist/detail/figure_block_ir.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/layout_ir.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/selector_ir.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "inline_picture_ir_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

// Appends one display line: its length byte (one byte wide, valued at the
// bytes of the line's content tokens) and then the tokens themselves.  The
// decoder's own walk turns this back into the framing; no test re-derives it.
void line(DecodedLogicalRecordSource &record,
          const std::vector<std::string> &tokens) {
  constexpr std::uint8_t token_width = 2;
  const auto length =
      static_cast<std::uint16_t>(tokens.size() * token_width);
  // The length byte's dictionary spelling is meaningless -- one byte, so one
  // word -- and the Layout IR sees it as the row's width-1 marker slot.
  record.encoded_tokens.push_back({length, 1});
  record.tokens.push_back(words("?"));
  for (const auto &token : tokens) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(0x40 + record.tokens.size()), token_width});
    record.tokens.push_back(words(token));
  }
}

void refresh(DecodedLogicalRecordSource &record) {
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  record.ir.logical_record = record.logical_record;
  record.ir.tokens.clear();
  std::uint32_t byte = 0;
  for (std::size_t token = 0; token < record.tokens.size(); ++token) {
    const auto encoded = record.encoded_tokens[token];
    const auto spacing = !record.tokens[token].empty() &&
                         record.tokens[token].front() < 4;
    record.ir.tokens.push_back(
        {token, encoded, record.tokens[token],
         {byte, static_cast<std::uint32_t>(byte + encoded.width)}, spacing,
         spacing ? record.tokens[token].front() : std::uint16_t{3}});
    byte += encoded.width;
  }
  record.ir.payload_range = {0, byte};
  geist::detail::assign_display_line_framing(record.ir);
  record.control_segments = geist::detail::decode_control_segments(
      record.logical_record, record.assembled, record.encoded_tokens,
      record.ir.display_lines);
}

struct Extracted {
  std::vector<DecodedLogicalRecordSource> records;
  std::optional<geist::detail::FigureBlocksIR> figures;
};

Extracted run(DecodedLogicalRecordSource record,
              const std::set<std::string> &resource_ids) {
  Extracted result;
  refresh(record);
  require(record.ir.display_lines_parse,
          "the synthetic record's display lines did not parse");
  result.records.push_back(std::move(record));
  std::string error;
  const auto selectors =
      geist::detail::extract_selector_catalog_ir(result.records, &error);
  require(selectors.has_value(), "selector extraction failed: " + error);
  if (!selectors) return result;
  const auto layout = geist::detail::extract_layout_ir(result.records);
  const auto ownership = geist::detail::build_verified_ownership_ir(
      result.records, layout, &error);
  require(ownership.has_value(), "ownership verification failed: " + error);
  if (!ownership) return result;
  result.figures = geist::detail::extract_figure_blocks_ir(
      result.records, layout, *ownership, *selectors, resource_ids);
  require(geist::detail::verify_figure_blocks_ir(result.records, layout,
                                                 *ownership, *selectors,
                                                 resource_ids,
                                                 *result.figures, &error),
          "figure blocks did not re-verify: " + error);
  return result;
}

// `cselect 0 10 PIC18` over a line that spells only `PICTURE 18`, with the
// caption beneath it: blanking the named columns leaves nothing, so this is a
// captioned block figure and the family claims it.
void captioned_block_form_is_a_figure() {
  DecodedLogicalRecordSource record;
  record.logical_record = 21;
  line(record, {"cselect", "0", "10", "PIC18"});
  line(record, {"PICTURE", "18"});
  line(record, {"Figure", "7.", "Demo", "Picture"});
  const auto extracted = run(std::move(record), {"18"});
  if (!extracted.figures) return;
  const auto &figures = *extracted.figures;
  require(figures.declined.empty(),
          "the captioned block form was declined: " +
              (figures.declined.empty() ? std::string{}
                                        : figures.declined.front().reason));
  require(figures.blocks.size() == 1,
          "the captioned block form did not become one figure block");
  if (figures.blocks.size() != 1) return;
  const auto &block = figures.blocks.front();
  require(block.body_kind == geist::detail::FigureBodyKindIR::picture &&
              block.target == "18",
          "the block does not carry picture resource 18");
  require(block.placeholder_text == "PICTURE 18",
          "the block did not claim the `PICTURE 18` placeholder words");
  require(block.caption && block.caption->text == "Figure 7. Demo Picture",
          "the block did not claim its caption");
}

// The same control over a line whose other columns carry a sentence: the
// image stands inside the prose, so the family declines by the one name the
// prose family is allowed to walk past.
void mid_sentence_form_is_declined_to_prose() {
  DecodedLogicalRecordSource record;
  record.logical_record = 20;
  line(record, {"cselect", "13", "10", "PIC17"});
  // Columns: `Click on the PICTURE 17 button.`; `PICTURE 17` is [13,23).
  line(record, {"Click", "on", "the", "PICTURE", "17", "button."});
  const auto extracted = run(std::move(record), {"17"});
  if (!extracted.figures) return;
  const auto &figures = *extracted.figures;
  require(figures.blocks.empty(),
          "a picture placed inside a sentence was claimed as a figure");
  require(figures.declined.size() == 1,
          "the mid-sentence form did not decline exactly once");
  if (figures.declined.size() != 1) return;
  require(figures.declined.front().reason ==
              geist::detail::figure_inline_picture_decline_reason(),
          "the mid-sentence form declined as '" +
              figures.declined.front().reason +
              "', not as an inline picture");
}

// Fail closed: when the named columns do not spell the placeholder the model
// has not proven where the image goes, so the region is not handed to prose
// as an inline picture.
void columns_that_do_not_spell_the_placeholder_are_not_inline() {
  DecodedLogicalRecordSource record;
  record.logical_record = 22;
  // Column 13 opens `17`, not `PICTURE 17`.
  line(record, {"cselect", "13", "10", "PIC17"});
  line(record, {"Click", "on", "the", "17", "PICTURE", "button."});
  const auto extracted = run(std::move(record), {"17"});
  if (!extracted.figures) return;
  const auto &figures = *extracted.figures;
  for (const auto &decline : figures.declined)
    require(decline.reason !=
                geist::detail::figure_inline_picture_decline_reason(),
            "a selector whose columns do not spell its placeholder was "
            "called an inline picture");
}

} // namespace

int main() {
  captioned_block_form_is_a_figure();
  mid_sentence_form_is_declined_to_prose();
  columns_that_do_not_spell_the_placeholder_are_not_inline();
  return 0;
}
