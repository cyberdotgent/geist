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
enum class FontStyleIR {
  unknown,
  highlight_1,
  highlight_2,
  highlight_3,
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

} // namespace geist::detail
