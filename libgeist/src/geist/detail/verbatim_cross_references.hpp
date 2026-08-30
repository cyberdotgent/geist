#pragma once

#include "geist/detail/internal.hpp"
#include "geist/detail/render_diagnostic_ir.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace geist::detail {

// Cross references inside a verbatim topic.
//
// A topic that no typed family claims still *names* its cross references:
// `cselect <column> <length> <target>` opens its own display line and marks
// the column range of the row that follows as a hotspot pointing at
// `<target>`.  That is the same evidence the drawn-figure family reads for
// its caption and body links, and the same evidence hosted BookServer serves
// as an `<a href>` inside the topic's `<pre>`.  The English wording of the
// phrase ("... in topic 2.7") is a rendering of the selector, never the
// evidence: the selector's operands are.
//
// Naming a destination is what `best_effort_anchors` already does for the
// other direction; this is the missing half, so that a verbatim topic both
// answers to a reference and resolves the ones it makes.
//
// Fail closed, per selector.  A selector is only turned into a link when
//
//   * its operands are canonical (`<column> <length> <target>`) -- the
//     `LNK` selector spellings carry more operands and are declined here;
//   * its target is an anchor-shaped identifier, and not a `pic<n>` stored
//     object (which names a picture, not a place in the text);
//   * a footnote target is one this same topic prints, because a footnote is
//     reachable only from the page that carries it;
//   * its opcode and operands sit alone on a display line that the verbatim
//     rendering therefore drops, so the row it marks is unambiguous;
//   * the marked column range lies inside the emitted row and covers visible
//     text;
//   * the covered text carries no Markdown link punctuation of its own; and
//   * it does not overlap a span already accepted on that row.
//
// Everything else stays plain text.  A dangling link is worse than none.
//
// The row's words, spacing and change bars are untouched: the only bytes
// added are the link syntax around the covered columns.
struct VerbatimCrossReferenceIR {
  // The verbatim rows, with the accepted links spelled into them.
  std::vector<std::string> lines;
  // The footnote anchors a link really used, so the renderer emits exactly
  // the local destinations the file now needs and no others.
  std::vector<std::string> footnote_anchors;
};

VerbatimCrossReferenceIR link_verbatim_cross_references(
    const std::vector<DecodedLogicalRecordSource> &sources,
    const std::vector<BestEffortLineIR> &lines,
    const std::vector<std::string> &printed_footnote_anchors);

} // namespace geist::detail
