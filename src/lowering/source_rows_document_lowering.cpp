// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/source_rows_document_lowering.hpp"

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/verbatim_cross_references.hpp"
#include "geist/detail/render/render_diagnostic_ir.hpp"

#include <algorithm>
#include <cctype>
#include <type_traits>
#include <utility>

namespace geist::detail {
namespace {

DocumentNodeOriginIR synthesized(std::string detail) {
  DocumentNodeOriginIR origin;
  origin.derivation = DocumentDerivationIR::synthesized;
  origin.detail = std::move(detail);
  return origin;
}

std::uint32_t heading_level_of(std::string value) {
  value = ascii_lower(value);
  value = value.substr(0, value.find_first_of(" \t"));
  if (!value.empty() && value.front() == ':') value.erase(0, 1);
  if (value.size() == 2 && value.front() == 'h' && value.back() >= '1' &&
      value.back() <= '6')
    return static_cast<std::uint32_t>(value.back() - '0');
  return 1;
}

DocumentNodeOriginIR line_origin(
    const std::vector<DecodedLogicalRecordSource> &sources,
    const BestEffortLineIR &line) {
  DocumentNodeOriginIR origin;
  origin.derivation = DocumentDerivationIR::semantic_lowering;
  origin.detail = "decoded source display row";
  if (line.record_index >= sources.size()) return origin;
  const auto &record = sources[line.record_index];
  const auto *display = record_display_lines(record);
  if (display == nullptr || line.display_line_index >= display->size())
    return origin;
  const auto &source_line = (*display)[line.display_line_index];
  const auto begin = std::min(source_line.prefix_token + 1,
                              record.ir.tokens.size());
  const auto end = std::min(source_line.token_end, record.ir.tokens.size());
  if (begin >= end) return origin;
  DocumentSourceSliceIR slice;
  slice.logical_record = record.logical_record;
  slice.token_begin = begin;
  slice.token_end = end;
  slice.byte_begin = record.ir.tokens[begin].byte_range.begin;
  slice.byte_end = record.ir.tokens[end - 1].byte_range.end;
  origin.slices.push_back(slice);
  return origin;
}

AnchorRoleIR anchor_role(const std::string &id) {
  if (ascii_starts_with_case_insensitive(id, "fig"))
    return AnchorRoleIR::figure;
  if (ascii_starts_with_case_insensitive(id, "tbl"))
    return AnchorRoleIR::table;
  return AnchorRoleIR::cross_reference;
}

BlockIR anchor_block(std::string id, AnchorRoleIR role) {
  BlockIR block;
  block.node = AnchorBlockIR{std::move(id), role};
  block.origin = synthesized("source-row structural destination");
  return block;
}

} // namespace

bool document_ir_has_body(const DocumentIR &document) {
  return std::any_of(document.blocks.begin(), document.blocks.end(),
                     [](const BlockIR &block) {
    return std::visit(
        [](const auto &node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, HeadingBlockIR> ||
                        std::is_same_v<T, AnchorBlockIR>)
            return false;
          else if constexpr (std::is_same_v<T, PreformattedBlockIR>)
            return !node.lines.empty();
          else
            return true;
        },
        block.node);
  });
}

SourceRowsDocumentLoweringIR lower_source_rows_to_document_ir(
    TopicIdentityIR topic,
    const std::vector<DecodedLogicalRecordSource> &sources) {
  SourceRowsDocumentLoweringIR result;
  result.source_decoded = !sources.empty();
  if (!result.source_decoded) return result;

  DocumentIR document;
  document.topic = std::move(topic);

  HeadingBlockIR heading;
  heading.level = heading_level_of(document.topic.heading_level);
  InlineIR heading_text;
  heading_text.node = TextInlineIR{
      (document.topic.id.empty() ? std::string()
                                 : document.topic.id + " ") +
      document.topic.title};
  heading_text.origin = synthesized("public topic identity and title");
  heading.content.push_back(std::move(heading_text));
  heading.revision_mark = topic_title_revision_mark(sources);
  BlockIR heading_block;
  heading_block.node = std::move(heading);
  heading_block.origin = synthesized("source-row topic heading");
  document.blocks.push_back(std::move(heading_block));

  for (auto id : best_effort_anchors(sources))
    document.blocks.push_back(anchor_block(id, anchor_role(id)));

  const auto printed_footnotes = best_effort_footnote_anchors(sources);
  auto lines = best_effort_display_lines(sources, document.topic.title);
  auto linked =
      link_verbatim_cross_references(sources, lines, printed_footnotes);
  // A footnote destination is placed in this document even when no selector
  // in this same topic uses it. References from another topic can still name
  // it, and the shared DocumentIR link map applies one policy to typed and
  // source-row representations.
  for (const auto &id : printed_footnotes)
    document.blocks.push_back(anchor_block(id, AnchorRoleIR::local));

  if (!linked.rows.empty()) {
    PreformattedBlockIR preformatted;
    preformatted.lines.reserve(linked.rows.size());
    preformatted.line_links.reserve(linked.rows.size());
    preformatted.line_origins.reserve(linked.rows.size());
    for (std::size_t index = 0; index < linked.rows.size(); ++index) {
      preformatted.lines.push_back(linked.rows[index].text);
      preformatted.line_links.push_back(std::move(linked.rows[index].links));
      preformatted.line_origins.push_back(line_origin(sources, lines[index]));
    }
    BlockIR block;
    block.node = std::move(preformatted);
    block.origin.derivation = DocumentDerivationIR::semantic_lowering;
    block.origin.detail = "decoded source display rows";
    document.blocks.push_back(std::move(block));
  }

  normalize_document_origin_slices(document);
  result.has_body = document_ir_has_body(document);
  result.document = std::move(document);
  return result;
}

} // namespace geist::detail
