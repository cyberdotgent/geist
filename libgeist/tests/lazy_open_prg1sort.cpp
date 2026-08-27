#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";

  const auto sort_reference =
      geist::BooDocument::open(root / "PRG1SORT.boo");
  const auto collating = sort_reference.topic_markdown("C.1");
  require(collating.find("<a id=\"NCS\"></a>") != std::string::npos &&
              collating.find("<a id=\"EBCDIC2\"></a>") !=
                  std::string::npos &&
              collating.find("<a id=\"EBCDIC3\"></a>") !=
                  std::string::npos &&
              collating.find("```text") != std::string::npos &&
              collating.find("Order in") != std::string::npos,
          "collating-sequence figures lost their fixed rows");
  require(collating.find("c.cp") == std::string::npos,
          "PRG1SORT exposed a CCP pagination control");
}
