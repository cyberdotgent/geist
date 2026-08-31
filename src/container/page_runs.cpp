#include "geist/detail/core/internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace geist::detail {

BooPageRole classify_run(std::uint32_t start_page,
                         std::uint16_t page_class,
                         const BooDirectory& directory) {
  if (start_page == 0) {
    return BooPageRole::file_header;
  }
  if (start_page == directory.page_number) {
    return BooPageRole::directory;
  }
  if (start_page == physical_page_for_logical(directory,
                                              directory.dictionary_start_page) &&
      page_class == 0x0100) {
    return BooPageRole::dictionary;
  }
  if (start_page == physical_page_for_logical(directory,
                                              directory.content_start_page) &&
      page_class == 0x0000) {
    return BooPageRole::content;
  }
  if (page_class == 0x0001) {
    return BooPageRole::logical_records;
  }
  return BooPageRole::unknown;
}

std::vector<BooPageRun> build_page_runs(const std::vector<std::uint8_t>& bytes,
                                        const BooDirectory& directory) {
  std::vector<BooPageRun> runs;
  const auto page_count = static_cast<std::uint32_t>(bytes.size() /
                                                    boo_page_size);
  if (page_count == 0) {
    return runs;
  }

  std::uint32_t run_start = 0;
  std::uint16_t run_class = read_be16(bytes, 0);

  for (std::uint32_t page = 1; page < page_count; ++page) {
    const auto page_class = read_be16(bytes, page * boo_page_size);
    if (page_class == run_class) {
      continue;
    }

    runs.push_back({run_start,
                    page - run_start,
                    run_class,
                    classify_run(run_start, run_class, directory)});
    run_start = page;
    run_class = page_class;
  }

  runs.push_back({run_start,
                  page_count - run_start,
                  run_class,
                  classify_run(run_start, run_class, directory)});
  return runs;
}

} // namespace geist::detail
