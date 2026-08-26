#include "geist/detail/internal.hpp"

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

bool looks_like_toc_entry_boundary(const std::string& lower_record,
                                   std::size_t offset) {
  static const std::array<const char*, 5> boundaries = {
      "?ctoce ", ", ctoce ", "?ctocdef=", ", ctocdef=", "?sh"};
  for (const auto* boundary : boundaries) {
    const std::string boundary_text(boundary);
    if (offset + boundary_text.size() <= lower_record.size() &&
        lower_record.compare(offset, boundary_text.size(), boundary_text) ==
            0) {
      return true;
    }
  }
  return false;
}

std::size_t find_toc_end_marker(const std::string& lower_record,
                                std::size_t offset,
                                std::size_t limit) {
  std::size_t end_marker = std::string::npos;
  const auto cz_off_etoc = lower_record.find("cz off etoc", offset);
  if (cz_off_etoc != std::string::npos && cz_off_etoc < limit) {
    end_marker = std::min(end_marker, cz_off_etoc);
  }
  const auto etoc = lower_record.find("etoc", offset);
  if (etoc != std::string::npos && etoc < limit) {
    end_marker = std::min(end_marker, etoc);
  }
  return end_marker;
}

std::vector<TocEntry> extract_toc_entries(const std::string& decoded_record) {
  std::vector<TocEntry> entries;
  const auto lower_record = ascii_lower(decoded_record);
  std::size_t search_offset = 0;

  while (search_offset < decoded_record.size()) {
    const auto found = lower_record.find("ctoce ", search_offset);
    if (found == std::string::npos) {
      break;
    }
    const auto record_toc_end =
        find_toc_end_marker(lower_record, search_offset, found + 1);
    if (record_toc_end != std::string::npos && record_toc_end < found) {
      break;
    }

    const auto marker_size = std::string("ctoce ").size();
    auto value_begin = found + marker_size;
    auto value_end = decoded_record.size();
    const auto next_entry = lower_record.find("ctoce ", value_begin);
    const auto toc_end =
        find_toc_end_marker(lower_record, value_begin, value_end);
    if (toc_end != std::string::npos &&
        (next_entry == std::string::npos || toc_end < next_entry)) {
      value_end = toc_end;
    }
    if (next_entry != std::string::npos) {
      value_end = std::min(value_end, next_entry);
    }
    for (auto cursor = value_begin; cursor < value_end; ++cursor) {
      if (looks_like_toc_entry_boundary(lower_record, cursor)) {
        value_end = cursor;
        break;
      }
    }

    const auto value =
        trim_ascii(decoded_record.substr(value_begin, value_end - value_begin));
    std::istringstream input(value);
    std::uint32_t level = 0;
    std::uint32_t style = 0;
    std::string id;
    if (input >> level >> style >> id) {
      if (style == 0) {
        search_offset = found + marker_size;
        continue;
      }
      std::string title;
      std::getline(input, title);
      title = normalize_toc_title(trim_ascii(title));
      if (!id.empty() && !title.empty()) {
        TocEntry entry;
        entry.id = normalize_toc_id(id);
        entry.title = std::move(title);
        entry.level = level;
        entry.style = style;
        entries.push_back(std::move(entry));
      }
    }

    search_offset = found + marker_size;
  }

  return entries;
}

bool is_contents_topic_record(const std::string& decoded_record) {
  const auto lower_record = ascii_lower(decoded_record);
  return lower_record.find("shcontents") != std::string::npos ||
         lower_record.find("chdlevel :toc") != std::string::npos ||
         lower_record.find("ctocdef=") != std::string::npos;
}

bool is_topic_header_record(const std::string& decoded_record) {
  // The topic header and its metadata are normally assembled into one
  // logical record.  Some books store the header (for example `sh2.6`) as
  // its own record and put CTopicN/CHdLevel/ST in the following record.
  // The SH topic id is the boundary, so do not require metadata to be in the
  // same record.
  if (extract_topic_header_id(decoded_record).empty()) {
    return false;
  }
  const auto lower_record = ascii_lower(decoded_record);
  if (lower_record.find("ctopicn") != std::string::npos) {
    return true;
  }

  // Without same-record metadata, accept only a standalone SH<id> boundary.
  // This excludes ordinary prose records beginning with words such as SHOULD
  // or SHIPPED from the topic index.
  const auto start = skip_decoded_separators(decoded_record);
  auto cursor = start + 2;
  while (cursor < decoded_record.size()) {
    const auto ch = decoded_record[cursor];
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte) == 0 && ch != '.' && ch != '_' && ch != '-') {
      break;
    }
    ++cursor;
  }
  return cursor > start + 2 &&
         skip_decoded_separators(decoded_record.substr(cursor)) ==
             decoded_record.size() - cursor;
}

std::string raw_gml_tag(const std::string& record) {
  if (record.empty() || record.front() != ':') {
    return {};
  }
  std::size_t cursor = 1;
  while (cursor < record.size() &&
         std::isalnum(static_cast<unsigned char>(record[cursor])) != 0) {
    ++cursor;
  }
  return ascii_lower(record.substr(1, cursor - 1));
}

bool is_topic_title_record(const std::string& record) {
  const auto tag = raw_gml_tag(record);
  static const std::set<std::string> title_tags = {
      "h1", "h2", "h3", "h4", "h5", "ih2", "preface", "appendix",
      "glossary"};
  return title_tags.find(tag) != title_tags.end();
}

std::string raw_gml_content_preserve_space(const std::string& record) {
  const auto dot = record.find('.');
  if (dot == std::string::npos) {
    return {};
  }
  return record.substr(dot + 1);
}

std::size_t find_decoded_control(const std::string& record,
                                 const std::string& lower_record,
                                 const std::string& control) {
  auto search = std::size_t{0};
  while (search < lower_record.size()) {
    const auto found = lower_record.find(control, search);
    if (found == std::string::npos) {
      return std::string::npos;
    }
    auto separator = found;
    while (separator > 0 &&
           std::isspace(static_cast<unsigned char>(record[separator - 1])) !=
               0) {
      --separator;
    }
    if (separator == 0 || record[separator - 1] == '?' ||
        record[separator - 1] == ',') {
      return found;
    }
    if (control == "st ") {
      const auto source = lower_record.rfind("csourcefn", found);
      const auto comma = lower_record.rfind(',', found);
      const auto question = lower_record.rfind('?', found);
      const auto previous_separator = std::max(
          comma == std::string::npos ? std::size_t{0} : comma,
          question == std::string::npos ? std::size_t{0} : question);
      if (source != std::string::npos && source >= previous_separator &&
          found > 0 &&
          std::isspace(static_cast<unsigned char>(record[found - 1])) != 0) {
        return found;
      }
    }
    search = found + 1;
  }
  return std::string::npos;
}

