#pragma once

#include "geist/detail/document_ir.hpp"

#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;
struct BookTopicCatalogIR;

// Attempts one complete typed whole-topic representation from lossless source
// records. A non-match or any rejected/ambiguous semantic envelope returns no
// document, leaving the caller's existing whole-topic compatibility path
// untouched.
std::optional<DocumentIR> try_lower_topic_to_document_ir(
    TopicIdentityIR topic,
    const std::vector<DecodedLogicalRecordSource> &sources,
    const BookTopicCatalogIR *book_topic_catalog = nullptr,
    std::string *typed_rejection = nullptr);

} // namespace geist::detail
