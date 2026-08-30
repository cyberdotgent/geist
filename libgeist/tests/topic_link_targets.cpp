// What a topic names, read off the typed Document IR instead of the legacy
// GML projection (issue #58).
//
// The book-wide link map `boo2git` builds used to come from
// `TocEntry::gml_records()` for every TOC entry, which ran the legacy string
// renderer for all 7,362 corpus topics rather than the 375 that render
// through it.  These checks pin the equivalence that made the switch safe:
// for a typed topic the two answers name the same ids with the same kinds,
// and the kinds are the ones the destinations depend on -- a cross-reference
// anchor resolves to the topic's file, a figure and a table to a fragment,
// and a footnote to nothing at all because `SRFTN` produces no GML record.
//
// Every expectation below was read from the corpus, and each id also appears
// in the exported Markdown: XWEBDEMO record 11's `SRFIGMONET1` is spelled
// `MONET1` by `:fig id=`, referenced as `FIGMONET1`, and served by hosted
// BookServer as `<a name="FIGMONET1">` (DT 19970423182524).

#include "geist/boo.hpp"
#include "geist/detail/topic_link_targets.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <filesystem>
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

std::filesystem::path book_path(const std::string& name) {
  return std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / name;
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
  // A typed picture figure: the anchor the document places is `FIGMONET1`,
  // the reference id is `MONET1`, and the external object is not a stored
  // book resource, so references resolve to the anchor and not to an image.
  {
    const auto document = geist::BooDocument::open(book_path("XWEBDEMO.boo"));
    const auto* topic = find_topic(document, "1.4.1");
    require(topic != nullptr, "XWEBDEMO 1.4.1 is missing");
    if (topic != nullptr) {
      const auto& targets = topic->link_targets();
      require(topic->render_diagnostic().route == "typed",
              "XWEBDEMO 1.4.1 no longer renders typed; this check needs a "
              "typed topic");
      require(names(targets, geist::LinkTargetKind::figure, "MONET1"),
              "XWEBDEMO 1.4.1 does not name figure MONET1: " +
                  describe(targets));
      require(!names_any(targets, "FIGMONET1"),
              "XWEBDEMO 1.4.1 names the placed anchor rather than the "
              "reference id: " + describe(targets));
    }
    // The topic that prints footnote FTNBUILD reaches it only from inside
    // itself; `SRFTN` produces no `:anchor` record, so the link map must not
    // gain a book-wide destination for it.
    const auto* footnote_topic = find_topic(document, "1.4");
    require(footnote_topic != nullptr, "XWEBDEMO 1.4 is missing");
    if (footnote_topic != nullptr)
      require(!names_any(footnote_topic->link_targets(), "FTNBUILD"),
              "XWEBDEMO 1.4 published its footnote as a book-wide "
              "destination: " + describe(footnote_topic->link_targets()));
  }

  // A typed prose topic names its `SR<id>` anchors as cross references, which
  // resolve to the topic's file with no fragment.
  {
    const auto document = geist::BooDocument::open(book_path("ACPZMST1.boo"));
    const auto* topic = find_topic(document, "3.2");
    require(topic != nullptr, "ACPZMST1 3.2 is missing");
    if (topic != nullptr)
      require(names(topic->link_targets(), geist::LinkTargetKind::anchor,
                    "HDRPMGR"),
              "ACPZMST1 3.2 does not name anchor HDRPMGR: " +
                  describe(topic->link_targets()));
  }

  // A trap catalog names the topic twice from inside one entry: N2AH1MST
  // record 385 spells `SRMSG AMD083I` and `SRSPTE083I` side by side, and the
  // book's own change summary links to the second.  The entry anchor is
  // placed in the document; the second name is not, but both reach the topic.
  {
    const auto document = geist::BooDocument::open(book_path("N2AH1MST.BOO"));
    const auto* topic = find_topic(document, "6.0");
    require(topic != nullptr, "N2AH1MST 6.0 is missing");
    if (topic != nullptr) {
      const auto& targets = topic->link_targets();
      require(names(targets, geist::LinkTargetKind::anchor, "MSG AMD083I"),
              "N2AH1MST 6.0 does not name its entry anchor MSG AMD083I");
      require(names(targets, geist::LinkTargetKind::anchor, "SPTE083I"),
              "N2AH1MST 6.0 does not name SPTE083I, which its change "
              "summary links to");
    }
  }

  // Whole books: the typed IR is now the only answer, so there is no second
  // projection to check it against. What is still worth pinning is that the
  // answer does not shrink -- a family that stops naming its figures would
  // otherwise break every reference to them silently -- so the figure and
  // table targets these books name are counted and ratcheted. Raise the
  // number when a slice legitimately names more; never lower it.
  constexpr std::size_t kNamedObjectBaseline = 285;
  std::size_t named_objects = 0;
  for (const auto* name : {"XWEBDEMO.boo", "ACPZMST1.boo", "SC33-033.boo"}) {
    const auto document = geist::BooDocument::open(book_path(name));
    for (const auto& entry : document.table_of_contents()) {
      for (const auto& target : entry.link_targets()) {
        if (target.kind != geist::LinkTargetKind::figure &&
            target.kind != geist::LinkTargetKind::table)
          continue;
        require(!target.id.empty(),
                std::string(name) + " " + entry.id +
                    ": a named object has an empty reference id");
        ++named_objects;
      }
    }
  }
  std::cout << "# named figure/table targets\t" << named_objects << "\n";
  require(named_objects >= kNamedObjectBaseline,
          "the typed IR names fewer figures and tables than the recorded "
          "baseline: " + std::to_string(named_objects) + " < " +
              std::to_string(kNamedObjectBaseline));

  std::cout << "topic link target checks passed\n";
  return 0;
}
