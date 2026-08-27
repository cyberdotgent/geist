#include "geist/document.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "menu_production: " << message << '\n';
    std::exit(1);
  }
}

std::size_t count(const std::string &text, const std::string &needle) {
  auto result = std::size_t{};
  auto offset = std::size_t{};
  while ((offset = text.find(needle, offset)) != std::string::npos) {
    ++result;
    offset += needle.size();
  }
  return result;
}

struct Fixture {
  const char *book;
  const char *topic;
  std::size_t items;
  const char *required_link;
  const char *required_intro;
};

} // namespace

int main() {
#ifdef GEIST_REPO_ROOT
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  const std::vector<Fixture> fixtures{
      {"FA1PLMM0.boo", "5.6", 1,
       "[Functions Supported by the VM/VSE Interface](<5.6.1>)", ""},
      {"SC33-033.boo", "5.3", 4, "[CICS/VS](<5.3.1>)",
       "\n\nThe PGF file names, file types, and record types used "},
      {"SC34-425.boo", "1.8.15.5", 1, "](<", ""},
      {"SC34-425.boo", "1.8.18.5", 1, "](<", ""},
      {"SC34-425.boo", "1.8.5.5", 1, "](<", ""},
      {"SH12-565.boo", "APPENDIX1.9.5", 3,
       "[Events Issued by the SNA Server Only](<APPENDIX1.9.5.3>)",
       "\n\nThe following describes the events issued by NetView FTP "},
  };
  for (const auto &fixture : fixtures) {
    const auto document = geist::BooDocument::open(root / fixture.book);
    const auto *entry = document.find_toc_entry(fixture.topic);
    std::vector<std::string> gml_before;
    if (entry != nullptr)
      gml_before = entry->gml_records();
    const auto markdown = document.topic_markdown(fixture.topic);
    const auto again = document.topic_markdown(fixture.topic);
    require(markdown == again,
            std::string(fixture.book) + ':' + fixture.topic +
                " repeated Markdown rendering changed");
    require(count(markdown, "](<") == fixture.items,
            std::string(fixture.book) + ':' + fixture.topic +
                " did not render the exact typed menu item count");
    require(markdown.find(fixture.required_link) != std::string::npos,
            std::string(fixture.book) + ':' + fixture.topic +
                " lost its catalog-validated link target or label");
    require(markdown.find(fixture.required_intro) != std::string::npos,
            std::string(fixture.book) + ':' + fixture.topic +
                " lost its independent source-proven introduction");
    if (entry != nullptr)
      require(entry->gml_records() == gml_before && !gml_before.empty(),
              std::string(fixture.book) + ':' + fixture.topic +
                  " typed rendering changed public GML");
  }

  const auto legacy = geist::BooDocument::open(root / "SC31-711.boo");
  const auto nonadmitted = legacy.topic_markdown("2.4");
  require(nonadmitted.find("[2.4.1 Customer Information](#2.4.1)") !=
              std::string::npos &&
              nonadmitted.find("](<2.4.1>)") == std::string::npos,
          "catalog-rejected menu spilled into the typed production route");
#endif
}
