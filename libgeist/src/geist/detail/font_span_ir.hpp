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
// Every other code is retained as an opaque style so consumers can conserve
// the span without claiming a presentation.
enum class FontStyleIR {
  unknown,
  highlight_1,
  highlight_2,
  highlight_3,
  citation,        // C: italic citation
  example_phrase,  // X, E, 4: monospace example text
  keyword,         // P: monospace parameter keyword (<kbd>)
  variable,        // V: italic programming variable (<var>)
  bold_phrase,     // R, H, I, J, K, M: bold keyword / inline heading
  italic_phrase,   // L: italic inline heading
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
