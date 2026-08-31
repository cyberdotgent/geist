#pragma once

#include "geist/detail/document_ir.hpp"
#include "geist/toc.hpp"

namespace geist::detail {

// The single construction of the topic identity handed to
// `try_lower_topic_to_document_ir`.
//
// Both consumers of the typed route -- `TocEntry::markdown()` (the export
// renderer) and `BooDocument::typed_route_inventory()` (the coverage metric)
// -- walk the same contents catalog, so they must derive the identity from
// the same evidence: the contents entry itself, whose topic-header fields
// (`heading_level`, `topic_number`, and the logical-record bounds) are
// attached from the book's topic table when the catalog is built.
//
// Building the identity twice, from two different projections of the same
// topic, made a title-sensitive rejection able to differ between the metric
// and the export it claims to measure (issue #58).  Every caller uses this
// function so that they agree by construction.
TopicIdentityIR make_topic_identity(const TocEntry &entry);

// True when the contents entry names a topic body that can be decoded: the
// topic table attached a non-empty logical-record range to it.  A contents
// entry without one has no typed route and renders through the legacy
// projection.
bool topic_identity_has_body(const TopicIdentityIR &identity);

} // namespace geist::detail
