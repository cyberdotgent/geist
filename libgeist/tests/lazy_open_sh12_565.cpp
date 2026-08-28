#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";

  const auto smf_layout =
      geist::BooDocument::open(root / "SH12-565.boo")
          .topic_markdown("APPENDIX1.8");
  require(smf_layout.find("number of *triplets*\\. Each triplet") !=
              std::string::npos &&
              smf_layout.find("**2** Delete **3** Query") !=
                  std::string::npos,
          "SMF fixed rows retained shifted CFONT spans");
  require(smf_layout.find("trip*lets") == std::string::npos &&
              smf_layout.find("Del**e**te") == std::string::npos,
          "SMF fixed rows retained torn words");
}
