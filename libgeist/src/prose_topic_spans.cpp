#include "geist/detail/prose_topic_internal.hpp"

#include "geist/detail/selector_ir.hpp"

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
namespace {

using Claim = std::pair<std::size_t, std::size_t>;  // record index, token

struct Region {
  ProseSpanKindIR kind = ProseSpanKindIR::table;
  std::size_t index = 0;  // into tables.blocks / figures.blocks
  SpanRegion extent;
  std::vector<Claim> claims;
  std::vector<ProseTableLinkIR> links;
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

bool has_table_envelope(const std::vector<DecodedLogicalRecordSource>& records) {
  for (const auto& record : records)
    for (const auto& segment : record.control_segments)
      if (segment.kind == BookControlKind::table_start ||
          segment.kind == BookControlKind::table_end)
        return true;
  return false;
}

bool has_figure_region(const std::vector<DecodedLogicalRecordSource>& records) {
  for (const auto& record : records) {
    const auto text = token_words_to_ascii(record.assembled.words);
    for (const auto& segment : record.control_segments) {
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
                  const FixedTableBlockIR& block, Region& region,
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
      if (segment.kind != BookControlKind::font &&
          segment.kind != BookControlKind::select)
        continue;
      const auto operands = operand_tokens(records[record], segment);
      for (const auto token : operands) region.claims.emplace_back(record, token);
      if (segment.kind != BookControlKind::select) continue;
      ProseTableLinkIR link;
      if (segment.malformed || operands.empty() ||
          !parse_selector_operand(
              operand_text(records[record], segment.operand_range),
              link.column, link.length, link.target))
        return fail(error, "table '" + block.object_id +
                               "' contains a selector that is not canonical");
      link.logical_record = records[record].logical_record;
      for (const auto token : segment.source_tokens)
        if (!std::binary_search(operands.begin(), operands.end(), token))
          link.payload_tokens.push_back(token);
      link.source = token_slice(records[record], operands.front(),
                                operands.back() + 1);
      region.links.push_back(std::move(link));
    }
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

// Admits a picture-less figure region as a frame when every visible token
// inside it belongs to an already planned table span.  The SRFIG operands
// become the frame anchor, everything else inside is padding.
bool plan_frame(const std::vector<DecodedLogicalRecordSource>& records,
                const FigureBlockDeclineIR& decline, Ledger& ledger,
                SpanPlan& plan, std::string* error) {
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
  for (auto record = frame.begin_record; record <= frame.end_record; ++record) {
    const auto first = record == frame.begin_record ? first_token : std::size_t{0};
    const auto last = record == frame.end_record
                          ? last_token
                          : records[record].ir.tokens.size() - 1;
    for (auto token = first; token <= last; ++token) {
      const auto& entry = ledger.at(record, token);
      if (entry.role == ProseTokenRoleIR::table) {
        table_inside = true;
        continue;
      }
      if (entry.role != ProseTokenRoleIR::unassigned) continue;
      const auto view = view_token(records, record, token);
      if (!region_structure(view))
        return fail(error, label + " declined: " + decline.reason +
                               " (visible token '" + body_text(view) +
                               "' outside any table)");
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

bool plan_spans(const std::vector<DecodedLogicalRecordSource>& records,
                const LayoutIR& layout, const OwnershipIR& ownership,
                const std::set<std::string>* resource_ids, Ledger& ledger,
                ProseTopicIR& topic, SpanPlan& plan, std::string* error) {
  std::vector<Region> regions;
  if (has_table_envelope(records)) {
    topic.tables = extract_fixed_table_blocks_ir(
        records, layout, ownership, {0, count_layout_rows(layout)});
    for (const auto& decline : topic.tables.declined)
      return fail(error, "table envelope '" + decline.object_id +
                             "' declined: " + decline.reason);
    for (std::size_t index = 0; index < topic.tables.blocks.size(); ++index) {
      Region region;
      region.kind = ProseSpanKindIR::table;
      region.index = index;
      if (!table_region(records, topic.tables.blocks[index], region, error))
        return false;
      regions.push_back(std::move(region));
    }
  }
  if (has_figure_region(records)) {
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
      for (auto token = first; token <= last; ++token) {
        while (claim != region.claims.end() &&
               *claim < Claim{record, token})
          ++claim;
        const auto claimed =
            claim != region.claims.end() && *claim == Claim{record, token};
        if (!claimed) {
          const auto view = view_token(records, record, token);
          if (!region_structure(view) &&
              !(region.kind == ProseSpanKindIR::table &&
                table_marker_slot(view)))
            return fail(error, "visible token '" + body_text(view) +
                                   "' inside the " + name +
                                   " region of record " +
                                   std::to_string(records[record].logical_record) +
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
    auto planned = extent;
    planned.span = span;
    plan.regions.push_back(planned);
  }
  for (const auto& decline : topic.figures.declined) {
    if (decline.reason != "figure region contains a table" || !decline.end)
      continue;
    if (!plan_frame(records, decline, ledger, plan, error)) return false;
  }
  return true;
}

} // namespace geist::detail::prose_internal