std::size_t find_st_control(const std::string& record,
                            const std::string& lower_record) {
  auto search = std::size_t{0};
  while (search < lower_record.size()) {
    const auto found = lower_record.find("st", search);
    if (found == std::string::npos) {
      return std::string::npos;
    }

    auto separator = found;
    while (separator > 0 &&
           std::isspace(static_cast<unsigned char>(record[separator - 1])) !=
               0) {
      --separator;
    }
    const auto has_boundary_before =
        separator == 0 || record[separator - 1] == '?' ||
        record[separator - 1] == ',';

    const auto next = found + 2;
    const auto has_boundary_after =
        next >= record.size() ||
        std::isspace(static_cast<unsigned char>(record[next])) != 0 ||
        record[next] == '|';
    if (has_boundary_before && has_boundary_after) {
      return found;
    }

    const auto source = lower_record.rfind("csourcefn", found);
    const auto comma = lower_record.rfind(',', found);
    const auto question = lower_record.rfind('?', found);
    const auto previous_separator = std::max(
        comma == std::string::npos ? std::size_t{0} : comma,
        question == std::string::npos ? std::size_t{0} : question);
    if (source != std::string::npos && source >= previous_separator &&
        found > 0 &&
        std::isspace(static_cast<unsigned char>(record[found - 1])) != 0 &&
        has_boundary_after) {
      return found;
    }

    search = found + 1;
  }
  return std::string::npos;
}

std::size_t st_value_begin(const std::string& record, std::size_t st_found) {
  auto cursor = st_found + 2;
  while (cursor < record.size() &&
         std::isspace(static_cast<unsigned char>(record[cursor])) != 0) {
    ++cursor;
  }
  if (cursor < record.size() && record[cursor] == '|') {
    ++cursor;
    while (cursor < record.size() &&
           std::isspace(static_cast<unsigned char>(record[cursor])) != 0) {
      ++cursor;
    }
  }
  return cursor;
}

bool paragraph_punctuation(char ch) {
  return ch == '.' || ch == ':' || ch == ';';
}

bool line_ends_paragraph(const std::string& line) {
  auto trimmed = trim_ascii(line);
  return !trimmed.empty() && paragraph_punctuation(trimmed.back());
}

std::string strip_leading_visual_bar(std::string line) {
  line = trim_ascii(std::move(line));
  if (!line.empty() && line.front() == '|') {
    line.erase(line.begin());
    line = trim_ascii(std::move(line));
  }
  if (!line.empty() && line.back() == '|') {
    line.pop_back();
    line = trim_ascii(std::move(line));
  }
  return line;
}

std::string normalize_message_catalog_intro(std::string value) {
  const auto lower = ascii_lower(value);
  if (const auto srmsg = lower.find("srmsg "); srmsg != std::string::npos) {
    value.resize(srmsg);
  }
  std::string output;
  output.reserve(value.size());
  for (std::size_t cursor = 0; cursor < value.size();) {
    if (value[cursor] == '\x1E') {
      output.push_back(' ');
      ++cursor;
      continue;
    }
    if (value[cursor] == '?') {
      while (cursor < value.size() && value[cursor] == '?') {
        ++cursor;
      }
      output.push_back(' ');
      continue;
    }
    const auto is_visual_marker = [](char ch) {
      return ch == '(' || ch == ')' || ch == '<' || ch == '>' || ch == '-' ||
             ch == '/' || ch == ':' || ch == '=' || ch == '!';
    };
    if (is_visual_marker(value[cursor])) {
      auto padding = cursor + 1;
      while (padding < value.size() &&
             std::isspace(static_cast<unsigned char>(value[padding])) != 0) {
        ++padding;
      }
      if (padding - cursor >= 3) {
        output.push_back(' ');
        cursor = padding;
        continue;
      }
    }
    output.push_back(value[cursor++]);
  }
  auto normalized = collapse_ascii_whitespace(std::move(output));
  while (!normalized.empty() &&
         (normalized.back() == ')' || normalized.back() == '(' ||
          normalized.back() == '/' || normalized.back() == '<' ||
          normalized.back() == '>')) {
    normalized.pop_back();
    normalized = trim_ascii(std::move(normalized));
  }
  if (ascii_starts_with_case_insensitive(normalized, "cfont ")) {
    normalized = trim_ascii(normalized.substr(5));
  }
  return normalized;
}

std::string clean_fixed_st_row_markers(std::string value) {
  static constexpr std::array<std::string_view, 9> kAlphaMarkers = {
      "action", "address", "adapter", "agent", "are", "can", "an", "as",
      "a"};
  for (auto gap = std::size_t{0}; gap < value.size();) {
    gap = value.find("    ", gap);
    if (gap == std::string::npos) {
      break;
    }
    if (gap > 0 &&
        (value[gap - 1] == '(' || value[gap - 1] == ')' ||
         value[gap - 1] == '<' || value[gap - 1] == '>' ||
         value[gap - 1] == '-' || value[gap - 1] == '/' ||
         value[gap - 1] == ':' || value[gap - 1] == '=' ||
         value[gap - 1] == '"')) {
      value.erase(gap - 1, 1);
      --gap;
    } else {
      auto token_begin = gap;
      while (token_begin > 0 &&
             std::isalpha(static_cast<unsigned char>(value[token_begin - 1])) !=
                 0) {
        --token_begin;
      }
      const auto token = ascii_lower(value.substr(token_begin,
                                                  gap - token_begin));
      const auto recognized =
          std::find(kAlphaMarkers.begin(), kAlphaMarkers.end(), token) !=
          kAlphaMarkers.end();
      const auto attached_to_punctuation =
          token_begin > 0 &&
          (value[token_begin - 1] == '.' || value[token_begin - 1] == ')' ||
           value[token_begin - 1] == ':');
      if (recognized && attached_to_punctuation) {
        value.erase(token_begin, gap - token_begin);
        gap = token_begin;
      }
    }
    gap += 4;
  }
  return value;
}

std::string clean_fixed_rendered_line(std::string value) {
  // Alphabetic overflow values are marker-field contents, not magic words.
  // Identify the field from its four-column padding and its row-boundary
  // position so newly observed values require no vocabulary update.
  for (auto gap = std::size_t{0}; gap + 4 <= value.size();) {
    gap = value.find("    ", gap);
    if (gap == std::string::npos) {
      break;
    }
    auto token_begin = gap;
    while (token_begin > 0 &&
           std::isalpha(static_cast<unsigned char>(value[token_begin - 1])) !=
               0) {
      --token_begin;
    }
    auto origin = token_begin;
    while (origin > 0 &&
           std::isspace(static_cast<unsigned char>(value[origin - 1])) != 0) {
      --origin;
    }
    const auto after_terminal =
        token_begin > 0 &&
        (value[token_begin - 1] == ')' || value[token_begin - 1] == ':' ||
         value[token_begin - 1] == ';' || value[token_begin - 1] == '.' ||
         std::isdigit(static_cast<unsigned char>(value[token_begin - 1])) != 0);
    if (token_begin != gap && (origin == 0 || after_terminal)) {
      value.replace(token_begin, gap - token_begin, gap - token_begin, ' ');
    }
    gap += 4;
  }
  value = trim_ascii(std::move(value));
  for (auto close = value.find(')'); close != std::string::npos;
       close = value.find(')', close + 1)) {
    auto marker_end = close + 1;
    if (marker_end >= value.size()) {
      continue;
    }
    if (value[marker_end] == '<' || value[marker_end] == '>' ||
        value[marker_end] == '/' || value[marker_end] == '"' ||
        value[marker_end] == '=') {
      ++marker_end;
    } else {
      while (marker_end < value.size() &&
             std::isalpha(static_cast<unsigned char>(value[marker_end])) != 0) {
        ++marker_end;
      }
    }
    if (marker_end > close + 1 &&
        (marker_end == value.size() ||
         std::isspace(static_cast<unsigned char>(value[marker_end])) != 0)) {
      value.erase(close + 1, marker_end - close - 1);
    }
  }
  while (!value.empty() &&
         (value.back() == '<' || value.back() == '>' || value.back() == '/' ||
          value.back() == '"' || value.back() == '=')) {
    value.pop_back();
    value = trim_ascii(std::move(value));
  }
  // A terminal alphabetic marker can lack the following padding when the
  // physical row ends at the record boundary.  Bibliographic rows provide an
  // unambiguous terminal `)` coordinate for this case.
  const auto close = value.rfind(')');
  if (close != std::string::npos && close + 1 < value.size() &&
      std::all_of(value.begin() + static_cast<std::ptrdiff_t>(close + 1),
                  value.end(), [](const auto ch) {
                    return std::isalpha(static_cast<unsigned char>(ch)) != 0;
                  })) {
    value.resize(close + 1);
  }
  return value;
}

