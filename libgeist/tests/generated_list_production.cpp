#include "geist/document.hpp"
#include "test_failures.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "generated_list_production: " << message << '\n';
    geist_test::record_failure();
    return;
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

struct Fixture {
  const char* book;
  const char* topic;
  std::size_t entries;
};

} // namespace

int main() {
#ifdef GEIST_REPO_ROOT
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  const std::vector<Fixture> fixtures{
      {"DREICMST.boo", "FIGURES", 129},
      {"FA1PLMM0.boo", "FIGURES", 113},
      {"GC23-046.boo", "FIGURES", 32},
      {"GC23-046.boo", "TABLES", 32},
      {"GC28-183.boo", "FIGURES", 55},
      {"GG24-395.boo", "FIGURES", 81},
      {"GG24-395.boo", "TABLES", 16},
      {"GG24-4302-00.boo", "FIGURES", 51},
      {"GG24-4302-00.boo", "TABLES", 15},
      {"IEAC6MST.BOO", "FIGURES", 100},
      {"ITPPIBOK.BOO", "FIGURES", 20},
      {"ITPPIBOK.BOO", "TABLES", 1},
      {"SC09-138.boo", "FIGURES", 162},
      {"SC09-138.boo", "TABLES", 44},
      {"SC24-546.boo", "FIGURES", 9},
      {"SC24-546.boo", "TABLES", 4},
      {"SC24-5527-02.boo", "FIGURES", 11},
      {"SC24-5527-02.boo", "TABLES", 71},
      {"SC26-457.boo", "FIGURES", 53},
      {"SC28-1881-05.boo", "FIGURES", 18},
      {"SC33-033.boo", "FIGURES", 7},
      {"SC33-033.boo", "TABLES", 4},
      {"SG24-204.boo", "FIGURES", 128},
      {"SH20-918.boo", "FIGURES", 12},
      {"SH20-918.boo", "TABLES", 9},
      {"XWEBDEMO.boo", "FIGURES", 3},
      {"packet.boo", "FIGURES", 9},
      {"packet.boo", "TABLES", 7},
  };
  auto total_entries = std::size_t{};
  for (const auto& fixture : fixtures) {
    const auto document = geist::BooDocument::open(root / fixture.book);
    const auto* toc = document.find_toc_entry(fixture.topic);
    require(toc != nullptr,
            std::string(fixture.book) + ':' + fixture.topic +
                " is not a TOC-exported topic");
    const auto gml_before = toc->gml_records();
    const auto markdown = document.topic_markdown(fixture.topic);
    const auto again = document.topic_markdown(fixture.topic);
    require(markdown == again,
            std::string(fixture.book) + ':' + fixture.topic +
                " repeated typed rendering changed");
    require(markdown.rfind("# " + std::string(fixture.topic) + ' ', 0) == 0,
            std::string(fixture.book) + ':' + fixture.topic +
                " did not enter the typed generated-list route");
    require(count(markdown, "](<#") == fixture.entries,
            std::string(fixture.book) + ':' + fixture.topic +
                " link count differs from typed entry inventory");
    require(toc->gml_records() == gml_before && !gml_before.empty(),
            std::string(fixture.book) + ':' + fixture.topic +
                " typed rendering changed public GML");
    total_entries += fixture.entries;

    if (std::string(fixture.book) == "GG24-395.boo" &&
        std::string(fixture.topic) == "FIGURES") {
      require(markdown.find(
                  "[1\\.  Five Styles of Client/Server Computing   "
                  "1\\.1\\.3](<#FIGCSMAST>)") != std::string::npos &&
                  markdown.find(
                      "[11\\.  Open Blueprint Model   "
                      "2\\.3\\.3](<#FIGFDSS101>)") != std::string::npos &&
                  markdown.find("[| 11") == std::string::npos,
              "embedded native marker entered a production label");
    }
    if (std::string(fixture.book) == "IEAC6MST.BOO" &&
        std::string(fixture.topic) == "FIGURES")
      require(markdown.find("[2\\-2\\.  Sample CLIST") !=
                      std::string::npos &&
                  markdown.find("[7\\-9\\.") != std::string::npos &&
                  markdown.find("[*/") == std::string::npos,
              "ordinal or cross-record decoration semantics changed");
    if (std::string(fixture.book) == "GC23-046.boo" &&
        std::string(fixture.topic) == "TABLES")
      require(markdown.find("[\\(CIDSIEXP\\)") != std::string::npos &&
                  markdown.find("[\\(CIDSIWTO\\)") != std::string::npos,
              "payload punctuation was lost in production lowering");
    if (std::string(fixture.book) == "SC09-138.boo" &&
        std::string(fixture.topic) == "TABLES")
      require(markdown.find(
                  "24\\.  Description of \\_\\_dyn\\_t data structure "
                  "elements   8\\.1\\.10\\.3") != std::string::npos &&
                  markdown.find("3\\.2 ??") == std::string::npos,
              "typed suffix or decoder-artifact policy changed");
  }
  require(fixtures.size() == 28 && total_entries == 1196,
          "production generated-list inventory changed");

  const auto legacy = geist::BooDocument::open(root / "SC31-711.boo");
  const auto ordinary = legacy.topic_markdown("2.1");
  require(!ordinary.empty() && ordinary.front() == '#',
          "non-generated topic lost legacy fallback");
#endif
}
