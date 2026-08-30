#include "geist/document.hpp"
#include "test_failures.hpp"

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
  geist_test::record_failure();
  return;
}

void require_not_contains(const std::string& haystack,
                          const std::string& needle,
                          const char* label) {
  if (haystack.find(needle) == std::string::npos) {
    return;
  }
  std::cerr << "unexpected " << label << ": " << needle << "\n";
  geist_test::record_failure();
  return;
}

std::vector<std::string> fenced_text_rows(const std::string& markdown) {
  const auto begin = markdown.find("```\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto rows_begin = begin + std::string("```\n").size();
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
  return document.topic_markdown(id);
}

} // namespace

int main() {
  const auto book =
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "QSYSNEWG.BOO";
  const auto document = geist::BooDocument::open(book);

  // Typed route (drawn box region): hosted DT 19910524085706 prints the box
  // verbatim inside its <pre>, so the rows are a preformatted block.  The
  // legacy route dropped the box outline and glued the closing row into the
  // body paragraph; the CFONT bold hosted shows inside the box is the one
  // thing a preformatted block cannot carry.
  const auto intro = topic_markdown(document, "1.0");
  require_contains(intro, " ___ In a Hurry? ____", "visual box top rule");
  require_contains(intro,
                   "| Official Introductory Chapter",
                   "visual box closing row");
  require_contains(intro,
                   "| This chapter contains background information about "
                   "computers and       |",
                   "visual box body row");
  require_not_contains(intro,
                       "**Hu**r**r**y",
                       "torn Hurry emphasis");
  require_not_contains(intro,
                       "Of**ficial I**n**troductory C**h**apter**",
                       "torn Official Introductory Chapter emphasis");

  // Typed route (prose composition): the renderer escapes sentence
  // punctuation; the words are those of hosted DT 19910524085706.
  const auto typical_system = topic_markdown(document, "1.2");
  require_contains(typical_system,
                   "Computers come in many forms and are used for many "
                   "different things\\. Here",
                   "aligned SI visible opening paragraph");
  require_contains(typical_system,
                   "There are many different devices for input, process, "
                   "output, and storage\\.",
                   "aligned SI visible later paragraph");
  require_not_contains(typical_system,
                       "computer, description of",
                       "hidden SI term before opening paragraph");
  require_not_contains(typical_system,
                       "computer, processor",
                       "hidden SI term before later paragraph");

  const auto front_notices = topic_markdown(document, "FRONT_1");
  // FRONT_1 renders through the typed prose family (word for word equal to
  // hosted BookServer DT 19910524085706): the notices flow as paragraphs and
  // the renderer escapes Markdown punctuation in the identity and body.
  require_contains(front_notices,
                   "# FRONT\\_1 Notices",
                   "TOC-derived FRONT_1 heading");
  require_contains(front_notices,
                   "References in this publication to IBM products, "
                   "programs, or services do not imply",
                   "FRONT_1 heading-carried first paragraph");
  require_contains(front_notices,
                   "\n\nReferences in this publication",
                   "FRONT_1 body paragraph");
  require_contains(front_notices,
                   "This publication contains examples of data and reports "
                   "used in daily business operations\\.",
                   "FRONT_1 following logical record");
  require_not_contains(front_notices,
                       "# FRONT_1 Notices References",
                       "FRONT_1 body folded into topic heading");

  const auto preface = topic_markdown(document, "PREFACE");
  require_contains(preface,
                   "# PREFACE About This Guide",
                   "TOC-derived PREFACE heading");
  // PREFACE renders through the typed prose family (front-matter `preface`
  // heading form).  Word for word equal to hosted BookServer DT
  // 19910524085706, which serves the body as `<pre>` rows led by a `|`
  // change bar and `°` list markers; the typed route reflows the rows into
  // paragraphs and list items and escapes Markdown punctuation.
  require_contains(preface,
                   "This guide contains a very basic approach to learning "
                   "about and using the AS/400 system",
                   "PREFACE first body block");
  require_contains(preface,
                   "- Sign on or off the AS/400 system from a display "
                   "station\\.",
                   "PREFACE task list line");
  require_contains(preface, "- Use online help\\.",
                   "PREFACE visual bullet glyph");
  require_contains(preface,
                   "- Send and receive messages and work with message "
                   "queues\\.",
                   "PREFACE continued visual bullet mode");
  require_not_contains(preface,
                       "- This manual is similar",
                       "PREFACE bullet mode stops before next paragraph");
  require_contains(preface,
                   "about a particular topic\\. The *Publications Guide*",
                   "PREFACE font continuation");
  require_contains(preface,
                   "see the [\"Bibliography\" in](<#HDRBIBL>) "
                   "[topic BIBLIOGRAPHY](<#HDRBIBL>)",
                   "PREFACE visual CSELECT links");
  require_not_contains(preface, "ST|", "leaked PREFACE ST control");
  require_not_contains(preface, "# About This Guide", "duplicate PREFACE heading");
  require_not_contains(preface, " | ", "stray PREFACE visual marker");
  require_not_contains(preface,
                       "see th [e \"Bibliography\" in]",
                       "PREFACE link starts one character late");
  require_not_contains(preface,
                       "[topic BIBLIOGRAP](#HDRBIBL) HY",
                       "PREFACE link ends too early");

  const auto sign_on = topic_markdown(document, "2.0");
  require_contains(sign_on,
                   "Before you can use the AS/400 system you must sign on",
                   "SREFIG trailing paragraph");
  require_contains(sign_on,
                   "unique **user name**",
                   "bar-row user name emphasis");
  require_contains(sign_on,
                   "secret **password**",
                   "bar-row password emphasis");
  require_contains(sign_on, "**If\\.\\.\\.**", "If branch emphasis");
  require_contains(sign_on, "**Then\\.\\.\\.**", "Then branch emphasis");
  require_contains(sign_on,
                   "*Security Concepts and Planning* manual and the "
                   "*Operator's Guide*\\.",
                   "manual title emphasis");
  require_not_contains(sign_on, "uniqu**e us**", "torn user emphasis");
  require_not_contains(sign_on, "Secu*rity", "torn Security emphasis");
  require_not_contains(sign_on, " | ", "stray fixed-row marker");

  const auto lets_go = topic_markdown(document, "2.1");
  require_contains(lets_go,
                   "If your display station screen is blank",
                   "SI pipe-visible continuation");
  require_contains(lets_go,
                   "the **Sign On** display comes on\\.",
                   "Sign On emphasis");
  require_contains(lets_go,
                   "the **User** line and the **Password** line\\.",
                   "User and Password line emphasis");
  require_contains(lets_go, "```\n", "text figure fence");
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
                   "of the display\\.",
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

  // Typed route, verbatim: the region keeps the source's double space in the
  // caption and the five hosted columns exactly as drawn (the legacy renderer
  // shifted the `Prompt` row by one column).  Hosted (DT 19910524085706)
  // serves the whole envelope inside `<pre>` and emits no `<table>`.
  const auto function_keys = topic_markdown(document, "F.1");
  require_contains(function_keys,
                   "   | Figure  F-1. Function Key Differences",
                   "function-key table title");
  require_contains(function_keys,
                   "   | Prompt     | F4         | Help       | CF4        | "
                   "Requests command   |",
                   "function-key table row");
  require_not_contains(function_keys,
                       "```text",
                       "table figure misclassified as fixed screen");
  require_not_contains(function_keys,
                       "|Prompt|",
                       "table cells spilled into fixed text");

  // 8.6 reaches the typed route since the change-bar margin fix, so the
  // renderer escapes sentence punctuation; the words are hosted's
  // (DT 19910524085706).
  const auto print_display = topic_markdown(document, "8.6");
  // 8.6 now renders through the typed prose family, which escapes Markdown
  // punctuation; the paragraph itself is unchanged and matches hosted DT
  // 19910524085706 word for word.
  require_contains(print_display,
                   "You can use the Print key to print any display you see "
                   "on your screen\\.",
                   "aligned SI visible print paragraph");
  require_not_contains(print_display,
                       "print display You can use",
                       "hidden SI print-display term");
}
