#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";

  const auto starter_trademarks =
      geist::BooDocument::open(root / "SH20-918.boo")
          .topic_markdown("FRONT_1.3");
  require(starter_trademarks.find(
              "The following terms are trademarks of other companies as "
              "follows:") != std::string::npos &&
              starter_trademarks.find(
                  "| PostScript | Adobe Systems Incorporated |") !=
                  std::string::npos &&
              starter_trademarks.find("c.cp") == std::string::npos,
          "SH20 trademark tables changed while projecting visible CCP prose");
}
