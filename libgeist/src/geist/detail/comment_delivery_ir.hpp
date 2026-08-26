#pragma once

#include "geist/detail/ownership_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

enum class CommentDeliveryKind {
  delivery_instructions,
  questionnaire,
};

enum class CommentDeliveryBlockKind {
  title_page,
  delivery_instructions,
  questionnaire_table,
  response_area,
};

enum class CommentMarkerDisposition {
  absent,
  layout_artifact,
  lexical_content,
};

// A maximal source-proven visible field within one physical line. Explicit
// layout padding separates fields; styling/control words inside a field do
// not. This keeps later document lowering from rediscovering columns by
// searching the flattened line text.
struct CommentSourceFieldIR {
  enum class Disposition {
    semantic_content,
    layout_decoration,
  };

  std::size_t token_begin = 0;
  std::size_t token_end = 0;
  std::string text;
  Disposition disposition = Disposition::semantic_content;
};

// Output-neutral source line retained by the comments/back-matter semantic
// layer. Marker candidates remain provenance, not rendered punctuation or
// Markdown syntax.
struct CommentSourceLineIR {
  std::string text;
  DisplayRunId run = 0;
  std::size_t row = 0;
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  std::size_t token_begin = 0;
  std::size_t token_end = 0;
  std::size_t native_origin = 0;
  PhysicalRowStartKind start = PhysicalRowStartKind::control_payload;
  PhysicalBreakKind break_before = PhysicalBreakKind::unknown;
  bool continues_previous_record = false;
  std::optional<MarkerSlotIR> marker;
  CommentMarkerDisposition marker_disposition =
      CommentMarkerDisposition::absent;
  std::vector<CommentSourceFieldIR> fields;
};

struct CommentDeliveryBlockIR {
  CommentDeliveryBlockKind kind =
      CommentDeliveryBlockKind::delivery_instructions;
  // For a form table this is the source SRTBL opcode. Other block kinds keep
  // it empty; consumers must not infer a renderer target from this field.
  std::string object_id;
  std::vector<CommentSourceLineIR> lines;
};

struct CommentDeliveryIR {
  CommentDeliveryKind kind = CommentDeliveryKind::delivery_instructions;
  std::string title;
  std::vector<CommentDeliveryBlockIR> blocks;
};

std::optional<CommentDeliveryIR> extract_comment_delivery_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    std::string* error = nullptr);
bool verify_comment_delivery_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const CommentDeliveryIR& delivery, std::string* error = nullptr);
std::string format_comment_delivery_ir(const CommentDeliveryIR& delivery);

} // namespace geist::detail
