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
  case FontStyleIR::sample_phrase:
  case FontStyleIR::keyword:
  case FontStyleIR::keyword_define:
  case FontStyleIR::variable_define:
  case FontStyleIR::highlight_9:
  // WARNING (`W`) and WARNINGTEXT (`G`) are hosted `<em>` on all four books
  // that carry them, so both take the default emphasis.
  case FontStyleIR::warning:
  case FontStyleIR::warning_text:
  case FontStyleIR::apl_symbol:
  // UNDERSCORE (`_`) lowers to its own underline inline in prose and is
  // declined by every other family; it never asks for an emphasis kind.
  case FontStyleIR::underscore:
  case FontStyleIR::unknown: break;
  }
  return EmphasisKindIR::emphasis;
}

bool font_style_is_code(FontStyleIR style) {
  // HP9 is the underscored HP4, the monospace example phrase.
  return font_style_is_example(style) ||
         style == FontStyleIR::keyword ||
         font_style_is_definition(style) ||
         style == FontStyleIR::highlight_9 ||
         style == FontStyleIR::apl_symbol;
}

PhraseKindIR font_style_phrase_kind(FontStyleIR style) {
  // Hosted BookServer serves each of these codes in its own element, inside
  // message entries and running text alike: `E` 6,829 of 6,844 spans
  // `<samp>` in 33 books, `P` 4,845 of 4,849 `<kbd>` in 26, `C` 1,610 of
  // 1,630 `<cite>` in 34 (doc/boo-spec/markup.adoc, the message-entry
  // census; every remaining span a matcher fault), and `V` 225 of 225
  // `<var>` in 28 (the variable census), and `Q` 194 of 194 `<dfn>` in 38
  // (the defined-keyword census).  `X` and `4` are hosted `<tt>`, which
  // HTML5 dropped; they stay plain code.  `Z` is hosted `<dfn>` as well,
  // but in too few books for a census, and stays plain code.
  switch (style) {
  case FontStyleIR::sample_phrase: return PhraseKindIR::sample;
  case FontStyleIR::keyword: return PhraseKindIR::keyboard;
  case FontStyleIR::citation: return PhraseKindIR::citation;
  case FontStyleIR::variable: return PhraseKindIR::variable;
  case FontStyleIR::keyword_define: return PhraseKindIR::definition;
  default: break;
  }
  return PhraseKindIR::none;
}

} // namespace geist::detail
