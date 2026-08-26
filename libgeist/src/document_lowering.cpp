#include "geist/detail/document_lowering.hpp"

#include <utility>

namespace geist::detail {

DocumentIR lower_legacy_topic_to_document_ir(
    TopicIdentityIR topic, std::vector<std::string> normalized_records) {
  DocumentIR document;
  document.topic = std::move(topic);

  LegacyGmlRegionIR region;
  region.normalized_records = std::move(normalized_records);
  region.state_scope = LegacyRendererStateScopeIR::whole_topic;

  BlockIR block;
  block.node = std::move(region);
  block.origin.derivation = DocumentDerivationIR::legacy_adapter;
  block.origin.detail = "whole-topic legacy GML compatibility region";
  document.blocks.push_back(std::move(block));
  return document;
}

} // namespace geist::detail
