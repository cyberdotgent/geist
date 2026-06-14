#include "geist/boo.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: boorender <book.boo> <chapter-id>\n";
    return 2;
  }

  try {
    const auto document = geist::BooDocument::open(argv[1]);
    std::cout << document.render_chapter_markdown(argv[2]);
  } catch (const std::exception& error) {
    std::cerr << "boorender: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
