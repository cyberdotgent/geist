#include "geist/document.hpp"

#include <filesystem>
#include <iostream>
#include <string>

int main() {
  const auto path = std::filesystem::path(GEIST_REPO_ROOT) /
                    "BOO" / "SC31-605.boo";
  const auto document = geist::BooDocument::open(path);
  const auto alert_table = document.topic_markdown("2.1");
  for (const auto* expected : {
           "TRANSFER MICROCODE DUMP",
           "MACHINE CHECK:STORE CONTROLLER",
           "USER APPLICATION GENERATED",
       }) {
    if (alert_table.find(expected) == std::string::npos) {
      std::cerr << "missing SC31-605 table cell: " << expected << "\n";
      return 1;
    }
  }

  struct Representative {
    const char* topic;
    const char* text;
    std::size_t minimum_bytes;
  };
  for (const auto& representative : {
           Representative{"1.1", "Block ID Index", 1000},
           Representative{"2.26", "OPERATOR GENERATED ALERT", 500},
           Representative{"3.1", "Event Code Index", 1000},
           Representative{"PREFACE.4", "Where to Find More Information", 1000},
           Representative{"FRONT_1.2", "Trademarks", 300},
       }) {
    const auto markdown = document.topic_markdown(representative.topic);
    if (markdown.size() < representative.minimum_bytes ||
        markdown.find(representative.text) == std::string::npos) {
      std::cerr << "SC31-605 representative topic remains empty: "
                << representative.topic << "\n";
      return 1;
    }
  }
  return 0;
}
