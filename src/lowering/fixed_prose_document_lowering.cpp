// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/fixed_prose_document_lowering.hpp"

#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

DocumentNodeOriginIR origin(DocumentSourceSliceIR slice, std::string detail) {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::semantic_lowering;
  result.slices.push_back(std::move(slice));
  result.detail = std::move(detail);
  return result;
}

bool same_slice(const DocumentSourceSliceIR& a,
                const DocumentSourceSliceIR& b) {
  return a.logical_record == b.logical_record &&
         a.segment_index == b.segment_index && a.token_begin == b.token_begin &&
         a.token_end == b.token_end && a.byte_begin == b.byte_begin &&
         a.byte_end == b.byte_end;
}

bool same_origin(const DocumentNodeOriginIR& a,
                 const DocumentNodeOriginIR& b) {
  if (a.derivation != b.derivation || a.detail != b.detail ||
      a.rows.size() != b.rows.size() || a.slices.size() != b.slices.size())
    return false;
  for (std::size_t index = 0; index < a.rows.size(); ++index)
    if (a.rows[index].display_run != b.rows[index].display_run ||
        a.rows[index].row_index != b.rows[index].row_index)
      return false;
  for (std::size_t index = 0; index < a.slices.size(); ++index)
    if (!same_slice(a.slices[index], b.slices[index])) return false;
  return true;
}

bool same_text_inline(const InlineIR& a, const InlineIR& b) {
  const auto* actual = std::get_if<TextInlineIR>(&a.node);
  const auto* expected = std::get_if<TextInlineIR>(&b.node);
  return actual != nullptr && expected != nullptr &&
         actual->text == expected->text && same_origin(a.origin, b.origin);
}

bool same_document(const DocumentIR& a, const DocumentIR& b) {
  if (a.topic.id != b.topic.id || a.topic.title != b.topic.title ||
      a.topic.heading_level != b.topic.heading_level ||
      a.topic.topic_number != b.topic.topic_number ||
      a.topic.start_logical_record != b.topic.start_logical_record ||
      a.topic.end_logical_record != b.topic.end_logical_record ||
      a.blocks.size() != b.blocks.size())
    return false;
  for (std::size_t index = 0; index < a.blocks.size(); ++index) {
    if (!same_origin(a.blocks[index].origin, b.blocks[index].origin))
      return false;
    if (const auto* expected =
            std::get_if<HeadingBlockIR>(&b.blocks[index].node)) {
      const auto* actual = std::get_if<HeadingBlockIR>(&a.blocks[index].node);
      if (actual == nullptr || actual->level != expected->level ||
          actual->content.size() != 1 || expected->content.size() != 1 ||
          !same_text_inline(actual->content.front(),
                            expected->content.front()))
        return false;
    } else if (const auto* expected =
                   std::get_if<AnchorBlockIR>(&b.blocks[index].node)) {
      const auto* actual = std::get_if<AnchorBlockIR>(&a.blocks[index].node);
      if (actual == nullptr || actual->id != expected->id) return false;
    } else {
      const auto* expected_paragraph =
          std::get_if<ParagraphBlockIR>(&b.blocks[index].node);
      const auto* actual =
          std::get_if<ParagraphBlockIR>(&a.blocks[index].node);
      if (actual == nullptr || expected_paragraph == nullptr ||
          actual->content.size() != 1 ||
          expected_paragraph->content.size() != 1 ||
          !same_text_inline(actual->content.front(),
                            expected_paragraph->content.front()))
        return false;
    }
  }
  return true;
}

} // namespace

std::optional<DocumentIR> lower_fixed_prose_topic_to_document_ir(
    TopicIdentityIR identity, const FixedProseTopicIR& prose,
    std::string* error) {
  if (prose.segments.empty() ||
      prose.prose.segment_index >= prose.segments.size()) {
    fail(error, "fixed prose topic has invalid segment provenance");
    return std::nullopt;
  }
  if (prose.heading_level.size() != 2 || prose.heading_level.front() != 'h' ||
      prose.heading_level.back() < '1' || prose.heading_level.back() > '6' ||
      prose.prose.title.empty() || prose.prose.paragraph.empty() ||
      prose.heading_source.token_begin >= prose.heading_source.token_end ||
      prose.paragraph_source.token_begin >= prose.paragraph_source.token_end) {
    fail(error, "fixed prose topic semantics are incomplete");
    return std::nullopt;
  }
  // The source-proven CHDLEVEL is authoritative. Compatibility TopicInfo
  // metadata can retain packed separators or other legacy projections.
  identity.heading_level = prose.heading_level;

  DocumentIR document;
  document.topic = std::move(identity);
  const auto level = static_cast<std::uint32_t>(prose.heading_level.back() - '0');

  auto heading_origin = origin(prose.heading_source, "fixed prose heading");
  InlineIR heading_text{TextInlineIR{prose.prose.title}, heading_origin};
  document.blocks.push_back(
      {HeadingBlockIR{level, {std::move(heading_text)}}, heading_origin});

  if (prose.anchor) {
    auto anchor_origin =
        origin(prose.anchor->source, "fixed prose source anchor");
    document.blocks.push_back(
        {AnchorBlockIR{prose.anchor->id}, std::move(anchor_origin)});
  }

  auto paragraph_origin = origin(
      prose.paragraph_source, "fixed prose paragraph");
  InlineIR paragraph_text{TextInlineIR{prose.prose.paragraph},
                          paragraph_origin};
  document.blocks.push_back(
      {ParagraphBlockIR{{std::move(paragraph_text)}}, paragraph_origin});

  // Every container names at least the BOO bytes its own content names
  // before the document is verified.
  normalize_document_origin_slices(document);
  std::string document_error;
  if (!verify_document_ir(document, &document_error)) {
    fail(error, "invalid fixed prose DocumentIR: " + document_error);
    return std::nullopt;
  }
  if (error != nullptr) error->clear();
  return document;
}

bool verify_fixed_prose_topic_document_ir(
    const FixedProseTopicIR& prose, const DocumentIR& document,
    std::string* error) {
  auto expected =
      lower_fixed_prose_topic_to_document_ir(document.topic, prose, error);
  if (!expected) return false;
  if (!same_document(document, *expected))
    return fail(error,
                "fixed prose DocumentIR differs from canonical lowering");
  if (error != nullptr) error->clear();
  return true;
}

} // namespace geist::detail
