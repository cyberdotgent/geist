// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

// A book's identity without opening it.
//
// A shelf listing needs a title, a document number and a build date for every
// book in a directory, and nothing else: no topics, no table of contents, no
// resources, no rendering.  `BooDocument::open` pays for all of those, which
// is the right trade for one book being read and the wrong one for a thousand
// being listed.
//
// This is deliberately *not* a half-open `BooDocument`.  A document whose
// `topics()` is empty and whose `markdown()` throws would turn a type with one
// invariant into one with modes, and every method would grow an unstated "if
// fully opened" precondition.  A summary is a different thing from a document,
// so it is a different type -- one holding plain values a caller can copy,
// cache and share with no lifetime or thread-safety contract at all.
//
// Both entry points read the container header through the same internal
// prologue and derive properties through the same control extraction, so a
// summary can never disagree with what opening the same file reports.

#include "geist/directory.hpp"
#include "geist/export.hpp"
#include "geist/metadata.hpp"
#include "geist/properties.hpp"

#include <filesystem>

namespace geist {

// What a shelf listing can learn about a book cheaply.  Every field carries
// exactly what `BooDocument` publishes under the same name.
struct BooBookSummary {
  // Path, file size and page count.
  BooMetadata metadata;
  // The physical directory page, whose `date` and `time` are the build stamp
  // a live BookServer addresses a revision by.
  BooDirectory directory;
  // Title, document number, authors and the rest of the book's own controls.
  BooBookProperties properties;
};

// Reads `path`'s identity without building topic, TOC or resource indexes.
// Throws on a file that is not a well-formed BOO container, on the same terms
// as `BooDocument::open`.
GEIST_API BooBookSummary probe_book(const std::filesystem::path& path);

} // namespace geist