bool looks_like_publication_catalog_row(const std::string& value) {
  const auto lower = ascii_lower(value);
  for (const auto* marker : {"(sc", "(gc", "(ga", "(sg", "(sh", "(sa",
                             "(sx", "(zz", "(isbn"}) {
    if (lower.find(marker) != std::string::npos) {
      return true;
    }
  }
  return false;
}

struct PublicationBlock {
  std::string title;
  std::string description;
};

std::optional<PublicationBlock> start_publication_block(std::string value) {
  const auto lower = ascii_lower(value);
  auto identifier = std::string::npos;
  for (const auto* marker : {"(sc", "(gc", "(ga", "(sg", "(sh", "(sa",
                             "(sx", "(zz", "(isbn"}) {
    const auto found = lower.find(marker);
    if (found != std::string::npos) {
      identifier = std::min(identifier, found);
    }
  }
  if (identifier == std::string::npos) {
    return std::nullopt;
  }
  const auto close = value.find(')', identifier + 1);
  if (close == std::string::npos) {
    return std::nullopt;
  }

  PublicationBlock block;
  block.title = clean_fixed_rendered_line(value.substr(0, close + 1));
  auto trailing = value.substr(close + 1);
  const auto first = trailing.find_first_not_of(" \t\r\n");
  if (first != std::string::npos && first == 0 &&
      std::string("()-<>/:=\"").find(trailing[first]) != std::string::npos &&
      (first + 1 == trailing.size() ||
       std::isspace(static_cast<unsigned char>(trailing[first + 1])) != 0)) {
    auto content = first + 1;
    while (content < trailing.size() &&
           std::isspace(static_cast<unsigned char>(trailing[content])) != 0) {
      ++content;
    }
    trailing.erase(first, content - first);
  }
  block.description = clean_fixed_rendered_line(
      clean_fixed_st_row_markers(std::move(trailing)));
  return block;
}

static bool contains_srmsg_control(const std::string& value) {
  const auto lower = ascii_lower(value);
  auto search = std::size_t{0};
  while (search < lower.size()) {
    const auto found = lower.find("srmsg ", search);
    if (found == std::string::npos) {
      return false;
    }
    if (found == 0 ||
        std::isalnum(static_cast<unsigned char>(lower[found - 1])) == 0) {
      return true;
    }
    search = found + 1;
  }
  return false;
}

bool raw_record_duplicates_st_body(const std::string& record,
                                   const std::string& body_text) {
  const auto normalize_for_duplicate_check = [](std::string value) {
    value = strip_leading_visual_bar(std::move(value));
    for (auto& ch : value) {
      if (ch == '|') {
        ch = ' ';
      }
    }
    return collapse_ascii_whitespace(std::move(value));
  };
  auto record_text = normalize_for_duplicate_check(
      raw_gml_content_preserve_space(record));
  auto body = normalize_for_duplicate_check(body_text);
  if (record_text.empty() || body.empty()) {
    return false;
  }
  constexpr auto kMinimumDuplicatePrefix = std::size_t{24};
  if (record_text.size() < kMinimumDuplicatePrefix) {
    return false;
  }
  // Inline styling and physical row joins can diverge shortly after the
  // opening clause.  A 40-character normalized prefix is long enough to
  // establish ownership while remaining before those presentation changes.
  constexpr auto kDuplicateProbeLength = std::size_t{40};
  const auto probe =
      record_text.substr(0, std::min(record_text.size(),
                                     kDuplicateProbeLength));
  return ascii_lower(body).find(ascii_lower(probe)) != std::string::npos;
}

bool contains_wide_space_run(const std::string& value) {
  auto spaces = std::size_t{0};
  for (const auto ch : value) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      ++spaces;
      if (spaces >= 8) {
        return true;
      }
    } else {
      spaces = 0;
    }
  }
  return false;
}

bool is_fixed_st_row_marker(char ch) {
  switch (ch) {
  case '(':
  case ')':
  case '-':
  case '<':
  case '>':
  case '/':
  case ':':
  case '=':
    return true;
  default:
    return false;
  }
}

bool fixed_st_row_marker_at(const std::string& value, std::size_t cursor) {
  const auto attached_overflow_marker =
      value[cursor] == '(' || value[cursor] == ')' || value[cursor] == '<' ||
      value[cursor] == '>' || value[cursor] == '/' || value[cursor] == '=';
  return cursor + 2 < value.size() && is_fixed_st_row_marker(value[cursor]) &&
         (cursor == 0 ||
          std::isalnum(static_cast<unsigned char>(value[cursor - 1])) == 0 ||
          attached_overflow_marker) &&
         std::isspace(static_cast<unsigned char>(value[cursor + 1])) != 0 &&
         std::isspace(static_cast<unsigned char>(value[cursor + 2])) != 0;
}

std::size_t fixed_st_alpha_row_marker_length_at(const std::string& value,
                                                std::size_t cursor) {
  if (cursor == 0 || cursor >= value.size() ||
      (value[cursor - 1] != '.' && value[cursor - 1] != ')' &&
       value[cursor - 1] != ':')) {
    return 0;
  }
  auto end = cursor;
  while (end < value.size() &&
         std::isalpha(static_cast<unsigned char>(value[end])) != 0) {
    ++end;
  }
  if (end == cursor || end + 3 >= value.size() || value.compare(end, 4, "    ") != 0) {
    return 0;
  }
  static constexpr std::array<std::string_view, 9> kAlphaMarkers = {
      "action", "address", "adapter", "agent", "are", "can", "an", "as",
      "a"};
  const auto token = std::string_view(value).substr(cursor, end - cursor);
  return std::find(kAlphaMarkers.begin(), kAlphaMarkers.end(), token) !=
                 kAlphaMarkers.end()
             ? end - cursor
             : 0;
}

