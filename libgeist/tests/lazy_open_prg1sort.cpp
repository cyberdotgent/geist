#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";

  const auto sort_reference =
      geist::BooDocument::open(root / "PRG1SORT.boo");
  const auto collating = sort_reference.topic_markdown("C.1");
  // C.1 renders through the typed prose route (three composed ASCII figure
  // spans): the anchors keep the served `FIG<id>` spelling (hosted
  // `<a name="FIGNCS">`, `FIGEBCDIC2`, `FIGEBCDIC3`, DT 19900829171904),
  // which the legacy route truncated, and the preformatted block opens with
  // a bare fence.
  require(collating.find("<a id=\"FIGNCS\"></a>") != std::string::npos &&
              collating.find("<a id=\"FIGEBCDIC2\"></a>") !=
                  std::string::npos &&
              collating.find("<a id=\"FIGEBCDIC3\"></a>") !=
                  std::string::npos &&
              collating.find("```") != std::string::npos &&
              collating.find("Order in") != std::string::npos,
          "collating-sequence figures lost their fixed rows");
  require(collating.find("c.cp") == std::string::npos,
          "PRG1SORT exposed a CCP pagination control");
}
