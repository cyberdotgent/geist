#include "geist/detail/internal.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require_contains(const std::string& haystack,
                      const std::string& needle,
                      const char* label) {
  if (haystack.find(needle) != std::string::npos) {
    return;
  }
  std::cerr << "missing " << label << ": " << needle << "\n"
            << "actual: " << haystack << "\n";
  std::exit(1);
}

void require_not_contains(const std::string& haystack,
                          const std::string& needle,
                          const char* label) {
  if (haystack.find(needle) == std::string::npos) {
    return;
  }
  std::cerr << "unexpected " << label << ": " << needle << "\n"
            << "actual: " << haystack << "\n";
  std::exit(1);
}

} // namespace

int main() {
  const auto literal =
      geist::detail::annotate_decoded_placeholders("In a Hurry?");
  require_contains(literal, "Hurry?", "literal question mark");
  require_not_contains(literal,
                       "<geist-placeholder",
                       "placeholder around literal question mark");

  const auto literal_before_padding =
      geist::detail::annotate_decoded_placeholders("In a Hurry? ????");
  require_contains(literal_before_padding,
                   "In a Hurry? <geist-placeholder",
                   "literal question mark before placeholder padding");

  const auto boundary =
      geist::detail::annotate_decoded_placeholders("Text?CFONT 1 2 3");
  require_contains(boundary,
                   "<geist-placeholder kind='control-boundary' offset='4' "
                   "len='1'>CFONT",
                   "control boundary placeholder");

  const auto run = geist::detail::annotate_decoded_placeholders(
      "CFONT 8 2 2 11 1 2 13 6 2      ???? In a Hurry?");
  require_contains(run,
                   "<geist-placeholder kind='decoded-question-run'",
                   "decoded question run");
  require_contains(run, "In a Hurry?", "literal question after run");

  const auto raw = geist::detail::render_gml_records({"CPICTURE 1 PIC1"});
  if (raw.size() != 1) {
    std::cerr << "unexpected raw record count: " << raw.size() << "\n";
    std::exit(1);
  }
  require_contains(raw.front(),
                   ":unknown-control name='CPICTURE' raw='CPICTURE 1 PIC1'.",
                   "unknown control raw projection");

  return 0;
}