bool has_reflow_off_line_markers(const std::string& value) {
  auto spaces = std::size_t{0};
  for (const auto ch : value) {
    if (ch == '?') {
      return true;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      ++spaces;
      if (spaces >= 8) {
        return true;
      }
    } else {
      spaces = 0;
    }
  }
  return false;
}

constexpr char kSyntheticRecordBoundary = '\x1E';

std::vector<std::string> split_reflow_off_body_lines(std::string value) {
  value = trim_ascii(std::move(value));
  std::vector<std::string> lines;
  std::string line;
  auto pending_simple_list = false;
  auto in_simple_list = false;
  auto reached_inline_control = false;

  const auto flush_line = [&](bool paragraph_break) {
    line = strip_leading_visual_bar(std::move(line));
    if (ascii_starts_with_case_insensitive(trim_ascii(line), "cfont ")) {
      line.clear();
      reached_inline_control = true;
      return;
    }
    if (!line.empty()) {
      if (in_simple_list && line.rfind("\xC2\xB0", 0) != 0) {
        line = "\xC2\xB0 " + line;
      }
      lines.push_back("   " + line);
      pending_simple_list = !line.empty() && line.back() == ':';
      if (paragraph_break) {
        lines.emplace_back();
      }
    } else if (paragraph_break) {
      pending_simple_list = false;
    }
    line.clear();
  };

  for (std::size_t cursor = 0;
       cursor < value.size() && !reached_inline_control;) {
    const auto alpha_marker_length =
        fixed_st_alpha_row_marker_length_at(value, cursor);
    if (alpha_marker_length != 0) {
      flush_line(false);
      cursor += alpha_marker_length;
      while (cursor < value.size() &&
             std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
        ++cursor;
      }
      continue;
    }
    if (fixed_st_row_marker_at(value, cursor)) {
      flush_line(false);
      ++cursor;
      while (cursor < value.size() &&
             std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
        ++cursor;
      }
      continue;
    }
    if (value[cursor] == kSyntheticRecordBoundary) {
      flush_line(in_simple_list);
      pending_simple_list = false;
      in_simple_list = false;
      ++cursor;
      continue;
    }

    if (value[cursor] == '?') {
      const auto run_begin = cursor;
      while (cursor < value.size() && value[cursor] == '?') {
        ++cursor;
      }
      const auto question_run = cursor - run_begin;
      auto skipped_spaces = std::size_t{0};
      while (cursor < value.size() &&
             std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
        ++skipped_spaces;
        ++cursor;
      }
      const auto next_begin = cursor;
      const auto next_end = value.find('?', next_begin);
      const auto next_segment =
          value.substr(next_begin,
                       next_end == std::string::npos
                           ? std::string::npos
                           : next_end - next_begin);
      if (question_run == 1 && skipped_spaces >= 1 &&
          strip_leading_visual_bar(line).empty() && !next_segment.empty()) {
        if (pending_simple_list || in_simple_list) {
          in_simple_list = true;
          line = "| \xC2\xB0 ";
        } else {
          line = "| ";
        }
        continue;
      }
      const auto paragraph_break =
          question_run < 8 &&
          (line_ends_paragraph(line) ||
           (line.size() < 40 && contains_wide_space_run(line) &&
            !contains_wide_space_run(next_segment)));
      flush_line(paragraph_break);
      continue;
    }

    if (std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
      auto spaces = std::size_t{0};
      while (cursor < value.size() &&
             std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
        ++spaces;
        ++cursor;
      }
      if (spaces >= 5 && line_ends_paragraph(line)) {
        flush_line(true);
      } else if (spaces >= 5 && line.size() >= 40) {
        flush_line(false);
      } else if (!line.empty() && line.back() != ' ') {
        line.append(spaces, ' ');
      }
      continue;
    }

    line.push_back(value[cursor++]);
  }
  flush_line(false);

  while (!lines.empty() && lines.back().empty()) {
    lines.pop_back();
  }
  return lines;
}

std::size_t topic_body_control_offset(const std::string& record,
                                      std::size_t value_begin,
                                      std::string& first_following_control) {
  const auto lower_record = ascii_lower(record);
  auto value_end = record.size();
  static const std::array<const char*, 12> following_controls = {
      "cselect", "cfont", "cmenu", "cmitem", "cemenu", "srtbl",
      "sretbl",  "srfig", "srefig", "srmsg", "cz",    "si"};
  for (const auto* control : following_controls) {
    auto search = value_begin;
    while (search < lower_record.size()) {
      const auto found = lower_record.find(control, search);
      if (found == std::string::npos) {
        break;
      }
      if (!looks_like_gml_control_at(record, found)) {
        search = found + 1;
        continue;
      }

      auto separator_end = found;
      while (separator_end > value_begin &&
             std::isspace(
                 static_cast<unsigned char>(record[separator_end - 1])) != 0) {
        --separator_end;
      }
      auto separator_begin = separator_end;
      while (separator_begin > value_begin &&
             (record[separator_begin - 1] == '?' ||
              record[separator_begin - 1] == ',' ||
              std::isspace(static_cast<unsigned char>(
                  record[separator_begin - 1])) != 0)) {
        --separator_begin;
      }
      if (separator_begin < separator_end &&
          separator_end <= record.size() &&
          (record[separator_end - 1] == '?' ||
           record[separator_end - 1] == ',')) {
        if (separator_begin < value_end) {
          value_end = separator_begin;
          first_following_control = control;
        }
        break;
      }
      search = found + 1;
    }
  }
  return value_end;
}

std::string preserve_reflow_off_st_body_lines(std::string value) {
  value = trim_ascii(std::move(value));
  std::vector<std::string> lines;
  std::string line;

  const auto flush_line = [&]() {
    line = strip_leading_visual_bar(std::move(line));
    if (!line.empty()) {
      lines.push_back(std::move(line));
    }
    line.clear();
  };

  for (std::size_t cursor = 0; cursor < value.size();) {
    if (value[cursor] == '?') {
      auto spaces = std::size_t{0};
      auto lookahead = cursor + 1;
      while (lookahead < value.size() &&
             std::isspace(static_cast<unsigned char>(value[lookahead])) != 0) {
        ++spaces;
        ++lookahead;
      }
      if (spaces >= 2) {
        flush_line();
        cursor = lookahead;
        continue;
      }
      if (!line.empty() && line.back() != ' ') {
        line.push_back(' ');
      }
      ++cursor;
      continue;
    }

    if (std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
      auto spaces = std::size_t{0};
      while (cursor < value.size() &&
             std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
        ++spaces;
        ++cursor;
      }
      if (spaces >= 8) {
        flush_line();
      } else if (!line.empty() && line.back() != ' ') {
        line.push_back(' ');
      }
      continue;
    }

    line.push_back(value[cursor++]);
  }
  flush_line();

  std::string output;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    if (index != 0) {
      output += "<br>\n";
    }
    output += lines[index];
  }
  return output;
}

