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

  const auto sign_on = topic_markdown(document, "2.0");
  require_contains(sign_on,
                   "Before you can use the AS/400 system you must sign on",
                   "SREFIG trailing paragraph");
  require_contains(sign_on,
                   "unique **user** **name**",
                   "bar-row user name emphasis");
  require_contains(sign_on,
                   "secret **password**",
                   "bar-row password emphasis");
  require_contains(sign_on, "**If...**", "If branch emphasis");
  require_contains(sign_on, "**Then...**", "Then branch emphasis");
  require_contains(sign_on,
                   "*Security* *Concepts* *and* *Planning* manual and the "
                   "*Operator's* *Guide*.",
                   "manual title emphasis");
  require_not_contains(sign_on, "uniqu**e us**", "torn user emphasis");
  require_not_contains(sign_on, "Secu*rity", "torn Security emphasis");

  const auto lets_go = topic_markdown(document, "2.1");
  require_contains(lets_go,
                   "If your display station screen is blank",
                   "SI pipe-visible continuation");
  require_contains(lets_go,
                   "the **Sign** **On** display comes on.",
                   "Sign On emphasis");
  require_contains(lets_go, "```text\n", "text figure fence");
  require_contains(lets_go, "Sign On", "text figure heading");
  require_contains(lets_go,
                   "System  . . . . . :   XXXXXXXX",
                   "text figure system row");
  require_contains(lets_go,
                   "Current library . . . . . . . . .   __________",
                   "text figure current library row");
  require_contains(lets_go,
                   "(C) COPYRIGHT IBM CORP. 1980, 1991.",
                   "text figure copyright row");
  require_contains(lets_go,
                   "You can ignore the information in the upper right corner "
                   "of the display.",
                   "SREFIG trailing text");
  require_contains(lets_go,
                   "**Note:** If the word **password** does not appear",
                   "fixed-row note emphasis");
  require_not_contains(lets_go,
                       "FIGUNIQ13 Sign On System",
                       "text figure folded into anchor id");
  require_not_contains(lets_go, "th**e Si**", "torn Sign emphasis");
}
