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
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "QS3X36CM.BOO";
  const auto document = geist::BooDocument::open(book);

  const auto intro = topic_markdown(document, "1.0");
  require_contains(intro,
                   "[Appendix, \"AS/400 Control Language Commands\" in topic "
                   "A.0](#HDRAPA). Detailed information",
                   "column-selected Appendix link");
  require_contains(intro, "the *CL* *Reference*.", "HP1 CL Reference spans");

  const auto online = topic_markdown(document, "1.1");
  require_contains(online, "*verb* part of the command", "verb emphasis");
  require_contains(online, "*noun* part of the command", "noun emphasis");
  require_contains(online, "`CMDxxx`", "XPH command span");

  const auto table = topic_markdown(document, "2.1");
  require_contains(table,
                   "| #STRTUP1 | ADDAJE<br>WRKSBSD | Adds an autostart job "
                   "entry to an<br>existing subsystem description<br>Allows "
                   "you to work with a list of<br>subsystem descriptions |",
                   "first procedure table row");
  require_contains(table,
                   "| ALERT | CHGMSGD | Changes an existing message<br>"
                   "description stored in a specified<br>message file |",
                   "wrapped table description");

  const auto contents = topic_markdown(document, "CONTENTS");
  require_contains(contents, "Summarize", "generated contents summary marker");
}
