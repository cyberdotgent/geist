#pragma once

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
// exact half-open spans in AssembledLogicalRecord::words. source_tokens are
// the record-local Token IR ordinals intersecting the complete segment.
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

std::vector<ControlSegmentIR>
decode_control_segments(std::uint32_t logical_record,
                        const AssembledLogicalRecord& assembled);
bool verify_control_segments(const AssembledLogicalRecord& assembled,
                             const std::vector<ControlSegmentIR>& segments,
                             std::string* error = nullptr);
std::string format_control_segment_ir(const ControlSegmentIR& segment);

} // namespace geist::detail
