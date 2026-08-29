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
    // Typed `CZ FLOW DT` definition list; hosted 1.2.5 (DT 19951003131222)
    // shows `<dt>   CPF24B4 E<dd>Severe error ...` and the typed renderer
    // writes the term as `- **term:** definition`.
    require(configuration_errors.find("- **" + std::string(expected) +
                                      ":** ") != std::string::npos,
            "definition list lost an error-code association");
  }
  const auto configuration_actions =
      configuration_manager.topic_markdown("1.1");
  // Hosted 1.1: `<dt>   <B>List</B><dd><B>Configuration</B> <B>Descriptions</B>
  // (QDCLCFGD) returns ...`.  The term is emphasised by the renderer, so the
  // wholly highlighted source term lowers as plain text.
  require(configuration_actions.find(
              "- **List:** **Configuration Descriptions** \\(QDCLCFGD\\) "
              "returns a list of configuration descriptions") !=
                  std::string::npos &&
              configuration_actions.find("- **Retrieve:** **Configuration "
                                         "Status**") != std::string::npos,
          "definition list lost an action description");
  // 1.2.2 now reaches the typed route: hosted (DT 19951003131222) serves the
  // qualifier rows as a definition list -- `<dt><B>*APPC</B><dd>APPC
  // controllers and devices only` -- which the typed route lowers as a
  // definition entry (the `:` after the term is the renderer's dt/dd
  // separator).  The legacy route tore the term's emphasis and dropped the
  // tail of every description.
  const auto configuration_qualifiers =
      configuration_manager.topic_markdown("1.2.2");
  for (const auto* expected :
       {"- **\\*APPC:** APPC controllers and devices only",
        "- **\\*FR:** Frame relay lines only",
        "- **\\*LANPRT:** LAN printer devices only",
        "- **\\*OPT:** Optical devices only",
        "- **\\*SNUF:** SNA upline facility devices only"}) {
    require(configuration_qualifiers.find(expected) != std::string::npos,
            "visual-bar CFONT row tore a configuration qualifier");
  }
  require(configuration_qualifiers.find("**|**") == std::string::npos,
          "visual-bar CFONT row highlighted its structural marker");
  // Hosted serves the cross-book links as
  // `../../DOCNUM/SC41-4801/HDRERRCOD` and `../../DOCNUM/SC41-4801/
  // CCONTENTS`; the legacy route emitted `#SC41-4801/4801` and split the
  // label into `"Err [or Code Parameter" in]`.
  require(configuration_qualifiers.find("cselect") == std::string::npos &&
              configuration_qualifiers.find("<BOOK>") == std::string::npos &&
              configuration_qualifiers.find(
                  "[\"Error Code Parameter\"](<DOCNUM/SC41-4801/HDRERRCOD>)") !=
                  std::string::npos &&
              configuration_qualifiers.find("<DOCNUM/SC41-4801/CCONTENTS>") !=
                  std::string::npos,
          "cross-book table selector metadata leaked or lost its target");
}
