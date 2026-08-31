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
  std::vector<std::pair<DisplayRunId, std::size_t>> title_source_rows;
  std::string introduction;
  std::vector<std::pair<DisplayRunId, std::size_t>> introduction_source_rows;
  std::vector<PublicationEntryIR> entries;
};

// Recognizes a source-conserved publication catalog from its typed envelope:
// one title control segment originating the title run, entry (font) control
// segments each originating one entry run, complete layout/ownership
// representation of that envelope, and publication semantics (a whole-word
// `publication(s)` title role or an IBM publication number on every entry).
// An entry control whose payload is empty at a record boundary originates its
// run on the next record's leading text segment (a deferred entry origin).
// The introduction is optional: a single-row title run yields title plus
// entries, and introduction provenance exists exactly when the text does.
// Font operand spelling and segment/run counts are not admission evidence.
// Ambiguous or partially owned candidates fail closed and return no catalog.
std::optional<PublicationCatalogIR> extract_publication_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout,
    const VerifiedOwnershipIR& ownership);
bool verify_publication_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout,
    const VerifiedOwnershipIR& ownership,
    const PublicationCatalogIR& catalog,
    std::string* error = nullptr);
std::string format_publication_catalog_ir(const PublicationCatalogIR& catalog);

} // namespace geist::detail
