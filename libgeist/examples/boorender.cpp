#include "geist/boo.hpp"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: boorender <book.boo> <topic-id> (--raw|--md)\n";
    return 2;
  }

  try {
    const std::string mode = argv[3];
    const auto document = geist::BooDocument::open(argv[1]);
    const auto* entry = document.find_toc_entry(argv[2]);
    if (entry == nullptr) {
      std::cerr << "boorender: BOO topic id was not found: " << argv[2]
                << "\n";
      return 1;
    }

    if (mode == "--raw") {
      for (const auto& record : entry->raw_records) {
        std::cout << record << '\n';
      }
    } else if (mode == "--md") {
      std::cout << "Markdown support is not yet implemented\n";
    } else {
      std::cerr << "usage: boorender <book.boo> <topic-id> (--raw|--md)\n";
      return 2;
    }
  } catch (const std::exception& error) {
    std::cerr << "boorender: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
