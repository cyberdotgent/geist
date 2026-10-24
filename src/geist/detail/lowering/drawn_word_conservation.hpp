// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace geist::detail {

// Source-side conservation: does a topic's rendered text contain every word
// the topic's source actually draws? Both serializers are checked separately:
// emitted HTML is text-projected and emitted Markdown is projected through its
// serialization trace. Inline link labels join adjacent text, while attributes
// and link destinations cannot conceal missing words in either format.
//
// Coverage answers which route rendered a topic, not whether the output is
// complete.  A topic can render, count as fully typed, and still drop a word:
// the flattened decoded string can open a control segment on a word that is
// only *spelled* like a control, and the word then leaves the render with an
// anchor in its place.  Nothing about the route says so, and a differential
// against another export cannot say so either, because the defect is present
// in both sides of the comparison.
//
// This check needs neither a second export nor the network.  The record's own
// display-line framing already states what the source draws:
//
//   * a record payload tiles into `<length byte><that many bytes>` display
//     lines, and the length byte is the row-control slot, always and only.  It
//     is a raw byte, but the token reader resolves any low byte through the
//     dictionary into an ordinary word -- `adapter`, `and`, `The` -- so only
//     position separates the two roles.  A length byte is never drawn text;
//     the checked display-text accessor is what keeps it out of this walk.
//   * the remaining cells of a line are drawn, minus the cells the ownership
//     ledger attributes to a control's opcode or operand, which are markup
//     the reader consumes rather than prints.
//
// Everything left over is text the reader puts on the screen, and must
// therefore survive into the rendered text.
//
// One distinction decides what this check can enforce.  A control's opcode is
// the first token of its own display line -- the encoder writes the line's
// length byte and then the opcode, in every control the corpus stores -- so a
// drawn word standing anywhere *else* on its line cannot be a control opcode,
// whatever it is spelled like.  Its disappearance from the render is therefore
// a defect and nothing else, and that is the invariant this check enforces.
//
// A word at the first token of its line is reported separately and not
// enforced: the corpus holds control opcodes the segment decoder does not
// classify (`citerm`, `cidelm`, `cgpsep`, `SI`), and each of those legitimately
// leaves the render.  Every unclassified opcode stands exactly there, so the
// split costs the enforced side nothing that can be told apart from a control.

// One word the source draws more often than the render emits it.
struct DrawnWordDeficitIR {
  // Lower-cased alphanumeric run, the unit both sides are cut into.
  std::string word;
  // Occurrences whose first cell is the first token of its display line --
  // the one position a control opcode can occupy.
  std::size_t drawn_opening = 0;
  // Occurrences standing anywhere else on their display line.  A word here is
  // display text and nothing else.
  std::size_t drawn_inline = 0;
  // Minimum retained count across Markdown and HTML; neither format can
  // compensate for text lost by the other.
  std::size_t emitted = 0;
  std::size_t emitted_markdown = 0;
  std::size_t emitted_html = 0;
  // The shortfall left after every opening occurrence is forgiven as a
  // possible unclassified control opcode.  Non-zero means the render dropped
  // display text.
  std::size_t unaccounted = 0;
  std::size_t unaccounted_markdown = 0;
  std::size_t unaccounted_html = 0;
  // Up to three of the display lines the word stands inside, joined by " | ",
  // with a count of the rest. A report that names the line can be acted on; a
  // report that only counts repeats the failure this check exists to fix.
  std::string evidence;
};

// One punctuation unit -- a word and the mark glued to its end -- drawn more
// often than the HTML fragment shows it.  The word check cannot see a lost
// `,` or a `.` that moved away from its word; this one can.  Opening
// occurrences (where a control opcode can stand) are not counted at all.
struct DrawnPunctuationDeficitIR {
  std::string unit;
  std::size_t drawn = 0;
  std::size_t emitted = 0;
  std::size_t emitted_markdown = 0;
  std::size_t emitted_html = 0;
  std::size_t unaccounted = 0;
  std::string evidence;
};

struct DrawnWordTopicIR {
  std::string id;
  // Stable identity in the source catalog, parallel to TypedRouteTopicIR.
  std::string source_kind;
  std::size_t source_index = 0;
  std::uint32_t source_start_logical_record = 0;
  std::uint32_t source_end_logical_record = 0;
  // The render diagnostic's route ("typed", "legacy", ...), so a report can
  // say whether a drop hides behind full typed coverage.
  std::string route;
  // Records whose payload does not tile into whole display lines.  Those
  // records have no decided framing, so nothing about them is checked; the
  // count is reported rather than silently folded into a clean result.
  std::size_t unframed_records = 0;
  std::size_t records = 0;
  // Alphanumeric runs drawn by the framed records, and emitted by the render.
  std::size_t drawn_words = 0;
  std::size_t emitted_words = 0;
  // Every word drawn more often than emitted, sorted by word.  Kept even when
  // the whole shortfall is forgiven as an opening occurrence, so the report
  // can show the evidence rather than a count.
  std::vector<DrawnWordDeficitIR> deficits;
  // Sum of `unaccounted` over `deficits`: display text this topic drops.
  std::size_t unaccounted = 0;
  std::size_t unaccounted_markdown = 0;
  std::size_t unaccounted_html = 0;
  // Sum of the shortfall forgiven because it stood where a control opcode
  // stands.
  std::size_t forgiven = 0;
  // The exact outputs this check compared, retained only when the caller asks
  // for them (the maintainer corpus gate fingerprints style and structure from
  // them). `markdown_visible_text` is the serializer-trace projection, never a
  // guess at Markdown syntax; `html_fragment` is the public fragment bytes.
  // Both stay empty for a topic without a body.
  std::string markdown_visible_text;
  std::string html_fragment;
  // The punctuation check, reported apart from the word check (see
  // `conservation_punctuation_units`).
  std::vector<DrawnPunctuationDeficitIR> punctuation_deficits;
  std::size_t unaccounted_punctuation = 0;
};

struct DrawnWordConservationIR {
  std::vector<DrawnWordTopicIR> topics;
  std::size_t topics_checked = 0;
  std::size_t topics_dropping = 0;
  std::size_t topics_dropping_markdown = 0;
  std::size_t topics_dropping_html = 0;
  std::size_t topics_with_unframed_records = 0;
  std::size_t unaccounted_words = 0;
  std::size_t unaccounted_markdown_words = 0;
  std::size_t unaccounted_html_words = 0;
  std::size_t forgiven_words = 0;
  std::size_t topics_dropping_punctuation = 0;
  std::size_t unaccounted_punctuation = 0;
};

// Cuts one UTF-8 (or ASCII-projected) string into the comparison unit: runs of
// `[0-9a-z]` after an ASCII case fold. Asterisks, backticks and backslashes
// retain the existing source-side glue policy; other bytes separate. Both
// sides of the comparison are cut the same way, so a dictionary word that
// spans a fragment boundary (`adapt` + `er`) and the render's `adapter` agree.
std::vector<std::string> conservation_words(const std::string& text);

// The punctuation unit: every word of `conservation_words` that a mark of
// `,.;:!)` follows directly, as the word and that mark (`cc,`).  A mark
// separated from its word by a blank, or standing alone, is no unit: it is
// layout as often as text, and the unit is what a reader sees lost.  `?` is
// no mark here: the drawn projection spells every glyph it cannot print as
// `?`, so a box border glued to a word would read as a lost question mark.
std::vector<std::string> conservation_punctuation_units(const std::string& text);

} // namespace geist::detail
