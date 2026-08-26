#include "geist/detail/generated_list_document_lowering.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

DocumentNodeOriginIR row_origin(const SelectorDisplayRowIR& row,
                                std::size_t begin, std::size_t end,
                                std::string detail) {
  DocumentNodeOriginIR origin;
  origin.derivation = DocumentDerivationIR::semantic_lowering;
  origin.detail = std::move(detail);
  if (row.owner.run != 0)
    origin.rows.push_back({row.owner.run, row.owner.physical_row_index});
  std::set<std::tuple<std::uint32_t, std::size_t, std::uint32_t,
                      std::uint32_t>> slices;
  for (auto index = begin; index < end; ++index) {
    const auto& source = row.cells[index].source;
    if (!source) continue;
    slices.emplace(source->logical_record, source->token_index,
                   source->token_bytes.begin, source->token_bytes.end);
  }
  for (const auto& slice : slices)
    origin.slices.push_back({std::get<0>(slice), row.owner.segment_index,
                             std::get<1>(slice), std::get<1>(slice) + 1,
                             std::get<2>(slice), std::get<3>(slice)});
  return origin;
}

std::string cell_text(const SelectorDisplayRowIR& row, std::size_t begin,
                      std::size_t end) {
  TokenWords words;
  words.reserve(end - begin);
  for (auto index = begin; index < end; ++index)
    words.push_back(row.cells[index].word);
  return token_words_to_ascii(words);
}

bool visible(std::uint16_t word) {
  return word >= 0x21 && word != '?' && word != 0x2666;
}

void append_text(InlineSequenceIR& inlines, const SelectorDisplayRowIR& row,
                 std::size_t begin, std::size_t end) {
  if (begin == end) return;
  auto text = cell_text(row, begin, end);
  if (text.empty()) return;
  auto origin = row_origin(row, begin, end, "generated-list row text");
  inlines.push_back({TextInlineIR{std::move(text)}, std::move(origin)});
}

CrossReferenceTargetIR target(const SelectorTargetIR& source) {
  switch (source.kind) {
  case SelectorTargetKind::internal_anchor:
    return {CrossReferenceTargetKindIR::anchor, source.raw_target};
  case SelectorTargetKind::picture_resource:
    return {CrossReferenceTargetKindIR::resource, source.raw_target};
  case SelectorTargetKind::external_deferred:
    return {CrossReferenceTargetKindIR::external, source.raw_target};
  }
  return {};
}

std::optional<DocumentIR> canonical_document(TopicIdentityIR identity,
                                             const GeneratedListTopicIR& list,
                                             std::string* error) {
  if (list.title.empty() || list.entries.empty()) {
    fail(error, "generated-list semantics are incomplete");
    return std::nullopt;
  }
  identity.heading_level = "h1";
  DocumentIR document;
  document.topic = std::move(identity);

  DocumentNodeOriginIR heading_origin;
  heading_origin.derivation = DocumentDerivationIR::semantic_lowering;
  heading_origin.slices.push_back(list.heading_source);
  heading_origin.detail = "generated-list heading";
  InlineIR heading_text{TextInlineIR{list.title}, heading_origin};
  document.blocks.push_back(
      {HeadingBlockIR{1, {std::move(heading_text)}}, heading_origin});

  for (const auto& row : list.entries) {
    if (row.spans.size() != 1 || row.cells.empty()) {
      fail(error, "generated-list entry geometry is incomplete");
      return std::nullopt;
    }
    auto begin = std::size_t{0};
    auto end = row.cells.size();
    while (begin < end && !visible(row.cells[begin].word)) ++begin;
    while (end > begin && !visible(row.cells[end - 1].word)) --end;
    const auto& span = row.spans.front();
    auto link_begin = std::max(begin, span.cell_begin);
    auto link_end = std::min(end, span.cell_end);
    while (link_begin < link_end && row.cells[link_begin].word == ' ')
      ++link_begin;
    while (link_end > link_begin && row.cells[link_end - 1].word == ' ')
      --link_end;
    if (link_begin == link_end) {
      fail(error, "generated-list entry has an empty link label");
      return std::nullopt;
    }
    InlineSequenceIR content;
    append_text(content, row, begin, link_begin);
    auto link_origin =
        row_origin(row, link_begin, link_end, "generated-list link label");
    content.push_back(
        {CrossReferenceInlineIR{target(span.target),
                                cell_text(row, link_begin, link_end)},
         link_origin});
    append_text(content, row, link_end, end);
    auto block_origin = row_origin(row, begin, end, "generated-list entry");
    document.blocks.push_back(
        {ParagraphBlockIR{std::move(content)}, std::move(block_origin)});
  }
  std::string document_error;
  if (!verify_document_ir(document, &document_error)) {
    fail(error, "invalid generated-list DocumentIR: " + document_error);
    return std::nullopt;
  }
  if (error != nullptr) error->clear();
  return document;
}

} // namespace

std::optional<DocumentIR> lower_generated_list_topic_to_document_ir(
    TopicIdentityIR identity, const GeneratedListTopicIR& list,
    std::string* error) {
  return canonical_document(std::move(identity), list, error);
}

bool verify_generated_list_topic_document_ir(
    const GeneratedListTopicIR& list, const DocumentIR& document,
    std::string* error) {
  const auto expected = canonical_document(document.topic, list, error);
  if (!expected) return false;
  // DocumentIR's formatter is a stable structural equality projection and
  // includes typed cross-reference kinds and source provenance.
  if (format_document_ir(*expected) != format_document_ir(document))
    return fail(error,
                "generated-list DocumentIR differs from canonical lowering");
  if (error != nullptr) error->clear();
  return true;
}

} // namespace geist::detail
