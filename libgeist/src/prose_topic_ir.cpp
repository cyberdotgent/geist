#include "geist/detail/prose_topic_ir.hpp"

#include "geist/detail/book_topic_catalog_ir.hpp"
#include "geist/detail/implicit_grid.hpp"
#include "geist/detail/prose_topic_internal.hpp"
#include "geist/detail/selector_ir.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace geist::detail::prose_internal {

std::string normalize_title(std::string value) {
  value = collapse_ascii_whitespace(std::move(value));
  while (!value.empty() &&
         std::ispunct(static_cast<unsigned char>(value.back())) != 0 &&
         value.back() != ')' && value.back() != '"' && value.back() != '\'')
    value.pop_back();
  value = trim_ascii(std::move(value));
  return ascii_lower(std::move(value));
}

} // namespace geist::detail::prose_internal

namespace geist::detail {

using namespace prose_internal;

std::optional<ProseTopicIR> extract_prose_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const VerifiedOwnershipIR& verified_ownership,
    const std::string& title, const BookTopicCatalogIR* book_topic_catalog,
    std::string* error, const std::set<std::string>* resource_ids) {
  const auto reject = [&](std::string message) -> std::optional<ProseTopicIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (records.empty()) return reject("topic has no records");
  const OwnershipIR& ownership = verified_ownership;
  std::string verification_error;
  if (!verify_layout_ir(records, layout, &verification_error) ||
      !ownership_verified_for(verified_ownership, records, layout,
                              &verification_error))
    return reject("source layout/ownership is not canonical: " +
                  verification_error);
  for (const auto& record : records)
    if (record.ir.tokens.size() != record.tokens.size() ||
        record.ir.tokens.size() != record.encoded_tokens.size())
      return reject("record token projections disagree");

  Ledger ledger(records);
  Envelope envelope;
  if (!parse_envelope(records, ledger, envelope, error)) return std::nullopt;
  // Table and figure spans claim their regions before the stream pass, so
  // the prose model only ever sees the tokens between them.
  ProseTopicIR topic;
  SpanPlan plan;
  if (!plan_spans(records, layout, ownership, resource_ids, ledger, topic, plan,
                  error))
    return std::nullopt;
  StreamBuild stream;
  stream.plan = &plan;
  stream.resource_ids = resource_ids;
  if (!collect_stream(records, envelope, ledger, stream, error))
    return std::nullopt;
  // A plural CFONT header over repeated encoded row controls is a
  // two-column form (SC31-711 1.2 `Directory` / `Type of Files`), which the
  // legacy route draws as a table; prose must not flatten it.
  for (const auto& item : stream.items) {
    if (item.kind != ItemKind::font || item.spans.size() < 2) continue;
    std::vector<ImplicitGridHeaderSpan> header;
    for (const auto& span : item.spans)
      header.push_back({span.column, span.length});
    if (extract_implicit_grid(records, header))
      return reject("body contains an implicit two-column grid");
  }
  LineBuild lines;
  if (!build_lines(records, stream.items, ledger, lines, error))
    return std::nullopt;

  // The topic title the book catalog carries is a *string* projection of the
  // same `ST` control: `build_topics` reads the flattened decoded record and
  // stops at the first decoder boundary, which is not where the display row
  // breaks.  Hosted BookServer serves the row: QSYSINFO 2.1.21 (DT
  // 19910524120827) heads the topic `<H3> 2.1.21   SC09-1159, Languages:
  // System/38-Compatible COBOL User's Guide and</H3>` and starts the body
  // with `Reference`, while the catalog string runs on to `... and Re`.
  // Both are therefore truncations of one word run -- the `ST` payload -- so
  // corroboration is positional: each must be a prefix of that run.  A title
  // that is not a prefix of its own source (a dropped leading glyph, a
  // compact word glued onto the last word) still fails closed.
  // An `ST` control with no payload: the topic has no title of its own and
  // the catalog string is the book's separate projection, so there is nothing
  // to corroborate positionally.  Provenance is the control's own tokens.
  const auto empty_title = stream.empty_title_source.has_value();
  if (!empty_title) {
    const auto typed = normalize_title(lines.title);
    const auto catalog = normalize_title(title);
    const auto run = normalize_title(lines.title_run);
    const auto prefix_of_run = [&](const std::string& value) {
      return !value.empty() && run.rfind(value, 0) == 0;
    };
    if (typed != catalog &&
        !(prefix_of_run(typed) && prefix_of_run(catalog)))
      return reject("ST title '" + lines.title +
                    "' does not match the topic title '" + title +
                    "' [run '" + lines.title_run + "']");
  }

  topic.record_count = records.size();
  topic.token_count = ledger.entries.size();
  topic.heading_level = envelope.heading_level;
  topic.heading_form = envelope.heading_form;
  topic.title = lines.title;
  if (empty_title) {
    topic.title.clear();
    topic.title_source = *stream.empty_title_source;
  } else {
    auto refs = lines.title_refs;
    std::sort(refs.begin(), refs.end());
    const auto slices = slices_for(records, refs);
    if (slices.empty()) return reject("ST title has no source provenance");
    topic.title_source = slices.front();
    if (slices.size() != 1) {
      topic.title_source.token_end = slices.back().token_end;
      topic.title_source.byte_end = slices.back().byte_end;
    }
  }
  for (auto anchor : stream.leading_anchors) {
    anchor.position = 0;
    topic.anchors.push_back(std::move(anchor));
  }
  if (!build_blocks(records, lines, ledger, topic, error)) return std::nullopt;
  topic.index_terms = lines.index_terms;
  topic.index_terms.insert(topic.index_terms.end(),
                           stream.trailing_index_terms.begin(),
                           stream.trailing_index_terms.end());
  topic.index_terms.insert(topic.index_terms.end(), plan.index_terms.begin(),
                           plan.index_terms.end());
  if (!build_menu(records, stream, book_topic_catalog, ledger, topic, error))
    return std::nullopt;
  for (auto anchor : stream.trailing_anchors) {
    anchor.position = topic.blocks.size();
    topic.anchors.push_back(std::move(anchor));
  }

  for (const auto& entry : ledger.entries) {
    if (entry.role == ProseTokenRoleIR::unassigned)
      return reject("token " + std::to_string(entry.token.token_index) +
                    " of record " + std::to_string(entry.token.logical_record) +
                    " has no disposition");
    if (entry.role == ProseTokenRoleIR::text && entry.block == npos)
      return reject("visible token " + std::to_string(entry.token.token_index) +
                    " of record " + std::to_string(entry.token.logical_record) +
                    " belongs to no block");
  }
  topic.ledger = std::move(ledger.entries);
  if (topic.blocks.empty() && topic.menu_items.empty() && topic.spans.empty())
    return reject("topic body has no prose blocks");
  if (error != nullptr) error->clear();
  return topic;
}

