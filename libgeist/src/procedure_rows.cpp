#include "geist/detail/procedure_rows.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <limits>
#include <sstream>
#include <tuple>

namespace geist::detail {
namespace {

struct Candidate {
  std::size_t record = 0;
  std::size_t segment = 0;
  unsigned number = 0;
  std::size_t number_begin = 0;
  std::size_t number_length = 0;
  std::size_t origin = 0;
};

struct ParsedStep { unsigned number; std::size_t begin; std::size_t length;
                    std::size_t origin; };

std::optional<ParsedStep> cfont_step(
    const DecodedMarkupSegmentSpan& segment) {
  auto value = segment.text;
  auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos ||
      !ascii_starts_with_case_insensitive(value, first, "cfont")) {
    return std::nullopt;
  }
  auto cursor = first + 5;
  std::vector<std::tuple<std::size_t, std::size_t, std::string>> spans;
  while (true) {
    while (cursor < value.size() &&
           std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
      ++cursor;
    }
    const auto before = cursor;
    char* end = nullptr;
    const auto offset = std::strtoul(value.c_str() + cursor, &end, 10);
    if (end == value.c_str() + cursor) {
      cursor = before;
      break;
    }
    cursor = static_cast<std::size_t>(end - value.c_str());
    while (cursor < value.size() &&
           std::isspace(static_cast<unsigned char>(value[cursor])) != 0) ++cursor;
    const auto length = std::strtoul(value.c_str() + cursor, &end, 10);
    if (end == value.c_str() + cursor) { cursor = before; break; }
    cursor = static_cast<std::size_t>(end - value.c_str());
    while (cursor < value.size() &&
           std::isspace(static_cast<unsigned char>(value[cursor])) != 0) ++cursor;
    const auto code_begin = cursor;
    while (cursor < value.size() &&
           std::isspace(static_cast<unsigned char>(value[cursor])) == 0) ++cursor;
    if (code_begin == cursor) { cursor = before; break; }
    spans.emplace_back(offset, length,
                       value.substr(code_begin, cursor - code_begin));
  }
  if (spans.empty()) return std::nullopt;
  const auto& [offset, length, code] = spans.front();
  if (!ascii_equals_case_insensitive(code, "2") || offset == 0) return std::nullopt;
  while (cursor < value.size() &&
         std::isspace(static_cast<unsigned char>(value[cursor])) != 0) ++cursor;
  if (cursor < value.size() &&
      std::string("()<>/=-:\"").find(value[cursor]) != std::string::npos) {
    ++cursor;
    while (cursor < value.size() &&
           std::isspace(static_cast<unsigned char>(value[cursor])) != 0) ++cursor;
  }
  const auto number_begin = cursor;
  while (cursor < value.size() &&
         std::isdigit(static_cast<unsigned char>(value[cursor])) != 0) ++cursor;
  if (cursor == number_begin || cursor >= value.size() || value[cursor] != '.' ||
      cursor + 1 >= value.size() ||
      std::isspace(static_cast<unsigned char>(value[cursor + 1])) == 0 ||
      length != cursor - number_begin + 1) {
    return std::nullopt;
  }
  const auto number = std::strtoul(value.c_str() + number_begin, nullptr, 10);
  if (number > std::numeric_limits<unsigned>::max()) return std::nullopt;
  return ParsedStep{static_cast<unsigned>(number), number_begin,
                    cursor - number_begin + 1, offset};
}

std::vector<Candidate> candidates(const std::vector<std::string>& records) {
  std::vector<Candidate> found;
  for (std::size_t record = 0; record < records.size(); ++record) {
    const auto segments = split_decoded_markup_segment_spans(records[record]);
    for (std::size_t segment = 0; segment < segments.size(); ++segment) {
      if (const auto step = cfont_step(segments[segment])) {
        const auto raw_begin = segments[segment].output_begin + step->begin;
        if (raw_begin + step->length > records[record].size() ||
            records[record].substr(raw_begin, step->length) !=
                segments[segment].text.substr(step->begin, step->length)) continue;
        found.push_back({record, segment, step->number, raw_begin,
                         step->length, step->origin});
      }
    }
  }
  return found;
}

bool local_barrier(const std::vector<std::string>& records,
                   const Candidate& left, const Candidate& right) {
  for (auto record = left.record; record <= right.record; ++record) {
    const auto spans = split_decoded_markup_segment_spans(records[record]);
    const auto begin = record == left.record ? left.segment + 1 : 0;
    const auto end = record == right.record ? right.segment : spans.size();
    for (auto segment = begin; segment < end; ++segment) {
      const auto lower = ascii_lower(spans[segment].text);
      for (const auto* control : {"st ", "chdlevel", "srmsg", "srgls",
                                  "srtbl", "srfig", "sretbl", "srefig"})
        if (ascii_starts_with_case_insensitive(lower, control)) return true;
    }
  }
  return false;
}

bool source_owned_origin(const Candidate& item,
                         const DecodedLogicalRecordSource& source) {
  if (item.number_begin < item.origin) return false;
  const auto begin = item.number_begin - item.origin;
  if (item.number_begin + item.number_length > source.assembled.words.size() ||
      !std::all_of(source.assembled.words.begin() + begin,
                   source.assembled.words.begin() + item.number_begin,
                   [](auto word) { return word == ' '; })) return false;
  const auto origin_tokens = source_tokens_intersecting_output(
      source.assembled, begin, item.number_begin);
  if (origin_tokens.size() != 1) return false;
  const auto origin_token = origin_tokens.front();
  if (origin_token >= source.tokens.size() ||
      origin_token >= source.encoded_tokens.size() ||
      origin_token >= source.assembled.tokens.size() ||
      source.encoded_tokens[origin_token].width != 1 ||
      source.assembled.tokens[origin_token].output_begin != begin ||
      source.assembled.tokens[origin_token].output_end != item.number_begin ||
      source.tokens[origin_token].size() != item.origin ||
      !std::all_of(source.tokens[origin_token].begin(),
                   source.tokens[origin_token].end(),
                   [](auto word) { return word == ' '; })) return false;
  const auto number_tokens = source_tokens_intersecting_output(
      source.assembled, item.number_begin,
      item.number_begin + item.number_length);
  return !number_tokens.empty() &&
      std::all_of(number_tokens.begin(), number_tokens.end(), [&](auto token) {
        if (token >= source.assembled.tokens.size() ||
            token >= source.encoded_tokens.size() ||
            source.encoded_tokens[token].width != 1) return false;
        const auto& span = source.assembled.tokens[token];
        for (auto output = span.output_begin; output < span.output_end; ++output) {
          const auto inside = output >= item.number_begin &&
              output < item.number_begin + item.number_length;
          if (!inside && source.assembled.words[output] != ' ') return false;
        }
        return true;
      });
}

} // namespace

