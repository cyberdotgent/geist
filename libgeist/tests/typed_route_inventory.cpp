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
// Every generated CONTENTS topic and every generated INDEX topic but one now
// take the typed `generated navigation` route.  The exception is SH20-918
// INDEX, whose record 636 token 275 is a two-byte token spelling the field
// delimiter twice (`citerm` U+25BA U+25BA `1`), so that term carries no term
// text at all; the family fails closed rather than invent one, and hosted
// (DT 19910520154851) truncates its own output before that group, so nothing
// can adjudicate it.
//
// Three topics still fail closed instead of printing a `c.<xx>` body control
// opcode the decoder glued to a text run (hosted BookServer serves no such
// word; checked on SH20-918 3.31.1 at DT 19910520154851, where the legacy and
// pre-composition typed routes both print `same results., c.cp`). The rest of
// that class is now typed: a display line whose whole visible content is one
// `c.<xx>` opcode and at most one operand is a body control line and draws
// nothing. Every book still only grows.
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
    {"ACPZMST1.boo", 186},
    {"DREICMST.boo", 363},
    {"FA1PLMM0.boo", 403},
    {"GC23-046.boo", 98},
    {"GC28-183.boo", 143},
    {"GG24-395.boo", 194},
    {"GG24-4302-00.boo", 224},
    {"GX27-3999-00.boo", 25},
    {"IBMMMSTR.boo", 41},
    {"IEAC6MST.BOO", 197},
    {"ITPPIBOK.BOO", 245},
    {"N2AH1MST.BOO", 34},
    {"OFCUSEOV.BOO", 179},
    {"PRG1SORT.boo", 175},
    {"QS3X36CM.BOO", 10},
    {"QSYSINFO.BOO", 392},
    {"QSYSNEWG.BOO", 152},
    {"SC09-138.boo", 513},
    {"SC09-2417-00.boo", 316},
    {"SC24-546.boo", 292},
    {"SC24-5520-00.boo", 639},
    {"SC24-5527-02.boo", 297},
    {"SC26-457.boo", 342},
    {"SC28-1881-05.boo", 81},
    {"SC31-605.boo", 109},
    {"SC31-711.boo", 75},
    {"SC33-033.boo", 215},
    {"SC34-425.boo", 221},
    {"SC41-485.boo", 27},
    {"SG24-204.boo", 86},
    {"SH12-565.boo", 274},
    {"SH20-918.boo", 191},
    {"XWEBDEMO.boo", 9},
    {"packet.boo", 120},
};
constexpr std::size_t kBaselineTotal = 6868;

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
