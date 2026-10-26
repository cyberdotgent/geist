// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/core/internal.hpp"

#include <vector>

namespace geist::detail {

// The revision characters a book header declares.
//
// The book compiler writes one display line
//
//   crevdesc <character> [description ...]
//
// into the header (the logical records before the first topic) for every
// SCRIPT revision code it resolved, beside the `c.rev <id> <character>`
// definitions themselves (doc/boo-spec/markup.adoc, "Revision characters
// declared in the book header").  SC41-4725-00 record 1 lines 64-69 store
// `crevdesc G`, `crevdesc |`, `crevdesc |`, `crevdesc +`, `crevdesc G`,
// `crevdesc @`; SC33-0807-00 record 1 stores `crevdesc 1 Changes since the
// First Draft of this book`.
//
// Reads the first operand of every such line: a single drawn word, taken from
// the token's own words (never a text projection, which folds unmapped glyphs
// to `?`).  An operand longer than one character, the decoder's unmapped
// word or a blank declares nothing.  Records whose display lines do not parse
// are skipped.  The result is sorted and unique.
DeclaredRevisionCharacters read_declared_revision_characters(
    const std::vector<LogicalRecordIR>& header_records);

// The revision characters declared by the book header of `context`: the
// logical records [1, `first_topic_record`) that precede the first topic.
DeclaredRevisionCharacters read_book_header_revision_characters(
    const LogicalDecodeContext& context, std::uint32_t first_topic_record);

} // namespace geist::detail
