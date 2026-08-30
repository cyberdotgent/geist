#include "geist/detail/generated_toc_index_document_lowering.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

DocumentNodeOriginIR origin(std::vector<DocumentSourceSliceIR> slices,
                            std::string detail) {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::semantic_lowering;
  result.detail = std::move(detail);
  std::sort(slices.begin(), slices.end(),
            [](const DocumentSourceSliceIR& left,
               const DocumentSourceSliceIR& right) {
              return std::tie(left.logical_record, left.segment_index,
                              left.token_begin, left.token_end) <
                     std::tie(right.logical_record, right.segment_index,
                              right.token_begin, right.token_end);
            });
  slices.erase(std::unique(slices.begin(), slices.end()), slices.end());
  result.slices = std::move(slices);
  return result;
}

std::optional<DocumentIR> canonical_document(
    TopicIdentityIR identity, const GeneratedTocIndexTopicIR& navigation,
    std::string* error) {
  identity.heading_level = navigation.heading_level;
  DocumentIR document;
  document.topic = std::move(identity);

  auto heading_origin =
      origin({navigation.heading_source}, "generated navigation heading");
  document.blocks.push_back(
      {HeadingBlockIR{1, {{TextInlineIR{navigation.title}, heading_origin}}},
       heading_origin});

  for (const auto& anchor : navigation.anchors) {
    auto anchor_origin =
        origin({anchor.second}, "generated navigation source anchor");
    document.blocks.push_back({AnchorBlockIR{anchor.first}, anchor_origin});
  }

  if (navigation.kind == GeneratedTocIndexKindIR::contents) {
    // BookServer serves the generated contents page with a leading
    // `[Summarize]` link to its own `CCONTENTSC` summary view (hosted
    // SC31-711 CONTENTS DT 19941010174546).  It is reader presentation, not
    // source text, exactly like the menu's `Subtopics:` lead line, so it is
    // synthesized here rather than claimed from any token.
    DocumentNodeOriginIR summarize;
    summarize.derivation = DocumentDerivationIR::synthesized;
    summarize.detail = "BookServer contents summary link";
    document.blocks.push_back(
        {ParagraphBlockIR{{{CrossReferenceInlineIR{
                                {CrossReferenceTargetKindIR::anchor,
                                 document.topic.id + "-summary"},
                                "Summarize"},
                            summarize}}},
         summarize});

    ListBlockIR list;
    std::vector<DocumentSourceSliceIR> list_slices;
    for (const auto& entry : navigation.entries) {
      ListItemIR item;
      item.depth = entry.depth;
      auto id_origin = origin(entry.topic_id_slices,
                              "generated contents entry topic id");
      auto title_origin =
          origin(entry.title_slices, "generated contents entry title");
      item.content.push_back({CodeInlineIR{entry.topic_id}, id_origin});
      item.content.push_back({TextInlineIR{" "}, id_origin});
      // The destination is the topic's own id.  `anchor` is the target kind
      // whose context-free destination is `#<id>`, which is what BookServer
      // serves (`<a href="COVER?DT=...">`) once `boo2git` maps it to the
      // exported topic file; the `topic` kind's context-free fallback is the
      // bare value, which is not a usable in-document destination.
      item.content.push_back(
          {CrossReferenceInlineIR{
               {CrossReferenceTargetKindIR::anchor, entry.topic_id},
               entry.title},
           title_origin});
      item.origin = origin({entry.source}, "generated contents entry");
      list_slices.push_back(entry.source);
      list.items.push_back(std::move(item));
    }
    if (list.items.empty()) {
      fail(error, "generated contents has no entries to lower");
      return std::nullopt;
    }
    document.blocks.push_back(
        {std::move(list),
         origin(std::move(list_slices), "generated contents list")});
  } else {
    for (const auto& group : navigation.groups) {
      auto group_origin = origin({group.source}, "generated index group");
      document.blocks.push_back(
          {HeadingBlockIR{2, {{TextInlineIR{group.label}, group_origin}}},
           group_origin});
      ListBlockIR list;
      std::vector<DocumentSourceSliceIR> list_slices;
      for (const auto& term : group.terms) {
        ListItemIR item;
        item.depth = term.level - 1;
        // A term the book wrote with no text at all names no words, so the
        // item carries none: its own source line is still its origin, and the
        // entries below it stay its children.
        auto term_origin = origin(
            term.term_slices.empty() ? std::vector{term.source}
                                     : term.term_slices,
            "generated index term");
        if (!term.term.empty())
          item.content.push_back({TextInlineIR{term.term}, term_origin});
        for (const auto& target : term.targets) {
          auto target_origin =
              origin(target.slices, "generated index term target");
          item.content.push_back({TextInlineIR{", "}, target_origin});
          item.content.push_back(
              {CrossReferenceInlineIR{
                   {CrossReferenceTargetKindIR::anchor, target.topic_id},
                   target.topic_id},
               target_origin});
          if (target.range_end_topic_id.empty()) continue;
          item.content.push_back({TextInlineIR{" to "}, target_origin});
          item.content.push_back(
              {CrossReferenceInlineIR{
                   {CrossReferenceTargetKindIR::anchor,
                    target.range_end_topic_id},
                   target.range_end_topic_id},
               target_origin});
        }
        // An entry the book wrote with no term text and no target of its own
        // draws nothing, but it is still the parent of the entries below it.
        item.empty_content = item.content.empty();
        item.origin = origin({term.source}, "generated index entry");
        list_slices.push_back(term.source);
        list.items.push_back(std::move(item));
      }
      if (list.items.empty()) {
        fail(error, "generated index group has no terms to lower");
        return std::nullopt;
      }
      document.blocks.push_back(
          {std::move(list),
           origin(std::move(list_slices), "generated index group list")});
    }
  }

  // Every container names at least the BOO bytes its own content names
  // before the document is verified.
  normalize_document_origin_slices(document);
  std::string document_error;
  if (!verify_document_ir(document, &document_error)) {
    fail(error, "invalid generated navigation DocumentIR: " + document_error);
    return std::nullopt;
  }
  if (error != nullptr) error->clear();
  return document;
}

} // namespace

std::optional<DocumentIR> lower_generated_toc_index_topic_to_document_ir(
    TopicIdentityIR identity, const GeneratedTocIndexTopicIR& navigation,
    std::string* error) {
  return canonical_document(std::move(identity), navigation, error);
}

bool verify_generated_toc_index_topic_document_ir(
    const GeneratedTocIndexTopicIR& navigation, const DocumentIR& document,
    std::string* error) {
  const auto expected = canonical_document(document.topic, navigation, error);
  if (!expected) return false;
  if (format_document_ir(*expected) != format_document_ir(document))
    return fail(error,
                "generated navigation DocumentIR differs from canonical "
                "lowering");
  if (error != nullptr) error->clear();
  return true;
}

} // namespace geist::detail
