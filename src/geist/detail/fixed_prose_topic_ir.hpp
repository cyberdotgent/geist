#pragma once

#include "geist/detail/fixed_prose_ir.hpp"
#include "geist/detail/provenance_ir.hpp"

#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct FixedProseTopicSegmentIR {
  BookControlKind kind = BookControlKind::text;
  std::string opcode;
  bool malformed = false;
  DocumentSourceSliceIR source;
};

struct FixedProseAnchorIR {
  std::string id;
  DocumentSourceSliceIR source;
};

// A whole-topic admission envelope around FixedProseIR.  Unlike the inner
// prose recognizer, this type accounts for every decoded segment in the topic
// and therefore cannot silently discard controls or visible trailing content.
struct FixedProseTopicIR {
  std::uint32_t logical_record = 0;
  SourceByteRange payload_bytes;
  std::size_t token_count = 0;
  std::string heading_level;
  std::optional<FixedProseAnchorIR> anchor;
  DocumentSourceSliceIR heading_source;
  DocumentSourceSliceIR paragraph_source;
  FixedProseIR prose;
  std::vector<FixedProseTopicSegmentIR> segments;
};

std::optional<FixedProseTopicIR> extract_fixed_prose_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const VerifiedOwnershipIR& ownership,
    std::string* error = nullptr);
bool verify_fixed_prose_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const VerifiedOwnershipIR& ownership,
    const FixedProseTopicIR& topic, std::string* error = nullptr);

} // namespace geist::detail
