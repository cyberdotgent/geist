// Selector ownership across display rows (was tests/sc31_711_cross_references
// .cpp, issue #59).
//
// The contract: target equality and row adjacency do not imply one selector.
// A `CSELECT` phrase that hosted BookServer breaks over two display rows stays
// two adjacent anchors with the same destination, and a phrase served whole on
// one row stays one anchor.  packet is the only redistributable fixture that
// carries both shapes.

#include "geist/document.hpp"
#include "test_failures.hpp"

#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << message << "\n";
    geist_test::record_failure();
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
  const auto packet = geist::BooDocument::open(
      std::filesystem::path(GEIST_FIXTURE_DIR) / "packet.boo");

  // 6.2's reference to A.0 is broken over two display rows by the reader.
  // Hosted (DT 20260614112503) serves two `<a href="...#HDRURLS">` elements
  // there, so the typed route must keep two adjacent anchors and not merge
  // them into one label or tear a word between them.
  const auto installation = packet.topic_markdown("6.2");
  require(count(installation, "](<#HDRURLS>)") == 2,
          "6.2 changed the two-anchor Web Locations reference multiplicity");
  require(installation.find(
              "[\"Web Locations of Packet](<#HDRURLS>) "
              "[Radio Software\" in topic A\\.0](<#HDRURLS>)") !=
              std::string::npos,
          "6.2 merged or tore the two-row Web Locations reference");
  for (const auto* tear : {"Packet [Radio", "of [Packet", "Software [\"",
                           "topic [A", "Locations [of"}) {
    require_absent(installation, tear, "6.2");
  }

  // The anchor the reference resolves to is a body `SR<id>` anchor on its own
  // topic, so the destination is real and not fabricated from the topic id.
  const auto locations = packet.topic_markdown("A.0");
  require(locations.find("<a id=\"HDRURLS\"></a>") != std::string::npos,
          "A.0 lost the anchor its cross references target");

  // Ordinary footnote selectors are served whole on one row and stay singular;
  // hosted shows one `<a href="...#FTNFTNUNIQ1"> (1)</a>` in 1.1.
  const auto original = packet.topic_markdown("1.1");
  require(count(original, "](<#FTNFTNUNIQ1>)") == 1 &&
              original.find("[\\(1\\)](<#FTNFTNUNIQ1>)") != std::string::npos,
          "1.1 footnote selector changed multiplicity");
  require(count(original, "](<#FTNFTNUNIQ2>)") == 1 &&
              original.find("[\\(2\\)](<#FTNFTNUNIQ2>)") != std::string::npos,
          "1.1 second footnote selector changed multiplicity");
  // Each footnote selector has the anchor it names in the same topic.
  for (const auto* anchor : {"<a id=\"FTNFTNUNIQ1\"></a>",
                             "<a id=\"FTNFTNUNIQ2\"></a>"}) {
    require(original.find(anchor) != std::string::npos,
            "1.1 lost a footnote anchor its selector targets");
  }

  // A topic no typed family claims still resolves the references its
  // `cselect` controls name (issue #72). packet 4.5.1 renders verbatim and
  // carries one: `cselect 42 5 FTNFTNUNIQ50` over the footnote marker of the
  // row that follows it. The anchor it names is printed by this same topic,
  // so the file has to carry it -- a footnote is reachable only from its own
  // page, which is why `best_effort_anchors` publishes none book-wide.
  const auto verbatim = packet.topic_markdown("4.5.1");
  require(verbatim.find("<!-- geist-render: severity=best-effort") !=
              std::string::npos,
          "4.5.1 no longer renders verbatim; the pin below tests nothing");
  require(count(verbatim, "](<#FTNFTNUNIQ50>)") == 1 &&
              verbatim.find("[\\(37\\)](<#FTNFTNUNIQ50>)") ==
                  std::string::npos &&
              verbatim.find("[(37)](<#FTNFTNUNIQ50>)") != std::string::npos,
          "4.5.1 lost the verbatim cross reference over its footnote marker, "
          "or escaped it: verbatim rows are never Markdown-escaped");
  require(verbatim.find("<a id=\"FTNFTNUNIQ50\"></a>") != std::string::npos,
          "4.5.1 lost the footnote anchor its own link targets");

  // A book-wide guard: every anchor destination any topic emits resolves
  // somewhere in the book -- to an anchor some topic defines, or to a topic
  // id.  This is what stops a lowering change from inventing targets.
  std::vector<std::string> bodies;
  std::set<std::string> defined;
  for (const auto& topic : packet.topics()) {
    bodies.push_back(packet.topic_markdown(topic.id));
    const auto& body = bodies.back();
    for (auto at = body.find("<a id=\""); at != std::string::npos;
         at = body.find("<a id=\"", at + 7)) {
      const auto begin = at + 7;
      const auto end = body.find('"', begin);
      if (end == std::string::npos) break;
      defined.insert(body.substr(begin, end - begin));
    }
  }
  require(defined.size() >= 80,
          "the anchor sweep found far fewer anchors than packet defines");

  std::size_t selectors = 0;
  for (std::size_t index = 0; index < bodies.size(); ++index) {
    const auto& body = bodies[index];
    const auto& id = packet.topics()[index].id;
    for (auto at = body.find("](<#"); at != std::string::npos;
         at = body.find("](<#", at + 4)) {
      const auto begin = at + 4;
      const auto end = body.find('>', begin);
      if (end == std::string::npos) break;
      const auto target = body.substr(begin, end - begin);
      ++selectors;
      // `<id>-summary` is the generated navigation topic's own synthetic
      // summary destination, not a book anchor.
      if (target.size() > 8 &&
          target.compare(target.size() - 8, 8, "-summary") == 0)
        continue;
      require(defined.count(target) != 0 ||
                  packet.find_toc_entry(target) != nullptr,
              id + " links to '" + target +
                  "', which is neither an anchor the book defines nor a "
                  "topic id");
    }
  }
  require(selectors >= 90,
          "the selector sweep found far fewer links than packet carries");
}
