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
#ifdef GEIST_FIXTURE_DIR
  const auto root = std::filesystem::path(GEIST_FIXTURE_DIR);
  // Only packet.boo may be redistributed; the other 26 book/topic fixtures
  // of this inventory went with the books that cannot be published, along
  // with the embedded-marker, ordinal-decoration, payload-punctuation and
  // decoder-artifact label checks they carried (issue #59).
  const std::vector<Fixture> fixtures{
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
    total_entries += fixture.entries;

  }
  require(fixtures.size() == 2 && total_entries == 16,
          "production generated-list inventory changed");

  // A topic that is not a generated list must not enter the route.
  const auto document = geist::BooDocument::open(root / "packet.boo");
  const auto ordinary = document.topic_markdown("2.1");
  require(!ordinary.empty() && ordinary.front() == '#',
          "non-generated topic lost its heading");
  require(count(ordinary, "](<#") < 9,
          "an ordinary prose topic rendered as a generated list");

  // The two generated lists name every figure and table the book stores, and
  // each entry's target is the object's own reference id.
  for (const auto* expected : {"[9\\.  LoRa Frame Format   7\\.1\\.3](<#FIGFIGUNIQ80>)",
                               "[1\\.  VHF/UHF LMR audio frequency range   "
                               "1\\.3](<#FIGFIGUNIQ5>)"}) {
    require(document.topic_markdown("FIGURES").find(expected) !=
                std::string::npos,
            "FIGURES lost a typed entry label or its resolved target");
  }
  require(document.topic_markdown("TABLES").find(
              "[1\\.  IPv4 Address Classes   2\\.4\\.4](<#TBLTBLUNIQ17>)") !=
              std::string::npos,
          "TABLES lost a typed entry label or its resolved target");
#endif
}
