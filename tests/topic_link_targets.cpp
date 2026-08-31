// What a topic names, read off the typed Document IR instead of the legacy
// GML projection (issue #58).
//
// The book-wide link map `boo2git` builds comes from the typed Document IR
// rather than from `TocEntry::gml_records()`.  These checks pin the contract
// that made the switch safe: a typed topic names the ids its destinations
// depend on, with the kinds those destinations depend on -- a cross-reference
// anchor resolves to the topic's file, a figure and a table to a fragment, a
// figure that draws a stored book resource also names that resource, and a
// footnote to its own topic's file, because hosted BookServer resolves a
// footnote reference that leaves the page it is printed on.
//
// The corpus-wide equivalences this used to assert over XWEBDEMO, ACPZMST1,
// SC33-033 and N2AH1MST went with those books (issue #59); packet carries the
// figure, table, anchor and footnote shapes, but not the external-object
// figure (XWEBDEMO 1.4.1's `/bookmgr/monetcoq.jpg`) or the trap catalog's
// double-named entry (N2AH1MST 6.0).

#include "geist/boo.hpp"
#include "geist/detail/api/topic_link_targets.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "topic_link_targets: " << message << '\n';
    geist_test::record_failure();
  }
}

const geist::TocEntry* find_topic(const geist::BooDocument& document,
                                  const std::string& id) {
  for (const auto& entry : document.table_of_contents())
    if (entry.id == id)
      return &entry;
  return nullptr;
}

bool names(const std::vector<geist::LinkTarget>& targets,
           geist::LinkTargetKind kind,
           const std::string& id) {
  return std::any_of(targets.begin(), targets.end(),
                     [&](const auto& target) {
                       return target.kind == kind && target.id == id;
                     });
}

bool names_any(const std::vector<geist::LinkTarget>& targets,
               const std::string& id) {
  return std::any_of(
      targets.begin(), targets.end(),
      [&](const auto& target) { return target.id == id; });
}

std::string describe(const std::vector<geist::LinkTarget>& targets) {
  std::string output;
  for (const auto& target : targets) {
    if (!output.empty())
      output += ", ";
    output += target.kind == geist::LinkTargetKind::figure   ? "figure "
              : target.kind == geist::LinkTargetKind::table  ? "table "
                                                             : "anchor ";
    output += target.id;
    if (!target.resource.empty())
      output += " -> " + target.resource;
  }
  return output;
}

} // namespace

