#pragma once

#include "geist/detail/book_ir.hpp"

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
std::vector<ControlSegmentIR>
decode_control_segments(std::uint32_t logical_record,
                        const AssembledLogicalRecord& assembled,
                        const std::vector<EncodedLogicalToken>& encoded_tokens =
                            {});
bool verify_control_segments(const AssembledLogicalRecord& assembled,
                             const std::vector<ControlSegmentIR>& segments,
                             std::string* error = nullptr);
std::string format_control_segment_ir(const ControlSegmentIR& segment);

} // namespace geist::detail
