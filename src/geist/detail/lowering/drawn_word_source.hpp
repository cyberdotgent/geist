// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;
struct OwnershipIR;
struct DocumentRenderTraceIR;

// Native display text and byte provenance used by the independent source-side
// conservation walk. Control metadata is excluded only where framing proves
// its opcode; a similarly spelled ordinary word remains drawn text.
struct DrawnTextIR {
  std::string text;
  // Per-byte provenance: true only in the first token after a native prefix,
  // the position where a control opcode can stand.
  std::vector<bool> opening;
  // Per-byte index into the source-line descriptions below.
  std::vector<std::size_t> line;
  std::vector<std::string> sources;
};

// A picture's placeholder row.  A `cselect <col> <len> PIC<n>` selector names
// columns of the display line below it whose stored text spells the reader's
// placeholder word and the picture number (` | PICTURE 1` under
// `cselect 3 9 PIC1`, G544-3290-04 record 217).  Hosted BookServer puts
// picture <n>'s image there and never prints the word (doc/boo-spec/
// markup.adoc, "Picture placeholder rows").  Those cells are not drawn text,
// but only where the lowered document really carries that image: a picture
// the render did not place keeps its placeholder words, so losing it still
// shows.
struct DrawnPicturePlaceholdersIR {
  // The placeholder word the book's `CLANGUAGE` selects
  // (`picture_placeholder_word`); empty when it selects none.
  std::string word;
  // The `<n>` of every `PIC<n>` whose image the lowered document carries.
  std::set<std::string> shown;
};

// The placeholder word hosted BookServer writes for a picture, chosen by the
// first one or two letters of the book's `CLANGUAGE` value (`ENGL` ->
// `PICTURE`).  Empty for a value that selects no word, so a caller fails
// closed.
std::string picture_placeholder_word(const std::string &language);

// The `<n>` of every book picture (`resource:<n>`) a Markdown render shows,
// read off the image destinations the render itself wrote.
std::set<std::string>
pictures_shown_in_markdown(const std::string &markdown,
                           const DocumentRenderTraceIR &trace);

DrawnTextIR
project_drawn_topic_text(const std::vector<DecodedLogicalRecordSource> &records,
                         const OwnershipIR &ownership,
                         std::size_t *unframed_records,
                         const DrawnPicturePlaceholdersIR *pictures = nullptr);

} // namespace geist::detail
