#include "geist/boo.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: bootoc <book.boo>\n";
    return 2;
  }

  try {
    const auto document = geist::BooDocument::open(argv[1]);
    const auto& toc = document.table_of_contents();

    if (toc.empty()) {
      std::cout << "No table of contents entries parsed yet.\n";
      return 0;
    }

    for (const auto& entry : toc) {
      for (std::uint32_t level = 0; level < entry.level; ++level) {
        std::cout << "  ";
      }
      std::cout << entry.id << '\t' << entry.title << "\tstyle "
                << entry.style << "\n";
    }
  } catch (const std::exception& error) {
    std::cerr << "bootoc: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
