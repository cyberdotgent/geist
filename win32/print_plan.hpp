// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The page order of a printed book, independent of how pages are drawn.
//
// The cover and title topics come first and are not numbered (a plain title
// page is generated when the book has neither). The book's own CONTENTS
// topic is replaced by a generated table of contents listing the printed
// page number of every other topic; without a CONTENTS topic the generated
// one follows the leading front matter. Every page after the cover and title
// is numbered from 1. The same rules as the macOS reader's Print Book.

#pragma once

#include "book_source.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace geist_reader {

struct PrintPage {
  enum class Kind { title, contents, topic } kind = Kind::topic;
  std::string topic;       // topic pages
  std::size_t index = 0;   // page within its topic or within the contents
  std::size_t number = 0;  // printed page number; 0 when unnumbered
};

struct PrintPlan {
  std::vector<ContentsRow> listing;                   // rows of the contents
  std::map<std::string, std::size_t> page_numbers;    // topic id -> first page
  std::vector<PrintPage> pages;
};

// Splits a topic `height` CSS pixels tall into page slices of at most
// `available` pixels. A slice ends at the last block start in its final
// fifth, else at the last line start in its final two fifths, else at the
// full height: the same choice as the macOS reader's pagination.
struct PageSlice {
  double top = 0;
  double bottom = 0;
};
std::vector<PageSlice> paginate(double height, double available,
                                const std::vector<double>& blocks,
                                const std::vector<double>& lines);

// Whether the topic is printed at all (the book's CONTENTS topic is not).
bool printable_topic(const ContentsRow& row);

// `topic_pages` gives each rendered topic's page count; `contents_pages`
// says how many pages the generated contents takes for a listing.
PrintPlan plan_book(
    const std::vector<ContentsRow>& rows,
    const std::map<std::string, std::size_t>& topic_pages,
    const std::function<std::size_t(const std::vector<ContentsRow>&)>& contents_pages);

} // namespace geist_reader
