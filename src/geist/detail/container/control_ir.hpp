// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/ir/book_ir.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace geist::detail {

struct AssembledLogicalRecord;

struct OutputRangeIR {
  std::size_t begin = 0;
  std::size_t end = 0;
};

enum class BookControlKind {
  text,
  topic_start,
  topic_number,
  parent,
  forward_level,
  back_level,
  summary,
  heading_level,
  source_file,
  title,
  font,
  select,
  spacing,
  layout_directive,
  table_start,
  table_end,
  menu_start,
  menu_item,
  menu_end,
  message_start,
  structural,
  unknown,
};

// Typed, source-ordered view of one decoded markup segment. Output ranges are
// exact half-open UTF-8 byte spans in token_words_to_ascii(assembled.words).
// source_tokens are the record-local Token IR ordinals intersecting the
// complete segment.
struct ControlSegmentIR {
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  BookControlKind kind = BookControlKind::text;
  std::string opcode;
  OutputRangeIR complete;
  OutputRangeIR opcode_range;
  OutputRangeIR operand_range;
  OutputRangeIR payload_range;
  std::vector<std::size_t> source_tokens;
  bool malformed = false;
  // Set when a later pass proved the control-shaped word that opens this
  // segment to be ordinary display text, so the segment carries no control
  // at all (`demote_bullet_owned_structural_controls`, display_lines.hpp).
  // The segment boundary itself stays: the flattened string split there.
  bool display_text = false;
};

// Converts a decoded UTF-8 byte range to the corresponding half-open range in
// AssembledLogicalRecord::words. Control IR consumers must use this before
// consulting word-coordinate token/source maps.
OutputRangeIR decoded_byte_range_to_word_range(
    const AssembledLogicalRecord& assembled, const OutputRangeIR& bytes);
OutputRangeIR decoded_word_range_to_byte_range(
    const AssembledLogicalRecord& assembled, const OutputRangeIR& words);

// `encoded_tokens` is the record's encoded token projection, in token order.
// It is optional only for synthetic assembled records in tests; production
// decoding always supplies it, because the encoded width and value of a token
// separate a body-control opcode from a display-line length byte whose
// dictionary spelling merely looks like one.
//
// `display_lines` is the record's display-line framing, already decided by
// the record decoder (`assign_display_line_framing`, display_lines.hpp).  A
// control's operands cannot run past the end of the display line the opcode
// stands on, because the next line's length byte begins there; without the
// framing the operand parser reads the whole trailing word list, which for a
// segment that runs to the next control includes the body text of every
// later display line.  Empty means the record's payload does not tile into
// display lines, and then there is no framing to appeal to.  Never
// re-derive it here: read what the decoder carried.
std::vector<ControlSegmentIR>
decode_control_segments(std::uint32_t logical_record,
                        const AssembledLogicalRecord& assembled,
                        const std::vector<EncodedLogicalToken>& encoded_tokens =
                            {},
                        const std::vector<DisplayLineIR>& display_lines = {});
bool verify_control_segments(const AssembledLogicalRecord& assembled,
                             const std::vector<ControlSegmentIR>& segments,
                             std::string* error = nullptr);
std::string format_control_segment_ir(const ControlSegmentIR& segment);

} // namespace geist::detail
