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

std::string collapse_spaces(const std::string& value) {
  std::string output;
  output.reserve(value.size());
  bool in_space = false;
  for (const auto ch : value) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      if (!output.empty()) {
        in_space = true;
      }
      continue;
    }
    if (in_space) {
      output.push_back(' ');
      in_space = false;
    }
    output.push_back(ch);
  }
  return output;
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

    std::cout << "CONTENTS \"" << title
              << "\" via IBM BookManager BookServer\n\n";
    std::cout << "Title: " << title
              << " Document Number: " << book.document_number
              << " Build Date: " << directory.date << ' ' << directory.time
              << " Build Version: " << build_version
              << " Book Path: " << metadata.path.string() << "\n\n";
    std::cout << "# CONTENTS Table of Contents\n\n";
    std::cout << "```\n[Summarize]";
    for (const auto& entry : toc) {
      std::cout << ' ' << entry.id << ' ' << collapse_spaces(entry.title);
    }
    std::cout << "\n```\n";
    if (!book.copyright.empty()) {
      std::cout << "\n" << book.copyright << "\n";
    }
  } catch (const std::exception& error) {
    std::cerr << "bootoc: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
