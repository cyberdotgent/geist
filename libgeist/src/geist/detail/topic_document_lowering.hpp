#pragma once

#include "geist/detail/document_ir.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;
struct BookTopicCatalogIR;

// Diagnostic trace of one typed lowering attempt. `family` names the typed
// family that claimed the topic (whether or not its verification succeeded);
// `declined` lists "<family>: <reason>" for every recognizer that was offered
// the source and declined it, so a legacy-routed topic can explain itself.
// Requesting a trace never changes the lowering result.
struct TypedLoweringTraceIR {
  std::string family;
  std::vector<std::string> declined;
};

// Attempts one complete typed whole-topic representation from lossless source
// records. A non-match or any rejected/ambiguous semantic envelope returns no
// document, leaving the caller's existing whole-topic compatibility path
// untouched. `resource_ids` are the book's lower-cased resource catalog ids;
// without them a figure span cannot prove its picture and its topic fails
// closed.
std::optional<DocumentIR> try_lower_topic_to_document_ir(
    TopicIdentityIR topic,
    const std::vector<DecodedLogicalRecordSource> &sources,
    const BookTopicCatalogIR *book_topic_catalog = nullptr,
    std::string *typed_rejection = nullptr,
    TypedLoweringTraceIR *trace = nullptr,
    const std::set<std::string> *resource_ids = nullptr);

} // namespace geist::detail