std::optional<std::size_t> st_body_begin_after_title(
    const std::string& st_value,
    const std::string& title) {
  if (st_value.size() <= title.size() ||
      !ascii_starts_with_case_insensitive(st_value, title)) {
    return std::nullopt;
  }

  auto cursor = title.size();
  const auto is_legacy_title_marker = [](char ch) {
    return ch == '-' || ch == '>' || ch == '/' || ch == '<' || ch == '(';
  };
  const auto has_following_space =
      cursor + 1 < st_value.size() &&
      std::isspace(static_cast<unsigned char>(st_value[cursor + 1])) != 0;
  const auto has_fixed_padding =
      cursor + 2 < st_value.size() && has_following_space &&
      std::isspace(static_cast<unsigned char>(st_value[cursor + 2])) != 0;
  if (cursor < st_value.size() &&
      ((is_legacy_title_marker(st_value[cursor]) && has_following_space) ||
       (is_fixed_st_row_marker(st_value[cursor]) && has_fixed_padding))) {
    ++cursor;
  } else if (cursor < st_value.size() &&
             std::isspace(static_cast<unsigned char>(st_value[cursor])) == 0) {
    return std::nullopt;
  }
  while (cursor < st_value.size() &&
         std::isspace(static_cast<unsigned char>(st_value[cursor])) != 0) {
    ++cursor;
  }

  // Fixed-layout structural topics may use a standalone dot between the
  // display title and body. It is a delimiter, not visible body text.
  if (cursor < st_value.size() && st_value[cursor] == '.' &&
      (cursor + 1 == st_value.size() ||
       std::isspace(static_cast<unsigned char>(st_value[cursor + 1])) != 0)) {
    ++cursor;
    while (cursor < st_value.size() &&
           std::isspace(static_cast<unsigned char>(st_value[cursor])) != 0) {
      ++cursor;
    }
  }
  return cursor;
}

std::vector<std::string> render_st_form_items(const std::string& body) {
  const auto find_delimiter = [&](std::size_t search) {
    for (auto found = body.find("__", search); found != std::string::npos;
         found = body.find("__", found + 2)) {
      const auto separated_before =
          found == 0 ||
          std::isspace(static_cast<unsigned char>(body[found - 1])) != 0 ||
          body[found - 1] == '?';
      const auto separated_after =
          found + 2 == body.size() ||
          std::isspace(static_cast<unsigned char>(body[found + 2])) != 0 ||
          body[found + 2] == '?';
      if (separated_before && separated_after) {
        return found;
      }
    }
    return std::string::npos;
  };

  std::vector<std::string> records;
  auto cursor = find_delimiter(0);
  auto prefix = cursor == std::string::npos
                    ? std::string{}
                    : normalize_message_catalog_intro(
                          clean_fixed_st_row_markers(body.substr(0, cursor)));
  while (cursor != std::string::npos) {
    const auto begin = cursor + 2;
    const auto next = find_delimiter(begin);
    auto item = collapse_ascii_whitespace(body.substr(
        begin, next == std::string::npos ? std::string::npos : next - begin));
    while (!item.empty() &&
           (item.front() == '-' || item.front() == '/' ||
            item.front() == '<' || item.front() == '>' ||
            item.front() == '(')) {
      item.erase(item.begin());
      item = trim_ascii(std::move(item));
    }
    while (!item.empty() &&
           (item.back() == '-' || item.back() == '/' || item.back() == '<' ||
            item.back() == '>' || item.back() == '(')) {
      item.pop_back();
      item = trim_ascii(std::move(item));
    }
    for (auto marker = item.find(" < "); marker != std::string::npos;
         marker = item.find(" < ", marker)) {
      item.replace(marker, 3, " ");
    }
    for (auto marker = item.find("( Number of ");
         marker != std::string::npos;
         marker = item.find("( Number of ", marker)) {
      item.replace(marker, 2, " ");
    }

    std::vector<std::string> item_parts;
    auto part_begin = std::size_t{0};
    auto part_end = item.find(" Number of ");
    while (part_end != std::string::npos) {
      item_parts.push_back(trim_ascii(item.substr(part_begin,
                                                  part_end - part_begin)));
      part_begin = part_end + 1;
      part_end = item.find(" Number of ", part_begin);
    }
    item_parts.push_back(trim_ascii(item.substr(part_begin)));
    for (auto& part : item_parts) {
      while (!part.empty() &&
             (part.back() == '-' || part.back() == '/' ||
              part.back() == '<' || part.back() == '>' ||
              part.back() == '(')) {
        part.pop_back();
        part = trim_ascii(std::move(part));
      }
      if (!part.empty()) {
        records.push_back(":li." + std::move(part));
      }
    }
    cursor = next;
  }
  if (records.empty()) {
    return records;
  }
  records.insert(records.begin(), ":ul type='form'.");
  records.push_back(":eul.");
  if (!prefix.empty()) {
    records.insert(records.begin(), ":p." + std::move(prefix));
  }
  return records;
}

std::string topic_st_body_after_toc_title(const TopicData& topic,
                                          const std::string& title) {
  if (topic.raw_records.empty() || title.empty()) {
    return {};
  }

  const auto& record = topic.raw_records.front();
  const auto lower_record = ascii_lower(record);
  const auto st_found = find_st_control(record, lower_record);
  if (st_found == std::string::npos) {
    return {};
  }

  const auto value_begin = st_value_begin(record, st_found);
  std::string first_following_control;
  const auto value_end =
      topic_body_control_offset(record, value_begin, first_following_control);

  auto st_value = record.substr(value_begin, value_end - value_begin);
  if (st_value.empty()) {
    return {};
  }
  st_value = trim_ascii(std::move(st_value));
  const auto body_begin = st_body_begin_after_title(st_value, title);
  if (!body_begin) {
    return {};
  }

  auto body = trim_ascii(st_value.substr(*body_begin));
  if (first_following_control != "cselect" || body.empty() ||
      body.back() != ':') {
    return {};
  }

  return preserve_reflow_off_st_body_lines(std::move(body));
}

std::string topic_st_body_text_after_toc_title(const TopicData& topic,
                                               const std::string& title) {
  if (topic.raw_records.empty() || title.empty()) {
    return {};
  }

  const auto& first_record = topic.raw_records.front();
  const auto lower_record = ascii_lower(first_record);
  const auto st_found = find_st_control(first_record, lower_record);
  if (st_found == std::string::npos) {
    return {};
  }

  const auto value_begin = st_value_begin(first_record, st_found);
  std::string first_following_control;
  const auto value_end =
      topic_body_control_offset(first_record, value_begin,
                                first_following_control);
  auto st_value = trim_ascii(first_record.substr(value_begin,
                                                 value_end - value_begin));
  const auto body_begin = st_body_begin_after_title(st_value, title);
  if (!body_begin) {
    return {};
  }

  auto body = trim_ascii(st_value.substr(*body_begin));
  if (!first_following_control.empty()) {
    return body;
  }

  for (std::size_t index = 1; index < topic.raw_records.size(); ++index) {
    std::string following_control;
    const auto end =
        topic_body_control_offset(topic.raw_records[index], 0,
                                  following_control);
    auto text = trim_ascii(topic.raw_records[index].substr(0, end));
    if (!text.empty()) {
      if (!body.empty()) {
        body.push_back(kSyntheticRecordBoundary);
      }
      body += std::move(text);
    }
    if (!following_control.empty()) {
      break;
    }
  }
  return body;
}

