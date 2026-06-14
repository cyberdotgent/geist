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
    if (mode == "--raw") {
      std::cout << document.read_topic_raw_markup(argv[2]);
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
