#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";

  const auto configuration_manager =
      geist::BooDocument::open(root / "SC41-485.boo");
  const auto configuration_errors =
      configuration_manager.topic_markdown("1.2.5");
  for (const auto* expected : {"CPF24B4 E", "CPF26A8 E", "CPF26A9 E",
                               "CPF26AA E", "CPF3C21 E", "CPF3C90 E",
                               "CPF3CF1 E", "CPF9872 E"}) {
    require(configuration_errors.find("- **" + std::string(expected) +
                                      "** — ") != std::string::npos,
            "definition list lost an error-code association");
  }
  const auto configuration_actions =
      configuration_manager.topic_markdown("1.1");
  require(configuration_actions.find("- **List** — ") !=
              std::string::npos &&
              configuration_actions.find("- **Retrieve** — ") !=
                  std::string::npos &&
              configuration_actions.find("(QDCLCFGD)") !=
                  std::string::npos,
          "definition list lost an action description");
  const auto configuration_qualifiers =
      configuration_manager.topic_markdown("1.2.2");
  for (const auto* expected : {"***APPC** APPC controllers",
                               "***FR** Frame relay lines",
                               "***LANPRT** LAN printer devices",
                               "***OPT** Optical devices",
                               "***SNUF** SNA upline facility devices"}) {
    require(configuration_qualifiers.find(expected) != std::string::npos,
            "visual-bar CFONT row tore a configuration qualifier");
  }
  require(configuration_qualifiers.find("**|**") == std::string::npos,
          "visual-bar CFONT row highlighted its structural marker");
  require(configuration_qualifiers.find("cselect") == std::string::npos &&
              configuration_qualifiers.find("<BOOK>") == std::string::npos &&
              configuration_qualifiers.find("SC41-4801/4801") !=
                  std::string::npos,
          "cross-book table selector metadata leaked or lost its target");
}