bool has_numbered_procedure_candidate(
    const std::vector<std::string>& decoded_records) {
  const auto found = candidates(decoded_records);
  for (std::size_t index = 1; index < found.size(); ++index) {
    if (found[index].number == found[index - 1].number + 1 &&
        found[index].origin == found[index - 1].origin &&
        !local_barrier(decoded_records, found[index - 1], found[index]))
      return true;
  }
  return false;
}

std::vector<std::vector<bool>> numbered_procedure_step_segments(
    const std::vector<std::string>& decoded_records,
    const std::vector<DecodedLogicalRecordSource>& sources) {
  std::vector<std::vector<bool>> flags(decoded_records.size());
  for (std::size_t record = 0; record < decoded_records.size(); ++record) {
    flags[record].resize(
        split_decoded_markup_segment_spans(decoded_records[record]).size());
  }
  if (sources.size() != decoded_records.size()) return flags;
  const auto found = candidates(decoded_records);
  for (std::size_t begin = 0; begin < found.size();) {
    auto end = begin + 1;
    while (end < found.size() &&
           found[end].number == found[end - 1].number + 1 &&
           found[end].origin == found[end - 1].origin &&
           !local_barrier(decoded_records, found[end - 1], found[end])) ++end;
    if (end - begin >= 2) {
      const auto proven = std::all_of(found.begin() + begin,
                                      found.begin() + end,
                                      [&](const auto& item) {
        return source_owned_origin(item, sources[item.record]);
      });
      if (proven) {
        for (auto index = begin; index < end; ++index) {
          const auto& item = found[index];
          flags[item.record][item.segment] = true;
        }
      }
    }
    begin = end;
  }
  return flags;
}

} // namespace geist::detail
