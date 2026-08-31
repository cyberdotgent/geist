#include "geist/boo.hpp"

#include <exception>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: booinfo <book.boo>\n";
    return 2;
  }

  try {
    const auto document = geist::BooDocument::open(argv[1]);
    const auto& metadata = document.metadata();
    const auto& file_header = document.file_header();
    const auto& directory = document.directory();
    const auto& book = document.book_properties();

    std::cout << "Path: " << metadata.path.string() << "\n";
    std::cout << "Size: " << metadata.file_size << " bytes\n";
    std::cout << "Pages: " << metadata.page_count << " x "
              << metadata.page_size << " bytes\n";
    std::cout << "Directory page: " << file_header.directory_page_number
              << "\n";
    std::cout << "Version: " << directory.version_text << "\n";
    std::cout << "Timestamp: " << directory.date << " " << directory.time
              << "\n";
    std::cout << "Dictionary run: page " << directory.dictionary_start_page
              << ", count " << directory.dictionary_page_count << "\n";
    std::cout << "Content run: page " << directory.content_start_page
              << ", count " << directory.content_page_count << "\n";
    std::cout << "Token threshold: 0x" << std::hex
              << static_cast<unsigned>(directory.token_threshold) << std::dec
              << "\n";

    std::cout << "Book properties:\n";
    std::cout << "  Language: " << book.language << "\n";
    std::cout << "  Version: " << book.version << "\n";
    if (!book.build_version.empty()) {
      std::cout << "  Build version: " << book.build_version << "\n";
    }
    std::cout << "  Title: " << book.title << "\n";
    std::cout << "  Short title: " << book.short_title << "\n";
    std::cout << "  Copyright: " << book.copyright << "\n";
    std::cout << "  Security: " << book.security << "\n";
    std::cout << "  Date: " << book.date << "\n";
    if (book.authors.empty()) {
      std::cout << "  Authors: none\n";
    } else {
      std::cout << "  Authors: ";
      for (std::size_t i = 0; i < book.authors.size(); ++i) {
        if (i != 0) {
          std::cout << "; ";
        }
        std::cout << book.authors[i];
      }
      std::cout << "\n";
    }
    std::cout << "  Document number: " << book.document_number << "\n";
    std::cout << "  Reflow: " << (book.reflow ? "on" : "off") << "\n";

    if (file_header.unknown_0102) {
      const std::vector<std::uint8_t> unknown_0102{
          file_header.unknown_0102->begin(), file_header.unknown_0102->end()};
      std::cout << "Page-0 0x0102 value: "
                << geist::bytes_to_hex(unknown_0102) << "\n";
    }

    std::cout << "Page runs:\n";
    for (const auto& run : document.page_runs()) {
      std::cout << "  page " << run.start_page << ", count " << run.page_count
                << ", class 0x" << std::hex << run.page_class << std::dec
                << ", role " << geist::to_string(run.role) << "\n";
    }
  } catch (const std::exception& error) {
    std::cerr << "booinfo: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
