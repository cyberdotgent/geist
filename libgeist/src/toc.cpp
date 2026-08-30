#include "geist/detail/internal.hpp"
#include "geist/detail/message_prose_rows.hpp"
#include "geist/detail/topic_header_title.hpp"

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

// M9 keep: fixed-body visual `|` rails.  Effect census at typed coverage
// 6,986/7,362 (disable, re-export the whole corpus, `diff -r`): 9 dependent
// topics - GG24-4302-00 FRONT_1, ITPPIBOK A.2.1, QSYSNEWG 6.3.1/7.7.2.1,
// SC24-546 2.1.3, SC24-5527-02 COMMENTS, SC33-033 A.2/A.3, SC34-425 2.5.6.
// Replaced by `LayoutIR` rows.
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

// M9 keep: legacy-route catalog introductions.  Effect census at typed
// coverage 6,986/7,362: 3 dependent topics - GX27-3999-00 B.0, SC09-138 F.1,
// SC31-711 4.3.5. The SC31-711 2.4.9/4.1.x/4.3.1-4.3.4 topics named in the
// earlier census are typed now. Replaced by `MessageTopicIR` introduction
// paragraphs (typed trap-catalog lowering).
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
  return normalized;
}

// M9 keep: legacy-only SRMSG catalog introductions and `ST` form prefixes.
// Effect census at typed coverage 6,986/7,362: exactly 1 dependent topic,
// SC31-711 4.3.5 (`(`/`)` glyph slots); 4.1.1/4.3.1/4.3.2/4.3.4 are typed
// now, so the function still runs on them but its output is discarded.
// Retires with a typed trap-catalog lowering
// (`MessageTopicIR` introduction). The glyph slot
// characters before the four-space padding are the legacy fixed-row marker
// glyph set (`is_fixed_st_row_marker` plus `"`).
std::string clean_fixed_st_row_markers(std::string value) {
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
    }
    gap += 4;
  }
  return value;
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

// M9 keep: glyph/`?`/wide-gap row-marker inference on flattened `ST` bodies
// (`is_fixed_st_row_marker`, `fixed_st_row_marker_at`,
// `has_reflow_off_line_markers`, `split_reflow_off_body_lines`,
// `preserve_reflow_off_st_body_lines`, `strip_leading_visual_bar`). Corpus
// effect census at typed coverage 6,986/7,362 (disable, re-export the whole
// corpus, `diff -r`): 14 dependent topics - FA1PLMM0 9.3/9.3.1, GG24-4302-00
// FRONT_1, IBMMMSTR TITLE, ITPPIBOK A.2.1, N2AH1MST 1.2.5, QSYSNEWG
// 6.3.1/7.7.2.1, SC24-546 2.1.3, SC24-5527-02 COMMENTS, SC31-711 BACK_1.12,
// SC33-033 A.2/A.3, SC34-425 2.5.6. Replaced by `LayoutIR` marker slots /
// `fixed_prose_ir.cpp` once the typed fixed-prose lowering admits those
// bodies.
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
  // M9 keep: a single-space title marker glyph (not the padded fixed-row
  // form) in 16 legacy-route topics: FA1PLMM0 16.10.5, 5.1.7.1; N2AH1MST
  // 1.2.3, FRONT_1; QSYSINFO APPENDIX1.4.2.9; SC24-5520-00 2.4.11.2,
  // 2.4.16.2, 2.4.7.2, 5.10.4; SC31-711 2.4.8, 3.1, 4.1.1, 4.1.2, 4.1.3,
  // 4.2.2, 4.3.5. Replaced by the title row's `MarkerSlotIR`.
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

void attach_topic_data(
    TocEntry& entry,
    const TopicData& topic,
    const std::map<std::string, std::string>* topic_titles) {
  (void)topic_titles;
  entry.heading_level = topic.heading_level;
  entry.topic_number = topic.topic_number;
  entry.start_logical_record = topic.start_logical_record;
  entry.end_logical_record = topic.end_logical_record;
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

std::vector<TopicData> build_topics(const LogicalDecodeContext& context,
                                    bool copy_records) {
  const auto& decoded_records = context.decoded_records;
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
    // The header title is the visible text of the `ST` display line
    // (topic_header_title.hpp).  The flattened `ST` payload run below is only
    // the fallback for a record whose display lines do not parse or that
    // carries no `ST` line at all; it stops at the first decoder boundary,
    // which is not where the display row breaks.
    std::optional<std::string> display_line_title;
    for (auto candidate = metadata_record;
         candidate < record_end && candidate < metadata_record + 6;
         ++candidate) {
      const auto sources = decode_logical_record_sources(
          context, static_cast<std::uint32_t>(candidate + 1),
          static_cast<std::uint32_t>(candidate + 2));
      if (sources.empty()) break;
      display_line_title = topic_header_title_of_record(sources.front());
      if (display_line_title) break;
    }
    topic.title = normalize_toc_title(
        display_line_title
            ? *display_line_title
            : extract_control_value_until_boundary(metadata, "st "));
    if (!topic.id.empty() && seen_topic_ids.insert(topic.id).second) {
      topics.push_back(std::move(topic));
    }
  }
  return topics;
}

} // namespace geist::detail
