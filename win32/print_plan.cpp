// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "print_plan.hpp"

#include <algorithm>
#include <cctype>

namespace geist_reader {
namespace {

bool is_topic(const ContentsRow& row, const char* id) {
  return equals_ignoring_case(row.id, id);
}

bool starts_with_upper(const std::string& id, const char* prefix) {
  std::string upper = id;
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return upper.rfind(prefix, 0) == 0;
}

bool front_matter(const ContentsRow& row) {
  for (const char* prefix : {"NOTICES", "EDITION", "PREFACE", "FRONT", "FOREWORD", "ACKNOWLEDG"})
    if (starts_with_upper(row.id, prefix)) return true;
  return false;
}

} // namespace

std::vector<PageSlice> paginate(double height, double available,
                                const std::vector<double>& blocks,
                                const std::vector<double>& lines) {
  std::vector<PageSlice> slices;
  if (available < 16) return slices;
  double start = 0;
  while (start < height - 1) {
    double end = std::min(start + available, height);
    if (end < height) {
      double best = 0;
      for (double position : blocks)
        if (position > start + available * .8 && position < end - 8) best = std::max(best, position);
      if (best == 0)
        for (double position : lines)
          if (position > start + available * .6 && position < end - 8) best = std::max(best, position);
      if (best > 0) end = best;
    }
    slices.push_back({start, end});
    start = end;
  }
  if (slices.empty()) slices.push_back({0, std::max(height, 1.0)});
  return slices;
}

bool printable_topic(const ContentsRow& row) { return !is_topic(row, "CONTENTS"); }

PrintPlan plan_book(
    const std::vector<ContentsRow>& rows,
    const std::map<std::string, std::size_t>& topic_pages,
    const std::function<std::size_t(const std::vector<ContentsRow>&)>& contents_pages) {
  std::vector<const ContentsRow*> ordered;
  std::vector<const ContentsRow*> title_topics;
  for (const char* id : {"COVER", "TITLE"})
    for (const auto& row : rows)
      if (is_topic(row, id)) title_topics.push_back(&row);
  ordered = title_topics;
  const std::size_t none = static_cast<std::size_t>(-1);
  std::size_t contents_index = none;
  for (const auto& row : rows) {
    if (is_topic(row, "COVER") || is_topic(row, "TITLE")) continue;
    if (is_topic(row, "CONTENTS")) {
      if (contents_index == none) contents_index = ordered.size();
      continue;
    }
    ordered.push_back(&row);
  }
  if (contents_index == none) {
    contents_index = title_topics.size();
    while (contents_index < ordered.size() && front_matter(*ordered[contents_index]))
      ++contents_index;
  }
  auto is_title = [&](const ContentsRow* row) {
    return std::find(title_topics.begin(), title_topics.end(), row) != title_topics.end();
  };
  auto pages_of = [&](const ContentsRow* row) -> std::size_t {
    const auto found = topic_pages.find(row->id);
    return found == topic_pages.end() ? 0 : found->second;
  };

  PrintPlan plan;
  for (const auto* row : ordered)
    if (!is_title(row)) plan.listing.push_back(*row);
  const std::size_t toc_pages = std::max<std::size_t>(1, contents_pages(plan.listing));

  std::size_t numbered = 1;
  for (std::size_t i = title_topics.size(); i <= ordered.size(); ++i) {
    if (i == contents_index) numbered += toc_pages;
    if (i == ordered.size()) break;
    if (!plan.page_numbers.count(ordered[i]->id))
      plan.page_numbers[ordered[i]->id] = numbered;
    numbered += pages_of(ordered[i]);
  }

  if (title_topics.empty()) plan.pages.push_back({PrintPage::Kind::title, {}, 0, 0});
  std::size_t number = 1;
  for (std::size_t i = 0; i <= ordered.size(); ++i) {
    if (i == contents_index)
      for (std::size_t p = 0; p < toc_pages; ++p)
        plan.pages.push_back({PrintPage::Kind::contents, {}, p, number++});
    if (i == ordered.size()) break;
    const bool title = is_title(ordered[i]);
    for (std::size_t p = 0; p < pages_of(ordered[i]); ++p)
      plan.pages.push_back({PrintPage::Kind::topic, ordered[i]->id, p, title ? 0 : number++});
  }
  return plan;
}

} // namespace geist_reader
