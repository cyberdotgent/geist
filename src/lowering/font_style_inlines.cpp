// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/font_style_inlines.hpp"

namespace geist::detail {

EmphasisKindIR font_style_emphasis_kind(FontStyleIR style) {
  switch (style) {
  case FontStyleIR::highlight_2:
  case FontStyleIR::bold_phrase: return EmphasisKindIR::strong;
  case FontStyleIR::highlight_3:
  // HP8 is the underscored HP3.
  case FontStyleIR::highlight_8: return EmphasisKindIR::strong_emphasis;
  // HP7 is the underscored HP2 (hosted <B><U>); Markdown has no underscore,
  // so the bold half survives.
  case FontStyleIR::highlight_7: return EmphasisKindIR::strong;
  case FontStyleIR::highlight_1:
  // HP5 (plain underscore, hosted <U>) and HP6 (underscored HP1) have no
  // Markdown underscore run and lower to ordinary emphasis.
  case FontStyleIR::highlight_5:
  case FontStyleIR::highlight_6:
  case FontStyleIR::citation:
  case FontStyleIR::variable:
  case FontStyleIR::italic_phrase:
  case FontStyleIR::example_phrase:
  case FontStyleIR::keyword:
  case FontStyleIR::keyword_define:
  case FontStyleIR::highlight_9:
  // WARNING (`W`) and WARNINGTEXT (`G`) are hosted `<em>` on all four books
  // that carry them, so both take the default emphasis.
  case FontStyleIR::warning:
  case FontStyleIR::warning_text:
  case FontStyleIR::apl_symbol:
  case FontStyleIR::unknown: break;
  }
  return EmphasisKindIR::emphasis;
}

bool font_style_is_code(FontStyleIR style) {
  // HP9 is the underscored HP4, the monospace example phrase.
  return style == FontStyleIR::example_phrase ||
         style == FontStyleIR::keyword ||
         style == FontStyleIR::keyword_define ||
         style == FontStyleIR::highlight_9 ||
         style == FontStyleIR::apl_symbol;
}

} // namespace geist::detail