std::string topic_st_body_following_control_after_toc_title(
    const TopicData& topic,
    const std::string& title) {
  if (topic.raw_records.empty() || title.empty()) {
    return {};
  }

  const auto& first_record = topic.raw_records.front();
  const auto lower_record = ascii_lower(first_record);
  const auto st_found = find_st_control(first_record, lower_record);
  if (st_found == std::string::npos) {
    return {};
  }

  const auto value_begin = st_value_begin(first_record, st_found);
  std::string following_control;
  const auto value_end =
      topic_body_control_offset(first_record, value_begin,
                                following_control);
  auto st_value = trim_ascii(first_record.substr(value_begin,
                                                 value_end - value_begin));
  if (!st_body_begin_after_title(st_value, title)) {
    return {};
  }
  if (!following_control.empty()) {
    return following_control;
  }

  for (std::size_t index = 1; index < topic.raw_records.size(); ++index) {
    std::string record_following_control;
    (void)topic_body_control_offset(topic.raw_records[index], 0,
                                    record_following_control);
    if (!record_following_control.empty()) {
      return record_following_control;
    }
  }
  return {};
}

std::string topic_st_following_control_after_toc_title(
    const TopicData& topic,
    const std::string& title) {
  if (topic.raw_records.empty() || title.empty()) {
    return {};
  }
  const auto& first_record = topic.raw_records.front();
  const auto lower_record = ascii_lower(first_record);
  const auto st_found = find_st_control(first_record, lower_record);
  if (st_found == std::string::npos) {
    return {};
  }
  const auto value_begin = st_value_begin(first_record, st_found);
  std::string following_control;
  const auto value_end =
      topic_body_control_offset(first_record, value_begin, following_control);
  auto st_value = trim_ascii(first_record.substr(value_begin,
                                                 value_end - value_begin));
  if (!st_body_begin_after_title(st_value, title)) {
    return {};
  }
  return following_control;
}

std::string clean_glossary_intro_fixed_line(std::string line) {
  if (const auto control = ascii_lower(line).find(" cfont ");
      control != std::string::npos) {
    line.resize(control);
  }
  line = trim_ascii(std::move(line));
  const auto gap = line.find("    ");
  if (gap != std::string::npos && gap > 0 && gap <= 11 &&
      line.find_first_of(" \t\r\n") == gap) {
    line.erase(0, line.find_first_not_of(" \t\r\n", gap));
  }
  while (!line.empty() &&
         (line.back() == '<' || line.back() == '>' || line.back() == '/' ||
          line.back() == '"' || line.back() == '(' || line.back() == ')' ||
          line.back() == '=')) {
    line.pop_back();
    line = trim_ascii(std::move(line));
  }
  return line;
}

