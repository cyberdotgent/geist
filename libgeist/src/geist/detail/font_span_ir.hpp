#pragma once

#include "geist/detail/control_ir.hpp"
#include "geist/detail/provenance_ir.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;

// Compact CFONT style codes resolve through the book-level CFONTDEF table
// documented in Format/markup.md ("Font And Highlight Controls"): `1`..`3`
// are the GML highlight phrases HP1..HP3. Every other code is retained as an
// opaque style so consumers can conserve the span without claiming a style.
// The CFONTDEF code table is identical in all 33 corpus books (checked with
// `bootrace --fonts` over every fixture): `1`..`3` HP1..HP3, `4` HP4, `C`
// CIT, `X` XPH, `E` XMP, `P` PK, `V` PV, `R` RK, `H`..`M` H1..H6.  Hosted
// BookServer renders CIT as <cite>, XPH/XMP/HP4 as <tt>/<samp>, PK as
// <kbd>, PV as <var>, RK/H1-H4/H6 as <b> and H5 as <i> (FA1PLMM0 11.5,
// ACPZMST1 8.14.1/2.4.1.2, GC23-046 B.2, DREICMST 1.4.2.1, FA1PLMM0 9.3.1).
// `HP5`..`HP9` are the underscored highlight phrases: hosted BookServer
// renders each as the underscore of the phrase five codes below it, so `5`
// is `H0` underscored (<U>), `6` is `HP1` underscored, `7` is `HP2`
// underscored (<B><U>), `8` is `HP3` underscored, and `9` is `HP4`
// underscored (<TT><U>).  Hosted evidence: `5` -> `<U>see</U>`
// (QSYSNEWG 2.3 `cfont 59 3 5`) and `<U>underscored</U>` (SC34-425 1.8
// `cfont 42 11 5`); `7` -> `<B><U>Operational</B></U> <B><U>Assistant</B></U>
// <B><U>overview</B></U>` (QSYSNEWG 5.1.4 `cfont 19 11 7 31 9 7 41 8 7`) and
// `<B><U>L</B></U>` (SG24-204 5.2.1 `cfont 33 1 7`, which styles the `L` of
// `LU` alone); `9` -> `<TT><U>/</TT></U>` (OFCUSEOV 5.2 `cfont 25 1 9`).
// `Q` (PKDEF) is <dfn> (PRG1SORT 2.1.4 `<dfn>*CURLIB</dfn>`; SC26-457
// 3.4.1.2 `<dfn>LIST</dfn>`).  Every other code is retained as an opaque
// style so consumers can conserve the span without claiming a presentation.
//
// `W` (WARNING) and `G` (WARNINGTEXT) are the two halves of one GML warning
// block: `W` styles its `Warning:` lead and `G` every word of its body, and
// hosted BookServer renders both as <em>.  Four books, each read off the
// hosted page: GC23-046 `6.9.3` serves
// `<em>Warning:</em>  <em>Do</em> <em>not</em> <em>use</em> <em>the</em>
// <em>ISPF</em> <em>LIBDEF</em> ...`; SC26-457 `1.6.5`
// `<em>Warning:</em>  <em>If</em> <em>the</em> <em>generic</em> ...`;
// SC31-711 `3.3` `<em>Warning:</em>  <em>Do</em> <em>not</em>
// <em>modify</em> ... <em>impaired.</em>`; SH12-565 `FRONT_1.1`
// `<em>Warning:</em> <em>Do</em> <em>not</em> <em>use</em> ...`.  Every one
// of those blocks is anchored `<a name="WRN">`.  Corpus-wide the two codes
// occur only in those four books (7 `W` spans, 363 `G` spans, measured with
// a `bootrace --fonts` sweep of all 7,362 topics).
//
// `Z` (PVDEF) and `_` (UNDERSCORE) stay fail-closed for want of a second
// witness.  The same corpus sweep finds `Z` in one topic of one book
// (SC26-457 `1.3` `cfont 3 7 P 11 10 Z 22 4 P 27 1 P 29 10 Z 39 1 P`, whose
// hosted row is `<kbd>COMMAND</kbd> <dfn>parameters</dfn> <kbd>....</kbd>
// <kbd>[</kbd> <dfn>terminator</dfn><kbd>]</kbd>`, so PVDEF presents exactly
// like PKDEF there) and `_` in one topic of one book (SC24-5527-02
// `COMMENTS`, which the hosted catalog does not serve at all).  One book is
// not evidence that the presentation generalises, and the corpus records
// repeatedly that identical geometry means different things in different
// books, so both remain unknown.
//
// A style code is one character of the book's own `CFONTDEF` table.  That
// table is byte-identical in all 34 fixtures (swept with `bootrace --fonts`):
// `0`..`9` H0/HP1..HP9, `A` APL, `B` CAUTION, `C` CIT, `D` DANGER, `E` XMP,
// `F` CAUTIONTEXT, `G` WARNINGTEXT, `H`..`M` H1..H6, `O` DANGERTEXT, `P` PK,
// `Q` PKDEF, `R` RK, `T` TP, `U` MD, `V` PV, `W` WARNING, `X` XPH, `Y`
// MDQUAL, `Z` PVDEF, `_` UNDERSCORE.  There is no `N` and no `S`.
//
// The final operand triple of a control can carry a trailing `,` separator
// glued to its style code as a prefix-1 token (`cfont 4 4 R,`).  The comma is
// not part of the code and not display text: hosted BookServer renders
// ACPZMST1 FRONT_1.1 `cfont 4 4 R,` as `<B>GUPI</B>` and GC28-183 2.2.1
// `cfont 17 1 E,` as `<samp>.</samp>`, in both cases with no comma and with
// the same geometry as the comma-less form in the same record.
enum class FontStyleIR {
  unknown,
  highlight_1,
  highlight_2,
  highlight_3,
  highlight_5,     // 5 (HP5): underscored plain phrase (<U>)
  highlight_6,     // 6 (HP6): underscored italic phrase
  highlight_7,     // 7 (HP7): underscored bold phrase (<B><U>)
  highlight_8,     // 8 (HP8): underscored bold italic phrase
  highlight_9,     // 9 (HP9): underscored monospace phrase (<TT><U>)
  citation,        // C: italic citation
  example_phrase,  // X, E, 4: monospace example text
  keyword,         // P: monospace parameter keyword (<kbd>)
  keyword_define,  // Q: defined parameter keyword (<dfn>)
  variable,        // V: italic programming variable (<var>)
  bold_phrase,     // R, H, I, J, K, M: bold keyword / inline heading
  italic_phrase,   // L: italic inline heading
  warning,         // W: the `Warning:` lead of a warning block (<em>)
  warning_text,    // G: the body of a warning block (<em>)
};

// One `<column> <length> <code>` operand triple of a CFONT control. Columns
// are display-column coordinates of the reader line model, not offsets into
// any decoded string; mapping them onto visible text is the consumer's
// responsibility and must fail closed when the geometry does not conserve
// whole display words.
struct FontSpanIR {
  std::size_t column = 0;
  std::size_t length = 0;
  std::string code;
  FontStyleIR style = FontStyleIR::unknown;
};

struct FontControlSpansIR {
  std::vector<FontSpanIR> spans;
  // Exact source provenance of the operand triples.
  DocumentSourceSliceIR operand_source;
};

// Decodes the complete operand of one typed `font` control segment. Fails
// closed on any non-font, malformed, or partially consumable operand.
std::optional<FontControlSpansIR>
decode_font_control_spans(const DecodedLogicalRecordSource& record,
                          const ControlSegmentIR& segment,
                          std::string* error = nullptr);

const char* font_style_name(FontStyleIR style);
// The style of one CFONT code word (`1`..`3`, `C`, `X`, `E`, `4`, `P`, `V`,
// `R`, `H`..`M`, `L`); unknown for any other code.
FontStyleIR font_style_for_code(const std::string& code);

} // namespace geist::detail
