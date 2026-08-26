#include "geist/detail/internal.hpp"

#include <algorithm>

namespace geist::detail {

bool output_spans_intersect(std::size_t left_begin, std::size_t left_end,
                            std::size_t right_begin, std::size_t right_end) {
  return left_begin < right_end && right_begin < left_end;
}

std::vector<std::size_t> source_tokens_intersecting_output(
    const AssembledLogicalRecord& assembled, std::size_t output_begin,
    std::size_t output_end) {
  std::vector<std::size_t> result;
  if (output_begin >= output_end || output_begin >= assembled.words.size()) {
    return result;
  }
  output_end = std::min(output_end, assembled.words.size());
  for (const auto& token : assembled.tokens) {
    if (output_spans_intersect(output_begin, output_end, token.output_begin,
                               token.output_end)) {
      result.push_back(token.token_index);
    }
  }
  return result;
}

} // namespace geist::detail
