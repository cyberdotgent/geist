#include "geist/document.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: geist_open_benchmark <book.boo> [iterations]\n";
    return 2;
  }
  const auto iterations = argc == 3 ? std::max(1, std::atoi(argv[2])) : 7;
  std::vector<double> milliseconds;
  milliseconds.reserve(static_cast<std::size_t>(iterations));

  for (int iteration = 0; iteration < iterations; ++iteration) {
    const auto begin = std::chrono::steady_clock::now();
    const auto document = geist::BooDocument::open(argv[1]);
    const auto end = std::chrono::steady_clock::now();
    if (document.metadata().page_count == 0) {
      return 1;
    }
    milliseconds.push_back(
        std::chrono::duration<double, std::milli>(end - begin).count());
  }

  std::sort(milliseconds.begin(), milliseconds.end());
  std::cout << std::fixed << std::setprecision(3)
            << "median_open_ms\t" << milliseconds[milliseconds.size() / 2]
            << "\n";
}
