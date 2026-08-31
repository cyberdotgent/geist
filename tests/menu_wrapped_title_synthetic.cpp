// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// A compiled menu label may spell a title its topic's header row cannot
// (issue #77).
//
// A topic header's title stops at the `ST` control's own display row, because
// that row is what the reader heads the topic with.  Hosted BookServer proves
// it at our own timestamp: GC23-0469-01 `A.0` (DT 19920330095121) is served
//
//   <a name="HDRAINSTL"><H1>| A.0   Appendix A.  Install Logic for SMP/E
//   Release 6 and the Feature for Online</H1></a>
//
// and the word `Books` that finishes the title is not in the heading at all --
// it is on the row below.  So for a title long enough to wrap, the header
// title is a *proper prefix* of the whole title, while the compiled menu's
// `CMITEM` label and the book's own TOC both spell the whole one.
//
// QSYSINFO `2.1.21` is the case on the issue: header
// `SC09-1159, Languages:  System/38-Compatible COBOL User's Guide and`,
// label and TOC `... User's Guide and Reference`.  `2.1.45` adds the second
// half of the shape: the TOC title carries a trailing `*` marker slot that
// the label does not, so label and TOC are not equal either.
//
// The label is therefore agreed when the two independent projections bracket
// it: it begins with the whole header title, and the TOC title begins with
// it.  That is an extra acceptance and never a new rejection, and it is not
// "prefer the TOC" -- a catalog *relabeling* changes the title's words, so it
// is not an extension of the header title and is still rejected.
//
// Everything here is synthetic: the catalogue and the raw menu are built by
// hand and no book is opened (issue #59).

#include "geist/detail/ir/book_topic_catalog_ir.hpp"
#include "geist/detail/ir/menu_ir.hpp"
#include "geist/detail/ir/menu_topic_ir.hpp"
#include "test_failures.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

using geist::detail::BookTopicCatalogIR;
using geist::detail::MenuIR;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "menu_wrapped_title_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

// One catalogue entry carrying both projections of a topic's title.
BookTopicCatalogIR catalog(const std::string &id, const std::string &header,
                           const std::string &toc) {
  BookTopicCatalogIR book;
  geist::detail::BookTopicCatalogEntryIR entry;
  entry.raw_topic_id = id;
  geist::detail::BookTopicHeaderEvidenceIR head;
  head.title = header;
  head.heading_level = ":H3";
  entry.topic_header = std::move(head);
  geist::detail::BookTopicTocEvidenceIR listed;
  listed.raw_id = id;
  listed.title = toc;
  entry.toc_entries.push_back(std::move(listed));
  book.topics.push_back(std::move(entry));
  return book;
}

// A raw menu of one item, with neither a compact terminal marker nor a record
// terminator -- so nothing but the bracketing rule can admit its label.
MenuIR menu(const std::string &id, const std::string &label) {
  MenuIR raw;
  geist::detail::MenuItemIR item;
  item.logical_record = 1;
  item.segment_index = 0;
  item.target = id;
  item.text = label;
  raw.items.push_back(std::move(item));
  return raw;
}

bool validates(const MenuIR &raw, const BookTopicCatalogIR &book,
               std::string *error) {
  return geist::detail::validate_source_menu_targets(raw, book, error)
      .has_value();
}

const char *kHeader = "SC09-1159, Languages:  System/38-Compatible COBOL "
                      "User's Guide and";
const char *kWhole = "SC09-1159, Languages:  System/38-Compatible COBOL "
                     "User's Guide and Reference";

// QSYSINFO 2.1.21: label equals the TOC title, which extends the header.
void a_label_bracketed_by_both_projections_is_agreed() {
  std::string error;
  require(validates(menu("2.1.21", kWhole), catalog("2.1.21", kHeader, kWhole),
                    &error),
          "a menu label spelling the whole wrapped title was rejected: " +
              error);
}

// QSYSINFO 2.1.45: the TOC title carries a trailing marker slot the label does
// not, so the label sits strictly between the two projections.
void a_toc_title_with_a_trailing_marker_still_agrees() {
  std::string error;
  require(validates(menu("2.1.45", kWhole),
                    catalog("2.1.45", kHeader, std::string(kWhole) + " *"),
                    &error),
          "a menu label bracketed by the header and a TOC title with a "
          "trailing marker was rejected: " + error);
}

// The label still has to begin with the *whole* header title.  A label that
// relabels the title -- changing its words rather than continuing it -- is
// not an extension of the header and stays rejected.
void a_relabelled_title_still_fails_closed() {
  std::string error;
  require(!validates(menu("2.1.21", "SC09-1159, COBOL Reference Manual"),
                     catalog("2.1.21", kHeader,
                             "SC09-1159, COBOL Reference Manual"),
                     &error),
          "a menu label that relabels the title was admitted; it must fail "
          "closed");
}

// And a label that extends the header but that the TOC does not corroborate
// is rejected too: one projection is not two.
void a_label_the_toc_does_not_corroborate_fails_closed() {
  std::string error;
  require(!validates(menu("2.1.21", std::string(kHeader) + " Something Else"),
                     catalog("2.1.21", kHeader, kWhole), &error),
          "a menu label the book's own TOC does not corroborate was "
          "admitted; it must fail closed");
}

} // namespace

int main() {
  a_label_bracketed_by_both_projections_is_agreed();
  a_toc_title_with_a_trailing_marker_still_agrees();
  a_relabelled_title_still_fails_closed();
  a_label_the_toc_does_not_corroborate_fails_closed();
  return 0;
}
