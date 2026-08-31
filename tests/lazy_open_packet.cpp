// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Container, directory, TOC and lazy-topic integration over the one
// redistributable fixture (issue #59).
//
// The BOO container cannot be synthesised -- there is no encoder in the tree --
// so everything here needs a real file.  This replaces the per-book
// lazy_open_*.cpp family, which asserted the same contract against thirteen
// books that cannot be published.

#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto path =
      std::filesystem::path(GEIST_FIXTURE_DIR) / "packet.boo";
  const auto document = geist::BooDocument::open(path);

  // The directory is read from the container header alone; opening must not
  // decode any content page.
  const auto& directory = document.directory();
  require(directory.content_page_index_offset == 0x0e8e,
          "unexpected content-page index offset");
  require(directory.logical_record_count == 0x0173,
          "unexpected logical-record count");
  require(directory.stream_table_offset == 0x0068,
          "unexpected topic-start index offset");
  require(directory.stream_table_count == 124,
          "unexpected topic count");
  require(document.topics().size() == 124,
          "lightweight topic index has the wrong size");
  require(document.resources().size() == 9,
          "resource index has the wrong size");

  // Book properties come from the directory page too.
  const auto& properties = document.book_properties();
  require(properties.title == "Amateur Packet Radio: A Complete Tutorial",
          "book title was not read from the directory");
  require(properties.document_number == "9963-0413-56",
          "book document number was not read from the directory");

  // A TOC entry renders its own topic without the caller ever naming a
  // logical record.
  const auto* intro = document.find_toc_entry("1.0");
  require(intro != nullptr, "missing 1.0 TOC entry");
  const auto markdown = intro->markdown();
  require(markdown.find("# 1\\.0 An Introduction to Packet Radio") !=
              std::string::npos,
          "lazy TOC entry rendering produced unexpected Markdown");

  // Direct lazy rendering by topic id, without walking the TOC.
  const auto index = document.topic_markdown("INDEX");
  require(index.find("## A") != std::string::npos,
          "direct lazy topic rendering lost the generated index");
  for (const auto* expected : {
           "- AX\\.25 Protocol, [2\\.1](<#2.1>)",
           "  - Digipeater, [2\\.1\\.3](<#2.1.3>)",
       }) {
    require(index.find(expected) != std::string::npos,
            "generated index lost a linked hierarchy entry");
  }

  // A leading anchor must not displace a nested topic's heading.
  for (const auto* nested : {"2.1.1", "2.1.2", "2.1.3", "2.1.4", "3.2.1",
                             "3.2.2", "3.2.3", "5.1.2.1.1"}) {
    require(markdown_visible_text(document.topic_markdown(nested))
                    .find(std::string("### ") + nested + " ") !=
                std::string::npos,
            "leading anchor displaced a nested topic heading");
  }

  // Repeated lazy loads of one topic are stable.
  require(document.topic_markdown("2.4") == document.topic_markdown("2.4"),
          "repeated lazy topic loads disagree");

  // Every topic in the index is reachable and renders a heading; this walks
  // the whole container through the lazy path.
  std::size_t rendered = 0;
  for (const auto& topic : document.topics()) {
    const auto body = document.topic_markdown(topic.id);
    require(!body.empty(), "a lazily loaded topic rendered nothing");
    ++rendered;
  }
  require(rendered == 124, "lazy walk did not cover every topic");

  // Resource decoding is lazy too: the ids are known from the directory, the
  // bitmaps only on demand.
  const auto picture = document.read_resource_png("6");
  require(!picture.empty(), "book resource 6 did not decode to a PNG");
  require(picture.size() > 8 && picture[1] == 'P' && picture[2] == 'N' &&
              picture[3] == 'G',
          "book resource 6 is not a PNG stream");
}
