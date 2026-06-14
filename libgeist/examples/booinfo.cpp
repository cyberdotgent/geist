#include "geist/boo.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: booinfo <book.boo>\n";
    return 2;
  }

  try {
    const auto document = geist::BooDocument::open(argv[1]);
    const auto& metadata = document.metadata();

    std::cout << "Path: " << metadata.path.string() << "\n";
    std::cout << "Size: " << metadata.file_size << " bytes\n";
    std::cout << "Leading bytes: "
              << geist::bytes_to_hex(metadata.leading_bytes) << "\n";
  } catch (const std::exception& error) {
    std::cerr << "booinfo: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
