#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";

  const auto acpz_edition =
      geist::BooDocument::open(root / "ACPZMST1.boo")
          .topic_markdown("EDITION");
  require(acpz_edition.find("VM Programmable Workstation Communication "
                            "Services") != std::string::npos &&
              acpz_edition.find("Managing VM PWSCS") != std::string::npos,
          "all-E banner gate changed ACPZMST1 ordinary CFONT prose");
  const auto dre_edition =
      geist::BooDocument::open(root / "DREICMST.boo");
  const auto dre_edition_markdown = dre_edition.topic_markdown("EDITION");
  require(dre_edition_markdown.find(
              "This major revision obsoletes and replaces") !=
              std::string::npos &&
              dre_edition_markdown.find("Service Level Reporter") !=
                  std::string::npos,
          "all-E banner gate changed DREICMST ordinary CFONT prose");
  require(dre_edition.topic_markdown("2.1").find(
              "Tables Used for Accounting.") != std::string::npos,
          "menu IR removed a punctuation token with its own row origin");
  const auto fa1 = geist::BooDocument::open(root / "FA1PLMM0.boo");
  const auto dynamic_classes = fa1.topic_markdown("6.4.1");
  require(dynamic_classes.find(
              "CLASS  ALLOC   SIZE   SP-GETV LUBS PROFILE  MAX-P   ENABLED") !=
              std::string::npos &&
              dynamic_classes.find("CLASS=    Z         5") !=
                  std::string::npos,
          "qualified all-E gate changed FA1PLMM0 dynamic-class display");
  const auto dfhpep = fa1.topic_markdown("15.4");
  require(dfhpep.find("* MODULE NAME = DFHPEP") != std::string::npos &&
              dfhpep.find("DFHPC TYPE=XCTL,PROGRAM=IESOPDC") !=
                  std::string::npos,
          "qualified all-E gate changed FA1PLMM0 DFHPEP listing");
  const auto destination_table = fa1.topic_markdown("H.2");
  // H.2 renders through the typed prose route; the composed figure span
  // opens its preformatted block with a bare fence.
  require(destination_table.find("```") != std::string::npos &&
              destination_table.find("DFHDCT TYPE=INITIAL,SUFFIX=SP") !=
                  std::string::npos,
          "qualified all-E gate changed FA1PLMM0 appendix listing");
}
