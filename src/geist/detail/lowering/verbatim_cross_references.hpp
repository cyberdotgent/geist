// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/core/internal.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace geist::detail {

// The verbatim topic as a node that can carry links.
//
// A topic no typed family claims still *names* its cross references.  The
// compiler writes each one as `cselect <column> <length> <target>`: the
// control opens its own display line and marks that column range of the row
// it precedes as a hotspot pointing at `<target>`.  Hosted BookServer serves
// exactly those spans as an `<a href>` *inside* the row of its `<pre>`,
// leaving every other column where it was:
//
//   | This book is designed to be used with the <a href="...">VM/ESA: ...
//
// A fenced Markdown block cannot hold an inline anchor -- inside a fence the
// link is inert text -- so the verbatim route is lowered to a node of rows
// and column-addressed links, which the Markdown renderer emits as a raw
// HTML `<pre>` block and which the HTML backend (#46) can consume directly.
//
// Fail closed, per selector.  A selector becomes a link only when
//
//   * its operands are canonical (`<column> <length> <target>`), or it is
//     the `LNK` dialect whose alternative list parses;
//   * its target is an anchor-shaped identifier, and not a `pic<n>` stored
//     object (which names a picture, not a place in the text);
//   * a footnote target is one this same topic prints, because a footnote is
//     reachable only from the page that carries it;
//   * its opcode, operands and `LNK` alternatives sit alone on a display
//     line that the verbatim rendering therefore drops, so the row it marks
//     is unambiguous;
//   * the marked column range starts inside the emitted row and covers
//     visible text -- a range naming more columns than the row drew is
//     clamped to the row's end, which is what hosted does; and
//   * it does not overlap a span already accepted on that row.
//
// Everything else stays plain text.  A dangling link is worse than none.
//
// The row's words, spacing, indentation and change bars are untouched: a
// link is a pair of offsets into the row, never a byte inserted into it.

// One emitted verbatim row, together with the source display line it came
// from and the byte offset in `text` of every display column of that line.
// A `cselect` names its cross reference as a column range on the row it
// precedes, so a consumer that has to place a link needs the column-to-byte
// map; re-deriving it from the flattened row would be a second, divergent
// implementation of the same walk.  `column_offsets` holds one entry per
// display column plus a final end offset, and it is built before the row's
// trailing spaces are trimmed, so an offset may point past `text.size()`.
struct BestEffortLineIR {
  std::string text;
  std::size_t record_index = 0;
  std::size_t display_line_index = 0;
  std::vector<std::size_t> column_offsets;
};

struct VerbatimCrossReferenceIR {
  std::vector<VerbatimRowIR> rows;
  // The footnote anchors a link really used, so the renderer emits exactly
  // the local destinations the file now needs and no others.
  std::vector<std::string> footnote_anchors;
};

VerbatimCrossReferenceIR link_verbatim_cross_references(
    const std::vector<DecodedLogicalRecordSource> &sources,
    const std::vector<BestEffortLineIR> &lines,
    const std::vector<std::string> &printed_footnote_anchors);

// One row as the inside of a `<pre>`: the row's own bytes with `&`, `<` and
// `>` escaped as HTML requires, and an `<a href>` opened and closed at the
// byte offsets each link names.  Nothing that occupies a column is added, so
// the rendered row is the drawn row, column for column.
//
// A destination is spelled only where a single-book export can prove one: an
// in-book reference resolves to `#<anchor>`, which the exporter rewrites to
// the file that defines it; an external reference resolves to its own URL.
// A cross-book reference names a book this export does not contain, so it
// leads nowhere (`#`) here and is left for a backend with a resolver -- the
// node carries every field of the selector either way.
std::string render_verbatim_row(const VerbatimRowIR &row);

} // namespace geist::detail
