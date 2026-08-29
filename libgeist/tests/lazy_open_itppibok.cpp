#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";

  const auto tpns = geist::BooDocument::open(root / "ITPPIBOK.BOO");
  const auto tpns_edition = tpns.topic_markdown("EDITION");
  require(tpns_edition.find(
              "Teleprocessing Network Simulator Version 3 Relelease 2") !=
                  std::string::npos &&
              tpns_edition.find("North Carolina 27709, U\\.S\\.A\\.") !=
                  std::string::npos &&
              tpns_edition.find("c.cp") == std::string::npos,
          "ITPPIBOK edition changed while projecting visible CCP prose");
  // PREFACE.2 renders through the typed prose family (word for word equal to
  // hosted BookServer DT 19910628074854); the typed renderer spells anchor
  // destinations as `<#id>` and escapes Markdown punctuation.
  const auto tpns_how_to = tpns.topic_markdown("PREFACE.2");
  const auto experienced = tpns_how_to.find("are an experienced TPNS user");
  require(experienced != std::string::npos &&
              tpns_how_to.substr(0, experienced).find("<BOOK>") ==
                  std::string::npos &&
              tpns_how_to.find("[Appendix A,](<#HDRMIG>)", experienced) !=
                  std::string::npos &&
              tpns_how_to.find(
                  "[\"Migrating from Previous Versions of TPNS\"](<#HDRMIG>)",
                  experienced) != std::string::npos &&
              tpns_how_to.find("Inst alling") == std::string::npos,
          "typed prose shifted or tore TPNS selector prose");
}