void attach_topic_data(TocEntry& entry, const TopicData& topic) {
  entry.heading_level = topic.heading_level;
  entry.topic_number = topic.topic_number;
  entry.start_logical_record = topic.start_logical_record;
  entry.end_logical_record = topic.end_logical_record;
  entry.raw_records = topic.fixed_layout_sources.empty()
                          ? render_gml_records(topic.raw_records)
                          : render_gml_records_with_source_layout(
                                topic.raw_records,
                                topic.fixed_layout_sources);
  std::vector<std::string> publication_rows;
  std::vector<PublicationBlock> publication_blocks;
  if (ascii_lower(entry.title).find("publications") != std::string::npos) {
    for (const auto& record : entry.raw_records) {
      auto content = raw_gml_content_preserve_space(record);
      if (auto block = start_publication_block(content)) {
        publication_blocks.push_back(std::move(*block));
      } else if (!publication_blocks.empty() &&
                 (raw_gml_tag(record) == "p" ||
                  raw_gml_tag(record) == "line")) {
        auto continuation = clean_fixed_rendered_line(
            clean_fixed_st_row_markers(std::move(content)));
        if (!continuation.empty()) {
          auto& description = publication_blocks.back().description;
          if (!description.empty()) {
            description.push_back(' ');
          }
          description += std::move(continuation);
        }
      }
      if (!looks_like_publication_catalog_row(content)) {
        continue;
      }
      content = clean_fixed_rendered_line(std::move(content));
      if (!content.empty() &&
          std::find(publication_rows.begin(), publication_rows.end(), content) ==
              publication_rows.end()) {
        publication_rows.push_back(std::move(content));
      }
    }
  }
  const auto is_message_catalog =
      std::any_of(topic.raw_records.begin(),
                  topic.raw_records.end(),
                  [](const auto& record) {
                    return contains_srmsg_control(record);
                  });
  if (!entry.raw_records.empty() && !entry.title.empty()) {
    auto heading = entry.raw_records.begin();
    while (heading != entry.raw_records.end()) {
      if (is_topic_title_record(*heading)) {
        break;
      }
      const auto tag = raw_gml_tag(*heading);
      if (tag != "anchor") {
        break;
      }
      ++heading;
    }
    if (heading == entry.raw_records.end()) {
      return;
    }

    if (!publication_blocks.empty() && is_topic_title_record(*heading)) {
      const auto dot = heading->find('.');
      if (dot != std::string::npos) {
        heading->resize(dot + 1);
        *heading += entry.title;
      }
      auto intro = topic_st_body_text_after_toc_title(topic, entry.title);
      const auto lower_intro = ascii_lower(intro);
      const auto cfont = lower_intro.find("cfont ");
      if (cfont != std::string::npos) {
        intro.resize(cfont);
      }
      intro = clean_fixed_rendered_line(
          clean_fixed_st_row_markers(std::move(intro)));
      entry.raw_records.erase(heading + 1, entry.raw_records.end());
      if (!intro.empty()) {
        entry.raw_records.push_back(":p." + std::move(intro));
      }
      for (auto& block : publication_blocks) {
        if (!block.title.empty()) {
          entry.raw_records.push_back(":p." + std::move(block.title));
        }
        if (!block.description.empty()) {
          entry.raw_records.push_back(":p." + std::move(block.description));
        }
      }
      return;
    }

    if (is_message_catalog && is_topic_title_record(*heading)) {
      const auto heading_offset = static_cast<std::size_t>(
          std::distance(entry.raw_records.begin(), heading));
      auto intro_begin = heading + 1;
      auto intro_end = intro_begin;
      std::string intro;
      while (intro_end != entry.raw_records.end() &&
             (raw_gml_tag(*intro_end) == "line" ||
              raw_gml_tag(*intro_end) == "p")) {
        auto content = raw_gml_content_preserve_space(*intro_end);
        if (!content.empty()) {
          if (!intro.empty()) {
            intro.push_back(' ');
          }
          intro += std::move(content);
        }
        ++intro_end;
      }
      if (intro_end != intro_begin) {
        intro = normalize_message_catalog_intro(
            clean_fixed_st_row_markers(strip_fixed_line_overflow_tokens(
                std::move(intro), true)));
        intro_begin = entry.raw_records.erase(intro_begin, intro_end);
        if (!intro.empty()) {
          entry.raw_records.insert(intro_begin, ":p." + std::move(intro));
        }
        heading = entry.raw_records.begin() +
                  static_cast<std::ptrdiff_t>(heading_offset);
      }
    }

    if (!is_topic_title_record(*heading)) {
      auto body_text = topic_st_body_text_after_toc_title(topic, entry.title);
      if (body_text.empty()) {
        return;
      }
      const auto visible_begin = skip_decoded_separators(body_text);
      if (visible_begin < body_text.size() &&
          looks_like_gml_control_at(body_text, visible_begin)) {
        return;
      }

      std::vector<std::string> body_records;
      if (has_reflow_off_line_markers(body_text)) {
        body_records.push_back(":xmp.");
        for (auto line : split_reflow_off_body_lines(std::move(body_text))) {
          body_records.push_back(":xline." + std::move(line));
        }
        body_records.push_back(":exmp.");
      } else {
        body_records.push_back(":p." + trim_ascii(std::move(body_text)));
      }
      entry.raw_records.insert(heading + 1,
                               std::make_move_iterator(body_records.begin()),
                               std::make_move_iterator(body_records.end()));
      return;
    }

    auto& first_record = *heading;
    const auto dot = first_record.find('.');
    if (dot != std::string::npos) {
      const auto content_begin = dot + 1;
      const auto content = first_record.substr(content_begin);
      const auto title_size = entry.title.size();
      const auto is_glossary_topic = raw_gml_tag(first_record) == "glossary";
      auto body_text = topic_st_body_text_after_toc_title(topic, entry.title);
      const auto title_body_begin =
          st_body_begin_after_title(content, entry.title);
      if (content.size() > title_size &&
          ascii_starts_with_case_insensitive(content, entry.title) &&
          (title_body_begin || !body_text.empty())) {
        auto trailing_text = topic_st_body_after_toc_title(topic, entry.title);
        const auto following_control =
            topic_st_following_control_after_toc_title(topic, entry.title);
        if (body_text.empty()) {
          body_text = trim_ascii(content.substr(title_size + 1));
        }
        first_record.resize(content_begin);
        first_record += entry.title;
        auto continuation = heading + 1;
        if (body_text.find("__") != std::string::npos &&
            continuation != entry.raw_records.end() &&
            raw_gml_tag(*continuation) == "p") {
          const auto continuation_dot = continuation->find('.');
          if (continuation_dot != std::string::npos) {
            const auto continuation_body =
                continuation->substr(continuation_dot + 1);
            if (ascii_starts_with_case_insensitive(continuation_body,
                                                   ":hp") &&
                continuation_body.find(" Number of ") !=
                    std::string::npos) {
              body_text += " " + continuation_body;
              entry.raw_records.erase(continuation);
            }
          }
        }
        if (is_glossary_topic && !body_text.empty()) {
          body_text = strip_fixed_line_overflow_tokens(
              std::move(body_text), false, true);
          auto erase_begin = heading + 1;
          auto erase_end = erase_begin;
          auto glossary_tail = std::string{};
          while (erase_end != entry.raw_records.end() &&
                 raw_gml_tag(*erase_end) != "line" &&
                 !ascii_starts_with_case_insensitive(
                     *erase_end, ":anchor id='GLS'")) {
            const auto content = raw_gml_content_preserve_space(*erase_end);
            const auto dictionary = content.find("The IBM Dictionary");
            if (dictionary != std::string::npos) {
              glossary_tail = trim_ascii(content.substr(dictionary));
            }
            ++erase_end;
          }
          erase_begin = entry.raw_records.erase(erase_begin, erase_end);
          std::vector<std::string> preserved{":xmp."};
          for (auto line : split_reflow_off_body_lines(std::move(body_text))) {
            line = clean_glossary_intro_fixed_line(std::move(line));
            if (!line.empty()) {
              preserved.push_back(":xline." + std::move(line));
            }
          }
          preserved.push_back(":exmp.");
          if (!glossary_tail.empty()) {
            preserved.push_back(":p." + std::move(glossary_tail));
          }
          entry.raw_records.insert(erase_begin,
                                   std::make_move_iterator(preserved.begin()),
                                   std::make_move_iterator(preserved.end()));
          return;
        }
        auto form_records = render_st_form_items(body_text);
        if (!form_records.empty()) {
          entry.raw_records.insert(
              heading + 1,
              std::make_move_iterator(form_records.begin()),
              std::make_move_iterator(form_records.end()));
        } else if (!trailing_text.empty()) {
          entry.raw_records.insert(heading + 1, ":p." + trailing_text);
        } else if (is_message_catalog && !body_text.empty()) {
          auto erase_begin = heading + 1;
          auto erase_end = erase_begin;
          while (erase_end != entry.raw_records.end() &&
                 !ascii_starts_with_case_insensitive(
                     *erase_end, ":anchor id='MSG ")) {
            ++erase_end;
          }
          erase_begin = entry.raw_records.erase(erase_begin, erase_end);
          auto intro = normalize_message_catalog_intro(
              clean_fixed_st_row_markers(strip_fixed_line_overflow_tokens(
                  std::move(body_text), true)));
          if (!intro.empty()) {
            entry.raw_records.insert(erase_begin, ":p." + std::move(intro));
          }
        } else if (following_control == "cselect" ||
                   following_control == "cfont") {
          auto cselect_intro = trim_ascii(body_text);
          if (following_control == "cselect") {
            while (!cselect_intro.empty() &&
                   (cselect_intro.back() == '?' ||
                    std::isspace(static_cast<unsigned char>(
                        cselect_intro.back())) != 0)) {
              cselect_intro.pop_back();
            }
            cselect_intro = trim_ascii(std::move(cselect_intro));
            if (!cselect_intro.empty() && cselect_intro.back() == ':') {
              cselect_intro =
                  preserve_reflow_off_st_body_lines(std::move(cselect_intro));
            }
          }
          entry.raw_records.insert(heading + 1, ":p." + cselect_intro);
        } else if (!body_text.empty() && has_reflow_off_line_markers(body_text)) {
          auto erase_begin = heading + 1;
          auto erase_end = erase_begin;
          while (erase_end != entry.raw_records.end() &&
                 (raw_gml_tag(*erase_end) == "p" ||
                  raw_gml_tag(*erase_end) == "line") &&
                 raw_record_duplicates_st_body(*erase_end, body_text)) {
            ++erase_end;
          }
          std::vector<std::string> inline_fixed_continuations;
          const auto body_following_control =
              topic_st_body_following_control_after_toc_title(topic,
                                                              entry.title);
          if (body_following_control == "cfont" ||
              body_following_control == "cselect" ||
              ascii_lower(body_text).find("cfont ") != std::string::npos) {
            // Some flattened ST records retain a printable row marker rather
            // than an explicit control separator before a typed continuation.
            // The normal projection below already owns that CFONT/CSELECT
            // content; keep only the ST prefix in the fixed body so the same
            // rows are not emitted twice.
            const auto lower_body = ascii_lower(body_text);
            auto typed_control = body_text.size();
            for (const auto* control : {"cfont ", "cselect "}) {
              auto search = lower_body.find(control);
              while (search != std::string::npos) {
                if (looks_like_gml_control_at(body_text, search)) {
                  typed_control = std::min(typed_control, search);
                  break;
                }
                search = lower_body.find(control, search + 1);
              }
            }
            if (typed_control < body_text.size()) {
              body_text = trim_ascii(body_text.substr(0, typed_control));
            }
            auto continuation = erase_end;
            while (continuation != entry.raw_records.end() &&
                   (raw_gml_tag(*continuation) == "p" ||
                    raw_gml_tag(*continuation) == "line")) {
              const auto duplicate_of_fixed_body =
                  raw_record_duplicates_st_body(*continuation, body_text);
              auto continuation_content = strip_leading_visual_bar(
                  raw_gml_content_preserve_space(*continuation));
              if (ascii_lower(entry.title).find("publications") !=
                  std::string::npos) {
                continuation_content = clean_fixed_rendered_line(
                    std::move(continuation_content));
              }
              if (!continuation_content.empty() && !duplicate_of_fixed_body) {
                inline_fixed_continuations.push_back(
                    std::move(continuation_content));
              }
              ++continuation;
            }
            erase_end = continuation;
          }
          erase_begin = entry.raw_records.erase(erase_begin, erase_end);
          std::vector<std::string> preserved{
              inline_fixed_continuations.empty() ? ":xmp."
                                                 : ":xmp inline='html'."};
          for (auto line : split_reflow_off_body_lines(std::move(body_text))) {
            if (ascii_lower(entry.title).find("publications") !=
                std::string::npos) {
              line = clean_fixed_rendered_line(std::move(line));
            }
            preserved.push_back(":xline." + std::move(line));
          }
          for (auto& continuation : inline_fixed_continuations) {
            preserved.push_back(":xline.   " + std::move(continuation));
          }
          preserved.push_back(":exmp.");
          entry.raw_records.insert(erase_begin,
                                   std::make_move_iterator(preserved.begin()),
                                   std::make_move_iterator(preserved.end()));
        } else if (!body_text.empty()) {
          entry.raw_records.insert(heading + 1, ":p." + body_text);
        }
      }
    }
  }
  auto rendered_publication_text = std::string{};
  for (const auto& record : entry.raw_records) {
    if (!rendered_publication_text.empty()) {
      rendered_publication_text.push_back(' ');
    }
    rendered_publication_text += clean_fixed_rendered_line(
        raw_gml_content_preserve_space(record));
  }
  rendered_publication_text =
      collapse_ascii_whitespace(std::move(rendered_publication_text));
  for (auto& publication : publication_rows) {
    const auto already_present =
        rendered_publication_text.find(
            collapse_ascii_whitespace(publication)) != std::string::npos;
    if (!already_present) {
      entry.raw_records.push_back(":line." + std::move(publication));
    }
  }
  if (!publication_rows.empty()) {
    for (auto& record : entry.raw_records) {
      const auto content = raw_gml_content_preserve_space(record);
      if (!looks_like_publication_catalog_row(content)) {
        continue;
      }
      const auto dot = record.find('.');
      if (dot != std::string::npos) {
        record.resize(dot + 1);
        record += clean_fixed_rendered_line(content);
      }
    }
  }
}

