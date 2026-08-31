// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/boo.hpp"

#include <cctype>
#include <exception>
#include <iostream>
#include <string>

namespace {

std::string trim_directory_version(std::string value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  return value;
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: bootoc <book.boo>\n";
    return 2;
  }

  try {
    const auto document = geist::BooDocument::open(argv[1]);
    const auto& metadata = document.metadata();
    const auto& directory = document.directory();
    const auto& book = document.book_properties();
    const auto& toc = document.table_of_contents();
    const auto title = book.short_title.empty() ? book.title : book.short_title;
    const auto build_version = book.build_version.empty()
                                   ? (book.version.empty()
                                          ? trim_directory_version(
                                                directory.version_text)
                                          : book.version)
                                   : book.build_version;

    if (toc.empty()) {
      std::cout << "No table of contents entries parsed yet.\n";
      return 0;
    }

    std::cout << "Title: " << title
              << "\nDocument number: " << book.document_number
              << "\nBuild date: " << directory.date << ' ' << directory.time
              << "\nBuild version: " << build_version
              << "\nPath: " << metadata.path.string() << "\n\n";
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