int main() {
  const auto document = geist::BooDocument::open(
      std::filesystem::path(GEIST_FIXTURE_DIR) / "packet.boo");

  // A typed picture figure that draws a stored book resource: the reference
  // id is the figure's own, and the target also names the resource the
  // exporter has to write out.
  {
    const auto* topic = find_topic(document, "2.4");
    require(topic != nullptr, "packet 2.4 is missing");
    if (topic != nullptr) {
      const auto& targets = topic->link_targets();
      require(topic->render_diagnostic().route == "typed",
              "packet 2.4 no longer renders typed; this check needs a typed "
              "topic");
      require(names(targets, geist::LinkTargetKind::figure, "FIGUNIQ16"),
              "packet 2.4 does not name figure FIGUNIQ16: " +
                  describe(targets));
      const auto figure = std::find_if(
          targets.begin(), targets.end(), [](const auto& target) {
            return target.id == "FIGUNIQ16";
          });
      require(figure != targets.end() && figure->resource == "resource:6",
              "packet 2.4's figure does not resolve to book resource 6: " +
                  describe(targets));
      require(!names_any(targets, "FIGFIGUNIQ16"),
              "packet 2.4 names the placed anchor rather than the reference "
              "id: " + describe(targets));
    }
  }

  // A typed table names a fragment and no resource.
  {
    const auto* topic = find_topic(document, "2.4.5");
    require(topic != nullptr, "packet 2.4.5 is missing");
    if (topic != nullptr) {
      const auto& targets = topic->link_targets();
      for (const auto* id : {"TBLTBLUNIQ18", "TBLTBLUNIQ19"}) {
        require(names(targets, geist::LinkTargetKind::table, id),
                std::string("packet 2.4.5 does not name table ") + id + ": " +
                    describe(targets));
      }
      for (const auto& target : targets) {
        require(target.kind != geist::LinkTargetKind::table ||
                    target.resource.empty(),
                "a table target names a book resource: " + describe(targets));
      }
    }
  }

  // A typed prose topic names its `SR<id>` anchor as a cross reference, which
  // resolves to the topic's file with no fragment.
  {
    const auto* topic = find_topic(document, "A.0");
    require(topic != nullptr, "packet A.0 is missing");
    if (topic != nullptr)
      require(names(topic->link_targets(), geist::LinkTargetKind::anchor,
                    "HDRURLS"),
              "packet A.0 does not name anchor HDRURLS: " +
                  describe(topic->link_targets()));
  }

  // A footnote is a book-wide destination.  It used to be withheld from the
  // map on the reading that a footnote is reachable only from the topic that
  // prints it, and hosted BookServer disproves that: SC31-6055-1
  // `BIBLIOGRAPHY.1` (DT 19911015203151) carries seven references to
  // `FTNMERBIB` and hosted answers every one with
  // `BIBLIOGRAPHY?DT=19911015203151#FTNMERBIB` -- the footnote printed by the
  // *parent* topic.  So the id is published, and a reference that leaves its
  // page resolves instead of dangling.  packet 1.1 prints FTNFTNUNIQ1 and
  // FTNFTNUNIQ2.
  {
    const auto* topic = find_topic(document, "1.1");
    require(topic != nullptr, "packet 1.1 is missing");
    if (topic != nullptr) {
      for (const auto* id : {"FTNFTNUNIQ1", "FTNFTNUNIQ2"}) {
        require(names(topic->link_targets(), geist::LinkTargetKind::anchor,
                      id),
                std::string("packet 1.1 does not publish footnote ") + id +
                    " as a book-wide destination: " +
                    describe(topic->link_targets()));
      }
    }
  }
  // Publishing them names exactly one destination each: no footnote id in
  // the book is printed by two topics.  (Nor anywhere in the corpus -- that
  // is what makes the id safe to publish at all.)
  {
    std::map<std::string, std::string> footnote_topic;
    for (const auto& entry : document.table_of_contents()) {
      for (const auto& target : entry.link_targets()) {
        if (target.id.rfind("FTN", 0) != 0)
          continue;
        const auto found = footnote_topic.find(target.id);
        if (found != footnote_topic.end()) {
          require(false, "footnote anchor " + target.id +
                             " is published by two topics, " + found->second +
                             " and " + entry.id +
                             "; the id no longer names one destination");
          continue;
        }
        footnote_topic.emplace(target.id, entry.id);
      }
    }
    require(!footnote_topic.empty(),
            "packet published no footnote destination at all");
  }

  // Whole book: the typed IR is the only answer, so there is no second
  // projection to check it against. What is worth pinning is that the answer
  // does not shrink -- a family that stops naming its figures would otherwise
  // break every reference to them silently -- so the figure and table targets
  // the book names are counted and ratcheted. Raise the number when a slice
  // legitimately names more; never lower it.
  constexpr std::size_t kNamedObjectBaseline = 16;
  std::size_t named_objects = 0;
  for (const auto& entry : document.table_of_contents()) {
    for (const auto& target : entry.link_targets()) {
      if (target.kind != geist::LinkTargetKind::figure &&
          target.kind != geist::LinkTargetKind::table)
        continue;
      require(!target.id.empty(),
              "packet " + entry.id +
                  ": a named object has an empty reference id");
      ++named_objects;
    }
  }
  std::cout << "# named figure/table targets\t" << named_objects << "\n";
  require(named_objects >= kNamedObjectBaseline,
          "the typed IR names fewer figures and tables than the recorded "
          "baseline: " + std::to_string(named_objects) + " < " +
              std::to_string(kNamedObjectBaseline));

  // Every figure that names a resource names one the book actually stores.
  for (const auto& entry : document.table_of_contents()) {
    for (const auto& target : entry.link_targets()) {
      if (target.resource.empty())
        continue;
      const auto id = target.resource.rfind("resource:", 0) == 0
                          ? target.resource.substr(9)
                          : target.resource;
      require(std::any_of(document.resources().begin(),
                          document.resources().end(),
                          [&](const auto& resource) {
                            return resource.id == id;
                          }),
              "packet " + entry.id + " names resource '" + target.resource +
                  "', which the book does not store");
    }
  }

  std::cout << "topic link target checks passed\n";
  return 0;
}
