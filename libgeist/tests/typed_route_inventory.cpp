// Typed-route coverage ratchet (issue #58).
//
// Runs BooDocument::typed_route_inventory over every BOO fixture: lowering
// only, no Markdown. Fails when the number of topics that reach the typed
// Document IR route drops below the committed baseline, in total or for any
// single book. The per-book table is printed so CTest output shows the
// current coverage.
//
// Updating the baseline: when a lowering slice raises coverage, run
//   build/typed_route_inventory_test
// and copy the printed "book<TAB>typed" pairs into kBaseline below (and the
// new total into kBaselineTotal). Lowering the baseline is a regression and
// needs an explicit explanation in the commit message.
//
// Runtime: about 11 minutes uncontended, ~9 of them in N2AH1MST.BOO whose
// SRMSG recognizers are slow; hence the `slow` label.

#include "geist/detail/typed_route_inventory.hpp"
#include "geist/document.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

struct BookBaseline {
  const char *book;
  std::size_t typed;
};

constexpr BookBaseline kBaseline[] = {
    {"ACPZMST1.boo", 63}, {"DREICMST.boo", 161}, {"FA1PLMM0.boo", 275},
    {"GC23-046.boo", 40}, {"GC28-183.boo", 59}, {"GG24-395.boo", 93},
    {"GG24-4302-00.boo", 81}, {"GX27-3999-00.boo", 0}, {"IBMMMSTR.boo", 17},
    {"IEAC6MST.BOO", 64}, {"ITPPIBOK.BOO", 129}, {"N2AH1MST.BOO", 6},
    {"OFCUSEOV.BOO", 36}, {"PRG1SORT.boo", 110}, {"QS3X36CM.BOO", 1},
    {"QSYSINFO.BOO", 171}, {"QSYSNEWG.BOO", 30}, {"SC09-138.boo", 181},
    {"SC09-2417-00.boo", 0}, {"SC24-546.boo", 84}, {"SC24-5520-00.boo", 328},
    {"SC24-5527-02.boo", 60}, {"SC26-457.boo", 127}, {"SC28-1881-05.boo", 14},
    {"SC31-605.boo", 14}, {"SC31-711.boo", 56}, {"SC33-033.boo", 71},
    {"SC34-425.boo", 115}, {"SC41-485.boo", 0}, {"SG24-204.boo", 35},
    {"SH12-565.boo", 147}, {"SH20-918.boo", 94}, {"XWEBDEMO.boo", 1},
    {"packet.boo", 2},
};
constexpr std::size_t kBaselineTotal = 2665;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "typed_route_inventory: " << message << "\n";
    geist_test::record_failure();
  }
}

} // namespace

int main() {
  const auto directory = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  std::vector<std::filesystem::path> books;
  for (const auto &file : std::filesystem::directory_iterator(directory)) {
    auto extension = file.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return std::tolower(ch); });
    if (extension == ".boo")
      books.push_back(file.path());
  }
  std::sort(books.begin(), books.end());

  std::map<std::string, std::size_t> baseline;
  for (const auto &entry : kBaseline)
    baseline.emplace(entry.book, entry.typed);

  std::size_t typed_total = 0;
  std::size_t legacy_total = 0;
  std::map<std::string, std::size_t> family_totals;
  std::cout << "book\ttyped\tlegacy\ttotal\tbaseline\n";
  for (const auto &path : books) {
    const auto book = path.filename().string();
    const auto document = geist::BooDocument::open(path);
    const auto inventory = document.typed_route_inventory();
    typed_total += inventory.typed_count;
    legacy_total += inventory.legacy_count;
    for (const auto &[family, count] : inventory.typed_by_family)
      family_totals[family] += count;

    const auto expected = baseline.find(book);
    std::cout << book << "\t" << inventory.typed_count << "\t"
              << inventory.legacy_count << "\t" << inventory.topics.size()
              << "\t"
              << (expected == baseline.end() ? std::string("(none)")
                                             : std::to_string(expected->second))
              << "\n";
    require(expected != baseline.end(),
            book + " has no committed baseline; add it to kBaseline");
    if (expected != baseline.end())
      require(inventory.typed_count >= expected->second,
              book + " typed coverage regressed: " +
                  std::to_string(inventory.typed_count) + " < baseline " +
                  std::to_string(expected->second));
    require(inventory.typed_count + inventory.legacy_count ==
                inventory.topics.size(),
            book + " inventory counts do not sum to its topic count");
  }
  for (const auto &entry : kBaseline)
    require(std::any_of(books.begin(), books.end(),
                        [&](const auto &path) {
                          return path.filename().string() == entry.book;
                        }),
            std::string(entry.book) + " is in kBaseline but not in BOO/");

  std::cout << "# families\n";
  for (const auto &[family, count] : family_totals)
    std::cout << family << "\t" << count << "\n";
  std::cout << "# summary\ttyped=" << typed_total << "\tlegacy=" << legacy_total
            << "\ttotal=" << typed_total + legacy_total
            << "\tbaseline=" << kBaselineTotal << "\n";
  require(typed_total >= kBaselineTotal,
          "corpus typed coverage regressed: " + std::to_string(typed_total) +
              " < baseline " + std::to_string(kBaselineTotal));
  return 0;
}
