#include "geist/detail/prose_topic_ir.hpp"

#include "geist/detail/book_topic_catalog_ir.hpp"
#include "geist/detail/implicit_grid.hpp"
#include "geist/detail/prose_topic_internal.hpp"

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
    const LayoutIR& layout, const OwnershipIR& ownership,
    const std::string& title, const BookTopicCatalogIR* book_topic_catalog,
    std::string* error) {
  const auto reject = [&](std::string message) -> std::optional<ProseTopicIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (records.empty()) return reject("topic has no records");
  std::string verification_error;
  if (!verify_layout_ir(records, layout, &verification_error) ||
      !verify_ownership_ir(records, layout, ownership, &verification_error))
    return reject("source layout/ownership is not canonical: " +
                  verification_error);
  for (const auto& record : records)
    if (record.ir.tokens.size() != record.tokens.size() ||
        record.ir.tokens.size() != record.encoded_tokens.size())
      return reject("record token projections disagree");

  Ledger ledger(records);
  Envelope envelope;
  if (!parse_envelope(records, ledger, envelope, error)) return std::nullopt;
  StreamBuild stream;
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

  if (normalize_title(lines.title) != normalize_title(title))
    return reject("ST title '" + lines.title +
                  "' does not match the topic title '" + title + "'");

  ProseTopicIR topic;
  topic.record_count = records.size();
  topic.token_count = ledger.entries.size();
  topic.heading_level = envelope.heading_level;
  topic.title = lines.title;
  {
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
  if (topic.blocks.empty() && topic.menu_items.empty())
    return reject("topic body has no prose blocks");
  if (error != nullptr) error->clear();
  return topic;
}

bool verify_prose_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const std::string& title, const BookTopicCatalogIR* book_topic_catalog,
    const ProseTopicIR& topic, std::string* error) {
  std::size_t total = 0;
  for (const auto& record : records) total += record.ir.tokens.size();
  if (topic.ledger.size() != total || topic.token_count != total ||
      topic.record_count != records.size())
    return fail(error, "prose ledger does not cover every source token");
  std::set<std::pair<std::uint32_t, std::size_t>> seen;
  std::map<std::pair<std::uint32_t, std::size_t>, const ProseTokenDispositionIR*>
      by_token;
  for (const auto& entry : topic.ledger) {
    if (!seen.emplace(entry.token.logical_record, entry.token.token_index).second)
      return fail(error, "prose ledger lists a token twice");
    if (entry.role == ProseTokenRoleIR::unassigned)
      return fail(error, "prose ledger holds an unassigned token");
    if ((entry.role == ProseTokenRoleIR::text) != (entry.block != npos))
      return fail(error, "text disposition and block ownership disagree");
    by_token.emplace(std::make_pair(entry.token.logical_record,
                                    entry.token.token_index),
                     &entry);
  }
  // Every text token is covered by exactly one inline slice and every inline
  // slice covers only text tokens of its own inline.
  std::set<std::pair<std::uint32_t, std::size_t>> covered;
  for (std::size_t block = 0; block < topic.blocks.size(); ++block) {
    const auto& node = topic.blocks[block];
    if (node.inlines.empty()) return fail(error, "prose block has no inlines");
    for (std::size_t index = 0; index < node.inlines.size(); ++index) {
      const auto& inline_node = node.inlines[index];
      if (inline_node.text.empty()) return fail(error, "inline text is empty");
      for (const auto& slice : inline_node.slices) {
        if (slice.token_begin >= slice.token_end)
          return fail(error, "inline slice is empty");
        for (auto token = slice.token_begin; token < slice.token_end; ++token) {
          const auto found = by_token.find({slice.logical_record, token});
          if (found == by_token.end() ||
              found->second->role != ProseTokenRoleIR::text ||
              found->second->block != block ||
              found->second->inline_index != index)
            return fail(error, "inline slice covers a token it does not own");
          if (!covered.emplace(slice.logical_record, token).second)
            return fail(error, "token covered by two inline slices");
        }
      }
    }
  }
  for (const auto& entry : topic.ledger)
    if (entry.role == ProseTokenRoleIR::text &&
        covered.count({entry.token.logical_record, entry.token.token_index}) == 0)
      return fail(error, "visible token is covered by no inline");
  const auto canonical = extract_prose_topic_ir(records, layout, ownership,
                                                title, book_topic_catalog, error);
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
  case ProseTokenRoleIR::menu: return "menu";
  case ProseTokenRoleIR::ordinal: return "ordinal";
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
  }
  out << ']';
}

} // namespace

std::string format_prose_topic_ir(const ProseTopicIR& topic) {
  std::ostringstream out;
  out << "prose_topic records=" << topic.record_count
      << " tokens=" << topic.token_count << " heading_level="
      << topic.heading_level << " title='" << topic.title << "'";
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
      }
      out << " '" << inline_node.text << "'";
      format_slices(out, inline_node.slices);
      out << '\n';
    }
  }
  for (const auto& term : topic.index_terms) {
    out << "index_term '" << term.term << "'";
    format_slices(out, term.slices);
    out << '\n';
  }
  for (const auto& item : topic.menu_items) {
    out << "menu_item target=" << item.target << " label='" << item.label
        << "'";
    format_slices(out, {item.source});
    out << '\n';
  }
  std::map<ProseTokenRoleIR, std::size_t> counts;
  for (const auto& entry : topic.ledger) ++counts[entry.role];
  out << "ledger";
  for (const auto& [role, count] : counts)
    out << ' ' << prose_token_role_name(role) << '=' << count;
  out << '\n';
  for (const auto& entry : topic.ledger) {
    if (entry.role != ProseTokenRoleIR::text) continue;
    out << "  text " << entry.token.logical_record << ':'
        << entry.token.token_index << " block=" << entry.block
        << " inline=" << entry.inline_index << '\n';
  }
  return out.str();
}

} // namespace geist::detail
