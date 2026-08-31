#include "geist/detail/lowering/generated_list_document_lowering.hpp"

#include "geist/detail/core/internal.hpp"

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

DocumentNodeOriginIR fragment_origin(const GeneratedListEntryIR& entry,
                                     std::string detail) {
  DocumentNodeOriginIR origin;
  origin.derivation = DocumentDerivationIR::semantic_lowering;
  origin.detail = std::move(detail);
  if (entry.display.owner.run != 0)
    origin.rows.push_back({entry.display.owner.run,
                           entry.display.owner.physical_row_index});
  std::set<std::tuple<std::uint32_t, std::size_t, std::size_t, std::size_t,
                      std::uint32_t, std::uint32_t>> slices;
  for (const auto& fragment : entry.label_fragments)
    for (const auto& slice : fragment.source_slices)
      slices.emplace(slice.logical_record, slice.segment_index,
                     slice.token_begin, slice.token_end, slice.byte_begin,
                     slice.byte_end);
  for (const auto& slice : slices)
    origin.slices.push_back({std::get<0>(slice), std::get<1>(slice),
                             std::get<2>(slice), std::get<3>(slice),
                             std::get<4>(slice), std::get<5>(slice)});
  return origin;
}

std::string label_text(const GeneratedListEntryIR& entry) {
  TokenWords words;
  for (const auto& fragment : entry.label_fragments)
    for (const auto& cell : fragment.cells) words.push_back(cell.word);
  return token_words_to_ascii(words);
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

  for (const auto& entry : list.entries) {
    if (entry.label_fragments.empty()) {
      fail(error, "generated-list entry semantics are incomplete");
      return std::nullopt;
    }
    auto label = label_text(entry);
    if (label.empty()) {
      fail(error, "generated-list entry has an empty link label");
      return std::nullopt;
    }
    auto link_origin =
        fragment_origin(entry, "generated-list typed link label");
    InlineSequenceIR content;
    content.push_back(
        {CrossReferenceInlineIR{target(entry.target), std::move(label)},
         link_origin});
    auto block_origin = fragment_origin(entry, "generated-list entry");
    document.blocks.push_back(
        {ParagraphBlockIR{std::move(content)}, std::move(block_origin)});
  }
  // Every container names at least the BOO bytes its own content names
  // before the document is verified.
  normalize_document_origin_slices(document);
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