std::vector<TocEntry> build_table_of_contents(
    const std::vector<std::string>& decoded_records,
    const std::vector<TopicData>& topics,
    bool attach_records) {
  std::vector<TocEntry> toc;
  bool in_contents_topic = false;
  for (const auto& decoded : decoded_records) {
    if (!in_contents_topic) {
      if (!is_contents_topic_record(decoded)) {
        continue;
      }
      in_contents_topic = true;
    } else if (is_topic_header_record(decoded)) {
      break;
    }

    auto entries = extract_toc_entries(decoded);
    if (entries.empty()) {
      continue;
    }
    for (auto& entry : entries) {
      if (const auto* topic = find_topic_data(topics, entry.id)) {
        if (attach_records) {
          attach_topic_data(entry, *topic);
        } else {
          entry.heading_level = topic->heading_level;
          entry.topic_number = topic->topic_number;
          entry.start_logical_record = topic->start_logical_record;
          entry.end_logical_record = topic->end_logical_record;
        }
      }
    }
    toc.insert(toc.end(), entries.begin(), entries.end());
  }
  return toc;
}

std::vector<std::string> build_raw_gml_records(
    const std::vector<TopicData>& topics) {
  std::vector<std::string> records;
  for (const auto& topic : topics) {
    auto topic_records = render_gml_records(topic.raw_records);
    records.insert(records.end(),
                   std::make_move_iterator(topic_records.begin()),
                   std::make_move_iterator(topic_records.end()));
  }
  return records;
}

std::vector<TopicData> build_topics(
    const std::vector<std::string>& decoded_records,
    bool copy_records) {
  std::vector<TopicData> topics;

  std::vector<std::size_t> header_indexes;
  for (std::size_t index = 0; index < decoded_records.size(); ++index) {
    if (is_topic_header_record(decoded_records[index]) &&
        !extract_topic_header_id(decoded_records[index]).empty()) {
      header_indexes.push_back(index);
    }
  }
  if (header_indexes.empty()) {
    return topics;
  }

  topics.reserve(header_indexes.size());
  std::set<std::string> seen_topic_ids;
  for (std::size_t index = 0; index < header_indexes.size(); ++index) {
    const auto record_begin = header_indexes[index];
    const auto record_end =
        (index + 1 < header_indexes.size())
            ? header_indexes[index + 1]
            : decoded_records.size();
    if (record_begin >= record_end) {
      continue;
    }

    TopicData topic;
    const auto& header = decoded_records[record_begin];
    // Metadata can follow a standalone SH boundary.  Use the first record
    // carrying the topic controls, rather than assuming the boundary and
    // metadata share one logical record.
    std::size_t metadata_record = record_begin;
    if (extract_uint_control_value(decoded_records[metadata_record],
                                   "ctopicn ") == 0) {
      for (auto candidate = record_begin + 1;
           candidate < record_end;
           ++candidate) {
        const auto& candidate_record = decoded_records[candidate];
        if (extract_uint_control_value(candidate_record, "ctopicn ") != 0 &&
            (!extract_control_value_until_boundary(candidate_record,
                                                   "chdlevel ")
                  .empty() ||
             !extract_control_value_until_boundary(candidate_record, "st ")
                  .empty())) {
          metadata_record = candidate;
          break;
        }
      }
    }
    const auto& metadata = decoded_records[metadata_record];
    topic.topic_number = extract_uint_control_value(metadata, "ctopicn ");
    topic.start_logical_record =
        static_cast<std::uint32_t>(record_begin + 1);
    topic.end_logical_record = static_cast<std::uint32_t>(record_end + 1);
    if (copy_records) {
      topic.raw_records.assign(decoded_records.begin() +
                                   static_cast<std::ptrdiff_t>(record_begin),
                               decoded_records.begin() +
                                   static_cast<std::ptrdiff_t>(record_end));
    }

    topic.id = extract_topic_header_id(header);
    topic.heading_level =
        extract_control_value_until_boundary(metadata, "chdlevel ");
    topic.title = normalize_toc_title(
        extract_control_value_until_boundary(metadata, "st "));
    if (!topic.id.empty() && seen_topic_ids.insert(topic.id).second) {
      topics.push_back(std::move(topic));
    }
  }
  return topics;
}

} // namespace geist::detail
