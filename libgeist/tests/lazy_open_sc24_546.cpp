#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";

  const auto rexx_tokens =
      geist::BooDocument::open(root / "SC24-546.boo")
          .topic_markdown("2.1.3");
  require(rexx_tokens.find("sequence including *any* characters") !=
              std::string::npos &&
              rexx_tokens.find("charac*ter*") == std::string::npos &&
              rexx_tokens.find("t`w`o") == std::string::npos,
          "REXX prose retained partial-word CFONT spans");
}
