#include "geist/document.hpp"
#include "test_failures.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "menu_production: " << message << '\n';
    geist_test::record_failure();
    return;
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
  // Hosted BookServer (2026-08-27) renders every one of these menus as a
  // `Subtopics:` line followed by `<topic id> <title>` links, e.g. FA1PLMM0
  // 5.6 (DT=19910927114801): `Subtopics:` / `5.6.1 Functions Supported by
  // the VM/VSE Interface`; SC33-033 5.3 (DT=19930422134757); SC34-425
  // 1.8.5.5 / 1.8.15.5 / 1.8.18.5 (DT=19921112160049); SH12-565
  // APPENDIX1.9.5 (DT=19941206115523).
  const std::vector<Fixture> fixtures{
      {"FA1PLMM0.boo", "5.6", 1,
       "- [5\\.6\\.1 Functions Supported by the VM/VSE Interface](<#5.6.1>)",
       ""},
      {"SC33-033.boo", "5.3", 4,
       "- [5\\.3\\.1 CICS/VS](<#5.3.1>)\n"
       "- [5\\.3\\.2 IMS/VS](<#5.3.2>)\n"
       "- [5\\.3\\.3 OS/TSO](<#5.3.3>)\n"
       "- [5\\.3\\.4 VM/SP CMS](<#5.3.4>)",
       "\n\nThe PGF file names, file types, and record types used "},
      {"SC34-425.boo", "1.8.15.5", 1,
       "- [1\\.8\\.15\\.5\\.1 Command Invocation](<#1.8.15.5.1>)",
       "\n\nThis example calls the MIGRATE service\\.\n\nSubtopics:"},
      {"SC34-425.boo", "1.8.18.5", 1,
       "- [1\\.8\\.18\\.5\\.1 Command Invocation](<#1.8.18.5.1>)",
       "\n\nThis example calls the RPTARCH service\\.\n\nSubtopics:"},
      {"SC34-425.boo", "1.8.5.5", 1,
       "- [1\\.8\\.5\\.5\\.1 Command Invocation](<#1.8.5.5.1>)",
       "\n\nThis example calls the DBUTIL service\\.\n\nSubtopics:"},
      {"SH12-565.boo", "APPENDIX1.9.5", 3,
       "- [APPENDIX1\\.9\\.5\\.3 Events Issued by the SNA Server Only]"
       "(<#APPENDIX1.9.5.3>)",
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
    require(count(markdown, "\n\nSubtopics:\n\n- [") == 1,
            std::string(fixture.book) + ':' + fixture.topic +
                " did not render exactly one BookServer Subtopics lead line");
    require(markdown.find("](<" + std::string(fixture.topic) + ".") ==
                    std::string::npos &&
                markdown.find("- [" + std::string(fixture.topic) + "\\.") ==
                    std::string::npos,
            std::string(fixture.book) + ':' + fixture.topic +
                " rendered a raw typed topic destination or a self-item");
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

  // SC31-711 2.4 is declined by the menu family (its catalog header title
  // is the ST-glued compatibility projection) and renders through the typed
  // prose family, whose trailing menu validates the labels against the TOC
  // titles; the output equals hosted BookServer word for word.
  const auto legacy = geist::BooDocument::open(root / "SC31-711.boo");
  const auto nonadmitted = legacy.topic_markdown("2.4");
  require(nonadmitted.find("Subtopics:\n\n- [2\\.4\\.1 Customer "
                           "Information](<#2.4.1>)") != std::string::npos,
          "prose topic lost its catalog-validated trailing menu");
#endif
}
