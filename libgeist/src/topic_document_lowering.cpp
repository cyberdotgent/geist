#include "geist/detail/topic_document_lowering.hpp"

#include "geist/detail/comment_delivery_document_lowering.hpp"
#include "geist/detail/comment_delivery_ir.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/layout_ir.hpp"
#include "geist/detail/ownership_ir.hpp"

#include <utility>

namespace geist::detail {
namespace {

void reject(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
}

void prepend_topic_id_to_heading(DocumentIR &document) {
  if (document.topic.id.empty() || document.blocks.empty())
    return;
  auto *heading = std::get_if<HeadingBlockIR>(&document.blocks.front().node);
  if (heading == nullptr)
    return;

  InlineIR identity;
  identity.node = TextInlineIR{document.topic.id + " "};
  identity.origin.derivation = DocumentDerivationIR::synthesized;
  identity.origin.detail = "public topic identity prefix";
  heading->content.insert(heading->content.begin(), std::move(identity));
}

} // namespace

std::optional<DocumentIR> try_lower_topic_to_document_ir(
    TopicIdentityIR topic,
    const std::vector<DecodedLogicalRecordSource> &sources,
    std::string *typed_rejection) {
  if (typed_rejection != nullptr)
    typed_rejection->clear();
  if (sources.empty())
    return std::nullopt;

  const auto layout = extract_layout_ir(sources);
  std::string error;
  if (!verify_layout_ir(sources, layout, &error)) {
    reject(typed_rejection, "comment delivery layout rejected: " + error);
    return std::nullopt;
  }
  const auto ownership = build_ownership_ir(sources, layout);
  if (!verify_ownership_ir(sources, layout, ownership, &error)) {
    reject(typed_rejection, "comment delivery ownership rejected: " + error);
    return std::nullopt;
  }

  const auto delivery =
      extract_comment_delivery_ir(sources, layout, ownership, &error);
  if (!delivery)
    return std::nullopt;
  if (!verify_comment_delivery_ir(sources, layout, ownership, *delivery,
                                  &error)) {
    reject(typed_rejection, "comment delivery semantics rejected: " + error);
    return std::nullopt;
  }

  auto document =
      lower_comment_delivery_to_document_ir(topic, *delivery, &error);
  if (!document ||
      !verify_comment_delivery_document_ir(*delivery, *document, &error)) {
    reject(typed_rejection, "comment delivery document rejected: " + error);
    return std::nullopt;
  }

  prepend_topic_id_to_heading(*document);
  if (!verify_document_ir(*document, &error)) {
    reject(typed_rejection,
           "comment delivery identity policy rejected: " + error);
    return std::nullopt;
  }
  return document;
}

} // namespace geist::detail
