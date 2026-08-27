#include "geist/document.hpp"
#include "test_failures.hpp"

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
  geist_test::record_failure();
  return;
}

std::string topic_markdown(const geist::BooDocument& document,
                           const std::string& id) {
  return document.topic_markdown(id);
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
  require_contains(online,
                   "- Type `GO` `CMDxxx`",
                   "top-level online command list");
  require_contains(online,
                   "  - `xxx` may also be the *noun* part",
                   "nested online command list");

  const auto cover = topic_markdown(document, "COVER");
  require_contains(cover,
                   "Document Number SX41-8209-00\n\nProgram Number 5738-SS1",
                   "cover metadata line split");

  const auto edition = topic_markdown(document, "EDITION");
  require_contains(edition,
                   "**First Edition (May 1991)**",
                   "edition notice heading");
  require_contains(edition,
                   "RPG/400<br>\n400",
                   "edition trademark list line split");
  require_contains(edition,
                   "**Copyright International Business Machines Corporation "
                   "1991. All rights reserved.**",
                   "edition copyright line");

  const auto command_index = topic_markdown(document, "2.0");
  require_contains(command_index,
                   "System/36 procedures, control commands, and OCL "
                   "statements are listed<br>\n"
                   "alphabetically, with cross-references to AS/400* "
                   "commands, beginning on<br>\n"
                   "the following pages:",
                   "2.0 reflow-off intro lines");
  require_contains(command_index,
                   "System/36 procedures     Page [2.1](#SPTPROC)\n"
                   "System/36 control commands Page [2.2](#SPTCONTROL)\n"
                   "System/36 OCL statements Page [2.3](#SPTOCL)",
                   "page reference block");

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

  const auto appendix = topic_markdown(document, "A.0");
  require_contains(appendix,
                   "# A.0 Appendix.  AS/400 Control Language Commands",
                   "appendix heading");
  require_contains(appendix,
                   "Following is a complete list of the AS/400 control "
                   "language (CL) commands",
                   "appendix prose");
  if (appendix.find("# Appendix. AS/400 Control Language Commands Following") !=
      std::string::npos) {
    std::cerr << "duplicate appendix heading remains\n";
    return 1;
  }
}
