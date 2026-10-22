// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/prose/prose_topic_internal.hpp"

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/ir/figure_block_ir.hpp"
#include "geist/detail/ir/selector_ir.hpp"
#include "geist/detail/ir/selector_link_ir.hpp"

#include <algorithm>
#include <map>
#include <utility>

// Table/figure span plan of the prose topic family.  The body of a prose
// topic is a sequence of spans in source order; this unit finds the
// non-prose spans before the stream pass runs, so that every token of a
// table envelope or figure region is owned by exactly one typed block and
// the stream pass sees the regions as opaque boundaries.
//
// Conservation rule: a region is the source extent from the block's first
// control token to its last owned token.  Inside it, every token the block
// claims receives the span's role; a token the block does not claim is
// admitted only when it carries no visible word (bare spacing, space run,
// decoder placeholder run or separator) and is then region structure of
// the same span; any other token rejects the topic.  A token claimed by two
// blocks, or by a block and the envelope, rejects through the ledger.
namespace geist::detail::prose_internal {

// The `cz OFF <verbatim>` .. `cz OFF E<verbatim>` regions of the topic, as
// closed [begin, end] segment-position ranges.  Inside such a region every
// display row is served character for character, so an `SRFIG`/`SRTBL`
// envelope that falls inside one is drawn box art and not an object: hosted
// (SC41-4853-00 `1.2` DT 19951003131222) serves the whole labelled box,
// grid rows included, as one `<pre width="132"><!-- lblbox -->` and emits no
// `<table>` -- the envelope's anchors become `<a name="TBLTBLUNIQ1">` on the
// box rows they open.  `cz OFF TABLE` is the only mark of a genuine table
// and there is none here.
//
// The mode and tag are read from the directive's own operand tokens, which
// the display-line framing already bounds to its own row.
std::vector<CzVerbatimRegion> cz_verbatim_regions(
    const std::vector<DecodedLogicalRecordSource>& records) {
  std::vector<CzVerbatimRegion> regions;
  bool open = false;
  std::pair<std::size_t, std::size_t> begin{};
  std::string open_tag;
  for (std::size_t index = 0; index < records.size(); ++index) {
    for (const auto& segment : records[index].control_segments) {
      if (segment.kind != BookControlKind::layout_directive ||
          segment.malformed || segment.source_tokens.empty())
        continue;
      std::vector<std::string> words;
      for (const auto token : segment.source_tokens) {
        const auto view = view_token(records, index, token);
        if (!is_visible(view) || is_padding(view)) continue;
        words.push_back(ascii_lower(body_text(view)));
        if (words.size() == 3) break;
      }
      if (words.size() < 3 || words.front() != "cz" || words[1] != "off")
        continue;
      const auto& tag = words.back();
      if (!open && cz_verbatim_region_tag(tag)) {
        open = true;
        open_tag = tag;
        begin = {index, segment.source_tokens.front()};
      } else if (open && tag == "e" + open_tag) {
        open = false;
        regions.push_back({begin, {index, segment.source_tokens.back()}});
      }
    }
  }
  // An unterminated region runs to the end of the topic (SG24-204 `NOTICES`).
  if (open && !records.empty())
    regions.push_back(
        {begin, {records.size() - 1,
                 records.back().ir.tokens.empty()
                     ? std::size_t{0}
                     : records.back().ir.tokens.size() - 1}});
  return regions;
}

bool inside_cz_verbatim(const std::vector<CzVerbatimRegion>& regions,
                        std::size_t record, std::size_t token) {
  const std::pair<std::size_t, std::size_t> at{record, token};
  return std::any_of(regions.begin(), regions.end(), [&](const auto& region) {
    return !(at < region.begin) && !(region.end < at);
  });
}


namespace {

using Claim = std::pair<std::size_t, std::size_t>;  // record index, token

struct Region {
  ProseSpanKindIR kind = ProseSpanKindIR::table;
  std::size_t index = 0;  // into tables.blocks / figures.blocks
  SpanRegion extent;
  std::vector<Claim> claims;
  std::vector<ProseTableLinkIR> links;
  std::vector<ProseIndexTermIR> index_terms;
};

bool region_structure(const TokenView& view) {
  return is_bare(view) || is_space_run(view) || is_placeholder_run(view) ||
         is_separator(view) || is_padding(view);
}

// A one-byte token between table lines that no physical row positioned is
// a hidden marker slot: punctuation (GG24-395 2.2.2.2 `|`, GG24-4302-00 6.8
// `-`, 7.1 `(`) or a word-shaped slot in the row-control byte range (SC26-457
// 3.6.2 `a`, GG24-4302-00 7.1 `an`, SC24-5520-00 2.1 `are`), the same range
// the prose rows treat as row controls.  Such slots are never displayed.
bool table_marker_slot(const TokenView& view) {
  return view.width == 1 &&
         (punctuation_glyph_token(view) || view.value < row_control_byte_limit);
}

// The tokens of a `SREFIG` end marker: the opcode of a structural segment,
// or the first visible word of a text segment (`SREFIG.`), which must spell
// the marker.  Returns false when the segment is no end marker.
bool figure_end_tokens(const std::vector<DecodedLogicalRecordSource>& records,
                       std::size_t record, const ControlSegmentIR& segment,
                       std::vector<std::size_t>& tokens) {
  tokens.clear();
  if (segment.kind == BookControlKind::structural) {
    if (ascii_lower(segment.opcode).rfind("srefig", 0) != 0) return false;
    tokens = operand_tokens(records[record], segment);
    if (tokens.empty()) tokens = segment.source_tokens;
    return !tokens.empty();
  }
  if (segment.kind != BookControlKind::text) return false;
  for (const auto token : segment.source_tokens) {
    const auto view = view_token(records, record, token);
    if (!is_visible(view)) continue;
    if (ascii_lower(body_text(view)).rfind("srefig", 0) != 0) return false;
    tokens.push_back(token);
    return true;
  }
  return false;
}

std::optional<std::size_t> record_index_of(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::uint32_t logical_record) {
  for (std::size_t index = 0; index < records.size(); ++index)
    if (records[index].logical_record == logical_record) return index;
  return std::nullopt;
}

// An envelope inside a closed drawn box is the box's own markup
// (`box_enclosed_envelopes`, prose_topic_boxes.cpp) and no table either.
// Only a topic whose every envelope is verbatim or box markup skips the
// table pass; a topic with one genuine envelope runs it over all of them,
// and the box then belongs to the table as before.
bool has_table_envelope(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::vector<CzVerbatimRegion>& verbatim,
    const std::set<std::pair<std::size_t, std::size_t>>& box_markup) {
  for (std::size_t index = 0; index < records.size(); ++index)
    for (const auto& segment : records[index].control_segments)
      if ((segment.kind == BookControlKind::table_start ||
           segment.kind == BookControlKind::table_end) &&
          !segment.source_tokens.empty() &&
          !inside_cz_verbatim(verbatim, index, segment.source_tokens.front()) &&
          !box_markup.count({index, segment.source_tokens.front()}))
        return true;
  return false;
}

bool has_figure_region(const std::vector<DecodedLogicalRecordSource>& records,
                       const std::vector<CzVerbatimRegion>& verbatim) {
  for (std::size_t index = 0; index < records.size(); ++index) {
    const auto& record = records[index];
    const auto text = token_words_to_ascii(record.assembled.words);
    for (const auto& segment : record.control_segments) {
      if (!segment.source_tokens.empty() &&
          inside_cz_verbatim(verbatim, index, segment.source_tokens.front()))
        continue;
      const auto opcode = ascii_lower(segment.opcode);
      if (segment.kind == BookControlKind::structural &&
          (opcode.rfind("srfig", 0) == 0 || opcode.rfind("srefig", 0) == 0))
        return true;
      if (segment.kind != BookControlKind::select) continue;
      const auto operand = ascii_lower(text.substr(
          segment.operand_range.begin,
          segment.operand_range.end - segment.operand_range.begin));
      // `<col> <len> PIC<n>` / `<col> <len> LNK ...`
      const auto target = operand.find_last_of(' ');
      const auto tail =
          target == std::string::npos ? operand : operand.substr(target + 1);
      if (tail.rfind("pic", 0) == 0) return true;
      if (operand.find(" lnk") != std::string::npos) return true;
    }
  }
  return false;
}

bool table_region(const std::vector<DecodedLogicalRecordSource>& records,
                  const FixedTableBlockIR& block,
                  const std::set<std::string>* resource_ids, Region& region,
                  std::string* error) {
  const auto& source = block.object_source;
  const auto begin_record = record_index_of(records, source.logical_record);
  if (!begin_record || source.token_begin >= source.token_end)
    return fail(error, "table '" + block.object_id +
                           "' has no SRTBL source position");
  region.extent.begin_record = *begin_record;
  region.extent.begin_segment = source.segment_index;
  region.extent.begin_token = source.token_begin;
  for (auto token = source.token_begin; token < source.token_end; ++token)
    region.claims.emplace_back(*begin_record, token);

  // The closing SRETBL: the first table end after the SRTBL segment.
  bool closed = false;
  for (auto record = *begin_record; record < records.size() && !closed;
       ++record) {
    const auto& segments = records[record].control_segments;
    const auto first =
        record == *begin_record ? source.segment_index + 1 : std::size_t{0};
    for (auto index = first; index < segments.size(); ++index) {
      const auto& segment = segments[index];
      if (segment.kind != BookControlKind::table_end) continue;
      if (segment.source_tokens.empty())
        return fail(error, "table '" + block.object_id +
                               "' closes with a token-less SRETBL");
      auto operands = operand_tokens(records[record], segment);
      if (operands.empty()) operands = segment.source_tokens;
      region.extent.end_record = record;
      region.extent.end_segment = index;
      region.extent.end_token = operands.back();
      for (const auto token : operands) region.claims.emplace_back(record, token);
      closed = true;
      break;
    }
  }
  if (!closed && block.closed_by_topic_end) {
    // No SRETBL follows: the table pass proved the envelope runs to the end
    // of the topic, so the region ends at the topic's last token.
    region.extent.end_record = records.size() - 1;
    region.extent.end_segment = records.back().control_segments.size();
    region.extent.end_token = records.back().ir.tokens.size() - 1;
    closed = !records.back().ir.tokens.empty();
    // The SRETBL it lacks stands doubled directly before its opener; hosted
    // prints nothing for it, so the region starts there and owns it.
    auto record = *begin_record;
    auto segment_index = source.segment_index;
    while (segment_index == 0 && record > 0)
      segment_index = records[--record].control_segments.size();
    if (segment_index == 0)
      return fail(error, "table '" + block.object_id +
                             "' has no doubled SRETBL before it");
    const auto& stray = records[record].control_segments[--segment_index];
    if (stray.kind != BookControlKind::table_end || stray.source_tokens.empty())
      return fail(error, "table '" + block.object_id +
                             "' has no doubled SRETBL before it");
    // Spacing or punctuation glued after the stray closer is its own
    // payload; anything visible there is no part of this envelope.
    auto opcode = operand_tokens(records[record], stray);
    if (opcode.empty()) opcode = stray.source_tokens;
    for (const auto token : stray.source_tokens) {
      if (!std::binary_search(opcode.begin(), opcode.end(), token) &&
          !region_structure(view_token(records, record, token)))
        return fail(error, "table '" + block.object_id +
                               "' follows an SRETBL that carries text");
      region.claims.emplace_back(record, token);
    }
    region.extent.begin_record = record;
    region.extent.begin_segment = segment_index;
    region.extent.begin_token = stray.source_tokens.front();
  }
  if (!closed)
    return fail(error, "table '" + block.object_id + "' is not closed");
  // CFONT/CSELECT controls inside the envelope style or link table cells;
  // the block consumes their spans and display payload, so their opcode and
  // operand tokens belong to the table as well.
  for (auto record = region.extent.begin_record;
       record <= region.extent.end_record; ++record) {
    const auto& segments = records[record].control_segments;
    const auto first = record == region.extent.begin_record
                           ? region.extent.begin_segment
                           : std::size_t{0};
    const auto last = record == region.extent.end_record
                          ? region.extent.end_segment
                          : segments.size();
    for (auto index = first; index < last && index < segments.size(); ++index) {
      const auto& segment = segments[index];
      if (segment.kind == BookControlKind::structural) {
        const auto spot = std::find_if(
            block.spot_anchors.begin(), block.spot_anchors.end(),
            [&](const auto& anchor) {
              return anchor.source.logical_record ==
                         records[record].logical_record &&
                     anchor.source.segment_index == segment.segment_index &&
                     anchor.id == segment.opcode.substr(2);
            });
        if (spot != block.spot_anchors.end()) {
          for (auto token = spot->source.token_begin;
               token < spot->source.token_end; ++token)
            region.claims.emplace_back(record, token);
          continue;
        }
        // `SRLIS<owner>` / `SRELIS<owner>` lines the verbatim region read
        // as a row anchor and a closer that prints nothing.
        bool listed = false;
        for (const auto* list : {&block.list_anchors, &block.list_closers})
          for (const auto& anchor : *list)
            if (anchor.source.logical_record == records[record].logical_record &&
                anchor.source.segment_index == segment.segment_index &&
                anchor.id == segment.opcode.substr(2)) {
              for (auto token = anchor.source.token_begin;
                   token < anchor.source.token_end; ++token)
                region.claims.emplace_back(record, token);
              listed = true;
            }
        if (listed) continue;
      }
      if (segment.kind != BookControlKind::font &&
          segment.kind != BookControlKind::select)
        continue;
      const auto operands = operand_tokens(records[record], segment);
      for (const auto token : operands) region.claims.emplace_back(record, token);
      if (segment.kind != BookControlKind::select) continue;
      // A preformatted region has no cells for a link to attach to.  Hosted
      // BookServer still serves the selector inside the `<pre>` -- GG24-395
      // 3.2.2 `TBLUNIQ6` (DT 19941215160749) opens the region with
      // `<a href="picture-29?mode=zoom"><img ... alt="PICTURE 29"></a>` on
      // the columns the `cselect 3 11 PIC29` names -- but the alt text and
      // the anchor label are the region's own display words, which the
      // reproduced lines already carry verbatim.  Markdown has no inline
      // inside a preformatted block, so the selector contributes its
      // columns as text and its opcode/operand tokens stay region
      // structure.  Nothing is dropped that the hosted page displays.
      if (block.geometry == FixedTableGeometryIR::preformatted) {
        for (const auto token : selector_link_alternative_tokens(
                 records[record], segment))
          region.claims.emplace_back(record, token);
        continue;
      }
      ProseTableLinkIR link;
      if (segment.malformed || operands.empty() ||
          !parse_selector_operand(
              operand_text(records[record], segment.operand_range),
              link.column, link.length, link.target))
        return fail(error, "table '" + block.object_id +
                               "' contains a selector that is not canonical");
      // A `PIC<n>` selector places a picture: hosted BookServer serves it as
      // an `<img>` over the columns the selector names (GG24-395 3.3.8,
      // `<a href="picture-69?mode=zoom"><img ... alt="PICTURE 69">`, DT
      // 19941215160749), replacing the `PICTURE n` placeholder words the
      // compiler wrote there.  A region that renders verbatim carries it in
      // the block's own `pictures`, which the lowering emits as an image
      // beside the reproduced art, and the selector contributes nothing
      // else here.  A region the source declared a `:table` lowers to a
      // Markdown table, whose cells do carry an inline: the picture becomes
      // an image over the cell line that spells its placeholder, the way a
      // cell link replaces the line it covers.
      std::vector<std::size_t> alternative_tokens;
      {
        const auto target = ascii_lower(link.target);
        if (figure_picture_target(target)) {
          const auto verbatim = !block.source_declared_table &&
                                !block.preformatted_lines.empty();
          if (verbatim) {
            const auto recorded = std::any_of(
                block.pictures.begin(), block.pictures.end(),
                [&](const auto& picture) {
                  return picture.logical_record ==
                             records[record].logical_record &&
                         picture.segment_index == segment.segment_index;
                });
            if (!recorded)
              return fail(error, "table '" + block.object_id +
                                     "' contains a picture selector the "
                                     "verbatim region did not record");
            continue;
          }
          if (!block.source_declared_table)
            return fail(error, "table '" + block.object_id +
                                   "' contains a picture selector");
          link.picture = true;
          link.target = figure_picture_resource(target);
          link.target_kind = CrossReferenceTargetKindIR::resource;
          if (link.target.empty() || resource_ids == nullptr ||
              resource_ids->count(link.target) == 0)
            return fail(error, "table '" + block.object_id +
                                   "' picture resource " + link.target +
                                   " is not in the resource catalog");
        }
        // A `LNK` selector in a cell is the same cross-book/external link
        // the prose inline carries: its destination is the leading `<...>`
        // alternative tokens of the payload, which are control metadata and
        // never display text.
        if (target == "lnk") {
          std::vector<std::string> tokens;
          alternative_tokens = selector_link_alternative_tokens(
              records[record], segment, &tokens);
          std::string link_error;
          const auto parsed = parse_selector_link(tokens, &link_error);
          if (!parsed)
            return fail(error, "table '" + block.object_id +
                                   "' selector rejected: " + link_error);
          if (parsed->kind == SelectorLinkKindIR::external_image)
            return fail(error, "table '" + block.object_id +
                                   "' cell carries an external image");
          link.target = parsed->alternatives[5];
          switch (parsed->kind) {
          case SelectorLinkKindIR::book_contents:
            link.target_kind = CrossReferenceTargetKindIR::book_contents;
            break;
          case SelectorLinkKindIR::book_heading:
            link.target_kind = CrossReferenceTargetKindIR::book_heading;
            break;
          case SelectorLinkKindIR::external_link:
            link.target_kind = CrossReferenceTargetKindIR::external;
            break;
          case SelectorLinkKindIR::external_image:
            return fail(error, "table '" + block.object_id +
                                   "' cell carries an external image");
          }
          link.target_alternatives = parsed->alternatives;
        }
      }
      link.logical_record = records[record].logical_record;
      {
        for (const auto token : segment.source_tokens) {
          if (std::binary_search(operands.begin(), operands.end(), token))
            continue;
          if (std::find(alternative_tokens.begin(), alternative_tokens.end(),
                        token) != alternative_tokens.end()) {
            region.claims.emplace_back(record, token);
            continue;
          }
          link.payload_tokens.push_back(token);
        }
      }
      link.source = token_slice(
          records[record], operands.front(),
          alternative_tokens.empty() ? operands.back() + 1
                                     : alternative_tokens.back() + 1);
      region.links.push_back(std::move(link));
    }
  }

  // Preformatted geometry reproduces the envelope line for line, so every
  // token of every reproduced line belongs to the block whether or not the
  // layout positioned it (DREICMST 2.1.3 `ACNTT1`: the header words `Table
  // Name`/`Type`/`ID`/`Data`/`Description` carry no positioned cell).
  for (const auto& line : block.preformatted_lines) {
    const auto record = record_index_of(records, line.logical_record);
    if (!record)
      return fail(error, "table '" + block.object_id +
                             "' reproduces a line outside the topic");
    for (auto token = line.prefix_token; token < line.token_end; ++token)
      region.claims.emplace_back(*record, token);
  }
  // A `SI` subject-index line inside the envelope displays nothing; its
  // words are the hidden term (see FixedTableBlockIR::index_lines).
  for (const auto& line : block.index_lines) {
    const auto record = record_index_of(records, line.logical_record);
    if (!record)
      return fail(error, "table '" + block.object_id +
                             "' carries an index line outside the topic");
    for (auto token = line.prefix_token; token < line.token_end; ++token)
      region.claims.emplace_back(*record, token);
    ProseIndexTermIR term;
    term.term = line.text;
    term.slices.push_back(
        token_slice(records[*record], line.prefix_token, line.token_end));
    region.index_terms.push_back(std::move(term));
  }

  const auto claim_positioned = [&](const PositionedRowCellIR& cell) {
    if (const auto record = record_index_of(records, cell.logical_record))
      region.claims.emplace_back(*record, cell.token_index);
  };
  const auto claim_row = [&](const FixedTableRowIR& row) {
    for (const auto& cell : row.cells)
      for (const auto& line : cell.lines) {
        for (const auto& source_cell : line.source_cells)
          claim_positioned(source_cell);
        for (const auto& source_cell : line.unpositioned_cells)
          if (const auto record =
                  record_index_of(records, source_cell.logical_record))
            region.claims.emplace_back(*record, source_cell.token_index);
      }
    for (const auto& cell : row.structural_cells) claim_positioned(cell);
  };
  if (block.caption) claim_row(*block.caption);
  for (const auto& row : block.body) claim_row(row);
  for (const auto& cell : block.structural_cells) claim_positioned(cell);
  return true;
}

bool figure_region(const std::vector<DecodedLogicalRecordSource>& records,
                   const FigureSourceBlockIR& block, Region& region,
                   std::string* error) {
  const auto begin_record =
      record_index_of(records, block.span.begin.logical_record);
  const auto end_record = record_index_of(records, block.span.end.logical_record);
  const auto label = block.anchor.empty() ? std::string("anchorless figure")
                                          : "figure '" + block.anchor + "'";
  if (!begin_record || !end_record ||
      block.span.begin.segment_index >=
          records[*begin_record].control_segments.size() ||
      block.span.end.segment_index >=
          records[*end_record].control_segments.size())
    return fail(error, label + " has no source span");
  const auto& begin_segment =
      records[*begin_record].control_segments[block.span.begin.segment_index];
  if (begin_segment.source_tokens.empty())
    return fail(error, label + " begins with a token-less segment");
  region.extent.begin_record = *begin_record;
  region.extent.begin_segment = block.span.begin.segment_index;
  region.extent.begin_token = begin_segment.source_tokens.front();
  region.extent.end_record = *end_record;
  region.extent.end_segment = block.span.end.segment_index;
  std::optional<std::size_t> end_token;
  for (const auto& cell : block.cells) {
    const auto record = record_index_of(records, cell.logical_record);
    if (!record) return fail(error, label + " claims a cell outside the topic");
    region.claims.emplace_back(*record, cell.token_index);
    if (*record == *end_record &&
        (!end_token || cell.token_index > *end_token))
      end_token = cell.token_index;
  }
  if (!end_token) return fail(error, label + " owns no token of its end record");
  region.extent.end_token = *end_token;
  return true;
}

// The display rows a picture-less frame draws around its table.
//
// ITPPIBOK 4.3.5.4 records 158-160 open `SRFIGSCHED` on record 158 line 19
// and draw the frame's top rule on line 20 and two rails on lines 21-22;
// `SRTBLTBLUNIQ34` stands alone on line 23, and the envelope's rows (lines
// 24 .. record 159 line 19) carry the frame's rails beside the table's own
// `| TASK | START DATE |` grid.  After `SRETBL` (record 159 line 20) the frame
// goes on with three rails, a blank line and its bottom rule (record 160
// lines 0-4) before the `Figure 2.` caption and `SREFIG`.  Hosted BookServer
// (DT 19910628074854) prints all of those rows, in source order, inside one
// `<pre>`, with `<a name="TBLTBLUNIQ34">` on the first row after the `SRTBL`
// line and no `<table>`.
//
// A frame row is a display line inside the frame that draws a box word and
// that no table owns; every such row is the enclosed table span's to print,
// so its tokens take the span's role.  Only the proven shape qualifies: the
// frame encloses exactly one table, its rows stand before the table's first
// row or after its last one, and no line of prose stands between a frame row
// and the table (the stream would place it on the wrong side of the rows).
// A blank display line between two drawn rows of one side is an empty row.
// Anything else fails closed: SC24-5527-02 COMMENTS draws a closed `Note` box
// between paragraphs of its `SRFIGFIGUNIQ548` form (record 977 line 4 ..
// record 978 line 0), with prose between the box and the `SRTBLTBLUNIQ549`
// table, and is no frame around that table.  A frame that draws no box word
// -- every other frame of the corpus -- is left exactly as it was.
bool claim_frame_rows(const std::vector<DecodedLogicalRecordSource>& records,
                      const Claim& begin, const Claim& end,
                      const std::string& label, Ledger& ledger,
                      ProseTopicIR& topic, std::string* error) {
  enum class Kind { blank, control, text, drawn, table };
  struct Line {
    std::size_t record = 0;
    const DisplayLineIR* line = nullptr;
    Kind kind = Kind::blank;
  };
  std::vector<Line> lines;
  constexpr auto none = static_cast<std::size_t>(-1);
  std::size_t span = none;
  bool several = false;
  for (auto record = begin.first; record <= end.first; ++record) {
    const auto* display = record_display_lines(records[record]);
    if (display == nullptr) continue;
    for (const auto& line : *display) {
      // Lines whose tokens lie inside the frame's extent.  The `SRFIG` line
      // and the `SREFIG` line are control lines either way.
      if (line.token_end <= line.prefix_token + 1) {
        if ((record > begin.first || line.prefix_token > begin.second) &&
            (record < end.first || line.prefix_token < end.second))
          lines.push_back({record, &line, Kind::blank});
        continue;
      }
      const auto first = line.prefix_token + 1;
      const auto last = line.token_end - 1;
      if ((record == begin.first && last < begin.second) ||
          (record == end.first && first > end.second))
        continue;
      Line entry{record, &line, Kind::blank};
      bool control = false;
      bool drawn = false;
      bool visible = false;
      for (auto token = first; token <= last; ++token) {
        const auto& disposition = ledger.at(record, token);
        if (disposition.role == ProseTokenRoleIR::table) {
          if (span != none && span != disposition.span) several = true;
          if (span == none) span = disposition.span;
          entry.kind = Kind::table;
        }
        const auto view = view_token(records, record, token);
        if (disposition.role == ProseTokenRoleIR::unassigned &&
            std::any_of(view.body.begin(), view.body.end(),
                        [](const auto word) { return box_word(word); }))
          drawn = true;
        if (disposition.role == ProseTokenRoleIR::unassigned &&
            !region_structure(view))
          visible = true;
      }
      for (const auto& segment : records[record].control_segments)
        if (segment.kind != BookControlKind::text &&
            !segment.source_tokens.empty() &&
            segment.source_tokens.front() >= first &&
            segment.source_tokens.front() <= last)
          control = true;
      if (entry.kind != Kind::table) {
        if (control && drawn)
          return fail(error, label + " draws a frame row on a control line");
        entry.kind = control  ? Kind::control
                     : drawn  ? Kind::drawn
                     : visible ? Kind::text
                               : Kind::blank;
      }
      lines.push_back(entry);
    }
  }
  const auto drawn_row = [](const Line& line) {
    return line.kind == Kind::drawn;
  };
  if (std::none_of(lines.begin(), lines.end(), drawn_row)) return true;
  if (span == none || several || span >= topic.spans.size() ||
      topic.spans[span].kind != ProseSpanKindIR::table)
    return fail(error, label + " draws frame rows around " +
                           (several ? "more than one table" : "no table"));
  auto& owner = topic.spans[span];
  if (!owner.frame_before.empty() || !owner.frame_after.empty())
    return fail(error, label + " draws frame rows around a framed table");
  std::size_t first_table = lines.size();
  std::size_t last_table = 0;
  for (std::size_t at = 0; at < lines.size(); ++at)
    if (lines[at].kind == Kind::table) {
      first_table = std::min(first_table, at);
      last_table = at;
    }
  for (std::size_t at = first_table; at <= last_table; ++at)
    if (lines[at].kind == Kind::drawn)
      return fail(error, label + " draws a frame row between table rows");
  // One side of the table, `lines[0, to)` with the table beyond `to`: the
  // drawn rows from the outermost one inward, with the blank lines between
  // them.  A prose line anywhere from the outermost drawn row to the table
  // fails, since the rows print with the table.
  const auto side = [&](std::size_t to,
                        std::vector<ProseFrameRowIR>& rows) -> bool {
    std::size_t outer = to;
    for (std::size_t at = 0; at < to; ++at)
      if (lines[at].kind == Kind::drawn) {
        outer = at;
        break;
      }
    std::size_t inner = 0;
    for (auto at = to; at > 0; --at)
      if (lines[at - 1].kind == Kind::drawn) {
        inner = at;
        break;
      }
    for (auto at = outer; at < to; ++at)
      if (lines[at].kind == Kind::text)
        return fail(error, label + " draws frame rows around prose");
    for (auto at = outer; at < inner; ++at) {
      const auto& entry = lines[at];
      if (entry.kind == Kind::control) continue;
      const auto& source = records[entry.record];
      ProseFrameRowIR row;
      row.source = token_slice(source, entry.line->prefix_token + 1,
                               std::max(entry.line->token_end,
                                        entry.line->prefix_token + 1));
      if (entry.kind == Kind::drawn) {
        row.text = display_line_text(source, *entry.line);
        while (!row.text.empty() && row.text.back() == ' ')
          row.text.pop_back();
        for (auto token = entry.line->prefix_token + 1;
             token < entry.line->token_end; ++token)
          if (ledger.at(entry.record, token).role ==
                  ProseTokenRoleIR::unassigned &&
              !ledger.assign(entry.record, token, ProseTokenRoleIR::table,
                             error, span))
            return false;
      }
      rows.push_back(std::move(row));
    }
    return true;
  };
  if (first_table == lines.size())
    return fail(error, label + " draws frame rows around no table row");
  // Before the table, the side ends at the table's first row; after it, the
  // walk is mirrored so the outermost drawn row is the last one.
  if (!side(first_table, owner.frame_before)) return false;
  {
    std::vector<Line> after(lines.begin() + last_table + 1, lines.end());
    std::reverse(after.begin(), after.end());
    lines = std::move(after);
    std::vector<ProseFrameRowIR> rows;
    if (!side(lines.size(), rows)) return false;
    std::reverse(rows.begin(), rows.end());
    owner.frame_after = std::move(rows);
  }
  return true;
}

// Admits a picture-less figure region as a frame when every visible token
// inside it belongs to an already planned table span, apart from display
// lines of prose the stream lowers and the frame's own drawn rows
// (`claim_frame_rows`), which the enclosed table span prints.  The SRFIG
// operands become the frame anchor, everything else inside is padding.
bool plan_frame(const std::vector<DecodedLogicalRecordSource>& records,
                const FigureBlockDeclineIR& decline, Ledger& ledger,
                ProseTopicIR& topic, SpanPlan& plan, std::string* error) {
  const auto label = "figure '" + decline.anchor + "'";
  const auto begin_record = record_index_of(records, decline.begin.logical_record);
  if (!begin_record || !decline.end)
    return fail(error, label + " declined: " + decline.reason);
  const auto end_record = record_index_of(records, decline.end->logical_record);
  if (!end_record ||
      decline.begin.segment_index >=
          records[*begin_record].control_segments.size() ||
      decline.end->segment_index >= records[*end_record].control_segments.size())
    return fail(error, label + " declined: " + decline.reason);
  const auto& begin_segment =
      records[*begin_record].control_segments[decline.begin.segment_index];
  const auto& end_segment =
      records[*end_record].control_segments[decline.end->segment_index];
  const auto operands = operand_tokens(records[*begin_record], begin_segment);
  std::vector<std::size_t> end_tokens;
  if (begin_segment.kind != BookControlKind::structural || operands.empty() ||
      begin_segment.source_tokens.empty() ||
      !figure_end_tokens(records, *end_record, end_segment, end_tokens))
    return fail(error, label + " declined: " + decline.reason);
  FrameRegion frame;
  frame.begin_record = *begin_record;
  frame.begin_segment = decline.begin.segment_index;
  frame.end_record = *end_record;
  frame.end_segment = decline.end->segment_index;
  frame.anchor_id = decline.anchor;
  if (!valid_anchor_id(frame.anchor_id))
    return fail(error, label + " has an invalid anchor id");
  frame.source = token_slice(records[*begin_record], operands.front(),
                             operands.back() + 1);
  for (const auto token : operands)
    if (!ledger.assign(*begin_record, token, ProseTokenRoleIR::control, error))
      return false;
  for (const auto token : end_tokens)
    if (!ledger.assign(*end_record, token, ProseTokenRoleIR::control, error))
      return false;
  bool table_inside = false;
  const auto first_token = begin_segment.source_tokens.front();
  const auto last_token = end_tokens.back();
  if (!claim_frame_rows(records, {*begin_record, first_token},
                        {*end_record, last_token}, label, ledger, topic,
                        error))
    return false;
  for (auto record = frame.begin_record; record <= frame.end_record; ++record) {
    const auto first = record == frame.begin_record ? first_token : std::size_t{0};
    const auto last = record == frame.end_record
                          ? last_token
                          : records[record].ir.tokens.size() - 1;
    // A captioned frame carries display lines of its own beside the table it
    // frames -- DREICMST 1.2.1 record 79 line `   split=yes.` between
    // `SRFIGLOGPROC` and the inner `SRFIGXXX`, and record 84's
    // `Figure 5. Where SLR Gets Its Data.` after the inner `SREFIG`; hosted
    // DT 19911219125856 serves both as text, the first inside the
    // `<a name="FIGLOGPROC">` anchor.  Such a line carries no table-owned
    // token, so its words stay unassigned for the stream pass to lower as
    // body text.  A visible token on a line the table does own is still the
    // conservation failure the frame must fail closed on.
    const auto lines = record_display_lines(records[record]);
    // A display line's length byte is a length, never text, whatever word
    // the dictionary spells for it: DREICMST record 84 token 247 is the
    // `SREFIG` line's length byte and spells `.`, which hosted does not
    // print after `Figure 5. Where SLR Gets Its Data`.
    const auto length_byte = [&](const std::size_t at) {
      return is_display_line_length_token(records[record], at);
    };
    const auto line_owned_by_table = [&](const std::size_t at) {
      if (!lines) return true;
      for (const auto& line : *lines) {
        if (at <= line.prefix_token || at >= line.token_end) continue;
        for (auto other = line.prefix_token + 1; other < line.token_end;
             ++other)
          if (ledger.at(record, other).role == ProseTokenRoleIR::table)
            return true;
        return false;
      }
      return true;
    };
    for (auto token = first; token <= last; ++token) {
      const auto& entry = ledger.at(record, token);
      if (entry.role == ProseTokenRoleIR::table) {
        table_inside = true;
        continue;
      }
      if (entry.role != ProseTokenRoleIR::unassigned) continue;
      const auto view = view_token(records, record, token);
      if (!region_structure(view) && !length_byte(token)) {
        if (!line_owned_by_table(token)) continue;
        return fail(error, label + " declined: " + decline.reason +
                               " (visible token '" + body_text(view) +
                               "' outside any table)");
      }
      if (!ledger.assign(record, token, ProseTokenRoleIR::padding, error))
        return false;
    }
  }
  if (!table_inside)
    return fail(error, label + " declined: " + decline.reason);
  plan.frames.push_back(std::move(frame));
  return true;
}

bool inside_table(const std::vector<Region>& regions,
                  const std::vector<DecodedLogicalRecordSource>& records,
                  const FigureSegmentRefIR& ref) {
  const auto record = record_index_of(records, ref.logical_record);
  if (!record) return false;
  for (const auto& region : regions) {
    if (region.kind != ProseSpanKindIR::table) continue;
    const auto& extent = region.extent;
    if (*record < extent.begin_record || *record > extent.end_record) continue;
    if (*record == extent.begin_record && ref.segment_index < extent.begin_segment)
      continue;
    if (*record == extent.end_record && ref.segment_index > extent.end_segment)
      continue;
    return true;
  }
  return false;
}

} // namespace

std::set<std::pair<std::size_t, std::size_t>> box_markup_envelopes(
    const std::vector<DecodedLogicalRecordSource>& records) {
  auto enclosed = box_enclosed_envelopes(records);
  if (enclosed.empty()) return {};
  // The table pass reports a declined envelope by its id alone, so an
  // enclosed opener is set aside by id only when no other opener of the
  // topic spells the same id.
  std::map<std::string, std::size_t> openers;
  for (std::size_t index = 0; index < records.size(); ++index)
    for (const auto& segment : records[index].control_segments)
      if (segment.kind == BookControlKind::table_start)
        ++openers[ascii_lower(segment.opcode)];
  for (const auto& [record, token] : enclosed)
    for (const auto& segment : records[record].control_segments)
      if (segment.kind == BookControlKind::table_start &&
          !segment.source_tokens.empty() &&
          segment.source_tokens.front() == token &&
          openers[ascii_lower(segment.opcode)] != 1)
        return {};
  return enclosed;
}

bool plan_spans(const std::vector<DecodedLogicalRecordSource>& records,
                const LayoutIR& layout, const OwnershipIR& ownership,
                const std::set<std::string>* resource_ids, Ledger& ledger,
                ProseTopicIR& topic, SpanPlan& plan, std::string* error) {
  std::vector<Region> regions;
  const auto verbatim = cz_verbatim_regions(records);
  const auto box_markup = box_markup_envelopes(records);
  if (has_table_envelope(records, verbatim, box_markup)) {
    topic.tables = extract_fixed_table_blocks_ir(
        records, layout, ownership, {0, count_layout_rows(layout)});
    // An envelope drawn inside a closed box is the box's own markup
    // (`box_markup_envelopes`): whatever the table pass made of it, it is
    // no table of the topic, and the box keeps its rows.  The extraction
    // stays whole, so its verifier still compares it exactly; the box's
    // blocks simply plan no span.
    std::set<std::string> box_ids;
    for (const auto& [record, token] : box_markup)
      for (const auto& segment : records[record].control_segments)
        if (segment.kind == BookControlKind::table_start &&
            !segment.source_tokens.empty() &&
            segment.source_tokens.front() == token)
          box_ids.insert(segment.opcode.substr(5));
    const auto boxed = [&](const FixedTableBlockIR& block) {
      const auto record = record_index_of(records, block.object_source.logical_record);
      return record &&
             box_markup.count({*record, block.object_source.token_begin}) != 0;
    };
    for (const auto& decline : topic.tables.declined) {
      if (box_ids.count(decline.object_id)) continue;
      return fail(error, "table envelope '" + decline.object_id +
                             "' declined: " + decline.reason);
    }
    for (std::size_t index = 0; index < topic.tables.blocks.size(); ++index) {
      if (boxed(topic.tables.blocks[index])) continue;
      Region region;
      region.kind = ProseSpanKindIR::table;
      region.index = index;
      if (!table_region(records, topic.tables.blocks[index], resource_ids,
                        region, error))
        return false;
      regions.push_back(std::move(region));
    }
  }
  if (has_figure_region(records, verbatim)) {
    std::string selector_error;
    SelectorCatalogIR selectors;
    if (const auto catalog = extract_selector_catalog_ir(records, &selector_error))
      selectors = *catalog;
    else if (selector_error != "source contains no selectors")
      return fail(error, "figure selectors rejected: " + selector_error);
    const std::set<std::string> no_resources;
    topic.figures = extract_figure_blocks_ir(
        records, layout, ownership, selectors,
        resource_ids != nullptr ? *resource_ids : no_resources);
    for (const auto& decline : topic.figures.declined) {
      // A picture selector inside an admitted table is a table cell; the
      // figure extractor reports it as table-owned and the table claims it.
      if ((decline.reason == "picture selector is table-owned" ||
           decline.reason == "figure starts inside a table") &&
          inside_table(regions, records, decline.begin))
        continue;
      // A frame around table spans is planned after the spans have claimed
      // their tokens.
      if (decline.reason == "figure region contains a table" && decline.end)
        continue;
      // A bare picture selector whose `PICTURE n` placeholder sits inside a
      // sentence is an inline image, not a figure: its tokens stay in the
      // stream and the display-line pass proves the covered columns.
      if (decline.reason == figure_inline_picture_decline_reason())
        continue;
      return fail(error, (decline.anchor.empty()
                              ? std::string("figure region")
                              : "figure '" + decline.anchor + "'") +
                             " declined: " + decline.reason);
    }
    for (std::size_t index = 0; index < topic.figures.blocks.size(); ++index) {
      Region region;
      region.kind = ProseSpanKindIR::figure;
      region.index = index;
      if (!figure_region(records, topic.figures.blocks[index], region, error))
        return false;
      regions.push_back(std::move(region));
    }
  }
  std::sort(regions.begin(), regions.end(), [](const auto& a, const auto& b) {
    return std::make_pair(a.extent.begin_record, a.extent.begin_token) <
           std::make_pair(b.extent.begin_record, b.extent.begin_token);
  });

  for (std::size_t span = 0; span < regions.size(); ++span) {
    auto& region = regions[span];
    const auto& extent = region.extent;
    const auto role = region.kind == ProseSpanKindIR::table
                          ? ProseTokenRoleIR::table
                          : ProseTokenRoleIR::figure;
    const auto name = region.kind == ProseSpanKindIR::table ? "table" : "figure";
    if (extent.end_record < extent.begin_record ||
        (extent.end_record == extent.begin_record &&
         extent.end_token < extent.begin_token))
      return fail(error, std::string(name) + " region ends before it begins");
    std::sort(region.claims.begin(), region.claims.end());
    region.claims.erase(std::unique(region.claims.begin(), region.claims.end()),
                        region.claims.end());
    {
      // A block may also claim the spacing token that carries its opening
      // control's display-line break (the bare token before `SRFIG`,
      // ACPZMST1 1.1.3).  Such a token lies outside the control-derived
      // region and stays with the stream, which gives it its spacing role;
      // a claim outside the region that carries a visible word would mean
      // the block reached into prose and rejects the topic.
      std::vector<Claim> inside;
      for (const auto& claim : region.claims) {
        const auto& [record, token] = claim;
        if (token >= records[record].ir.tokens.size())
          return fail(error, std::string(name) +
                                 " block claims a token that does not exist");
        const auto before = record < extent.begin_record ||
                            (record == extent.begin_record &&
                             token < extent.begin_token);
        const auto after = record > extent.end_record ||
                           (record == extent.end_record &&
                            token > extent.end_token);
        if (!before && !after) {
          inside.push_back(claim);
          continue;
        }
        const auto view = view_token(records, record, token);
        if (!region_structure(view))
          return fail(error, std::string(name) + " block claims visible token '" +
                                 body_text(view) + "' outside its region");
      }
      region.claims = std::move(inside);
    }
    auto claim = region.claims.begin();
    for (auto record = extent.begin_record; record <= extent.end_record;
         ++record) {
      const auto first =
          record == extent.begin_record ? extent.begin_token : std::size_t{0};
      const auto last = record == extent.end_record
                            ? extent.end_token
                            : records[record].ir.tokens.size() - 1;
      // A display line's length byte is structure whatever word the
      // dictionary spells for it (GG24-395 COMMENTS record 826 token 0 is
      // byte 65 and spells `cparent`; SH20-918 3.16 record 216 token 52
      // spells `cfont`), so no block has to claim it.
      for (auto token = first; token <= last; ++token) {
        while (claim != region.claims.end() &&
               *claim < Claim{record, token})
          ++claim;
        const auto claimed =
            claim != region.claims.end() && *claim == Claim{record, token};
        if (!claimed) {
          const auto view = view_token(records, record, token);
          if (!region_structure(view) &&
              !is_display_line_length_token(records[record], token) &&
              !(region.kind == ProseSpanKindIR::table &&
                table_marker_slot(view)))
            return fail(error, "visible token '" + body_text(view) +
                                   "' inside the " + name +
                                   " region of record " +
                                   std::to_string(records[record].logical_record) +
                                   " token " + std::to_string(token) +
                                   " is claimed by no block");
        }
        if (!ledger.assign(record, token, role, error, span)) return false;
      }
    }
    ProseSpanIR typed;
    typed.kind = region.kind;
    typed.index = region.index;
    topic.spans.push_back(typed);
    for (auto link : region.links) {
      link.span = span;
      topic.table_links.push_back(std::move(link));
    }
    for (auto& term : region.index_terms)
      plan.index_terms.push_back(std::move(term));
    auto planned = extent;
    planned.span = span;
    plan.regions.push_back(planned);
  }
  for (const auto& decline : topic.figures.declined) {
    if (decline.reason != "figure region contains a table" || !decline.end)
      continue;
    if (!plan_frame(records, decline, ledger, topic, plan, error))
      return false;
  }
  return true;
}

} // namespace geist::detail::prose_internal
