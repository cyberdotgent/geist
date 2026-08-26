#pragma once

#include "geist/detail/ownership_ir.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace geist::detail {

struct PublicationParagraphIR {
  std::string text;
  std::vector<std::pair<DisplayRunId, std::size_t>> source_rows;
};

struct PublicationEntryIR {
  std::string text;
  std::vector<PublicationParagraphIR> paragraphs;
  std::vector<std::pair<DisplayRunId, std::size_t>> source_rows;
};

struct PublicationCatalogIR {
  std::string heading_level;
  std::string title;
  std::string introduction;
  std::vector<PublicationEntryIR> entries;
};

// Recognizes an all-C, source-conserved publication stream. Ambiguous or
// partially owned candidates fail closed and return no semantic catalog.
std::optional<PublicationCatalogIR> extract_publication_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout,
    const OwnershipIR& ownership);
bool verify_publication_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout,
    const OwnershipIR& ownership,
    const PublicationCatalogIR& catalog,
    std::string* error = nullptr);
std::string format_publication_catalog_ir(const PublicationCatalogIR& catalog);

} // namespace geist::detail
