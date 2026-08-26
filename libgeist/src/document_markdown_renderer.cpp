#include "geist/detail/document_markdown_renderer.hpp"

#include "geist/detail/internal.hpp"

#include <stdexcept>
#include <string>

namespace geist::detail {

std::string render_document_markdown(const DocumentIR& document) {
  std::string error;
  if (!verify_document_ir(document, &error)) {
    throw std::invalid_argument("invalid DocumentIR: " + error);
  }

  // verify_document_ir already guarantees that a legacy region is the only
  // block.  Keep an explicit adapter check here so future verifier extensions
  // cannot accidentally split a stateful legacy topic across renderer calls.
  if (document.blocks.size() != 1) {
    throw std::invalid_argument(
        "DocumentIR Markdown adapter requires one legacy region");
  }
  const auto* region =
      std::get_if<LegacyGmlRegionIR>(&document.blocks.front().node);
  if (region == nullptr ||
      region->state_scope != LegacyRendererStateScopeIR::whole_topic) {
    throw std::invalid_argument(
        "DocumentIR Markdown adapter requires one whole-topic legacy region");
  }
  return render_markdown_records(region->normalized_records);
}

} // namespace geist::detail
