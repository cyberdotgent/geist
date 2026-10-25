// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/prose/prose_topic_internal.hpp"

#include "geist/detail/ir/book_topic_catalog_ir.hpp"
#include "geist/detail/ir/menu_ir.hpp"
#include "geist/detail/ir/menu_topic_ir.hpp"
#include "geist/detail/ir/picture_placement_ir.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <tuple>

namespace geist::detail::prose_internal {

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------

// Cells join one run by presentation.  `E` (XMP) and `X`/`4` present alike
// (monospace), as do `Q` (PKDEF) and `Z` (PVDEF), and keep joining as they
// always have; the run they share takes the style of its family that names
// no phrase (see the run loop below).
static FontStyleIR run_presentation(FontStyleIR style) {
  if (font_style_is_example(style)) return FontStyleIR::example_phrase;
  if (font_style_is_definition(style)) return FontStyleIR::variable_define;
  return style;
}

struct Attr {
  FontStyleIR style = FontStyleIR::unknown;
  std::vector<std::size_t> links;
  // The revision mark of the cell's display row, 0 when it carries none.
  char32_t revision = 0;
  bool operator==(const Attr& other) const {
    return run_presentation(style) == run_presentation(other.style) &&
           links == other.links && revision == other.revision;
  }
  bool operator!=(const Attr& other) const { return !(*this == other); }
};

struct BlockChar {
  std::string text;
  Attr attr;
  std::size_t record = npos;
  std::size_t token = 0;
  bool space = false;
  // Byte offset of this display cell inside its token's decoded word.
  std::uint32_t offset = 0;
  // Byte width in the source projection.  A font-selected code page can
  // produce a UTF-8 glyph whose encoded width differs from that projection.
  std::uint32_t source_width = 0;
  // A blank cell an UNDERSCORE (`_`) span draws as `_`.
  bool filled = false;
};

// One inline's byte range inside a token's decoded word, in source order.
struct TokenClaim {
  std::size_t record = 0;
  std::size_t token = 0;
  std::uint32_t begin = 0;
  std::uint32_t end = 0;
};

// Turns the claims of one inline into source slices: whole tokens compress
// into contiguous token ranges exactly as before, while a token the inline
// owns only in part becomes a single-token slice carrying the byte range.
// BookServer styles part of a decoded word wherever a span boundary falls
// inside one (GC23-046 6.0 `SMPWRK<I>x</I>`, SC09-138 3.3.1
// `<TT>CLIST</TT>s`), so the ledger has to own sub-token ranges to keep
// exactly one owner per display character.
std::vector<DocumentSourceSliceIR> slices_for_claims(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::vector<TokenClaim>& claims) {
  std::vector<DocumentSourceSliceIR> result;
  std::vector<std::pair<std::size_t, std::size_t>> whole;
  const auto flush = [&]() {
    if (whole.empty()) return;
    auto slices = slices_for(records, whole);
    result.insert(result.end(), slices.begin(), slices.end());
    whole.clear();
  };
  for (const auto& claim : claims) {
    const auto length = static_cast<std::uint32_t>(
        display_body_text(view_token(records, claim.record, claim.token))
            .size());
    if (claim.begin == 0 && claim.end == length) {
      whole.emplace_back(claim.record, claim.token);
      continue;
    }
    flush();
    auto slice = token_slice(records[claim.record], claim.token,
                             claim.token + 1);
    slice.character_begin = claim.begin;
    slice.character_end = claim.end;
    result.push_back(slice);
  }
  flush();
  return result;
}

// A span boundary that falls inside a run of word characters is either a
// genuine sub-word highlight or the symptom of a row whose columns are off.
bool word_char(const std::string& text) {
  if (text.empty()) return false;
  const auto ch = static_cast<unsigned char>(text.front());
  return std::isalnum(ch) != 0 || ch >= 0x80 || ch == '_';
}

bool boundary_between(const Line& line, std::size_t left, std::size_t right) {
  if (left >= line.cells.size() || right >= line.cells.size()) return true;
  return line.cells[left].space || line.cells[right].space ||
         !word_char(line.cells[left].text) || !word_char(line.cells[right].text);
}

std::string line_text(const Line& line) {
  std::string text;
  for (const auto& cell : line.cells) text += cell.text;
  return text;
}

// `A` in CFONTDEF is APL, whose cells use IBM code page 293 rather than the
// book's ordinary CP037 interpretation.  The corpus currently supplies only
// byte 0x8A in such spans: CP037 projected that byte as U+00AB, while CP293
// defines it as U+2191.  Keep this deliberately closed over the witnessed
// byte; accepting A as ordinary emphasis would preserve the wrong glyph.
std::string font_cell_text(FontStyleIR style, const std::string& text) {
  if (style == FontStyleIR::apl_symbol && text == "\xC2\xAB")
    return "\xE2\x86\x91";
  return text;
}

// BookServer gives every highlighted phrase its own operand triple, and a
// triple whose boundary falls inside a decoded word always stays inside that
// word: the sub-word cases hosted serves are `<TT>CLIST</TT>s` (SC09-138
// 3.3.1 `cfont 24 5 4`), `SMPWRK<I>x</I>` (GC23-046 6.0 `cfont 43 1 1`),
// `<B>EDCK</B><B><I>nnn</I></B>` (SC09-138 6.2.8.4) and `<B><U>L</B></U>`
// (SG24-204 5.2.1).  A span that starts or ends inside a word *and* reaches
// across a blank column is instead the signature of a row whose left margin
// the model has not proven: every span of such a row is off by the same
// amount and the words come out torn.  That fails the topic closed.
bool torn_across_a_gap(const Line& line, std::size_t begin, std::size_t end) {
  const auto aligned_begin =
      begin <= line.text_begin || boundary_between(line, begin - 1, begin);
  const auto aligned_end =
      end >= line.cells.size() || boundary_between(line, end - 1, end);
  if (aligned_begin && aligned_end) return false;
  for (auto cell = begin; cell < end; ++cell)
    if (line.cells[cell].space) return true;
  return false;
}

// The visible extent of `span` on `line`: clamped to the row's cells and
// trimmed of blank cells at both ends.  Empty when the span covers no text.
std::pair<std::size_t, std::size_t> visible_extent(const Line& line,
                                                   const Span& span) {
  auto begin = span.begin;
  auto end = std::min(span.end, line.cells.size());
  if (begin >= end) return {0, 0};
  while (begin < end && line.cells[begin].space) ++begin;
  while (end > begin && line.cells[end - 1].space) --end;
  return {begin, end};
}

// A selector whose label the record stores glued to the word in front of it.
// The compiler wrote no space between running text and the generated label,
// so one dictionary token spells both: SC24-5527-02 4.2.3 record 399 token
// 294 is the two-byte word `inTable` under `cselect 57 9 TBLXREXBL`, and
// hosted (DT 19921218151459) serves `in<a ...>Table 4-8</a>`; SG24-204 5.3.3
// record 233 token 289 `inFigure` under `cselect 45 26 FIGTRASR07` is served
// `in<a ...>Figure 90 in topic 4.2.2.4</a>` (DT 19971218054640); SC21-8295-03
// 3.1.3.2 record 391 token 158 `manualS` under `cselect 32 41 LNK ...` is
// served `manual<a ...><cite>S/36</cite> ...` (DT 19970409135933).  Such a
// label splits its first word *and* crosses the blanks between its own words,
// which is the signature of a displaced row margin too, so the reading needs
// more than the span: the split falls inside one stored token, the label
// ends on a word boundary, and another span of the same row lands on whole
// display words, which proves the row's columns.  A displaced margin moves
// every span of the row by the same amount and leaves none of them whole.
bool glued_selector_label(const Line& line, const Span& self, std::size_t begin,
                          std::size_t end) {
  if (begin == 0 || begin <= line.text_begin || begin >= line.cells.size())
    return false;
  const auto& left = line.cells[begin - 1];
  const auto& right = line.cells[begin];
  if (left.space || right.space || left.record == npos ||
      left.record != right.record || left.token != right.token)
    return false;
  if (end < line.cells.size() && !boundary_between(line, end - 1, end))
    return false;
  const auto whole = [&](const Span& other) {
    if (&other == &self) return false;
    const auto [other_begin, other_end] = visible_extent(line, other);
    if (other_begin >= other_end) return false;
    return (other_begin == 0 ||
            boundary_between(line, other_begin - 1, other_begin)) &&
           (other_end >= line.cells.size() ||
            boundary_between(line, other_end - 1, other_end));
  };
  return std::any_of(line.fonts.begin(), line.fonts.end(), whole) ||
         std::any_of(line.links.begin(), line.links.end(), whole);
}

// One selector span's destination as the block builder needs it: the typed
// cross-reference target, plus whether the selector is a picture (the
// covered columns are the image, not a link label).
struct LinkTarget {
  ProseCrossReferenceTargetIR target;
  bool picture = false;
};

bool resolve_spans(const Line& line, std::vector<Attr>& attrs,
                   std::vector<LinkTarget>& targets,
                   std::string* error) {
  attrs.assign(line.cells.size(), Attr{});
  const auto apply = [&](const Span& span, bool link) -> bool {
    auto begin = span.begin;
    auto end = span.end;
    const auto where = [&]() {
      return " [" + std::to_string(span.begin) + "," +
             std::to_string(span.end) + ") on '" + line_text(line) + "'";
    };
    if (begin >= end || begin >= line.cells.size())
      return fail(error, "font/selector span [" + std::to_string(begin) + "," +
                             std::to_string(end) +
                             ") exceeds the display line of " +
                             std::to_string(line.cells.size()) + " cells" +
                             where());
    // `_` (UNDERSCORE) underscores every column it covers, blank ones
    // included: hosted BookServer draws a blank column of the span as `_`
    // inside one `<u>`.  SC24-5527-02 COMMENTS (DT 19921218151459) record
    // 978 `cfont ... 29 3 _ 32 3 2 37 3 _ 41 2 2` over
    // `   Would you like a reply?      YES      NO` is served
    // `<u>___</u><B>YES</B>  <u>___</u> <B>NO</B>`, and SL26-0018-02 COVER
    // `cfont 3 17 _` over `CMS/TSO Pipelines` is `<u>CMS/TSO_Pipelines</u>`.
    // The blank cells are therefore content and are not trimmed.  Two shapes
    // are not modelled and fail closed: a span reaching past the row's last
    // cell, which hosted pads out with underscores, and a span sharing cells
    // with another font or a selector, where hosted underscores only the
    // first gap (SG24-3910-01 2.1, SH35-0069-06 4.1.2.2).
    if (!link && span.style == FontStyleIR::underscore) {
      if (end > line.cells.size())
        return fail(error, "underscore span runs past the row" + where());
      if (begin < line.text_begin)
        return fail(error, "underscore span covers the row's structure" +
                               where());
      auto first = begin;
      auto last = end;
      while (first < last && line.cells[first].space) ++first;
      while (last > first && line.cells[last - 1].space) --last;
      if (first < last && torn_across_a_gap(line, first, last))
        return fail(error, "row columns are unproven: underscore span" +
                               where());
      for (auto cell = begin; cell < end; ++cell) {
        auto& attr = attrs[cell];
        if (attr.style != FontStyleIR::unknown || !attr.links.empty())
          return fail(error, "underscore span shares a cell" + where());
        attr.style = span.style;
      }
      return true;
    }
    // A span may run past the last cell the row model materialised: the
    // stored row keeps its trailing display padding, which carries no word.
    // Hosted BookServer styles only the visible text (ACPZMST1 8.1
    // `cselect 3 38 HDRXCCOE` on a 40-column row, GG24-395 PREFACE.3
    // `cselect 3 22 HDRHPRT100` on a 23-column row), and a highlight never
    // continues onto the next display row: every wrapped phrase carries its
    // own triple on each row.
    if (end > line.cells.size()) end = line.cells.size();
    while (begin < end && line.cells[begin].space) ++begin;
    while (end > begin && line.cells[end - 1].space) --end;
    if (begin >= end) return fail(error, "font/selector span is blank" + where());
    // A span boundary inside a decoded word is a fact of the format, not an
    // error: BookServer styles part of one word wherever the operand says so
    // (GC23-046 6.0 `cfont 43 1 1` -> `SMPWRK<I>x</I>`, SG24-204 5.2.1
    // `cfont 33 1 7 34 1 2` -> `<B><U>L</B></U><B>U</B>`, SC09-138 3.3.1
    // `<TT>CLIST</TT>s`).  The inline ledger owns the byte range inside the
    // token's decoded word, so the two inlines that meet inside a word each
    // own their own half and no token is claimed twice.
    if (torn_across_a_gap(line, begin, end) &&
        !(link && glued_selector_label(line, span, begin, end)))
      return fail(error, "row columns are unproven: span [" +
                             std::to_string(span.begin) + "," +
                             std::to_string(span.end) +
                             ") both splits a word and crosses a blank column "
                             "on '" + line_text(line) + "'");
    // The cells in front of a row's text are not the row's words: they are
    // the origin run, the bullet glyph or slot, the revision change bar and
    // (a CZ ordered item) the ordinal label -- structure the reader draws
    // itself from the block it lowers the row into.  A *font* span that lies
    // wholly inside them decorates that structure, and hosted BookServer has
    // nowhere to put the decoration: it serves the row as a list item and
    // prints the marker its own way.  SC09-138 `8.1.10.4` record 1359 stores
    // `cfont 3 1 X 7 4 X 12 7 X` over `   °   char *dsname`, styling the
    // `U+2666` at column 3 as well as the two words; `6.2.2.2` record 900
    // stores `cfont 3 1 X 7 5 X` over `   °   EDCXV`.  The sibling rows of
    // both lists carry the same two word spans and no bullet span at all
    // (`cfont 7 4 X 12 7 X`), and hosted serves every item of the list
    // identically, so the extra triple adds nothing the row displays.  Drop
    // it and keep the row.
    //
    // Fail closed on anything wider.  A span that starts in the structure and
    // reaches into the text means the row's columns and the operand's columns
    // disagree, which is the defect this check exists to catch; and a
    // *selector* span there is a cross reference, which is content, so it
    // still fails the topic rather than being silently dropped.
    if (!link && end <= line.text_begin) return true;
    if (begin < line.text_begin) return fail(error, "span covers the bullet");
    // A font span wholly inside one selector span decorates the link text:
    // hosted BookServer serves ACPZMST1 8.1 as
    // `<a href="8.2..."><B>&quot;Check_On_Event</B> ... <B>8.2</B></a>`.  Link
    // destination and font are independent attributes of the same display
    // cells, so retain both in the prose inline.  A font over a picture
    // placement still has no label to decorate and keeps the prior
    // disposition; a font that only partly meets a selector fails closed.
    if (!link) {
      std::vector<std::size_t> inside;
      bool all_inside = true;
      for (auto cell = begin; cell < end && all_inside; ++cell) {
        if (line.cells[cell].space) continue;
        const auto& attached_links = attrs[cell].links;
        if (attached_links.empty()) all_inside = false;
        else if (inside.empty()) inside = attached_links;
        else if (inside != attached_links) all_inside = false;
      }
      if (all_inside && inside.size() == 1 && targets[inside.front()].picture)
        return true;
    }
    const auto target_index = targets.size();
    if (link)
      targets.push_back({{span.target_kind, span.target,
                          span.target_alternatives, span.target_source},
                         span.picture});
    for (auto cell = begin; cell < end; ++cell) {
      if (line.cells[cell].space) continue;
      auto& attr = attrs[cell];
      if (link) {
        if (span.picture && !attr.links.empty())
          return fail(error, "picture selector overlaps another selector");
        if (std::any_of(attr.links.begin(), attr.links.end(),
                        [&](const auto existing) {
                          return targets[existing].picture;
                        }))
          return fail(error, "selector overlaps a picture selector");
        attr.links.push_back(target_index);
      } else {
        if (attr.style != FontStyleIR::unknown)
          return fail(error, "overlapping font spans" + where());
        attr.style = span.style;
      }
    }
    return true;
  };
  for (const auto& span : line.links)
    if (!apply(span, true)) return false;
  for (const auto& span : line.fonts)
    if (!apply(span, false)) return false;
  return true;
}

// The revision mark a row's display line stores in its margin
// (`DisplayLineIR::margin_mark`, a body row's cell 1), when the row consumed
// that cell as its margin rather than printing it as text: the mark token
// holds no text role in the ledger.  Hosted BookServer prints the mark in the
// row's three-column margin (doc/boo-spec/markup.adoc, "The three-column left
// margin"; BLGR3MST EDITION is served ` | Second Edition (September 1989)`).
// 0 when the row carries none.
char32_t row_revision_mark(const std::vector<DecodedLogicalRecordSource>& records,
                           const Line& line, Ledger& ledger) {
  for (std::size_t cell = line.text_begin; cell < line.cells.size(); ++cell) {
    const auto& source = line.cells[cell];
    if (source.record == npos || source.record >= records.size()) continue;
    const auto& record = records[source.record];
    const auto* display = display_line_of_token(record, source.token);
    if (display == nullptr || !display->margin_mark ||
        display->margin_mark_place != MarginMarkPlaceIR::row_margin)
      return 0;
    const auto role = ledger.at(source.record, display->margin_mark_token).role;
    if (role == ProseTokenRoleIR::text || role == ProseTokenRoleIR::title ||
        role == ProseTokenRoleIR::unassigned)
      return 0;
    return *display->margin_mark;
  }
  return 0;
}

bool build_block(const std::vector<DecodedLogicalRecordSource>& records,
                 const std::vector<Line>& lines, std::size_t begin,
                 std::size_t end, ProseBlockIR& block, Ledger& ledger,
                 std::size_t block_index, std::string* error,
                 bool preserve_whitespace) {
  std::vector<BlockChar> chars;
  std::vector<LinkTarget> targets;
  for (auto index = begin; index < end; ++index) {
    const auto& line = lines[index];
    std::vector<Attr> attrs;
    std::vector<LinkTarget> line_targets;
    if (!resolve_spans(line, attrs, line_targets, error)) return false;
    const auto target_base = targets.size();
    targets.insert(targets.end(), line_targets.begin(), line_targets.end());
    const auto revision = row_revision_mark(records, line, ledger);
    for (auto& attr : attrs) attr.revision = revision;
    if (index != begin) chars.push_back({" ", {}, npos, 0, true, 0, 0});
    // Byte offset of every cell inside its own token's decoded word: cells of
    // one token are emitted in word order, so the running total over the row
    // is that offset.
    std::map<std::pair<std::size_t, std::size_t>, std::uint32_t> offsets;
    for (std::size_t cell = 0; cell < line.cells.size(); ++cell) {
      const auto& source = line.cells[cell];
      std::uint32_t offset = 0;
      if (source.record != npos) {
        auto& running = offsets[{source.record, source.token}];
        offset = running;
        running += static_cast<std::uint32_t>(source.text.size());
      }
      if (cell < line.text_begin) continue;
      auto attr = attrs[cell];
      for (auto& link : attr.links) link += target_base;
      const auto filled =
          source.space && attr.style == FontStyleIR::underscore;
      chars.push_back({filled ? std::string("_")
                              : font_cell_text(attr.style, source.text),
                       attr, source.record, source.token,
                       source.space && !filled, offset,
                       static_cast<std::uint32_t>(source.text.size()),
                       filled});
    }
  }
  // Collapse whitespace; a space takes the attributes of its neighbours when
  // they agree and is plain text otherwise.
  std::vector<BlockChar> collapsed;
  for (std::size_t index = 0; index < chars.size(); ++index) {
    const auto& ch = chars[index];
    if (preserve_whitespace) {
      collapsed.push_back(ch);
      continue;
    }
    if (ch.space) {
      if (collapsed.empty() || collapsed.back().space) continue;
      collapsed.push_back(ch);
      collapsed.back().attr = {};
      continue;
    }
    collapsed.push_back(ch);
  }
  if (!preserve_whitespace)
    while (!collapsed.empty() && collapsed.back().space) collapsed.pop_back();
  if (collapsed.empty()) return fail(error, "block has no visible text");
  // Attribute a complete gap to matching endpoints, including native runs
  // that retain several cells. This changes no text or source claims and
  // keeps one selector a single semantic link across its internal spaces.
  for (std::size_t index = 0; index < collapsed.size();) {
    if (!collapsed[index].space) {
      ++index;
      continue;
    }
    const auto begin = index;
    while (index < collapsed.size() && collapsed[index].space) ++index;
    // A gap between two rows with different revision marks keeps the style
    // the two runs share, so a styled run continues across the margin change
    // and only the mark splits it; the gap itself stays outside the revised
    // side.
    if (begin != 0 && index < collapsed.size() &&
        collapsed[begin - 1].attr == collapsed[index].attr)
      for (auto cell = begin; cell < index; ++cell)
        collapsed[cell].attr = collapsed[index].attr;
    else if (begin != 0 && index < collapsed.size() &&
             collapsed[begin - 1].attr.revision !=
                 collapsed[index].attr.revision) {
      auto left = collapsed[begin - 1].attr;
      left.revision = collapsed[index].attr.revision;
      if (left == collapsed[index].attr) {
        auto gap = collapsed[begin - 1].attr;
        if (collapsed[index].attr.revision == 0) gap.revision = 0;
        for (auto cell = begin; cell < index; ++cell)
          collapsed[cell].attr = gap;
      }
    } else if (begin != 0 && index < collapsed.size() &&
               collapsed[begin - 1].attr.revision ==
                   collapsed[index].attr.revision) {
      // A plain gap inside revised rows stays inside their revised run.
      for (auto cell = begin; cell < index; ++cell)
        collapsed[cell].attr.revision = collapsed[index].attr.revision;
    }
  }
  std::vector<std::pair<std::size_t, std::size_t>> block_refs;
  std::size_t run_begin = 0;
  while (run_begin < collapsed.size()) {
    auto run_end = run_begin;
    while (run_end < collapsed.size() &&
           collapsed[run_end].attr == collapsed[run_begin].attr)
      ++run_end;
    ProseInlineIR inline_node;
    const auto& attr = collapsed[run_begin].attr;
    inline_node.style = attr.style;
    inline_node.revision_mark = attr.revision;
    // A run joining `E` and `X`/`4` cells is the plain example phrase, and
    // one joining `Q` and `Z` cells the phraseless defined term.
    for (auto index = run_begin; index < run_end; ++index)
      if (collapsed[index].attr.style != attr.style)
        inline_node.style = run_presentation(attr.style);
    if (!attr.links.empty()) {
      const auto picture = attr.links.size() == 1 &&
                           targets[attr.links.front()].picture;
      inline_node.kind = picture ? ProseInlineKindIR::image
                                 : ProseInlineKindIR::cross_reference;
      for (const auto link : attr.links)
        inline_node.targets.push_back(targets[link].target);
      inline_node.target = inline_node.targets.front().value;
      inline_node.target_kind = inline_node.targets.front().kind;
    } else if (attr.style != FontStyleIR::unknown) {
      inline_node.kind = ProseInlineKindIR::emphasis;
    }
    // The inline's cells, merged into one claim per token: contiguous byte
    // ranges of the token's decoded word, in source order.
    std::vector<TokenClaim> claims;
    // The blank columns an underscore span draws: their space-run tokens
    // stay layout and are not claimed, but they are the inline's provenance.
    std::vector<TokenClaim> fills;
    for (auto index = run_begin; index < run_end; ++index) {
      const auto& ch = collapsed[index];
      inline_node.text += ch.text;
      if (ch.record == npos) continue;
      if (ledger.at(ch.record, ch.token).role != ProseTokenRoleIR::text) {
        if (ch.filled) {
          const auto end = ch.offset + ch.source_width;
          if (!fills.empty() && fills.back().record == ch.record &&
              fills.back().token == ch.token && fills.back().end == ch.offset)
            fills.back().end = end;
          else
            fills.push_back({ch.record, ch.token, ch.offset, end});
        }
        continue;  // space-run/origin cells keep their layout role
      }
      const auto end = ch.offset + ch.source_width;
      if (!claims.empty() && claims.back().record == ch.record &&
          claims.back().token == ch.token) {
        if (claims.back().end != ch.offset)
          return fail(error, "inline owns a discontinuous part of a word");
        claims.back().end = end;
        continue;
      }
      claims.push_back({ch.record, ch.token, ch.offset, end});
    }
    // The image replaces exactly the placeholder words the compiler wrote
    // into the covered columns; anything else means the selector's columns
    // are not the ones the model read, and the topic fails closed.
    if (inline_node.kind == ProseInlineKindIR::image) {
      const auto placeholder = figure_picture_placeholder(inline_node.target);
      if (!picture_placeholder_matches(inline_node.text, inline_node.target))
        return fail(error, "inline picture selector covers '" +
                               inline_node.text + "', not '" + placeholder +
                               "'");
    }
    std::vector<std::pair<std::size_t, std::size_t>> text_refs;
    for (const auto& claim : claims) {
      auto& entry = ledger.at(claim.record, claim.token);
      if (entry.block != npos && entry.block != block_index)
        return fail(error, "text token shared by two blocks");
      if (!entry.claims.empty() &&
          entry.claims.back().character_end != claim.begin)
        return fail(error, "token claimed twice by one inline run");
      if (entry.claims.empty()) {
        entry.block = block_index;
        entry.inline_index = block.inlines.size();
      }
      entry.claims.push_back({block_index, block.inlines.size(), claim.begin,
                              claim.end});
      text_refs.emplace_back(claim.record, claim.token);
    }
    std::sort(text_refs.begin(), text_refs.end());
    text_refs.erase(std::unique(text_refs.begin(), text_refs.end()),
                    text_refs.end());
    inline_node.slices = slices_for_claims(records, claims);
    if (!fills.empty()) {
      auto fill_slices = slices_for_claims(records, fills);
      inline_node.slices.insert(inline_node.slices.end(), fill_slices.begin(),
                                fill_slices.end());
      std::stable_sort(inline_node.slices.begin(), inline_node.slices.end(),
                       [](const auto& left, const auto& right) {
                         return std::tie(left.logical_record, left.token_begin,
                                         left.character_begin) <
                                std::tie(right.logical_record,
                                         right.token_begin,
                                         right.character_begin);
                       });
    }
    block_refs.insert(block_refs.end(), text_refs.begin(), text_refs.end());
    if (inline_node.kind != ProseInlineKindIR::text &&
        inline_node.slices.empty())
      return fail(error, "styled inline has no source provenance");
    block.inlines.push_back(std::move(inline_node));
    run_begin = run_end;
  }
  std::sort(block_refs.begin(), block_refs.end());
  // A CZ definition entry or note builds its term and body with two calls
  // on one block; the term precedes the body in source order.
  auto slices = slices_for(records, block_refs);
  block.slices.insert(block.slices.end(), slices.begin(), slices.end());
  return true;
}

// A drawn box region: its rows are the hosted display lines verbatim
// (prose_topic_boxes.cpp). Native CFONT/CSELECT coordinates still apply to
// those rows, so the ordinary inline builder partitions each non-blank row
// without changing its cells. The exact row extent is carried separately:
// a genuine empty display row has no cell from which to reconstruct it.
bool build_box_block(const std::vector<DecodedLogicalRecordSource>& records,
                     const std::vector<Line>& lines, std::size_t begin,
                     std::size_t end, ProseBlockIR& block, Ledger& ledger,
                     std::size_t block_index, std::string* error) {
  block.kind = ProseBlockKindIR::preformatted;
  block.verbatim_kind = lines[begin].grid ? "prose-implicit-grid-verbatim"
                                           : "prose-drawn-box-verbatim";
  std::vector<std::string> rows;
  std::vector<std::pair<std::size_t, std::size_t>> block_refs;
  for (auto index = begin; index < end; ++index) {
    const auto& line = lines[index];
    if (line.box_record == npos || line.box_record >= records.size() ||
        line.box_token_begin >= line.box_token_end)
      return fail(error, "drawn box row has no native source extent");
    auto text = line_text(line);
    std::vector<std::pair<std::size_t, std::size_t>> refs;
    for (const auto& cell : line.cells) {
      if (cell.record == npos) continue;
      auto& entry = ledger.at(cell.record, cell.token);
      if (entry.role != ProseTokenRoleIR::text) continue;
      refs.emplace_back(cell.record, cell.token);
    }
    std::sort(refs.begin(), refs.end());
    refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
    if (!refs.empty()) {
      auto native = line;
      native.text_begin = 0;
      if (!build_block(records, {native}, 0, 1, block, ledger, block_index,
                       error, true))
        return false;
      block_refs.insert(block_refs.end(), refs.begin(), refs.end());
    }
    block.preformatted_line_inline_ends.push_back(block.inlines.size());
    block.preformatted_line_sources.push_back(token_slice(
        records[line.box_record], line.box_token_begin, line.box_token_end));
    rows.push_back(std::move(text));
  }
  if (block.inlines.empty())
    return fail(error, "drawn box block has no text row");
  // The box's left margin is content, not padding: hosted BookServer serves
  // the region inside `<pre>` at the columns the display lines put the box
  // in.  Measured over all 856 drawn box regions the corpus emits against
  // hosted: with the common indent removed not one matched hosted line for
  // line; keeping it makes 628 of 856 (73.4%) exact, and no block that
  // already differed got worse.  The margin varies per region, so no single
  // column can be reinstated later -- GC23-046 serves boxes at column 3,
  // OFCUSEOV `4.2.2` at 7, GC23-046 `7.5.3` at 1, QSYSNEWG `7.3` at 19.
  block.preformatted_lines = std::move(rows);
  std::sort(block_refs.begin(), block_refs.end());
  block.slices = slices_for(records, block_refs);
  return true;
}

// True when the display line directly above `line`'s first source cell is a
// subject-index line: its first content token is the `SI` keyword the
// display-line pass claimed (`index_keyword`).  Such a line displays nothing.
bool follows_index_line(const std::vector<DecodedLogicalRecordSource>& records,
                        const Line& line, Ledger& ledger) {
  const auto cell = std::find_if(
      line.cells.begin(), line.cells.end(),
      [](const Cell& candidate) { return candidate.record != npos; });
  if (cell == line.cells.end() || cell->record >= records.size()) return false;
  const auto* display = record_display_lines(records[cell->record]);
  if (display == nullptr) return false;
  const auto own = std::find_if(
      display->begin(), display->end(), [&](const DisplayLineIR& candidate) {
        return cell->token >= candidate.prefix_token &&
               cell->token < candidate.token_end;
      });
  if (own == display->end()) return false;
  std::size_t record = cell->record;
  const DisplayLineIR* above = nullptr;
  if (own != display->begin()) {
    above = &*(own - 1);
  } else {
    if (record == 0) return false;
    --record;
    const auto* previous = record_display_lines(records[record]);
    if (previous == nullptr || previous->empty()) return false;
    above = &previous->back();
  }
  const auto first = above->prefix_token + 1;
  if (first >= above->token_end) return false;
  return ledger.at(record, first).role == ProseTokenRoleIR::index_keyword;
}

bool build_blocks(const std::vector<DecodedLogicalRecordSource>& records,
                  const LineBuild& lines_build, Ledger& ledger,
                  ProseTopicIR& topic, std::string* error) {
  // The CZ dialect names every block boundary; the origin heuristics below
  // are for the flattened dialect only.
  if (!lines_build.directives.empty())
    return build_cz_blocks(records, lines_build, ledger, topic, error);
  const auto& lines = lines_build.lines;
  std::vector<std::pair<std::size_t, std::size_t>> ranges;  // [begin,end)
  std::vector<bool> is_item;
  std::vector<bool> is_box;
  std::vector<bool> is_thematic_break;
  std::vector<std::size_t> origins;
  // A table/figure span between two lines ends the block before it.
  std::set<std::size_t> span_lines;
  for (const auto& mark : lines_build.span_marks) {
    if (mark.span >= topic.spans.size() || mark.line > lines.size())
      return fail(error, "span mark addresses no span or line");
    span_lines.insert(mark.line);
  }
  std::size_t index = 0;
  std::size_t carried_breaks = 0;
  while (index < lines.size()) {
    const auto& first = lines[index];
    if (first.thematic_break) {
      ranges.emplace_back(index, index + 1);
      is_item.push_back(false);
      is_box.push_back(false);
      is_thematic_break.push_back(true);
      origins.push_back(first.origin);
      ++index;
      carried_breaks = 0;
      continue;
    }
    // A drawn box region: its rows are verbatim display lines and form one
    // preformatted block.  Regions drawn directly under one another share
    // the block, but a region that empty display lines or a table/figure
    // span separate from the one above starts its own: hosted BookServer
    // prints those rows between the boxes.  QSYSNEWG 3.1.2.3 (DT
    // 19910524085706) draws the `F1=Help` box, three empty lines, the
    // `SRFIGFIGUNIQ43` picture, four empty lines and the `F3=Exit` box (record
    // 93 lines 34-48); run together, the picture moved behind the last box.
    if (first.box) {
      auto end = index + 1;
      while (end < lines.size() && lines[end].box &&
             !(lines[end].box_first &&
               (lines[end].breaks_before != 0 ||
                span_lines.count(end) != 0)))
        ++end;
      ranges.emplace_back(index, end);
      is_item.push_back(false);
      is_box.push_back(true);
      is_thematic_break.push_back(false);
      origins.push_back(first.origin);
      index = end;
      carried_breaks = 0;
      continue;
    }
    if (first.cells.size() <= first.text_begin) {
      // A row without text is vertical spacing between blocks.
      if (first.bullet)
        return fail(error, "bullet row has no text: '" + line_text(first) +
                               "'");
      carried_breaks += first.breaks_before + 1;
      ++index;
      continue;
    }
    carried_breaks = 0;
    const auto next_span = span_lines.upper_bound(index);
    const auto example_limit = next_span == span_lines.end()
                                  ? lines.size() : *next_span;
    const auto example_end = styled_example_range_end(
        records, lines, index, example_limit, ledger);
    if (example_end > index) {
      ranges.emplace_back(index, example_end);
      is_item.push_back(false);
      is_box.push_back(false);
      is_thematic_break.push_back(false);
      origins.push_back(first.origin);
      index = example_end;
      continue;
    }
    auto end = index + 1;
    // The paragraph's margin.  A paragraph whose first line stands directly
    // below a subject-index line is sometimes set one column deeper than the
    // lines that continue it: ITPPIBOK 4.3.5.4 record 158 line 11 is
    // `    The final section ... to follow` (a four-cell origin, token 43)
    // under the hidden `SI schedule, example` line, and line 12 goes on at
    // three cells, `   during the test.`  No empty display line stands
    // between them, and hosted DT 19910628074854 serves both rows, the
    // extra blank included, in one paragraph.  Across the corpus the shape
    // is always that one column (144 paragraphs in 136 topics, 4 then 3
    // cells, every one a sentence that runs on), while a line after an `SI`
    // line otherwise keeps its paragraph's margin (over two thousand
    // paragraphs).  So the one-column step after an index line is the
    // line's own extra blank, not an outdent, and the paragraph keeps the
    // margin of the line that continues it.  Any other outdent still ends
    // the block.
    auto margin = first.origin;
    while (end < lines.size()) {
      const auto& next = lines[end];
      if (span_lines.count(end) != 0) break;
      if (next.box) break;
      if (next.breaks_before != 0 || next.anchor_before || next.bullet) break;
      if (next.cells.size() <= next.text_begin) break;
      if (first.bullet && next.origin <= first.origin) break;
      if (!first.bullet && end == index + 1 && next.origin + 1 == margin &&
          follows_index_line(records, first, ledger))
        margin = next.origin;
      if (!first.bullet && next.origin < margin) break;
      if (styled_example_range_end(records, lines, end, example_limit, ledger) > end)
        break;
      ++end;
    }
    ranges.emplace_back(index, end);
    is_item.push_back(first.bullet);
    is_box.push_back(false);
    is_thematic_break.push_back(false);
    origins.push_back(margin);
    index = end;
  }
  std::size_t list_ordinal = 0;
  // The list items still open at the current block, outermost first.  An item
  // whose row stands at the text column of the open item above it is the
  // first item of a list one level down, owned by that item: ACPZMST1 1.2
  // record 32 sets `   °   Communications partners` (text at column 7) and
  // then `       -   Resource manager` (bullet at column 7), and hosted
  // (DT 19920319123146) prints the `-` rows inside the item.  An item at an
  // open item's origin is that item's sibling, closing every deeper level.
  // Any other origin is a level the rows do not prove, and fails closed.
  struct OpenItem {
    std::size_t block = 0;
    std::size_t origin = 0;
    std::size_t text_column = 0;
    std::size_t ordinal = 0;
  };
  std::vector<OpenItem> open_items;
  for (std::size_t block_index = 0; block_index < ranges.size(); ++block_index) {
    ProseBlockIR block;
    block.origin = origins[block_index];
    if (is_thematic_break[block_index]) {
      const auto& source_line = lines[ranges[block_index].first];
      if (source_line.thematic_record >= records.size() ||
          source_line.thematic_token_begin >=
              source_line.thematic_token_end)
        return fail(error, "thematic break has no native row provenance");
      block.kind = ProseBlockKindIR::thematic_break;
      block.slices.push_back(token_slice(
          records[source_line.thematic_record],
          source_line.thematic_token_begin, source_line.thematic_token_end));
      topic.blocks.push_back(std::move(block));
      continue;
    }
    if (is_box[block_index]) {
      if (!build_box_block(records, lines, ranges[block_index].first,
                           ranges[block_index].second, block, ledger,
                           block_index, error))
        return false;
      topic.blocks.push_back(std::move(block));
      continue;
    }
    if (!is_item[block_index]) open_items.clear();
    if (is_item[block_index]) {
      block.kind = ProseBlockKindIR::list_item;
      const auto origin = origins[block_index];
      const auto self = topic.blocks.size();
      while (!open_items.empty() && open_items.back().origin > origin)
        open_items.pop_back();
      OpenItem opened{self, origin, lines[ranges[block_index].first].text_begin,
                      0};
      if (block_index == 0 || !is_item[block_index - 1] ||
          open_items.empty()) {
        opened.ordinal = ++list_ordinal;
        open_items.assign(1, opened);
      } else if (!open_items.empty() && open_items.back().origin == origin) {
        opened.ordinal = open_items.back().ordinal;
        open_items.back() = opened;
        if (open_items.size() > 1)
          block.parent_block = open_items[open_items.size() - 2].block;
      } else if (!open_items.empty() &&
                 open_items.back().text_column == origin &&
                 open_items.back().origin < origin) {
        opened.ordinal = ++list_ordinal;
        block.parent_block = open_items.back().block;
        open_items.push_back(opened);
      } else {
        return fail(error,
                    "nested or misaligned list items (origin " +
                        std::to_string(origin) + " after an item at " +
                        std::to_string(open_items.back().origin) +
                        " with text at " +
                        std::to_string(open_items.back().text_column) + ")");
      }
      if (block.parent_block != npos)
        topic.blocks[block.parent_block].body_blocks.push_back(self);
      block.list_ordinal = opened.ordinal;
    }
    if (!is_item[block_index]) {
      bool example = false;
      if (!build_styled_example_block(records, lines, ranges[block_index].first,
                                     ranges[block_index].second, block, ledger,
                                     block_index, example, error)) return false;
      if (example) {
        topic.blocks.push_back(std::move(block));
        continue;
      }
    }
    if (!build_block(records, lines, ranges[block_index].first,
                     ranges[block_index].second, block, ledger, block_index,
                     error))
      return false;
    topic.blocks.push_back(std::move(block));
  }
  // Anchors: leading ones precede block 0; body anchors precede the block
  // that starts at their line, or the end.
  const auto leading_anchors = topic.anchors.size();
  struct Placement {
    std::size_t position = 0;
    std::size_t row = static_cast<std::size_t>(-1);
    bool found = false;
  };
  std::vector<Placement> placements;
  for (std::size_t anchor = 0; anchor < lines_build.body_anchors.size();
       ++anchor) {
    Placement placed;
    placed.position = ranges.size();
    for (std::size_t block_index = 0; block_index < ranges.size(); ++block_index) {
      const auto& first = lines[ranges[block_index].first];
      if (first.anchor_before && first.anchor_index == anchor) {
        placed.position = block_index;
        placed.found = true;
        break;
      }
    }
    // A drawn box carries its envelope anchors on the rows they open
    // (`emit_box`); every other block opens at its anchored line.
    for (std::size_t block_index = 0; !placed.found && block_index < ranges.size();
         ++block_index) {
      for (auto line = ranges[block_index].first + 1;
           line < ranges[block_index].second; ++line) {
        if (!lines[line].anchor_before || lines[line].anchor_index != anchor)
          continue;
        if (!is_box[block_index])
          return fail(error, "anchor stands inside a block that is no box");
        placed.position = block_index;
        placed.row = line - ranges[block_index].first;
        placed.found = true;
        break;
      }
    }
    // An anchored line that draws nothing forms no block: the anchor
    // precedes the first block after it.  A display line's length byte can
    // decode as a blank run (dfhla313 1.2.3.1.3 record 791 token 190, value
    // 16, seven blanks) that opens a line for an index control line
    // (`SI ...`); `SRHDRDFHA314` (token 182) before it then belongs to the
    // paragraph `CICS task manager parameters: ...` the record goes on to
    // store, not to the end of the topic.
    for (std::size_t line = 0; !placed.found && line < lines.size(); ++line) {
      if (!lines[line].anchor_before || lines[line].anchor_index != anchor)
        continue;
      for (std::size_t block_index = 0; block_index < ranges.size();
           ++block_index)
        if (ranges[block_index].first > line) {
          placed.position = block_index;
          break;
        }
      placed.found = true;
    }
    placements.push_back(placed);
  }
  // An anchor followed by another before any row opened marks no row of its
  // own (only the last of the run does, `Line::anchor_index`); it precedes
  // the row its run's last anchor names.  Hosted SH24-5221-01 4.2 (DT
  // 19930802093837) serves a bare `SRMSG` and the next `SRMSG 002` as
  // `<a name="MSG"><a name="MSG 002"><hr>`, and 2.0 `SRSPTMSGS` + `SRMSG
  // 000I` as `<a name="SPTMSGS"><a name="MSG 000I"><hr>`.  The CZ dialect
  // places its runs the same way (`place_anchors`, prose_topic_cz.cpp).
  for (std::size_t anchor = 0; anchor < placements.size(); ++anchor) {
    auto& placed = placements[anchor];
    for (auto next = anchor;
         !placed.found && next < lines_build.anchor_successors.size() &&
         lines_build.anchor_successors[next] != npos;) {
      next = lines_build.anchor_successors[next];
      if (next >= placements.size())
        return fail(error, "anchor successor is out of range");
      if (placements[next].found) placed = placements[next];
    }
    auto ir = lines_build.body_anchors[anchor];
    ir.position = placed.position;
    ir.row = placed.row;
    topic.anchors.push_back(std::move(ir));
  }
  // Spans precede the first block starting at or after their line (or the
  // end) and, within that position, follow the leading anchors and the body
  // anchors already seen at their line.
  std::vector<bool> placed_spans(topic.spans.size(), false);
  for (const auto& mark : lines_build.span_marks) {
    if (placed_spans[mark.span])
      return fail(error, "span is marked twice");
    placed_spans[mark.span] = true;
    std::size_t position = ranges.size();
    for (std::size_t block_index = 0; block_index < ranges.size(); ++block_index)
      if (ranges[block_index].first >= mark.line) {
        position = block_index;
        break;
      }
    auto& span = topic.spans[mark.span];
    span.position = position;
    span.anchors_before = position == 0 ? leading_anchors : 0;
    for (std::size_t anchor = 0;
         anchor < mark.anchors_seen && anchor < lines_build.body_anchors.size();
         ++anchor)
      if (topic.anchors[leading_anchors + anchor].position == position)
        ++span.anchors_before;
  }
  if (std::any_of(placed_spans.begin(), placed_spans.end(),
                  [](const bool placed) { return !placed; }))
    return fail(error, "table/figure span never reached the body stream");
  return true;
}

// ---------------------------------------------------------------------------
// Trailing menu
// ---------------------------------------------------------------------------

bool build_menu(const std::vector<DecodedLogicalRecordSource>& records,
                const StreamBuild& build,
                const BookTopicCatalogIR* book_topic_catalog, Ledger& ledger,
                ProseTopicIR& topic, std::string* error) {
  if (build.menu_record == npos) return true;
  if (book_topic_catalog == nullptr)
    return fail(error, "trailing menu needs the book topic catalog");
  std::string menu_error;
  const auto raw = extract_source_menu_ir(records, &menu_error);
  if (!raw) return fail(error, "trailing menu rejected: " + menu_error);
  auto validation =
      validate_source_menu_targets(*raw, *book_topic_catalog, &menu_error);
  if (!validation) {
    // The catalog's topic-header title is a compatibility projection that can
    // carry glued body text (`<title>??? SI ...`); the TOC title is the
    // canonical spelling BookServer prints.  Accept a label that equals the
    // target's TOC title verbatim.
    MenuTargetValidationIR fallback;
    for (const auto& item : raw->items) {
      const auto* entry =
          find_book_topic_catalog_entry(*book_topic_catalog, item.target);
      if (entry == nullptr)
        return fail(error, "trailing menu targets rejected: " + menu_error);
      // The full label is tried first: a one-byte final word such as
      // SH20-918 `... SCRIPT/VS and GML` is title text.  Only when it
      // disagrees is the source-proven compact terminal token (SC31-711
      // record 127 `Generic Traps :`) excluded.
      const auto label_matches = [&](const std::string& candidate) {
        if (candidate.empty()) return false;
        if (std::any_of(entry->toc_entries.begin(), entry->toc_entries.end(),
                        [&](const auto& toc) {
                          return ascii_equals_case_insensitive(
                              candidate,
                              collapse_ascii_whitespace(trim_ascii(toc.title)));
                        }))
          return true;
        if (!entry->topic_header) return false;
        // The header title is now the visible text of the target's own `ST`
        // display line (topic_header_title.hpp), which is the same row the
        // label is a projection of, so the two agree exactly or not at all.
        return ascii_equals_case_insensitive(
            candidate,
            collapse_ascii_whitespace(trim_ascii(entry->topic_header->title)));
      };
      auto label = collapse_ascii_whitespace(trim_ascii(item.text));
      auto matches = label_matches(label);
      // The item's two source-proven terminal tokens carry no label text
      // once the catalog agrees without them: the record terminator `.`
      // standing before CEMENU, and a compact display marker.
      const auto without = [&](std::size_t label_cell_begin) {
        std::string stripped;
        for (std::size_t cell = 0;
             cell < label_cell_begin && cell < item.label_cells.size(); ++cell)
          stripped += token_words_to_ascii({item.label_cells[cell].word});
        return collapse_ascii_whitespace(trim_ascii(stripped));
      };
      if (!matches && item.terminator) {
        const auto stripped = without(item.terminator->label_cell_begin);
        if (label_matches(stripped)) {
          label = stripped;
          matches = true;
        }
      }
      if (!matches && item.compact_terminal) {
        const auto stripped = without(item.compact_terminal->label_cell_begin);
        if (label_matches(stripped)) {
          label = stripped;
          matches = true;
        }
      }
      if (!matches)
        return fail(error, "trailing menu targets rejected: " + menu_error);
      MenuTargetValidationEntryIR validated;
      validated.target = item.target;
      validated.label = label;
      validated.existence =
          MenuTargetValidationEntryIR::ExistenceEvidence::toc_entry;
      validated.label_evidence =
          MenuTargetValidationEntryIR::LabelEvidence::toc_title;
      fallback.items.push_back(std::move(validated));
    }
    validation = std::move(fallback);
  }
  if (validation->items.size() != raw->items.size())
    return fail(error, "trailing menu validation is incomplete");
  // Every token from the menu start onwards belongs to the menu.
  for (auto record_index = build.menu_record; record_index < records.size();
       ++record_index) {
    const auto& record = records[record_index];
    std::size_t first_token = 0;
    if (record_index == build.menu_record) {
      if (build.menu_segment >= record.control_segments.size())
        return fail(error, "menu record has no segments");
      const auto& menu = record.control_segments[build.menu_segment];
      if (menu.source_tokens.empty())
        return fail(error, "menu start has no source token");
      first_token = menu.source_tokens.front();
    }
    for (auto token = first_token; token < record.ir.tokens.size(); ++token) {
      if (ledger.at(record_index, token).role != ProseTokenRoleIR::unassigned)
        continue;
      if (!ledger.assign(record_index, token, ProseTokenRoleIR::menu, error))
        return false;
    }
    // Anything before the menu start in this record must already be owned.
  }
  for (std::size_t index = 0; index < raw->items.size(); ++index) {
    const auto& item = raw->items[index];
    const auto& entry = validation->items[index];
    if (entry.target != item.target)
      return fail(error, "menu validation target mismatch");
    ProseMenuItemIR typed;
    typed.target = entry.target;
    typed.label = entry.label;
    const auto record = std::find_if(
        records.begin(), records.end(), [&](const auto& candidate) {
          return candidate.logical_record == item.logical_record;
        });
    if (record == records.end() || item.target_cells.empty())
      return fail(error, "menu item provenance is incomplete");
    auto begin = item.target_cells.front().token_index;
    auto end = begin + 1;
    for (const auto* cells : {&item.target_cells, &item.label_cells})
      for (const auto& cell : *cells) {
        begin = std::min(begin, cell.token_index);
        end = std::max(end, cell.token_index + 1);
      }
    typed.source = token_slice(*record, begin, end);
    topic.menu_items.push_back(std::move(typed));
  }
  if (topic.menu_items.empty()) return fail(error, "trailing menu is empty");
  return true;
}

} // namespace geist::detail::prose_internal
