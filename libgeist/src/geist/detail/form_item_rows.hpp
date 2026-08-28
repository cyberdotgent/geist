#pragma once

#include "geist/detail/layout_ir.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;

// Typed row evidence for `__` ballot checklists whose flattened `ST` body is
// continued by CFONT display runs while the topic still renders through the
// legacy GML path (SC31-711 `2.4.7`, LR78). The `ST` segment carries the
// title and the first three ballot rows; segment 9 (`cfont 10 7 X`) carries
// the continuation row of the third item (`/usr/OV`, native origin 3,
// padding 10) and segment 10 (`cfont 44 4 X`) carries the fourth ballot row
// (`__     Amount of free space available in /tmp`, native origin 18,
// padding 3). The hosted reader draws both as part of the checklist, with the
// CFONT spans as `<tt>` phrases at display columns 10 and 44.
//
// A CFONT span column indexes the row's display line after the native origin
// is removed (the reader's line model), so `column`/`length` project onto the
// row text directly. Projection fails closed unless every span of the run
// lands on whole words of one of its rows and resolves to a known style.
struct FormItemFontRowIR {
  DisplayRunId run = 0;
  std::size_t row_index = 0;
  // The row's first visible word is the `__` ballot glyph followed by slot
  // padding: it starts a new checklist item. Otherwise the row continues the
  // previous item.
  bool starts_item = false;
  // Collapsed visible text without the ballot glyph.
  std::string plain_text;
  // The same text with the row's CFONT spans projected as inline GML phrases
  // (`:xph.`/`:hp1.`..`:hp3.`).
  std::string gml_text;
};

// Rows of every CFONT display run that follows the topic's leading `ST`
// title (or text) run.
// Returns nullopt (fail closed) when the layout does not verify, when a run
// after the first is not a CFONT run, or when a span cannot be conserved.
// Returns an empty list when the topic has no CFONT run after its text run.
std::optional<std::vector<FormItemFontRowIR>> extract_form_item_font_rows_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::string* error = nullptr);

// Visible text of an inline GML record body with highlight/example phrase
// tags removed and whitespace collapsed; the conservation form compared
// against FormItemFontRowIR::plain_text.
std::string form_item_plain_text(const std::string& gml_text);

} // namespace geist::detail
