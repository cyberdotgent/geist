#include "geist/boo.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: boorsrc <book.boo> <output-dir>\n";
    return 2;
  }

  try {
    const auto document = geist::BooDocument::open(argv[1]);
    const auto& resources = document.resources();

    if (resources.empty()) {
      std::cout << "No resources parsed yet.\n";
      return 0;
    }

    std::cout << "Resource extraction is not implemented yet. Output directory: "
              << argv[2] << "\n";
  } catch (const std::exception& error) {
    std::cerr << "boorsrc: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
