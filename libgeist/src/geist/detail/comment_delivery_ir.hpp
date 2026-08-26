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

enum class CommentAffixAttachment {
  prefix_current_field,
  suffix_owning_field,
};

enum class CommentAffixSpacing {
  none,
  space_before,
  space_after,
};

// A printable source fragment which is not part of the physical row's visible
// field cells. Semantic fragments are attached to exactly one field; all other
// printable marker/opaque/padding cells are retained explicitly as structural
// suppression evidence. The word range permits a dictionary token to be
// audited without pretending that the encoded byte range can be subdivided.
struct CommentSourceFragmentIR {
  enum class Disposition {
    semantic_affix,
    structural_suppression,
  };

  std::uint32_t logical_record = 0;
  std::size_t token_index = 0;
  std::size_t word_begin = 0;
  std::size_t word_end = 0;
  std::uint32_t byte_begin = 0;
  std::uint32_t byte_end = 0;
  std::string text;
  Disposition disposition = Disposition::structural_suppression;
  CommentAffixAttachment attachment =
      CommentAffixAttachment::suffix_owning_field;
  CommentAffixSpacing spacing = CommentAffixSpacing::none;
};

inline bool operator==(const CommentSourceFragmentIR& left,
                       const CommentSourceFragmentIR& right) noexcept {
  return left.logical_record == right.logical_record &&
         left.token_index == right.token_index &&
         left.word_begin == right.word_begin &&
         left.word_end == right.word_end &&
         left.byte_begin == right.byte_begin &&
         left.byte_end == right.byte_end && left.text == right.text &&
         left.disposition == right.disposition &&
         left.attachment == right.attachment && left.spacing == right.spacing;
}

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
  std::vector<CommentSourceFragmentIR> affixes;
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
  std::uint32_t object_logical_record = 0;
  std::size_t object_segment_index = 0;
  std::size_t object_token_begin = 0;
  std::size_t object_token_end = 0;
};

struct CommentDeliveryIR {
  CommentDeliveryKind kind = CommentDeliveryKind::delivery_instructions;
  std::string title;
  std::vector<CommentDeliveryBlockIR> blocks;
  std::vector<CommentSourceFragmentIR> suppressed_fragments;
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
