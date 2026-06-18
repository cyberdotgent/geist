#include "geist/document.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

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

std::vector<std::string> fenced_text_rows(const std::string& markdown) {
  const auto begin = markdown.find("```text\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto rows_begin = begin + std::string("```text\n").size();
  const auto end = markdown.find("```", rows_begin);
  if (end == std::string::npos) {
    return {};
  }
  std::vector<std::string> rows;
  std::size_t cursor = rows_begin;
  while (cursor < end) {
    const auto newline = markdown.find('\n', cursor);
    const auto row_end = newline == std::string::npos || newline > end
                             ? end
                             : newline;
    rows.push_back(markdown.substr(cursor, row_end - cursor));
    if (newline == std::string::npos || newline >= end) {
      break;
    }
    cursor = newline + 1;
  }
  return rows;
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
  require_not_contains(sign_on, " | ", "stray fixed-row marker");

  const auto lets_go = topic_markdown(document, "2.1");
  require_contains(lets_go,
                   "If your display station screen is blank",
                   "SI pipe-visible continuation");
  require_contains(lets_go,
                   "the **Sign** **On** display comes on.",
                   "Sign On emphasis");
  require_contains(lets_go, "```text\n", "text figure fence");
  require_contains(lets_go,
                   "    ________________________________",
                   "text figure top border");
  require_contains(lets_go,
                   "   |                                    Sign On"
                   "                                       |",
                   "text figure heading row");
  require_contains(lets_go,
                   "   |                                                  "
                   "                                |",
                   "text figure blank row");
  require_contains(lets_go,
                   "   |                                                System  . . . . . :"
                   "   XXXXXXXX    |",
                   "text figure system row");
  require_contains(lets_go,
                   "   |                 Current library . . . . . . . . ."
                   "   __________                   |",
                   "text figure current library row");
  require_contains(lets_go,
                   " | |                                         (C) COPYRIGHT IBM CORP."
                   " 1980, 1991.      |",
                   "text figure copyright row");
  require_contains(lets_go,
                   "   |_______________________________",
                   "text figure bottom border");
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

  const auto screen_rows = fenced_text_rows(lets_go);
  if (screen_rows.size() != 28) {
    std::cerr << "unexpected Sign On screen row count: "
              << screen_rows.size() << "\n";
    return 1;
  }
  for (const auto& row : screen_rows) {
    if (row.size() != 87) {
      std::cerr << "unexpected Sign On screen row width: " << row.size()
                << " row=" << row << "\n";
      return 1;
    }
  }

  const auto function_keys = topic_markdown(document, "F.1");
  require_contains(function_keys,
                   "Figure F-1. Function Key Differences",
                   "function-key table title");
  require_contains(function_keys,
                   "| Help | CF4 | Requests command<br>name or parameter"
                   "<br>value assistance | Refresh |",
                   "function-key table row");
  require_not_contains(function_keys,
                       "```text",
                       "table figure misclassified as fixed screen");
  require_not_contains(function_keys,
                       "|Prompt|",
                       "table cells spilled into fixed text");
}
