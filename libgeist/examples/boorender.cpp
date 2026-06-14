#include "geist/boo.hpp"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) {
    std::cerr << "usage: boorender <book.boo> [topic-id] (--raw|--md)\n";
    return 2;
  }

  try {
    const auto has_topic_id = argc == 4;
    const std::string mode = argv[has_topic_id ? 3 : 2];
    const auto document = geist::BooDocument::open(argv[1]);
    if (mode == "--raw") {
      const auto* entry =
          has_topic_id ? document.find_toc_entry(argv[2]) : nullptr;
      if (has_topic_id && entry == nullptr) {
        std::cerr << "boorender: BOO topic id was not found: " << argv[2]
                  << "\n";
        return 1;
      }

      const auto& records = has_topic_id ? entry->raw_records
                                         : document.raw_gml_records();
      for (const auto& record : records) {
        std::cout << record << '\n';
      }
    } else if (mode == "--md") {
      std::cout << "Markdown support is not yet implemented\n";
    } else {
      std::cerr << "usage: boorender <book.boo> [topic-id] (--raw|--md)\n";
      return 2;
    }
  } catch (const std::exception& error) {
    std::cerr << "boorender: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
