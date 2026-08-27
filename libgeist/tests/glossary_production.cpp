#include "geist/document.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "glossary_production: " << message << '\n';
    std::exit(1);
  }
}

std::size_t count(const std::string& text, const std::string& needle) {
  auto result = std::size_t{};
  auto offset = std::size_t{};
  while ((offset = text.find(needle, offset)) != std::string::npos) {
    ++result;
    offset += needle.size();
  }
  return result;
}

} // namespace

int main() {
#ifdef GEIST_REPO_ROOT
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  const auto document = geist::BooDocument::open(root / "SC31-711.boo");
  const auto* entry = document.find_toc_entry("GLOSSARY");
  require(entry != nullptr, "glossary is not a TOC topic");
  const auto gml_before = entry->gml_records();

  const auto markdown = document.topic_markdown("GLOSSARY");
  const auto repeated = document.topic_markdown("GLOSSARY");
  require(markdown == repeated, "repeated typed rendering changed");
  require(entry->gml_records() == gml_before && !gml_before.empty(),
          "typed rendering changed public GML");

  require(count(markdown, "\n# ") == 0 &&
              count(markdown, "# GLOSSARY Glossary") == 1,
          "glossary did not render one canonical level-one heading");
  require(count(markdown, "\n## ") == 21,
          "glossary alphabet section inventory changed");
  require(count(markdown, "<a id=\"GLS ") == 281 &&
              count(markdown, "\n- **") == 281,
          "glossary term-anchor or semantic-list inventory changed");

  for (const auto* expected : {
           "<a id=\"GLS accelerator\"></a>",
           "- **accelerator:** \\(1\\) In the AIXwindows "
           "program",
           "<a id=\"GLS data link connection identifier (DLCI)\"></a>",
           "| DLCI Values | Function |",
           "| 1\\-15 | reserved |",
           "| 1023 | in\\-channel layer management |",
           "<a id=\"GLS wildcard character\"></a>",
           "- **wildcard character:** Synonym for "
           "pattern\\-matching character\\.",
           "<a id=\"GLS X.25 interface\"></a>",
           "- **X\\.25 interface:** An interface "
           "consisting of a data terminal equipment",
           "program can use the AIX NetView Service Point program to "
           "communicate with the NetView and NETCENTER programs",
       }) {
    require(markdown.find(expected) != std::string::npos,
            std::string("glossary lost canonical content: ") + expected);
  }

  require(markdown.find("???????????") == std::string::npos &&
              markdown.find("keys on the: keyboard") == std::string::npos &&
              markdown.find("speed and: greater") == std::string::npos &&
              markdown.find("operating system\\.: The") ==
                  std::string::npos &&
              markdown.find("Recommendation X\\.25\\.\\.") ==
                  std::string::npos &&
              markdown.find("<pre>") == std::string::npos &&
              markdown.find("Subtopics:") == std::string::npos,
          "layout padding or legacy fallback leaked into typed Markdown");

  const auto terminal = markdown.find("<a id=\"GLS X.25 interface\"></a>");
  require(terminal != std::string::npos &&
              markdown.find("procedures described in the CCITT "
                            "Recommendation X\\.25\\.",
                            terminal) != std::string::npos &&
              markdown.find("<a id=\"GLS ", terminal + 1) ==
                  std::string::npos,
          "terminal glossary definition was truncated or reordered");

  const auto legacy = document.topic_markdown("2.1");
  require(legacy.find("Subtopics:") != std::string::npos &&
              legacy.find("](#2.1.1)") != std::string::npos,
          "non-glossary topic lost the legacy fallback route");
#endif
}
