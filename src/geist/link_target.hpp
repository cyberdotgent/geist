// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

namespace geist {

// What a topic names, so that a book-wide link map can resolve a cross
// reference to it.  This is the book's own naming, independent of how the
// topic renders: a consumer asks the topic what it names and gets the same
// answer whether the Markdown came from the typed Document IR or from the
// legacy string renderer.
enum class LinkTargetKind {
  // `SR<id>`: a place inside the topic.  A reference to it resolves to the
  // topic's file; the reader lands at the top, which is what BookServer
  // itself serves for a topic-level anchor.
  anchor,
  // `SRFIG<id>`: a figure object.  References name it both as `<id>` and as
  // `FIG<id>`, and resolve to a fragment inside the topic's file -- unless
  // the figure's body is a stored object, when they resolve to the object.
  figure,
  // `SRTBL<id>`: a table object; references resolve to a fragment.
  table,
};

struct LinkTarget {
  LinkTargetKind kind = LinkTargetKind::anchor;
  // The id a cross reference spells, which is the source control's operand.
  std::string id;
  // Set only for a figure whose body is a stored object: the `resource:<id>`
  // URI of that object, which references to the figure resolve to instead of
  // to a place in the Markdown.
  std::string resource;
  // The anchor id the topic's Markdown really emits for this destination,
  // which is what a `#fragment` has to name.  It is not always `id`: a
  // reference to an object spells the id without its object prefix
  // (`MPROKEY`) while the anchor is written with it (`TBLMPROKEY`), so a
  // destination built from `id` alone points at a fragment no file carries.
  // Empty means the destination is the file itself, with no fragment.
  std::string fragment;
};

} // namespace geist
