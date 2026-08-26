#include "geist/detail/topic_document_lowering.hpp"

#include "geist/detail/comment_delivery_document_lowering.hpp"
#include "geist/detail/comment_delivery_ir.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/layout_ir.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/publication_document_lowering.hpp"
#include "geist/detail/publication_ir.hpp"

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
    reject(typed_rejection, "topic layout rejected: " + error);
    return std::nullopt;
  }
  const auto ownership = build_ownership_ir(sources, layout);
  if (!verify_ownership_ir(sources, layout, ownership, &error)) {
    reject(typed_rejection, "topic ownership rejected: " + error);
    return std::nullopt;
  }

  const auto delivery =
      extract_comment_delivery_ir(sources, layout, ownership, nullptr);
  const auto publications =
      extract_publication_catalog_ir(sources, layout, ownership);
  const auto family_count = static_cast<unsigned>(delivery.has_value()) +
                            static_cast<unsigned>(publications.has_value());
  if (family_count == 0)
    return std::nullopt;
  if (family_count != 1) {
    reject(typed_rejection,
           "topic source ambiguously matches multiple typed families");
    return std::nullopt;
  }

  std::optional<DocumentIR> document;
  std::string family;
  if (delivery) {
    family = "comment delivery";
    if (!verify_comment_delivery_ir(sources, layout, ownership, *delivery,
                                    &error)) {
      reject(typed_rejection, family + " semantics rejected: " + error);
      return std::nullopt;
    }
    document = lower_comment_delivery_to_document_ir(topic, *delivery, &error);
    if (!document ||
        !verify_comment_delivery_document_ir(*delivery, *document, &error)) {
      reject(typed_rejection, family + " document rejected: " + error);
      return std::nullopt;
    }
  } else {
    family = "publication catalog";
    if (!verify_publication_catalog_ir(sources, layout, ownership,
                                       *publications, &error)) {
      reject(typed_rejection, family + " semantics rejected: " + error);
      return std::nullopt;
    }
    // The verified source model is authoritative. TopicData's compatibility
    // metadata field may include packed controls after CHDLEVEL when the
    // source uses spaced separators.
    topic.heading_level = publications->heading_level;
    document = lower_publication_catalog_to_document_ir(
        topic, *publications, &error);
    if (!document || !verify_publication_catalog_document_ir(
                         *publications, *document, &error)) {
      reject(typed_rejection, family + " document rejected: " + error);
      return std::nullopt;
    }
  }

  prepend_topic_id_to_heading(*document);
  if (!verify_document_ir(*document, &error)) {
    reject(typed_rejection,
           family + " identity policy rejected: " + error);
    return std::nullopt;
  }
  return document;
}

} // namespace geist::detail