bool verify_prose_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const VerifiedOwnershipIR& verified_ownership,
    const std::string& title, const BookTopicCatalogIR* book_topic_catalog,
    const ProseTopicIR& topic, std::string* error,
    const std::set<std::string>* resource_ids) {
  const OwnershipIR& ownership = verified_ownership;
  std::size_t total = 0;
  for (const auto& record : records) total += record.ir.tokens.size();
  if (topic.ledger.size() != total || topic.token_count != total ||
      topic.record_count != records.size())
    return fail(error, "prose ledger does not cover every source token");
  std::set<std::pair<std::uint32_t, std::size_t>> seen;
  std::map<std::pair<std::uint32_t, std::size_t>, const ProseTokenDispositionIR*>
      by_token;
  std::vector<std::size_t> span_tokens(topic.spans.size(), 0);
  for (const auto& entry : topic.ledger) {
    if (!seen.emplace(entry.token.logical_record, entry.token.token_index).second)
      return fail(error, "prose ledger lists a token twice");
    if (entry.role == ProseTokenRoleIR::unassigned)
      return fail(error, "prose ledger holds an unassigned token");
    if ((entry.role == ProseTokenRoleIR::text) != (entry.block != npos))
      return fail(error, "text disposition and block ownership disagree");
    const auto span_role = entry.role == ProseTokenRoleIR::table ||
                           entry.role == ProseTokenRoleIR::figure;
    if (span_role != (entry.span != npos))
      return fail(error, "span disposition and span ownership disagree");
    if (span_role) {
      if (entry.span >= topic.spans.size())
        return fail(error, "token is owned by a span that does not exist");
      const auto& span = topic.spans[entry.span];
      if ((span.kind == ProseSpanKindIR::table) !=
          (entry.role == ProseTokenRoleIR::table))
        return fail(error, "token role disagrees with its span kind");
      ++span_tokens[entry.span];
    }
    by_token.emplace(std::make_pair(entry.token.logical_record,
                                    entry.token.token_index),
                     &entry);
  }
  // Every span owns at least one token and addresses a verified block; the
  // block verifiers re-extract the canonical blocks and check each claimed
  // source cell against the ownership ledger.
  for (std::size_t index = 0; index < topic.spans.size(); ++index) {
    const auto& span = topic.spans[index];
    if (span_tokens[index] == 0) return fail(error, "span owns no token");
    if (span.position > topic.blocks.size())
      return fail(error, "span position is outside the block sequence");
    if (span.kind == ProseSpanKindIR::table
            ? span.index >= topic.tables.blocks.size()
            : span.index >= topic.figures.blocks.size())
      return fail(error, "span addresses no typed block");
  }
  for (const auto& link : topic.table_links) {
    if (link.span >= topic.spans.size() ||
        topic.spans[link.span].kind != ProseSpanKindIR::table)
      return fail(error, "table link belongs to no table span");
    if (link.target.empty() || link.length == 0)
      return fail(error, "table link is incomplete");
    for (const auto token : link.payload_tokens) {
      const auto found = by_token.find({link.logical_record, token});
      if (found == by_token.end() ||
          found->second->role != ProseTokenRoleIR::table ||
          found->second->span != link.span)
        return fail(error, "table link payload is not owned by its span");
    }
  }
  {
    std::string block_error;
    if (!topic.tables.blocks.empty() || !topic.tables.declined.empty()) {
      if (!verify_fixed_table_blocks_ir(records, layout, ownership,
                                        {0, count_layout_rows(layout)},
                                        topic.tables, &block_error))
        return fail(error, "table spans rejected: " + block_error);
    }
    if (!topic.figures.blocks.empty() || !topic.figures.declined.empty()) {
      SelectorCatalogIR selectors;
      if (const auto catalog = extract_selector_catalog_ir(records))
        selectors = *catalog;
      const std::set<std::string> no_resources;
      if (!verify_figure_blocks_ir(records, layout, ownership, selectors,
                                   resource_ids != nullptr ? *resource_ids
                                                           : no_resources,
                                   topic.figures, &block_error))
        return fail(error, "figure spans rejected: " + block_error);
    }
  }
  // Every text token is covered by inline slices that reproduce the ledger's
  // claims exactly, and those claims partition the token's decoded word.  A
  // token whose display columns a CFONT/CSELECT span splits is owned by two
  // or more inlines, each holding one byte range of the word.
  std::map<std::uint32_t, std::size_t> record_index;
  for (std::size_t index = 0; index < records.size(); ++index)
    record_index.emplace(records[index].logical_record, index);
  const auto word_length =
      [&](std::uint32_t logical_record, std::size_t token) -> std::uint32_t {
    const auto found = record_index.find(logical_record);
    if (found == record_index.end()) return 0;
    return static_cast<std::uint32_t>(
        prose_internal::body_text(
            prose_internal::view_token(records, found->second, token))
            .size());
  };
  std::map<std::pair<std::uint32_t, std::size_t>,
           std::vector<ProseInlineClaimIR>>
      covered;
  for (std::size_t block = 0; block < topic.blocks.size(); ++block) {
    const auto& node = topic.blocks[block];
    if (node.inlines.empty()) return fail(error, "prose block has no inlines");
    for (std::size_t index = 0; index < node.inlines.size(); ++index) {
      const auto& inline_node = node.inlines[index];
      if (inline_node.text.empty()) return fail(error, "inline text is empty");
      for (const auto& slice : inline_node.slices) {
        if (slice.token_begin >= slice.token_end)
          return fail(error, "inline slice is empty");
        if (slice_is_partial(slice) &&
            slice.token_end != slice.token_begin + 1)
          return fail(error, "sub-token slice spans more than one token");
        for (auto token = slice.token_begin; token < slice.token_end; ++token) {
          const auto found = by_token.find({slice.logical_record, token});
          if (found == by_token.end() ||
              found->second->role != ProseTokenRoleIR::text ||
              found->second->block != block)
            return fail(error, "inline slice covers a token it does not own");
          const auto length = word_length(slice.logical_record, token);
          const auto begin = slice_is_partial(slice) ? slice.character_begin : 0;
          const auto end = slice_is_partial(slice) ? slice.character_end : length;
          if (begin >= end || end > length)
            return fail(error, "sub-token slice is outside the decoded word");
          covered[{slice.logical_record, token}].push_back(
              {block, index, begin, end});
        }
      }
    }
  }
  for (const auto& entry : topic.ledger) {
    if (entry.role != ProseTokenRoleIR::text) {
      if (!entry.claims.empty())
        return fail(error, "a non-text token carries inline claims");
      continue;
    }
    const auto key = std::make_pair(entry.token.logical_record,
                                    entry.token.token_index);
    const auto found = covered.find(key);
    if (found == covered.end())
      return fail(error, "visible token is covered by no inline");
    const auto& claims = found->second;
    if (claims.size() != entry.claims.size())
      return fail(error, "inline slices disagree with the ledger claims");
    std::uint32_t cursor = 0;
    for (std::size_t index = 0; index < claims.size(); ++index) {
      const auto& claim = claims[index];
      const auto& expected = entry.claims[index];
      if (claim.block != expected.block ||
          claim.inline_index != expected.inline_index ||
          claim.character_begin != expected.character_begin ||
          claim.character_end != expected.character_end)
        return fail(error, "inline slices disagree with the ledger claims");
      if (claim.character_begin != cursor)
        return fail(error, "inline claims do not cover the decoded word");
      cursor = claim.character_end;
      if (index == 0 &&
          (claim.block != entry.block || claim.inline_index != entry.inline_index))
        return fail(error, "ledger claim disagrees with the token's inline");
    }
    if (cursor != word_length(entry.token.logical_record,
                              entry.token.token_index))
      return fail(error, "inline claims do not cover the decoded word");
  }
  const auto canonical =
      extract_prose_topic_ir(records, layout, verified_ownership, title,
                             book_topic_catalog, error, resource_ids);
  if (!canonical) return false;
  if (format_prose_topic_ir(*canonical) != format_prose_topic_ir(topic))
    return fail(error, "prose topic differs from canonical extraction");
  if (error != nullptr) error->clear();
  return true;
}

