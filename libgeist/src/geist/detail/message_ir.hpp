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

struct MessageSectionIR {
  MessageSectionKind kind = MessageSectionKind::meaning;
  DisplayRunId run = 0;
  std::size_t row = 0;
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  bool recovered_record_continuation = false;
};

struct MessageEntryIR {
  std::string id;
  std::uint32_t logical_record = 0;
  std::size_t segment_index = 0;
  std::vector<MessageSectionIR> sections;
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
