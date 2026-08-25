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
           "| Action Code | Event Type | Event or Alert Text |",
           "TRANSFER MICROCODE DUMP",
           "MACHINE CHECK:STORE CONTROLLER",
           "USER APPLICATION GENERATED",
       }) {
    if (alert_table.find(expected) == std::string::npos) {
      std::cerr << "missing SC31-605 table cell: " << expected << "\n";
      return 1;
    }
  }
  for (const auto* expected : {
           "| 05 | 1 | MACHINE CHECK:STORE CONTROLLER |",
           "| 0F | 1 | DATA LOST:STORE CONTROLLER |",
           "| 19 | 1 | TICKET READ FILE FULL:USER |",
           "| 23 | 1 | INTERVENTION REQUIRED:PRINTER |",
           "| 63 | 5 | USER APPLICATION GENERATED |",
           "| 6F | 5 | USER APPLICATION GENERATED |",
           "| 7B | 5 | USER APPLICATION GENERATED |",
       }) {
    if (alert_table.find(expected) == std::string::npos) {
      std::cerr << "misaligned SC31-605 boundary row: " << expected << "\n";
      return 1;
    }
  }

  const auto event_3650 = document.topic_markdown("3.2");
  const auto event_series1 = document.topic_markdown("3.5");
  const auto event_3725 = document.topic_markdown("3.8");
  const auto event_3647 = document.topic_markdown("3.3");
  for (const auto* expected : {
           "| Event Code | Qualifier 1 | Qualifier 2 | Qualifier 3 |",
           "| 00504 | Panel message |  |  |",
           "| 00505 | Panel message |  |  |",
       }) {
    if (event_3650.find(expected) == std::string::npos) {
      std::cerr << "misaligned SC31-605 3650 event row: " << expected << "\n";
      return 1;
    }
  }
  for (const auto* expected : {
           "| 02136 | Node ID,device<br>type | Device address | Log record ID |",
           "| 02142 | Node ID,device<br>type | Device address,<br>malfunction code | Log record ID |",
           "| 0216B | Node ID,device<br>type | Device address,<br>malfunction code | Log record ID |",
       }) {
    if (event_series1.find(expected) == std::string::npos) {
      std::cerr << "misaligned SC31-605 Series/1 event row: " << expected
                << "\n";
      return 1;
    }
  }
  if (event_3725.find("| 02D07 | Abend code |  |  |") ==
          std::string::npos ||
      event_3725.find("| 02D08 | Abend code |  |  |") ==
          std::string::npos ||
      event_3725.find("| 02D0B | Scanner position | Lower range of<br>line "
                      "addresses | Upper range of<br>line addresses |") ==
          std::string::npos) {
    std::cerr << "SC31-605 wrapped qualifier rows lost their columns\n";
    return 1;
  }
  if (event_3647.find(
          "| 01601 | Log record number | Device address | Status code |") ==
      std::string::npos) {
    std::cerr << "SC31-605 sparse event grid lost its semantic columns\n";
    return 1;
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
