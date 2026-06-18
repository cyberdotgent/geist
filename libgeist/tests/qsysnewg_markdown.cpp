#include "geist/document.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require_contains(const std::string& haystack,
                      const std::string& needle,
                      const char* label) {
  if (haystack.find(needle) != std::string::npos) {
    return;
  }
  std::cerr << "missing " << label << ": " << needle << "\n";
  std::exit(1);
}

void require_not_contains(const std::string& haystack,
                          const std::string& needle,
                          const char* label) {
  if (haystack.find(needle) == std::string::npos) {
    return;
  }
  std::cerr << "unexpected " << label << ": " << needle << "\n";
  std::exit(1);
}

std::string topic_markdown(const geist::BooDocument& document,
                           const std::string& id) {
  const auto* entry = document.find_toc_entry(id);
  if (entry == nullptr) {
    std::cerr << "missing topic " << id << "\n";
    std::exit(1);
  }
  return entry->markdown();
}

} // namespace

int main() {
  const auto book =
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "QSYSNEWG.BOO";
  const auto document = geist::BooDocument::open(book);

  const auto intro = topic_markdown(document, "1.0");
  require_contains(intro, "**In** **a** **Hurry?**", "visual box heading");
  require_contains(intro,
                   "**Official** **Introductory** **Chapter**",
                   "visual box closing heading");
  require_contains(intro,
                   "This chapter contains background information about "
                   "computers and computer terminology",
                   "visual box body text");
  require_not_contains(intro,
                       "**Hu**r**r**y",
                       "torn Hurry emphasis");
  require_not_contains(intro,
                       "Of**ficial I**n**troductory C**h**apter**",
                       "torn Official Introductory Chapter emphasis");
}
