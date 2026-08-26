#pragma once

#include "geist/detail/ownership_ir.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace geist::detail {

enum class MessageSectionKind {
  meaning,
  action,
};

using MessageSourceRowIR = std::pair<DisplayRunId, std::size_t>;

struct MessageParagraphIR {
  std::string text;
  std::vector<MessageSourceRowIR> source_rows;
  std::vector<std::pair<std::uint32_t, std::size_t>> source_segments;
};

struct MessageSectionIR {
  MessageSectionKind kind = MessageSectionKind::meaning;
  DisplayRunId run = 0;
  std::size_t row = 0;
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  bool recovered_record_continuation = false;
  std::vector<MessageSourceRowIR> label_source_rows;
  std::vector<MessageParagraphIR> paragraphs;
  std::vector<MessageSourceRowIR> source_rows;
};

struct MessageEntryIR {
  std::string id;
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  MessageParagraphIR headline;
  std::vector<MessageParagraphIR> body;
  std::vector<MessageSectionIR> sections;
  // Complete ordered ledger of physical rows assigned to this message. Rows
  // carrying structural separator artifacts are retained separately rather
  // than silently discarded from source ownership.
  std::vector<MessageSourceRowIR> source_rows;
  std::vector<MessageSourceRowIR> suppressed_source_rows;
};

struct MessageCatalogIR {
  std::vector<MessageEntryIR> entries;
};

std::optional<MessageCatalogIR> extract_message_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout,
    const OwnershipIR& ownership,
    std::string* error = nullptr);
bool verify_message_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout,
    const OwnershipIR& ownership,
    const MessageCatalogIR& catalog,
    std::string* error = nullptr);
std::string format_message_catalog_ir(const MessageCatalogIR& catalog);

} // namespace geist::detail
