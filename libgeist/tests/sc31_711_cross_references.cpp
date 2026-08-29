#include "geist/document.hpp"
#include "test_failures.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << message << "\n";
    geist_test::record_failure();
    return;
  }
}

std::size_t count(const std::string& text, const std::string& needle) {
  std::size_t result = 0;
  for (auto at = text.find(needle); at != std::string::npos;
       at = text.find(needle, at + needle.size())) {
    ++result;
  }
  return result;
}

void require_absent(const std::string& text, const std::string& needle,
                    const char* topic) {
  require(text.find(needle) == std::string::npos,
          std::string(topic) + " retained torn selector text: " + needle);
}

} // namespace

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  const auto sc31 = geist::BooDocument::open(root / "SC31-711.boo");

  const auto chapter_two = sc31.topic_markdown("2.0");
  // 2.0, 2.2, 2.3.2-2.3.4 render through the typed prose family: anchor
  // destinations are `<#id>` and the renderer escapes label punctuation;
  // the split same-target selectors remain separate adjacent links.
  require(count(chapter_two, "](<#HDRHOWTSL>)") == 3,
          "2.0 lost the hosted two-row and list HOWTSL selector ownership");
  require(chapter_two.find(
              "[\"Gathering Problem](<#HDRHOWTSL>) "
              "[Information\" in topic 2\\.1](<#HDRHOWTSL>)") !=
              std::string::npos,
          "2.0 merged or tore the hosted two-anchor Gathering reference");
  require(count(chapter_two, "](<#HDRPRBWRKS>)") == 2,
          "2.0 changed the hosted two-anchor worksheet reference");
  require(chapter_two.find(
              "[\"Problem](<#HDRPRBWRKS>) "
              "[Documentation Worksheet\" in topic 2\\.4](<#HDRPRBWRKS>)") !=
              std::string::npos,
          "2.0 worksheet selector rows are not adjacent and complete");

  const auto general = sc31.topic_markdown("2.2");
  require(count(general, "](<#HDRAPPSPRB>)") == 2 &&
              general.find(
                  "[\"LNM for AIX Application](<#HDRAPPSPRB>) "
                  "[Problems\" in topic 2\\.3](<#HDRAPPSPRB>)") !=
                  std::string::npos,
          "2.2 changed the hosted two-anchor application reference");

  // 2.3.1 moved to the typed prose route (issue #58); anchors keep the
  // typed destination spelling and Markdown escaping, with the same
  // one-anchor and two-row references hosted serves.
  const auto os2 = sc31.topic_markdown("2.3.1");
  require(count(os2, "](<#HDRPRNETL>)") == 1 &&
              os2.find(
                  "[\"Checking the nettl Log\" in topic 2\\.1\\.2]"
                  "(<#HDRPRNETL>)") != std::string::npos,
          "2.3.1 Checking-nettl reference is not one complete anchor");
  require(count(os2, "](<#HDRLMATRP>)") == 2 &&
              os2.find("[\"LNM](<#HDRLMATRP>) ") != std::string::npos &&
              os2.find(
                  "[OS/2 Agent Application Traps\" in topic 4\\.1]"
                  "(<#HDRLMATRP>)") != std::string::npos,
          "2.3.1 changed the hosted two-row LNM trap reference");
  require(os2.find(
              "[\"Using the Tracing Command\" in topic 2\\.1\\.3]"
              "(<#HDRKILL30>)") != std::string::npos,
          "2.3.1 tracing reference is not one complete anchor");

  for (const auto* topic : {"2.3.2", "2.3.3"}) {
    const auto markdown = sc31.topic_markdown(topic);
    require(count(markdown, "](<#HDRKILL30>)") == 2 &&
                markdown.find("[\"Using the Tracing](<#HDRKILL30>) ") !=
                    std::string::npos &&
                markdown.find(
                    "[Command\" in topic 2\\.1\\.3](<#HDRKILL30>)") !=
                    std::string::npos,
            std::string(topic) +
                " changed the hosted two-anchor tracing reference");
  }

  const auto fddi = sc31.topic_markdown("2.3.4");
  require(fddi.find(
              "[\"Gathering Problem Information\" in topic 2\\.1]"
              "(<#HDRHOWTSL>)") != std::string::npos &&
              fddi.find(
                  "[\"Checking the nettl Log\" in topic 2\\.1\\.2]"
                  "(<#HDRPRNETL>)") != std::string::npos,
          "2.3.4 clean sibling selectors regressed");

  // 2.4.9 renders through the typed prose family; hosted DT 19941010174546
  // serves both references whole on one display row, so each is one anchor.
  const auto additional = sc31.topic_markdown("2.4.9");
  require(count(additional, "](<#HDRPRLNMS>)") == 1 &&
              additional.find(
                  "[\"Displaying LNM for AIX Status Information\" in topic "
                  "2\\.1\\.1](<#HDRPRLNMS>)") != std::string::npos,
          "2.4.9 status reference is not one complete anchor");
  require(count(additional, "](<#HDRLNMKLOG>)") == 1 &&
              additional.find(
                  "[\"Logging with LNM for AIX\" in topic 3\\.1]"
                  "(<#HDRLNMKLOG>)") != std::string::npos,
          "2.4.9 logging reference is not one complete anchor");

  const auto messages = sc31.topic_markdown("5.0");
  require(count(messages, "](<#HDRPROBS>)") == 2 &&
              messages.find("[Chapter 2, \"Problem](<#HDRPROBS>) ") !=
                  std::string::npos &&
              messages.find(
                  "[Determination\" in topic 2\\.0](<#HDRPROBS>)") !=
                  std::string::npos,
          "5.0 changed the hosted two-anchor Problem Determination reference");

  for (const auto& [topic, markdown] : {
           std::pair{"2.0", chapter_two}, std::pair{"2.3.1", os2},
           std::pair{"2.3.2", sc31.topic_markdown("2.3.2")},
           std::pair{"2.3.3", sc31.topic_markdown("2.3.3")},
           std::pair{"2.4.9", additional}, std::pair{"5.0", messages}}) {
    for (const auto* tear : {"LNM [for", "C [hapter", "Ag [ent", "Command [",
                             "for in](#", "Dis [playing", "Loggin [g",
                             "for mo](#"}) {
      require_absent(markdown, tear, topic);
    }
  }

  // Cross-book guards: target equality and row adjacency do not imply one
  // selector, and ordinary footnote selectors remain singular.
  const auto gg24 = geist::BooDocument::open(root / "GG24-4302-00.boo")
                        .topic_markdown("NOTICES");
  // GG24 NOTICES is a drawn `___ Take Note! ___` box, so its rows lower as a
  // preformatted block that reproduces hosted (DT 19950308184737) character
  // for character; a drawn box carries no inline markup, so the two-row
  // `CSELECT` is dropped there exactly as `CFONT` is.
  require(gg24.find("___ Take Note! ___") != std::string::npos &&
              gg24.find("| to read the general information under \"Special "
                        "Notices\" in") != std::string::npos &&
              gg24.find("| topic FRONT_1.") != std::string::npos &&
              count(gg24, "](#HDRNOTICES)") == 0,
          "GG24 two-row notices reference changed shape");
  const auto packet = geist::BooDocument::open(root / "packet.boo")
                          .topic_markdown("1.1");
  // packet 1.1 now renders through the typed CZ prose family; hosted
  // (DT 20260614112503) shows one `<a href="...#FTNFTNUNIQ1"> (1)</a>`.
  require(count(packet, "](<#FTNFTNUNIQ1>)") == 1 &&
              packet.find("[\\(1\\)](<#FTNFTNUNIQ1>)") != std::string::npos,
          "packet footnote selector changed multiplicity");
  const auto figures = geist::BooDocument::open(root / "IEAC6MST.BOO")
                           .topic_markdown("FIGURES");
  // Typed generated-list form from 9def3e3; IEAC6MST FIGURES only fell back
  // to the legacy route while non-numeric topic-start controls (SHFIGURES)
  // were misclassified as prose.
  require(count(figures, "](<#FIGALLO>)") == 2 &&
              figures.find("[2\\-2\\.  Sample CLIST to Add IPCS Libraries to "
                           "Data Set Concatenations and](<#FIGALLO>)") !=
                  std::string::npos &&
              figures.find("[Access IPCS   2\\.6](<#FIGALLO>)") !=
                  std::string::npos,
          "IEAC6MST adjacent typed same-target figure selectors were merged");
}