const char* prose_token_role_name(ProseTokenRoleIR role) {
  switch (role) {
  case ProseTokenRoleIR::unassigned: return "unassigned";
  case ProseTokenRoleIR::envelope: return "envelope";
  case ProseTokenRoleIR::control: return "control";
  case ProseTokenRoleIR::padding: return "padding";
  case ProseTokenRoleIR::title: return "title";
  case ProseTokenRoleIR::spacing: return "spacing";
  case ProseTokenRoleIR::fill: return "fill";
  case ProseTokenRoleIR::origin: return "origin";
  case ProseTokenRoleIR::marker: return "marker";
  case ProseTokenRoleIR::bullet: return "bullet";
  case ProseTokenRoleIR::gap: return "gap";
  case ProseTokenRoleIR::text: return "text";
  case ProseTokenRoleIR::index_keyword: return "index_keyword";
  case ProseTokenRoleIR::index_term: return "index_term";
  case ProseTokenRoleIR::index_structure: return "index_structure";
  case ProseTokenRoleIR::menu: return "menu";
  case ProseTokenRoleIR::ordinal: return "ordinal";
  case ProseTokenRoleIR::table: return "table";
  case ProseTokenRoleIR::figure: return "figure";
  }
  return "invalid";
}

const char* prose_block_kind_name(ProseBlockKindIR kind) {
  switch (kind) {
  case ProseBlockKindIR::paragraph: return "paragraph";
  case ProseBlockKindIR::list_item: return "list_item";
  case ProseBlockKindIR::definition_entry: return "definition_entry";
  case ProseBlockKindIR::heading: return "heading";
  case ProseBlockKindIR::note: return "note";
  case ProseBlockKindIR::footnote: return "footnote";
  case ProseBlockKindIR::preformatted: return "preformatted";
  }
  return "invalid";
}

namespace {

void format_slices(std::ostream& out,
                   const std::vector<DocumentSourceSliceIR>& slices) {
  out << " slices=[";
  for (std::size_t index = 0; index < slices.size(); ++index) {
    const auto& slice = slices[index];
    if (index != 0) out << ' ';
    out << slice.logical_record << ':' << slice.segment_index << ':'
        << slice.token_begin << '-' << slice.token_end << ":0x" << std::hex
        << slice.byte_begin << "-0x" << slice.byte_end << std::dec;
    if (slice_is_partial(slice))
      out << ':' << slice.character_begin << '+' << slice.character_end;
  }
  out << ']';
}

} // namespace

std::string format_prose_topic_ir(const ProseTopicIR& topic) {
  std::ostringstream out;
  out << "prose_topic records=" << topic.record_count
      << " tokens=" << topic.token_count << " heading_level="
      << topic.heading_level << " heading_form=" << topic.heading_form
      << " title='" << topic.title << "'";
  format_slices(out, {topic.title_source});
  out << '\n';
  for (const auto& anchor : topic.anchors) {
    out << "anchor id=" << anchor.id << " position=" << anchor.position
        << (anchor.after_menu ? " after_menu" : "");
    format_slices(out, {anchor.source});
    out << '\n';
  }
  for (std::size_t index = 0; index < topic.blocks.size(); ++index) {
    const auto& block = topic.blocks[index];
    out << "block " << index << ' ' << prose_block_kind_name(block.kind)
        << " origin=" << block.origin;
    if (block.kind == ProseBlockKindIR::list_item ||
        block.kind == ProseBlockKindIR::definition_entry)
      out << " list=" << block.list_ordinal;
    if (block.ordered) out << " ordered";
    if (!block.ordinal.empty()) out << " ordinal='" << block.ordinal << "'";
    if (block.heading_level != 0) out << " level=" << block.heading_level;
    if (!block.anchor_id.empty()) out << " anchor=" << block.anchor_id;
    if (block.term_inline_count != 0)
      out << " term_inlines=" << block.term_inline_count;
    format_slices(out, block.slices);
    out << '\n';
    for (const auto& row : block.preformatted_lines)
      out << "  row '" << row << "'\n";
    for (const auto& inline_node : block.inlines) {
      out << "  ";
      switch (inline_node.kind) {
      case ProseInlineKindIR::text: out << "text"; break;
      case ProseInlineKindIR::emphasis:
        out << "emphasis style=" << font_style_name(inline_node.style);
        break;
      case ProseInlineKindIR::cross_reference:
        out << "xref target=" << inline_node.target;
        break;
      case ProseInlineKindIR::image:
        out << "image resource=" << inline_node.target;
        break;
      }
      out << " '" << inline_node.text << "'";
      format_slices(out, inline_node.slices);
      out << '\n';
    }
  }
  for (const auto& term : topic.index_terms) {
    out << "index_term '" << term.term << "'";
    if (term.structured) out << " structured";
    format_slices(out, term.slices);
    out << '\n';
  }
  for (const auto& item : topic.menu_items) {
    out << "menu_item target=" << item.target << " label='" << item.label
        << "'";
    format_slices(out, {item.source});
    out << '\n';
  }
  for (std::size_t index = 0; index < topic.spans.size(); ++index) {
    const auto& span = topic.spans[index];
    out << "span " << index << ' '
        << (span.kind == ProseSpanKindIR::table ? "table" : "figure")
        << " index=" << span.index << " position=" << span.position
        << " anchors_before=" << span.anchors_before << '\n';
  }
  for (const auto& link : topic.table_links) {
    out << "table_link span=" << link.span << " column=" << link.column
        << " length=" << link.length << " target=" << link.target
        << " record=" << link.logical_record << " payload=";
    for (const auto token : link.payload_tokens) out << token << ',';
    format_slices(out, {link.source});
    out << '\n';
  }
  if (!topic.tables.blocks.empty() || !topic.tables.declined.empty())
    out << "tables\n" << format_fixed_table_blocks_ir(topic.tables);
  if (!topic.figures.blocks.empty() || !topic.figures.declined.empty())
    out << "figures\n" << format_figure_blocks_ir(topic.figures);
  std::map<ProseTokenRoleIR, std::size_t> counts;
  for (const auto& entry : topic.ledger) ++counts[entry.role];
  out << "ledger";
  for (const auto& [role, count] : counts)
    out << ' ' << prose_token_role_name(role) << '=' << count;
  out << '\n';
  for (const auto& entry : topic.ledger) {
    if (entry.role == ProseTokenRoleIR::table ||
        entry.role == ProseTokenRoleIR::figure) {
      out << "  " << prose_token_role_name(entry.role) << ' '
          << entry.token.logical_record << ':' << entry.token.token_index
          << " span=" << entry.span << '\n';
      continue;
    }
    if (entry.role != ProseTokenRoleIR::text) continue;
    out << "  text " << entry.token.logical_record << ':'
        << entry.token.token_index << " block=" << entry.block
        << " inline=" << entry.inline_index << '\n';
  }
  return out.str();
}

} // namespace geist::detail
