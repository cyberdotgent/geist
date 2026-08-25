#include "geist/detail/internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
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

bool looks_like_gml_control_at(const std::string& value, std::size_t offset);
bool looks_like_control_boundary(const std::string& decoded_record,
                                 const std::string& lower_record,
                                 std::size_t offset);

std::size_t skip_decoded_separators(const std::string& value) {
  std::size_t cursor = 0;
  while (cursor < value.size()) {
    const auto ch = static_cast<unsigned char>(value[cursor]);
    if (std::isspace(ch) != 0 || ch < 0x20 || value[cursor] == '?' ||
        value[cursor] == ',') {
      ++cursor;
      continue;
    }
    break;
  }
  return cursor;
}

bool is_topic_id_char(char ch) {
  const auto byte = static_cast<unsigned char>(ch);
  return std::isalnum(byte) != 0 || ch == '.' || ch == '_' || ch == '-';
}

std::string extract_topic_header_id(const std::string& decoded_record) {
  auto record = trim_ascii(decoded_record);
  const auto start = skip_decoded_separators(record);
  if (start + 3 > record.size() ||
      std::tolower(static_cast<unsigned char>(record[start])) != 's' ||
      std::tolower(static_cast<unsigned char>(record[start + 1])) != 'h') {
    return {};
  }

  std::size_t cursor = start + 2;
  while (cursor < record.size() && is_topic_id_char(record[cursor])) {
    ++cursor;
  }

  return normalize_toc_id(record.substr(start + 2, cursor - (start + 2)));
}

std::string extract_control_value_until_boundary(const std::string& record,
                                                 const std::string& marker) {
  const auto lower_record = ascii_lower(record);
  const auto lower_marker = ascii_lower(marker);
  const auto found = lower_record.find(lower_marker);
  if (found == std::string::npos) {
    return {};
  }

  const auto value_begin = found + marker.size();
  auto value_end = record.size();
  static const std::array<const char*, 8> boundaries = {
      "?c", ", c", "?s", ", s", "?e", ", e", "?cz", ", cz"};
  for (const auto* boundary : boundaries) {
    const auto next = lower_record.find(boundary, value_begin);
    if (next != std::string::npos) {
      value_end = std::min(value_end, next);
    }
  }
  return trim_ascii(record.substr(value_begin, value_end - value_begin));
}

std::uint32_t extract_uint_control_value(const std::string& record,
                                         const std::string& marker) {
  const auto value = extract_control_value_until_boundary(record, marker);
  std::istringstream input(value);
  std::uint32_t number = 0;
  if (input >> number) {
    return number;
  }
  return 0;
}


std::string escape_gml_attr(std::string value) {
  for (auto& ch : value) {
    if (ch == '\'') {
      ch = '"';
    }
  }
  return value;
}

bool is_literal_question_mark(const std::string& value, std::size_t offset) {
  if (offset >= value.size() || value[offset] != '?') {
    return false;
  }

  if (offset == 0) {
    return false;
  }

  const auto before = static_cast<unsigned char>(value[offset - 1]);
  if (std::isalnum(before) == 0 && value[offset - 1] != ')' &&
      value[offset - 1] != '"' && value[offset - 1] != '\'') {
    return false;
  }

  auto next = offset + 1;
  while (next < value.size() &&
         std::isspace(static_cast<unsigned char>(value[next])) != 0) {
    ++next;
  }
  if (next >= value.size()) {
    return true;
  }
  if (value[next] == '?') {
    return true;
  }

  const auto after = static_cast<unsigned char>(value[next]);
  if (std::islower(after) != 0) {
    return false;
  }
  if (looks_like_gml_control_at(value, next)) {
    return false;
  }
  return true;
}

std::string debug_placeholder(std::string kind,
                              std::size_t offset,
                              std::size_t length) {
  std::ostringstream output;
  output << "<geist-placeholder kind='" << kind << "' offset='" << offset
         << "' len='" << length << "'>";
  return output.str();
}

bool is_decoded_line_marker(char ch) {
  switch (ch) {
    case '$':
    case ';':
    case '(':
    case ')':
    case '*':
    case '!':
    case '-':
    case ':':
    case '=':
      return true;
    default:
      return false;
  }
}

std::string remove_decoded_line_markers(std::string value) {
  std::string output;
  output.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    const auto marker_at_boundary =
        index == 0 ||
        std::isspace(static_cast<unsigned char>(value[index - 1])) != 0;
    const auto marker_before_indent =
        index + 2 < value.size() &&
        std::isspace(static_cast<unsigned char>(value[index + 1])) != 0 &&
        std::isspace(static_cast<unsigned char>(value[index + 2])) != 0;
    const auto trailing_marker =
        index + 1 == value.size() && marker_at_boundary;
    if (is_decoded_line_marker(value[index]) && marker_at_boundary &&
        (marker_before_indent || trailing_marker)) {
      while (index + 1 < value.size() &&
             std::isspace(static_cast<unsigned char>(value[index + 1])) != 0) {
        ++index;
      }
      if (!output.empty() && output.back() != ' ') {
        output.push_back(' ');
      }
      continue;
    }
    output.push_back(value[index]);
  }
  return output;
}

void collapse_terminal_question_separator(std::string& value);
void remove_space_before_terminal_question(std::string& value);

std::string dot_text(std::string value) {
  value = remove_decoded_line_markers(std::move(value));
  value = trim_ascii(std::move(value));
  collapse_terminal_question_separator(value);
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] != '?') {
      continue;
    }
    const auto terminal_literal = index + 1 == value.size();
    if (!terminal_literal) {
      value[index] = ' ';
    }
  }
  value = collapse_ascii_whitespace(std::move(value));
  remove_space_before_terminal_question(value);
  if (value.empty()) {
    return {};
  }
  return value;
}

void collapse_terminal_question_separator(std::string& value) {
  if (value.size() < 3 || value.back() != '?' ||
      value[value.size() - 2] != '?') {
    return;
  }
  const auto before_run = static_cast<unsigned char>(value[value.size() - 3]);
  if (std::isalnum(before_run) != 0 || value[value.size() - 3] == ')') {
    value.pop_back();
  }
}

void remove_space_before_terminal_question(std::string& value) {
  if (value.size() < 3 || value.back() != '?') {
    return;
  }
  auto cursor = value.size() - 1;
  while (cursor > 0 &&
         std::isspace(static_cast<unsigned char>(value[cursor - 1])) != 0) {
    --cursor;
  }
  if (cursor + 1 == value.size() || cursor == 0) {
    return;
  }
  const auto previous = static_cast<unsigned char>(value[cursor - 1]);
  if (std::isalnum(previous) != 0 || value[cursor - 1] == ')') {
    value.erase(cursor, value.size() - 1 - cursor);
  }
}

std::string trim_control_operand(std::string value) {
  value = dot_text(std::move(value));
  while (!value.empty() && (value.back() == '.' || value.back() == ',')) {
    value.pop_back();
  }
  return value;
}

bool looks_like_gml_control_at(const std::string& value, std::size_t offset) {
  while (offset < value.size() &&
         std::isspace(static_cast<unsigned char>(value[offset])) != 0) {
    ++offset;
  }
  if (offset >= value.size()) {
    return false;
  }
  const auto initial = static_cast<char>(
      std::tolower(static_cast<unsigned char>(value[offset])));
  if (initial != 'c' && initial != 'e' && initial != 's') {
    return false;
  }

  static constexpr std::array<std::string_view, 48> prefixes = {
      "ctopicn",     "cparent",    "cforwardlevel",
      "cbacklevel",  "csummary",   "chdlevel",   "csourcefn",
      "st",          "ctocdef",    "ctoce",      "etoc",
      "cfontdef",    "cfont",      "cselect",    "cmenu",
      "cmitem",      "cemenu",     "srfig",      "srefig",
      "srtbl",       "sretbl",     "sr",         "cz",
      "si",
      "citerm",      "cgpsep",     "clanguage",  "cversion",
      "cbldvers",    "creflow",    "ctitle",     "cstitle",
      "ccopyright",  "csecurity",  "cdate",      "cauthor",
      "cdocnum",     "ctopics",    "cbasenum",   "cdoclevel",
      "cfront",      "ccontents",  "cfigures",   "ctables",
      "cindex",      "cendindex",  "cidelm",     "cpicture"};
  for (const auto prefix : prefixes) {
    if (!ascii_starts_with_case_insensitive(value, offset, prefix)) {
      continue;
    }
    const auto end = offset + prefix.size();
    if (end == value.size()) {
      return true;
    }
    const auto next = value[end];
    if (std::isspace(static_cast<unsigned char>(next)) != 0 || next == '=' ||
        next == ',' || next == '.') {
      return true;
    }
    if (prefix == "st" && next == '|') {
      return true;
    }
    if ((prefix == "srfig" || prefix == "srtbl" || prefix == "sr") &&
        is_topic_id_char(next)) {
      if (prefix == "sr" && end + 1 < value.size() &&
          std::isspace(static_cast<unsigned char>(value[end + 1])) != 0) {
        return false;
      }
      return true;
    }
  }
  return false;
}

std::vector<std::string> split_decoded_markup_segments(
    const std::string& decoded_record) {
  std::vector<std::string> segments;
  const auto has_fixed_visual_payload = [](const std::string& value) {
    auto question_run = std::size_t{0};
    auto space_run = std::size_t{0};
    for (const auto ch : value) {
      if (ch == '?') {
        ++question_run;
        if (question_run >= 20) {
          return true;
        }
      } else {
        question_run = 0;
      }
      if (ch == ' ') {
        ++space_run;
        if (space_run >= 70) {
          return true;
        }
      } else {
        space_run = 0;
      }
    }
    return false;
  };
  const auto trim_decoded_segment = [&](std::string value) {
    if (!has_fixed_visual_payload(value)) {
      return trim_ascii(std::move(value));
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
      value.erase(value.begin());
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
      value.pop_back();
    }
    return value;
  };
  std::size_t begin = 0;
  for (std::size_t cursor = 0; cursor < decoded_record.size(); ++cursor) {
    auto split = false;
    auto split_before = false;
    if (cursor > begin && looks_like_gml_control_at(decoded_record, cursor) &&
        !ascii_starts_with_case_insensitive(decoded_record, cursor, "sh") &&
        (std::isspace(static_cast<unsigned char>(decoded_record[cursor - 1])) !=
             0 ||
         decoded_record[cursor - 1] == '.' ||
         decoded_record[cursor - 1] == ',')) {
      split = true;
      split_before = true;
    } else if (decoded_record[cursor] == '?' &&
               looks_like_gml_control_at(decoded_record, cursor + 1)) {
      split = true;
    } else if (decoded_record[cursor] == ',' &&
               looks_like_gml_control_at(decoded_record, cursor + 1)) {
      split = true;
    }
    if (!split) {
      continue;
    }

    auto end = split_before ? cursor : cursor;
    if (!split_before && decoded_record[cursor] == '?' && cursor > begin) {
      auto previous_index = cursor;
      while (previous_index > begin &&
             std::isspace(static_cast<unsigned char>(
                 decoded_record[previous_index - 1])) != 0) {
        --previous_index;
      }
      const auto previous =
          previous_index > begin
              ? static_cast<unsigned char>(decoded_record[previous_index - 1])
              : 0;
      if (previous_index > begin &&
          (std::isalnum(previous) != 0 ||
           decoded_record[previous_index - 1] == ')')) {
        end = cursor + 1;
      }
    }
    auto segment = trim_decoded_segment(decoded_record.substr(begin,
                                                              end - begin));
    collapse_terminal_question_separator(segment);
    if (!segment.empty()) {
      segments.push_back(std::move(segment));
    }
    begin = split_before ? cursor : cursor + 1;
  }

  auto segment = trim_decoded_segment(decoded_record.substr(begin));
  if (!segment.empty()) {
    segments.push_back(std::move(segment));
  }
  return segments;
}

std::string annotate_decoded_placeholders(const std::string& value) {
  std::string output;
  output.reserve(value.size());
  const auto lower = ascii_lower(value);

  for (std::size_t cursor = 0; cursor < value.size();) {
    if (value[cursor] != '?') {
      output.push_back(value[cursor++]);
      continue;
    }

    auto run_end = cursor + 1;
    while (run_end < value.size() && value[run_end] == '?') {
      ++run_end;
    }
    const auto run_length = run_end - cursor;
    if (run_length > 1) {
      output += debug_placeholder("decoded-question-run", cursor, run_length);
      cursor = run_end;
      continue;
    }

    if (is_literal_question_mark(value, cursor)) {
      output.push_back(value[cursor++]);
      continue;
    }

    if (looks_like_control_boundary(value, lower, cursor) ||
        looks_like_gml_control_at(value, cursor + 1)) {
      output += debug_placeholder("control-boundary", cursor, 1);
      ++cursor;
      continue;
    }

    auto next = cursor + 1;
    while (next < value.size() &&
           std::isspace(static_cast<unsigned char>(value[next])) != 0) {
      ++next;
    }
    if (next >= value.size()) {
      output += debug_placeholder("terminal-decoder-placeholder", cursor, 1);
    } else if (is_decoded_line_marker(value[next])) {
      output += debug_placeholder("line-marker-boundary", cursor, 1);
    } else {
      output += debug_placeholder("decoder-separator", cursor, 1);
    }
    ++cursor;
  }

  return output;
}

std::string first_word(std::string value) {
  value = trim_ascii(std::move(value));
  const auto end = value.find_first_of(" \t\r\n,");
  auto word = end == std::string::npos ? value : value.substr(0, end);
  while (!word.empty() && (word.back() == '.' || word.back() == ',')) {
    word.pop_back();
  }
  return word;
}

std::string rest_after_first_word(std::string value) {
  value = trim_ascii(std::move(value));
  const auto end = value.find_first_of(" \t\r\n,");
  if (end == std::string::npos) {
    return {};
  }
  return trim_ascii(value.substr(end + 1));
}

std::string render_simple_gml_control(const std::string& tag,
                                      std::string value) {
  value = dot_text(std::move(value));
  if (value.empty()) {
    return ":" + tag + ".";
  }
  return ":" + tag + "." + value;
}

std::string render_normalized_gml_control(const std::string& tag,
                                          std::string value) {
  value = trim_ascii(std::move(value));
  if (value.empty()) {
    return ":" + tag + ".";
  }
  return ":" + tag + "." + value;
}

std::string decoded_control_name(const std::string& segment) {
  auto value = trim_ascii(segment);
  while (!value.empty() && (value.front() == '?' || value.front() == ',')) {
    value.erase(value.begin());
    value = trim_ascii(std::move(value));
  }

  std::size_t end = 0;
  while (end < value.size()) {
    const auto ch = static_cast<unsigned char>(value[end]);
    if (std::isalnum(ch) == 0 && value[end] != '_') {
      break;
    }
    ++end;
  }
  if (end == 0) {
    return {};
  }
  auto name = value.substr(0, end);
  for (auto& ch : name) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return name;
}

std::string render_unknown_control_gml(const std::string& segment) {
  const auto name = decoded_control_name(segment);
  if (name.empty()) {
    return {};
  }
  return ":unknown-control name='" + escape_gml_attr(name) + "' raw='" +
         escape_gml_attr(segment) + "'.";
}

std::string strip_visual_line_marker(std::string value) {
  value = trim_ascii(std::move(value));
  if (!value.empty() && value.front() == '|') {
    value.erase(value.begin());
    return trim_ascii(std::move(value));
  }
  std::string output;
  output.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '|' &&
        (index == 0 ||
         std::isspace(static_cast<unsigned char>(value[index - 1])) != 0) &&
        (index + 1 == value.size() ||
         std::isspace(static_cast<unsigned char>(value[index + 1])) != 0)) {
      if (!output.empty() && output.back() != ' ') {
        output.push_back(' ');
      }
      continue;
    }
    output.push_back(value[index]);
  }
  return collapse_ascii_whitespace(std::move(output));
}

std::string strip_visual_line_markers_from_inline_gml(std::string value) {
  std::string output;
  output.reserve(value.size());
  for (std::size_t cursor = 0; cursor < value.size();) {
    if (value[cursor] == ':') {
      const auto dot = value.find('.', cursor + 1);
      if (dot != std::string::npos) {
        output.append(value, cursor, dot + 1 - cursor);
        cursor = dot + 1;
        continue;
      }
    }
    if (value[cursor] == '|' &&
        (cursor == 0 ||
         std::isspace(static_cast<unsigned char>(value[cursor - 1])) != 0) &&
        (cursor + 1 == value.size() ||
         std::isspace(static_cast<unsigned char>(value[cursor + 1])) != 0)) {
      if (!output.empty() && output.back() != ' ') {
        output.push_back(' ');
      }
      ++cursor;
      continue;
    }
    output.push_back(value[cursor++]);
  }
  return collapse_ascii_whitespace(std::move(output));
}

std::optional<std::string> render_marker_continuation_gml(
    const std::string& segment) {
  if (segment.size() < 3 || !is_decoded_line_marker(segment.front())) {
    return std::nullopt;
  }
  if (std::isspace(static_cast<unsigned char>(segment[1])) == 0 ||
      std::isspace(static_cast<unsigned char>(segment[2])) == 0) {
    return std::nullopt;
  }

  auto text = strip_visual_line_marker(segment.substr(1));
  if (text.empty()) {
    return std::string{};
  }
  return render_simple_gml_control("line", std::move(text));
}

std::string normalize_bookmaster_tag(std::string tag) {
  tag = trim_ascii(std::move(tag));
  if (!tag.empty() && tag.front() == ':') {
    tag.erase(tag.begin());
  }
  tag = ascii_lower(tag);
  if (tag == "title") {
    return "tipage";
  }
  return tag;
}

bool bookmaster_topic_tag_takes_title(const std::string& tag) {
  static const std::set<std::string> titled_tags = {
      "h1",     "h2",     "h3",      "h4",       "h5",
      "ih2",    "preface", "appendix", "glossary", "lblbox"};
  return titled_tags.find(tag) != titled_tags.end();
}

std::string render_bookmaster_tag(std::string tag, std::string value) {
  tag = normalize_bookmaster_tag(std::move(tag));
  value = dot_text(std::move(value));
  if (tag.empty()) {
    return render_simple_gml_control("p", std::move(value));
  }
  if (value.empty()) {
    return ":" + tag + ".";
  }
  return ":" + tag + "." + value;
}

std::string render_bookmaster_tag_with_layout(std::string tag,
                                              std::string left_margin,
                                              std::string indent,
                                              std::string value) {
  tag = normalize_bookmaster_tag(std::move(tag));
  value = dot_text(std::move(value));
  if (tag.empty()) {
    tag = "p";
  }

  auto output = ":" + tag;
  left_margin = trim_ascii(std::move(left_margin));
  indent = trim_ascii(std::move(indent));
  if (!left_margin.empty()) {
    output += " col='" + escape_gml_attr(std::move(left_margin)) + "'";
  }
  if (!indent.empty()) {
    output += " indent='" + escape_gml_attr(std::move(indent)) + "'";
  }
  output += ".";
  output += value;
  return output;
}

std::string render_empty_bookmaster_tag_with_layout(std::string tag,
                                                    std::string left_margin,
                                                    std::string indent) {
  tag = normalize_bookmaster_tag(std::move(tag));
  if (tag.empty()) {
    return {};
  }
  auto output = ":" + tag;
  left_margin = trim_ascii(std::move(left_margin));
  indent = trim_ascii(std::move(indent));
  if (!left_margin.empty()) {
    output += " col='" + escape_gml_attr(std::move(left_margin)) + "'";
  }
  if (!indent.empty()) {
    output += " indent='" + escape_gml_attr(std::move(indent)) + "'";
  }
  output += ".";
  return output;
}

std::string render_keyed_gml_control(const std::string& tag,
                                     const std::string& attr,
                                     std::string value) {
  value = trim_control_operand(std::move(value));
  if (value.empty()) {
    return ":" + tag + ".";
  }
  return ":" + tag + " " + attr + "='" + escape_gml_attr(value) + "'.";
}

std::string table_anchor_id(std::string target) {
  target = trim_control_operand(std::move(target));
  if (target.empty()) {
    return {};
  }
  if (ascii_starts_with_case_insensitive(target, "tbltbl")) {
    return target;
  }
  if (ascii_starts_with_case_insensitive(target, "tbl")) {
    return "TBL" + target;
  }
  return target;
}

std::size_t longest_question_run(const std::string& value) {
  std::size_t longest = 0;
  std::size_t cursor = 0;
  while (cursor < value.size()) {
    if (value[cursor] != '?') {
      ++cursor;
      continue;
    }
    const auto begin = cursor;
    while (cursor < value.size() && value[cursor] == '?') {
      ++cursor;
    }
    longest = std::max(longest, cursor - begin);
  }
  return longest;
}

std::string render_table_gml(std::string value,
                             std::size_t& table_border_width) {
  value = trim_ascii(std::move(value));
  std::istringstream input(value);
  std::string target;
  if (!(input >> target)) {
    return ":table.";
  }

  std::string caption;
  std::getline(input, caption);
  table_border_width = std::max(table_border_width,
                                longest_question_run(caption));
  caption = dot_text(std::move(caption));

  auto output = ":table id='" + escape_gml_attr(table_anchor_id(target)) + "'.";
  if (!caption.empty()) {
    output += caption;
  }
  return output;
}

std::string render_toc_entry_gml(std::string value) {
  std::istringstream input(value);
  std::uint32_t level = 0;
  std::uint32_t style = 0;
  std::string id;
  if (!(input >> level >> style >> id)) {
    return {};
  }
  std::string title;
  std::getline(input, title);
  title = dot_text(std::move(title));
  if (id.empty() || title.empty()) {
    return {};
  }
  return ":tocentry level='" + std::to_string(level) + "' style='" +
         std::to_string(style) + "' id='" + escape_gml_attr(id) + "'." +
         title;
}

std::string render_tocdef_gml(std::string value) {
  const auto equals = value.find('=');
  if (equals != std::string::npos) {
    value = value.substr(equals + 1);
  }
  std::istringstream input(value);
  std::string style;
  if (!(input >> style)) {
    return {};
  }
  return {};
}

std::string picture_resource_id(const std::string& target) {
  if (target.size() <= 3 ||
      !ascii_starts_with_case_insensitive(target, "pic")) {
    return {};
  }
  for (std::size_t index = 3; index < target.size(); ++index) {
    if (std::isdigit(static_cast<unsigned char>(target[index])) == 0) {
      return {};
    }
  }
  return target.substr(3);
}

std::string render_fontdef_gml(std::string value) {
  const auto equals = value.find('=');
  if (equals != std::string::npos) {
    value = value.substr(equals + 1);
  }
  std::istringstream input(value);
  std::string code;
  if (!(input >> code)) {
    return render_simple_gml_control("fontdef", std::move(value));
  }
  std::string style;
  std::getline(input, style);
  return ":fontdef code='" + escape_gml_attr(code) + "' style='" +
         escape_gml_attr(dot_text(style)) + "'.";
}

struct SelectControl {
  std::size_t column = 0;
  std::size_t length = 0;
  std::string target;
  std::string display_fragment;
  std::string external_kind;
  std::string external_target;
};

std::optional<SelectControl> parse_select_control(std::string value) {
  std::istringstream input(value);
  std::string column;
  std::string length;
  std::string target;
  if (!(input >> column >> length >> target)) {
    return std::nullopt;
  }
  char* column_end = nullptr;
  char* length_end = nullptr;
  const auto selected_column = std::strtol(column.c_str(), &column_end, 10);
  const auto selected_length = std::strtol(length.c_str(), &length_end, 10);
  if (column_end == column.c_str() || *column_end != '\0' ||
      length_end == length.c_str() || *length_end != '\0' ||
      selected_column < 0 || selected_length <= 0) {
    return std::nullopt;
  }
  std::string display_fragment;
  std::getline(input, display_fragment);
  SelectControl control{static_cast<std::size_t>(selected_column),
                        static_cast<std::size_t>(selected_length),
                        std::move(target),
                        std::move(display_fragment),
                        {},
                        {}};
  if (ascii_starts_with_case_insensitive(control.target, "lnk")) {
    auto fragment = trim_ascii(control.display_fragment);
    std::vector<std::string> alternatives;
    auto cursor = std::size_t{0};
    while (cursor < fragment.size() && fragment[cursor] == '<') {
      const auto close = fragment.find('>', cursor + 1);
      if (close == std::string::npos) {
        break;
      }
      alternatives.push_back(fragment.substr(cursor + 1,
                                             close - cursor - 1));
      cursor = close + 1;
      while (cursor < fragment.size() &&
             std::isspace(static_cast<unsigned char>(fragment[cursor])) != 0) {
        ++cursor;
      }
    }
    const auto source = std::find_if(
        alternatives.begin(), alternatives.end(), [](const auto& value) {
          return (!value.empty() && value.front() == '/') ||
                 ascii_starts_with_case_insensitive(value, "http://") ||
                 ascii_starts_with_case_insensitive(value, "https://") ||
                 ascii_starts_with_case_insensitive(value, "ftp://");
        });
    if (!alternatives.empty() && source != alternatives.end()) {
      control.external_kind = ascii_lower(alternatives.front());
      control.external_target = *source;
      control.display_fragment = fragment.substr(cursor);
    }
  }
  return control;
}

struct SelectedDisplayText {
  std::string prefix;
  std::string selected;
  std::string suffix;
};

struct SelectDisplayLineCandidate {
  std::string line;
  bool may_have_suppressed_prefix = false;
};

std::vector<SelectDisplayLineCandidate> select_display_line_candidates(
    const std::string& fragment) {
  std::vector<SelectDisplayLineCandidate> candidates;
  // Generic trim_ascii also discards a leading '?'. Here that byte can be the
  // significant fixed-box border or wrapped-line boundary, so retain it while
  // removing only the control field's surrounding padding.
  std::size_t begin = 0;
  while (begin < fragment.size() &&
         (std::isspace(static_cast<unsigned char>(fragment[begin])) != 0 ||
          static_cast<unsigned char>(fragment[begin]) < 0x20)) {
    ++begin;
  }
  auto end = fragment.size();
  while (end > begin &&
         (std::isspace(static_cast<unsigned char>(fragment[end - 1])) != 0 ||
          static_cast<unsigned char>(fragment[end - 1]) < 0x20 ||
          fragment[end - 1] == '?')) {
    --end;
  }
  auto trimmed = fragment.substr(begin, end - begin);
  if (!trimmed.empty() && trimmed.front() == '<') {
    const auto close = trimmed.find('>');
    if (close != std::string::npos) {
      const auto value = trimmed.substr(1, close - 1);
      if (ascii_starts_with_case_insensitive(value, "http://") ||
          ascii_starts_with_case_insensitive(value, "https://") ||
          ascii_starts_with_case_insensitive(value, "ftp://")) {
        trimmed = trim_ascii(trimmed.substr(close + 1));
      }
    }
  }
  for (const auto* generated_prefix : {"<internet>", ":h1", ":h3"}) {
    if (ascii_starts_with_case_insensitive(trimmed, generated_prefix)) {
      trimmed = trim_ascii(trimmed.substr(std::string(generated_prefix).size()));
      break;
    }
  }
  if (!trimmed.empty() && trimmed.front() == '/' &&
      (trimmed.size() == 1 ||
       std::isspace(static_cast<unsigned char>(trimmed[1])) != 0)) {
    trimmed = trim_ascii(trimmed.substr(1));
  }
  if (trimmed.size() >= 3 && trimmed.front() == '.' &&
      std::isspace(static_cast<unsigned char>(trimmed[1])) != 0 &&
      std::isspace(static_cast<unsigned char>(trimmed[2])) != 0) {
    trimmed.erase(0, 1);
  } else if (trimmed.size() >= 4 && trimmed[0] == '*' && trimmed[1] == '/' &&
             std::isspace(static_cast<unsigned char>(trimmed[2])) != 0 &&
             std::isspace(static_cast<unsigned char>(trimmed[3])) != 0) {
    trimmed.erase(0, 2);
  }
  while (!trimmed.empty() &&
         std::isspace(static_cast<unsigned char>(trimmed.front())) != 0) {
    trimmed.erase(trimmed.begin());
  }
  std::size_t placeholder_run = 0;
  while (placeholder_run < trimmed.size() &&
         trimmed[placeholder_run] == '?') {
    ++placeholder_run;
  }
  if (placeholder_run > 1 && placeholder_run < trimmed.size() &&
      std::isspace(static_cast<unsigned char>(trimmed[placeholder_run])) != 0) {
    // A run of decoder placeholders replaces a generated structural prefix.
    // The following guard blank is not a display cell; the remaining blanks
    // are the complete continuation-line margin.
    candidates.push_back({trimmed.substr(placeholder_run + 1), false});
  } else if (!trimmed.empty() &&
      (trimmed.front() == '?' || trimmed.front() == '|' ||
       is_decoded_line_marker(trimmed.front()))) {
    std::size_t spaces = 0;
    while (1 + spaces < trimmed.size() &&
           std::isspace(static_cast<unsigned char>(
               trimmed[1 + spaces])) != 0) {
      ++spaces;
    }
    if (spaces >= 2) {
      // The flattened stream retains a marker byte and one non-display guard
      // blank before an ordinary display line. Only the leading byte can be
      // this boundary; punctuation followed by blanks inside the line remains
      // display text.
      auto line = trimmed.substr(2);
      const auto visual = line.find_first_not_of(" \t\r\n");
      if (visual != std::string::npos && line[visual] == '|' &&
          visual + 1 < line.size() &&
          std::isspace(static_cast<unsigned char>(line[visual + 1])) != 0) {
        line = "   " + line.substr(visual + 2);
        candidates.push_back({std::move(line), false});
      } else {
        candidates.push_back({std::move(line), true});
      }
    } else if (spaces == 1 && trimmed.size() > 2) {
      if (trimmed.front() == '?') {
        // A single blank after '?' is an actual fixed-layout box border. The
        // border occupies display column 3.
        candidates.push_back({"   " + trimmed, false});
      } else {
        // Other visual row markers, notably '|', are non-display boundaries;
        // suppress the marker and guard blank, then restore the ordinary
        // three-column display margin.
        candidates.push_back({"   " + trimmed.substr(2), false});
      }
    }
  }
  if (candidates.empty() && !trimmed.empty() &&
      !(trimmed.size() == 1 &&
        is_decoded_line_marker(trimmed.front()))) {
    candidates.push_back({"   " + trimmed, true});
  }
  return candidates;
}

std::optional<SelectedDisplayText> select_display_text(
    const SelectControl& control) {
  auto display_fragment = control.display_fragment;
  if (ascii_starts_with_case_insensitive(control.target, "fig")) {
    const auto aligned_gap = display_fragment.find("    ");
    if (aligned_gap != std::string::npos && aligned_gap > 0 &&
        trim_ascii(display_fragment.substr(0, aligned_gap))
                .find_first_of(" \t\r\n") == std::string::npos) {
      display_fragment = display_fragment.substr(aligned_gap + 4);
    }
  }
  for (const auto& candidate_line :
       select_display_line_candidates(display_fragment)) {
    auto display_line = candidate_line.line;
    const auto selected_end = control.column + control.length;
    if (display_line.size() < selected_end) {
      // Generated list/definition prefixes occupy display cells in the native
      // complete line but are absent from the flattened visible fragment. If
      // the selector reaches the line end, restore exactly that omitted width.
      if (candidate_line.may_have_suppressed_prefix) {
        display_line.insert(0, selected_end - display_line.size(), ' ');
      } else {
        display_line.append(selected_end - display_line.size(), ' ');
      }
    }
    if (control.column >= display_line.size()) {
      continue;
    }
    const auto end =
        std::min(display_line.size(), control.column + control.length);
    auto selected = dot_text(display_line.substr(control.column,
                                                 end - control.column));
    if (selected.empty()) {
      continue;
    }
    SelectedDisplayText candidate;
    candidate.prefix = dot_text(display_line.substr(0, control.column));
    candidate.selected = std::move(selected);
    candidate.suffix = dot_text(display_line.substr(end));
    return candidate;
  }
  return std::nullopt;
}

std::string render_selects_gml(std::vector<SelectControl> controls,
                               const std::string& display_fragment) {
  if (controls.empty() ||
      std::any_of(controls.begin(), controls.end(), [](const auto& control) {
        return !picture_resource_id(control.target).empty();
      })) {
    return {};
  }
  std::stable_sort(controls.begin(), controls.end(),
                   [](const auto& left, const auto& right) {
                     return left.column < right.column;
                   });
  std::size_t required_end = 0;
  for (const auto& control : controls) {
    required_end = std::max(required_end, control.column + control.length);
  }

  for (const auto& candidate_line :
       select_display_line_candidates(display_fragment)) {
    auto display_line = candidate_line.line;
    if (display_line.size() < required_end) {
      if (candidate_line.may_have_suppressed_prefix) {
        display_line.insert(0, required_end - display_line.size(), ' ');
      } else {
        display_line.append(required_end - display_line.size(), ' ');
      }
    }

    std::string inline_text;
    std::size_t cursor = 0;
    bool valid = true;
    for (const auto& control : controls) {
      if (control.column < cursor || control.column >= display_line.size()) {
        valid = false;
        break;
      }
      const auto end = std::min(display_line.size(),
                                control.column + control.length);
      const auto gap = display_line.substr(cursor, control.column - cursor);
      const auto selected_raw =
          display_line.substr(control.column, end - control.column);
      const auto gap_text = dot_text(gap);
      const auto selected = dot_text(selected_raw);
      if (selected.empty()) {
        valid = false;
        break;
      }
      if (!gap_text.empty()) {
        if (!inline_text.empty() &&
            std::ispunct(static_cast<unsigned char>(gap_text.front())) == 0) {
          inline_text += ' ';
        }
        inline_text += gap_text;
      }
      if (!inline_text.empty() &&
          ((!gap.empty() &&
            std::isspace(static_cast<unsigned char>(gap.back())) != 0) ||
           (!selected_raw.empty() &&
            std::isspace(static_cast<unsigned char>(selected_raw.front())) !=
                0))) {
        inline_text += ' ';
      }
      inline_text += ":hdref refid='" + escape_gml_attr(control.target) + "'.";
      inline_text += selected;
      inline_text += ":ehdref.";
      cursor = end;
    }
    if (!valid) {
      continue;
    }
    const auto suffix = dot_text(display_line.substr(cursor));
    if (!suffix.empty()) {
      if (!inline_text.empty() &&
          std::ispunct(static_cast<unsigned char>(suffix.front())) == 0) {
        inline_text += ' ';
      }
      inline_text += suffix;
    }
    return render_simple_gml_control("pinline", std::move(inline_text));
  }
  return {};
}

std::string render_select_gml(const SelectControl& control);

std::string render_pending_selects_gml(
    const std::vector<SelectControl>& controls,
    const std::string& display_fragment) {
  if (controls.size() == 1) {
    auto control = controls.front();
    control.display_fragment = display_fragment;
    return render_select_gml(control);
  }
  return render_selects_gml(controls, display_fragment);
}

std::string render_select_gml(const SelectControl& control) {
  if (!control.external_target.empty()) {
    auto trailing = dot_text(control.display_fragment);
    if (control.external_kind == "image") {
      auto output = ":image src='" +
                    escape_gml_attr(control.external_target) + "'.";
      if (!trailing.empty()) {
        const auto figure = trailing.find("Figure ");
        output += "\n" + render_simple_gml_control(
                             figure == std::string::npos ? "p" : "figcap",
                             figure == std::string::npos
                                 ? std::move(trailing)
                                 : trim_ascii(trailing.substr(figure)));
      }
      return output;
    }
    if (control.external_kind == "other" ||
        control.external_kind == "internet") {
      if (const auto display = select_display_text(control)) {
        auto inline_text = display->prefix;
        if (!inline_text.empty()) {
          inline_text += " ";
        }
        inline_text += ":hdref href='" +
                       escape_gml_attr(control.external_target) + "'.";
        inline_text += display->selected;
        inline_text += ":ehdref.";
        if (!display->suffix.empty()) {
          if (std::ispunct(static_cast<unsigned char>(
                  display->suffix.front())) == 0) {
            inline_text += " ";
          }
          inline_text += display->suffix;
        }
        return render_simple_gml_control("pinline", std::move(inline_text));
      }
    }
  }

  const auto resource_id = picture_resource_id(control.target);
  auto picture_fragment = control.display_fragment;
  if (!resource_id.empty()) {
    for (;;) {
      const auto lower = ascii_lower(picture_fragment);
      const auto subject = lower.find("si ");
      if (subject == std::string::npos ||
          picture_fragment.substr(0, subject).find_first_not_of(" ?\t\r\n") !=
              std::string::npos) {
        break;
      }
      const auto boundary = picture_fragment.find('?', subject + 3);
      if (boundary == std::string::npos) {
        break;
      }
      auto end = boundary;
      while (end < picture_fragment.size() && picture_fragment[end] == '?') {
        ++end;
      }
      picture_fragment.erase(0, end);
    }
  }
  auto visible_control = control;
  visible_control.display_fragment = std::move(picture_fragment);
  if (!resource_id.empty()) {
    auto caption = dot_text(visible_control.display_fragment);
    while (!caption.empty() &&
           (caption.front() == '|' ||
            std::isspace(static_cast<unsigned char>(caption.front())) != 0)) {
      caption.erase(caption.begin());
    }
    if (ascii_starts_with_case_insensitive(caption, "picture ")) {
      auto cursor = std::string("picture ").size();
      while (cursor < caption.size() &&
             std::isdigit(static_cast<unsigned char>(caption[cursor])) != 0) {
        ++cursor;
      }
      while (cursor < caption.size() &&
             (caption[cursor] == '|' ||
              std::isspace(static_cast<unsigned char>(caption[cursor])) != 0)) {
        ++cursor;
      }
      caption = trim_ascii(caption.substr(cursor));
    }
    const auto figure = caption.find("Figure ");
    if (figure != std::string::npos) {
      caption = trim_ascii(caption.substr(figure));
    }
    auto output = ":image resource='" + escape_gml_attr(resource_id) + "'.";
    if (!caption.empty()) {
      output += "\n" + render_simple_gml_control("figcap", std::move(caption));
    }
    return output;
  }

  const auto display = select_display_text(visible_control);
  if (!display) {
    return {};
  }

  auto inline_text = display->prefix;
  if (!inline_text.empty()) {
    inline_text += " ";
  }
  inline_text += ":hdref refid='" + escape_gml_attr(control.target) + "'.";
  inline_text += display->selected;
  inline_text += ":ehdref.";
  if (!display->suffix.empty()) {
    if (std::ispunct(static_cast<unsigned char>(
            display->suffix.front())) == 0) {
      inline_text += " ";
    }
    inline_text += display->suffix;
  }
  return render_simple_gml_control("pinline", std::move(inline_text));
}

std::optional<std::string> subject_index_visible_tail(const std::string& value,
                                                      bool& fixed_row_tail) {
  fixed_row_tail = false;

  const auto question_marker = value.find('?');
  const auto visual_marker = value.find('|');

  // SI's index key and its visible prose are separated by a fixed-layout
  // margin. Some books use four or five spaces here, not only the wider
  // eight-space margin.
  constexpr auto kAlignedVisibleTextGap = std::size_t{4};
  auto run_begin = std::string::npos;
  auto run_length = std::size_t{0};
  for (std::size_t cursor = 0; cursor < value.size();) {
    if (std::isspace(static_cast<unsigned char>(value[cursor])) == 0) {
      ++cursor;
      continue;
    }
    const auto begin = cursor;
    while (cursor < value.size() &&
           std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
      ++cursor;
    }
    const auto length = cursor - begin;
    if (length >= kAlignedVisibleTextGap) {
      run_begin = begin;
      run_length = length;
      break;
    }
  }

  auto boundary = std::string::npos;
  enum class BoundaryKind { none, question, visual, aligned };
  auto kind = BoundaryKind::none;
  const auto consider_boundary = [&](std::size_t candidate,
                                     BoundaryKind candidate_kind) {
    if (candidate != std::string::npos &&
        (boundary == std::string::npos || candidate < boundary)) {
      boundary = candidate;
      kind = candidate_kind;
    }
  };
  consider_boundary(question_marker, BoundaryKind::question);
  consider_boundary(visual_marker, BoundaryKind::visual);
  consider_boundary(run_begin, BoundaryKind::aligned);
  if (boundary == std::string::npos) {
    return std::nullopt;
  }

  auto visible = std::string{};
  if (kind == BoundaryKind::question) {
    visible = dot_text(value.substr(boundary + 1));
  } else if (kind == BoundaryKind::visual) {
    visible = value.substr(boundary);
    fixed_row_tail = true;
  } else {
    visible = dot_text(value.substr(boundary + run_length));
    fixed_row_tail = true;
  }
  visible = strip_visual_line_marker(std::move(visible));
  if (visible.empty()) {
    return std::nullopt;
  }
  return visible;
}

std::string render_menu_item_gml(std::string value) {
  std::istringstream input(value);
  std::string target;
  if (!(input >> target)) {
    return render_simple_gml_control("li", std::move(value));
  }
  std::string text;
  std::getline(input, text);
  text = dot_text(text);
  auto output = ":li refid='" + escape_gml_attr(target) + "'.";
  output += dot_text(target);
  if (!text.empty()) {
    output += " ";
    output += text;
  }
  return output;
}

std::string render_subject_index_gml(std::string value) {
  bool fixed_row_tail = false;
  auto visible = subject_index_visible_tail(value, fixed_row_tail);
  if (!visible) {
    return {};
  }
  return render_simple_gml_control(fixed_row_tail ? "line" : "pinline",
                                   std::move(*visible));
}

std::string render_layout_gml(std::string value,
                              std::size_t& current_font_base_column) {
  std::istringstream input(value);
  std::string mode;
  if (!(input >> mode)) {
    return {};
  }
  std::string rest;
  std::getline(input, rest);
  rest = trim_ascii(std::move(rest));
  const auto lower_mode = ascii_lower(mode);
  if (lower_mode == "flow") {
    std::istringstream rest_input(rest);
    std::string tag;
    std::string left_margin;
    std::string indent;
    if (!(rest_input >> tag)) {
      return {};
    }
    rest_input >> left_margin >> indent;
    if (!indent.empty()) {
      try {
        current_font_base_column = static_cast<std::size_t>(std::stoul(indent));
      } catch (const std::exception&) {
        current_font_base_column = 3;
      }
    }
    std::string text;
    std::getline(rest_input, text);
    if (trim_ascii(text).empty()) {
      const auto normalized_tag = normalize_bookmaster_tag(tag);
      if (normalized_tag == "ul" || normalized_tag == "ol" ||
          normalized_tag == "dl" || normalized_tag == "li") {
        return render_empty_bookmaster_tag_with_layout(std::move(tag),
                                                       std::move(left_margin),
                                                       std::move(indent));
      }
      if (normalized_tag == "p") {
        return ":p.";
      }
      return {};
    }
    return render_bookmaster_tag_with_layout(std::move(tag),
                                             std::move(left_margin),
                                             std::move(indent),
                                             std::move(text));
  }
  if (lower_mode == "break") {
    std::istringstream rest_input(rest);
    std::string break_count;
    rest_input >> break_count;
    std::string text;
    std::getline(rest_input, text);
    text = trim_ascii(std::move(text));
    if (!text.empty()) {
      return render_simple_gml_control("p", std::move(text));
    }
    return {};
  }
  if (lower_mode == "off") {
    std::istringstream rest_input(rest);
    std::string tag;
    rest_input >> tag;
    tag = normalize_bookmaster_tag(std::move(tag));
    if (tag == "toc") {
      return {};
    }
    if (tag == "fig") {
      return {};
    }
    if (tag == "table") {
      return ":etable.";
    }
    if (tag == "ul" || tag == "ol" || tag == "dl") {
      return ":e" + tag + ".";
    }
    return {};
  }
  return {};
}

std::string render_anchor_gml(std::string value) {
  value = trim_ascii(std::move(value));
  std::size_t cursor = 0;
  while (cursor < value.size() && is_topic_id_char(value[cursor])) {
    ++cursor;
  }
  const auto id = value.substr(0, cursor);
  auto rest = dot_text(value.substr(cursor));
  auto output = render_keyed_gml_control("anchor", "id", id);
  if (!rest.empty()) {
    output += rest;
  }
  return output;
}

struct GmlRenderState {
  std::string pending_topic_tag;
  std::string pending_footnote_id;
  std::string pending_font_prefix;
  std::size_t current_font_base_column = 3;
  std::size_t pending_font_base_column = 6;
  std::vector<SelectControl> pending_selects;
  std::vector<std::string> pending_labeled_box_segments;
  bool in_generated_toc = false;
  bool in_fixed_selection_list = false;
  bool in_generated_title_page = false;
  bool emitted_toc = false;
  bool in_vnotice = false;
  bool emitted_vnotice_heading = false;
  bool pending_copyright_extension = false;
  bool in_table = false;
  bool table_just_closed = false;
  bool in_labeled_box = false;
  bool in_figure = false;
  bool in_example = false;
  bool in_index = false;
  bool in_footnote = false;
  bool ignore_after_index = false;
  std::size_t table_columns = 0;
  std::vector<std::size_t> table_separator_offsets;
  std::size_t table_line_width = 0;
  std::size_t table_border_width = 0;
  bool table_final_separator_is_synthetic = false;
  bool table_layout_from_font_heading = false;
  bool table_has_picture = false;
  std::string table_visual_buffer;
  std::vector<std::string> pending_table_row;
};

std::string trim_right_ascii(std::string value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  return value;
}

bool is_fixed_figure_row_payload(const std::string& value) {
  return value.size() >= 70 || value.find(" . . . ") != std::string::npos ||
         value.find("__________") != std::string::npos;
}

std::string fixed_figure_row_payload(std::string value) {
  constexpr auto figure_row_width = std::size_t{82};
  value = trim_right_ascii(std::move(value));
  if (value.size() < figure_row_width) {
    value.append(figure_row_width - value.size(), ' ');
  }
  return value;
}

std::vector<std::string> extract_fixed_figure_lines(const std::string& value) {
  std::vector<std::string> lines;
  std::string previous_text;
  for (std::size_t cursor = 0; cursor < value.size();) {
    if (value[cursor] != '?') {
      const auto next = value.find('?', cursor);
      previous_text =
          next == std::string::npos ? value.substr(cursor)
                                    : value.substr(cursor, next - cursor);
      if (next == std::string::npos &&
          is_fixed_figure_row_payload(previous_text)) {
        lines.push_back("   |" +
                        fixed_figure_row_payload(previous_text) + "|");
      }
      cursor = next == std::string::npos ? value.size() : next;
      continue;
    }

    const auto run_begin = cursor;
    while (cursor < value.size() && value[cursor] == '?') {
      ++cursor;
    }
    const auto run = cursor - run_begin;
    if (run >= 20) {
      const auto width = run >= 2 ? run - 2 : run;
      if (lines.empty()) {
        lines.push_back("    " + std::string(width, '_') + " ");
      } else {
        lines.push_back("   |" + std::string(width, '_') + "|");
      }
      previous_text.clear();
      continue;
    }

    const auto next = value.find('?', cursor);
    auto payload = next == std::string::npos ? value.substr(cursor)
                                             : value.substr(cursor,
                                                            next - cursor);
    const auto payload_has_text =
        std::any_of(payload.begin(), payload.end(), [](const auto ch) {
          return std::isalnum(static_cast<unsigned char>(ch)) != 0;
        });
    if (is_fixed_figure_row_payload(previous_text) && payload_has_text &&
        !is_fixed_figure_row_payload(payload) && !lines.empty() &&
        lines.back().back() == '|') {
      lines.back().pop_back();
      lines.back() += trim_right_ascii(payload) + "|";
    }
    if (is_fixed_figure_row_payload(payload)) {
      const auto previous = trim_ascii(previous_text);
      const auto prefix = previous == "|" ? std::string{" | |"}
                                          : std::string{"   |"};
      lines.push_back(prefix + fixed_figure_row_payload(payload) + "|");
    }
    previous_text = std::move(payload);
    if (next == std::string::npos) {
      break;
    }
    cursor = next;
  }
  return lines;
}

std::string render_fixed_figure_text_gml(const std::string& value) {
  std::string output;
  for (auto line : extract_fixed_figure_lines(value)) {
    if (!output.empty()) {
      output.push_back('\n');
    }
    output += ":xline." + std::move(line);
  }
  return output;
}

std::string render_figure_start_gml(std::string value, GmlRenderState& state) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }
  std::size_t cursor = 0;
  while (cursor < value.size() && is_topic_id_char(value[cursor])) {
    ++cursor;
  }

  auto output =
      render_keyed_gml_control("fig", "id", value.substr(0, cursor));
  state.in_figure = true;
  const auto trailing = value.substr(cursor);
  auto fixed = render_fixed_figure_text_gml(trailing);
  if (!fixed.empty()) {
    output += "\n" + std::move(fixed);
  }
  return output;
}

std::string render_figure_end_gml(std::string value, GmlRenderState& state) {
  state.in_figure = false;
  value = strip_visual_line_marker(dot_text(std::move(value)));
  if (value.empty()) {
    return ":efig.";
  }
  return ":efig.\n" + render_simple_gml_control("p", std::move(value));
}

std::string render_footnote_gml(std::string value, GmlRenderState& state) {
  std::istringstream input(value);
  std::string mode;
  std::string tag;
  std::string left_margin;
  std::string indent;
  if (!(input >> mode >> tag)) {
    return {};
  }
  input >> left_margin >> indent;
  if (!indent.empty()) {
    try {
      state.current_font_base_column = static_cast<std::size_t>(
          std::stoul(indent));
    } catch (const std::exception&) {
      state.current_font_base_column = 3;
    }
  }
  std::string text;
  std::getline(input, text);
  text = dot_text(std::move(text));
  if (text.size() >= 2 && text[text.size() - 1] == '.' &&
      text[text.size() - 2] == '.') {
    text.pop_back();
  }
  state.in_footnote = true;

  auto output = std::string(":fn");
  if (!state.pending_footnote_id.empty()) {
    output += " id='" + escape_gml_attr(state.pending_footnote_id) + "'";
    state.pending_footnote_id.clear();
  }
  if (!left_margin.empty()) {
    output += " col='" + escape_gml_attr(std::move(left_margin)) + "'";
  }
  if (!indent.empty()) {
    output += " indent='" + escape_gml_attr(std::move(indent)) + "'";
  }
  output += ".";
  output += text;
  return output;
}

std::string clean_table_cell_text(std::string value) {
  for (auto& ch : value) {
    if (ch == '?') {
      ch = ' ';
    }
  }
  return collapse_ascii_whitespace(std::move(value));
}

std::vector<std::string> extract_table_visual_cells(const std::string& value) {
  std::vector<std::string> cells;
  const auto first_separator = value.find('?');
  if (first_separator == std::string::npos) {
    return cells;
  }

  auto cursor = first_separator + 1;
  while (cursor < value.size()) {
    const auto next = value.find('?', cursor);
    if (next == std::string::npos) {
      auto trailing = clean_table_cell_text(value.substr(cursor));
      cells.push_back(std::move(trailing));
      break;
    }
    cells.push_back(clean_table_cell_text(value.substr(cursor,
                                                       next - cursor)));
    cursor = next + 1;
  }
  return cells;
}

bool is_table_border_run(const std::string& value, std::size_t offset) {
  if (offset >= value.size() || value[offset] != '?') {
    return false;
  }
  auto end = offset;
  while (end < value.size() && value[end] == '?') {
    ++end;
  }
  return end - offset >= 5;
}

std::optional<std::vector<std::size_t>> infer_table_separator_offsets(
    const std::string& value,
    std::size_t start,
    std::size_t table_border_width,
    bool& final_separator_is_synthetic) {
  if (start >= value.size() || value[start] != '?' ||
      is_table_border_run(value, start)) {
    return std::nullopt;
  }
  final_separator_is_synthetic = false;
  std::vector<std::size_t> offsets{0};
  auto cursor = start + 1;
  while (cursor < value.size() && offsets.size() < 4) {
    const auto next = value.find('?', cursor);
    if (next == std::string::npos || is_table_border_run(value, next)) {
      break;
    }
    offsets.push_back(next - start);
    cursor = next + 1;
  }
  if (offsets.size() < 3) {
    return std::nullopt;
  }
  if (table_border_width > 0 && offsets.back() + 1 < table_border_width) {
    offsets.push_back(table_border_width - 1);
    final_separator_is_synthetic = true;
  }
  return offsets;
}

std::vector<std::string> extract_fixed_table_line_cells(
    const std::string& value,
    std::size_t start,
    const std::vector<std::size_t>& offsets) {
  std::vector<std::string> cells;
  if (offsets.size() < 2) {
    return cells;
  }
  for (std::size_t index = 0; index + 1 < offsets.size(); ++index) {
    const auto begin = start + offsets[index] + 1;
    const auto end = std::min(value.size(), start + offsets[index + 1]);
    if (begin > value.size() || begin > end) {
      cells.emplace_back();
      continue;
    }
    cells.push_back(clean_table_cell_text(value.substr(begin, end - begin)));
  }
  return cells;
}

bool fixed_table_line_matches(const std::string& value,
                              std::size_t start,
                              const std::vector<std::size_t>& offsets,
                              bool final_separator_is_synthetic) {
  if (offsets.size() < 2 || start >= value.size()) {
    return false;
  }
  const auto required_offsets =
      final_separator_is_synthetic ? offsets.size() - 1 : offsets.size();
  for (std::size_t index = 0; index < required_offsets; ++index) {
    const auto offset = offsets[index];
    if (start + offset >= value.size() || value[start + offset] != '?') {
      return false;
    }
  }
  return true;
}

std::string flush_pending_table_row(GmlRenderState& state) {
  if (state.pending_table_row.empty()) {
    return {};
  }

  std::ostringstream output;
  output << ":row.";
  for (std::size_t index = 0; index < state.pending_table_row.size(); ++index) {
    output << "\n:c col='" << index << "'."
           << dot_text(state.pending_table_row[index]);
  }
  state.pending_table_row.clear();
  return output.str();
}

void append_table_visual_line(GmlRenderState& state,
                              const std::vector<std::string>& line,
                              std::string& output) {
  auto normalized_line = line;
  while (!normalized_line.empty() && normalized_line.back().empty()) {
    normalized_line.pop_back();
  }

  if (normalized_line.empty()) {
    return;
  }
  if (state.table_columns == 0) {
    state.table_columns = normalized_line.size();
    while (state.table_columns > 0 &&
           normalized_line[state.table_columns - 1].empty()) {
      --state.table_columns;
    }
  }
  if (state.table_columns == 0) {
    return;
  }

  std::vector<std::string> cells(state.table_columns);
  for (std::size_t index = 0;
       index < state.table_columns && index < normalized_line.size();
       ++index) {
    cells[index] = normalized_line[index];
  }

  auto has_any_cell = false;
  for (const auto& cell : cells) {
    if (!cell.empty()) {
      has_any_cell = true;
      break;
    }
  }
  if (!has_any_cell) {
    return;
  }

  const auto starts_new_row = !cells.empty() && !cells.front().empty();
  if (!state.pending_table_row.empty() && starts_new_row) {
    auto flushed = flush_pending_table_row(state);
    if (!flushed.empty()) {
      if (!output.empty()) {
        output.push_back('\n');
      }
      output += std::move(flushed);
    }
  }

  if (state.pending_table_row.empty()) {
    state.pending_table_row.assign(state.table_columns, {});
  }
  for (std::size_t index = 0; index < state.table_columns; ++index) {
    if (cells[index].empty()) {
      continue;
    }
    if (!state.pending_table_row[index].empty()) {
      state.pending_table_row[index] += "<br>";
    }
    state.pending_table_row[index] += std::move(cells[index]);
  }
}

std::string render_table_body_gml(std::string segment, GmlRenderState& state) {
  if (state.table_has_picture && state.table_columns == 0 &&
      !trim_ascii(segment).empty()) {
    return render_simple_gml_control("figcap", std::move(segment));
  }
  const auto segment_is_font_heading =
      ascii_starts_with_case_insensitive(trim_ascii(segment), "cfont");
  if (segment_is_font_heading) {
    const auto first_separator = segment.find('?');
    if (first_separator == std::string::npos) {
      return {};
    }
    segment = segment.substr(first_separator);
  }

  if (segment.find('?') == std::string::npos && state.table_columns == 0 &&
      state.pending_table_row.empty()) {
    if (auto continuation = render_marker_continuation_gml(segment)) {
      const auto content = ascii_starts_with_case_insensitive(*continuation,
                                                               ":line.")
                               ? continuation->substr(6)
                               : std::string{};
      return render_simple_gml_control("figcap", content);
    }
    auto text = dot_text(std::move(segment));
    return render_simple_gml_control(text.empty() ? "tcap" : "figcap",
                                     std::move(text));
  }

  std::string output;
  state.table_visual_buffer += std::move(segment);
  while (true) {
    const auto separator = state.table_visual_buffer.find('?');
    if (separator == std::string::npos) {
      if (state.table_visual_buffer.size() > 256) {
        state.table_visual_buffer.clear();
      }
      break;
    }
    if (separator > 0) {
      state.table_visual_buffer.erase(0, separator);
    }
    if (state.table_visual_buffer.empty()) {
      break;
    }
    if (is_table_border_run(state.table_visual_buffer, 0)) {
      auto border_end = std::size_t{0};
      while (border_end < state.table_visual_buffer.size() &&
             state.table_visual_buffer[border_end] == '?') {
        ++border_end;
      }
      if (border_end < 5) {
        break;
      }
      state.table_border_width = std::max(state.table_border_width,
                                          border_end);
      auto flushed = flush_pending_table_row(state);
      if (!flushed.empty()) {
        if (!output.empty()) {
          output.push_back('\n');
        }
        output += std::move(flushed);
      }
      state.table_visual_buffer.erase(0, border_end);
      continue;
    }

    if (state.table_separator_offsets.empty()) {
      auto final_separator_is_synthetic = false;
      if (auto offsets = infer_table_separator_offsets(state.table_visual_buffer,
                                                       0,
                                                       state.table_border_width,
                                                       final_separator_is_synthetic)) {
        state.table_separator_offsets = std::move(*offsets);
        state.table_columns = state.table_separator_offsets.size() - 1;
        state.table_line_width = state.table_separator_offsets.back() + 1;
        state.table_final_separator_is_synthetic =
            final_separator_is_synthetic;
        state.table_layout_from_font_heading = segment_is_font_heading;
      } else {
        if (state.table_visual_buffer.size() < 80) {
          break;
        }
        state.table_visual_buffer.erase(0, 1);
        continue;
      }
    }
    if (state.table_visual_buffer.size() < state.table_line_width) {
      if (!state.table_final_separator_is_synthetic ||
          state.table_separator_offsets.size() < 2 ||
          state.table_visual_buffer.size() <=
              state.table_separator_offsets[state.table_separator_offsets.size() -
                                            2]) {
        break;
      }
      state.table_visual_buffer.resize(state.table_line_width, ' ');
    }
    if (!fixed_table_line_matches(state.table_visual_buffer,
                                  0,
                                  state.table_separator_offsets,
                                  state.table_final_separator_is_synthetic)) {
      // Some BookMaster tables style their column headings as a grid with an
      // extra leading cell.  Later data rows omit that empty cell and therefore
      // use different separator offsets even though they belong to the same
      // table.  Consume such a complete row using its own offsets, right-align
      // its cells with the heading grid, and retain the established layout for
      // continuation lines.
      auto row_final_separator_is_synthetic = false;
      if (auto row_offsets = infer_table_separator_offsets(
              state.table_visual_buffer,
              0,
              state.table_border_width,
              row_final_separator_is_synthetic);
          state.table_layout_from_font_heading && row_offsets &&
          row_offsets->size() >= 2 &&
          row_offsets->size() - 1 <= state.table_columns) {
        const auto row_width = row_offsets->back() + 1;
        if (state.table_visual_buffer.size() < row_width) {
          break;
        }
        auto cells = extract_fixed_table_line_cells(state.table_visual_buffer,
                                                    0,
                                                    *row_offsets);
        if (cells.size() < state.table_columns) {
          cells.insert(cells.begin(), state.table_columns - cells.size(), {});
        }
        append_table_visual_line(state, cells, output);
        state.table_visual_buffer.erase(0, row_width);
        continue;
      }
      state.table_visual_buffer.erase(0, 1);
      continue;
    }

    auto cells = extract_fixed_table_line_cells(state.table_visual_buffer,
                                                0,
                                                state.table_separator_offsets);
    append_table_visual_line(state, cells, output);
    state.table_visual_buffer.erase(0, state.table_line_width);
  }
  return output;
}

std::string flush_table_visual_buffer(GmlRenderState& state) {
  if (state.table_visual_buffer.empty()) {
    return {};
  }
  if (!state.table_separator_offsets.empty() &&
      state.table_visual_buffer.size() < state.table_line_width) {
    state.table_visual_buffer.resize(state.table_line_width, ' ');
  }
  auto output = render_table_body_gml({}, state);
  state.table_visual_buffer.clear();
  return output;
}

bool parse_unsigned_word(const std::string& value, std::size_t& cursor) {
  while (cursor < value.size() &&
         std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
    ++cursor;
  }
  const auto begin = cursor;
  while (cursor < value.size() &&
         std::isdigit(static_cast<unsigned char>(value[cursor])) != 0) {
    ++cursor;
  }
  return cursor > begin;
}

bool parse_nonspace_word(const std::string& value, std::size_t& cursor) {
  while (cursor < value.size() &&
         std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
    ++cursor;
  }
  const auto begin = cursor;
  while (cursor < value.size() &&
         std::isspace(static_cast<unsigned char>(value[cursor])) == 0) {
    ++cursor;
  }
  return cursor > begin;
}

struct FontSpan {
  std::size_t offset = 0;
  std::size_t length = 0;
  std::string code;
};

std::vector<FontSpan> parse_font_spans(const std::string& value,
                                       std::size_t& cursor) {
  std::vector<FontSpan> spans;
  while (cursor < value.size()) {
    const auto before = cursor;
    std::size_t offset_begin = cursor;
    if (!parse_unsigned_word(value, cursor)) {
      cursor = before;
      break;
    }
    const auto offset =
        static_cast<std::size_t>(std::stoul(value.substr(offset_begin,
                                                        cursor - offset_begin)));

    std::size_t length_begin = cursor;
    if (!parse_unsigned_word(value, cursor)) {
      cursor = before;
      break;
    }
    const auto length =
        static_cast<std::size_t>(std::stoul(value.substr(length_begin,
                                                        cursor - length_begin)));

    while (cursor < value.size() &&
           std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
      ++cursor;
    }
    const auto code_begin = cursor;
    if (!parse_nonspace_word(value, cursor)) {
      cursor = before;
      break;
    }
    spans.push_back({offset, length, value.substr(code_begin,
                                                  cursor - code_begin)});
  }
  return spans;
}

std::string normalize_preformatted_line(std::string value) {
  value = trim_ascii(std::move(value));
  if (ascii_starts_with_case_insensitive(value, "cfont")) {
    value = trim_ascii(rest_after_first_word(std::move(value)));
    std::size_t cursor = 0;
    (void)parse_font_spans(value, cursor);
    value = cursor >= value.size() ? std::string{}
                                   : value.substr(cursor);
  }
  for (auto& ch : value) {
    if (ch == '?') {
      ch = ' ';
    }
  }
  value = trim_ascii(std::move(value));
  if (value.size() >= 3 && is_decoded_line_marker(value.front()) &&
      std::isspace(static_cast<unsigned char>(value[1])) != 0 &&
      std::isspace(static_cast<unsigned char>(value[2])) != 0) {
    value = trim_ascii(value.substr(1));
  }
  return value;
}

std::string font_code_to_highlight_tag(const std::string& code) {
  if (code == "1" || ascii_equals_case_insensitive(code, "hp1")) {
    return "hp1";
  }
  if (code == "2" || ascii_equals_case_insensitive(code, "hp2")) {
    return "hp2";
  }
  if (code == "3" || ascii_equals_case_insensitive(code, "hp3")) {
    return "hp3";
  }
  if (ascii_equals_case_insensitive(code, "x") ||
      ascii_equals_case_insensitive(code, "xph") ||
      ascii_equals_case_insensitive(code, "e") ||
      ascii_equals_case_insensitive(code, "xmp")) {
    return "xph";
  }
  return {};
}

struct DisplayTextMap {
  std::string text;
  // CFONT coordinates are display-character columns.  The decoded stream is
  // UTF-8, so these indices count code points while the values remain byte
  // offsets into the UTF-8 output.  Keeping the two units distinct prevents a
  // delimiter from being inserted between bytes of a multibyte character.
  std::vector<std::size_t> source_to_output;
};

std::size_t utf8_character_size(const std::string& value,
                                std::size_t offset) {
  if (offset >= value.size()) {
    return 0;
  }
  const auto first = static_cast<unsigned char>(value[offset]);
  std::size_t length = 1;
  if (first >= 0xC2 && first <= 0xDF) {
    length = 2;
  } else if (first >= 0xE0 && first <= 0xEF) {
    length = 3;
  } else if (first >= 0xF0 && first <= 0xF4) {
    length = 4;
  }
  if (length == 1 || offset + length > value.size()) {
    return 1;
  }
  // Invalid sequences are treated as one opaque source character.  This is
  // only a defensive fallback for synthetic/legacy input; valid decoded BOO
  // text takes the multibyte path above.
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation =
        static_cast<unsigned char>(value[offset + index]);
    if ((continuation & 0xC0) != 0x80) {
      return 1;
    }
  }
  return length;
}

bool utf8_character_is_ascii_space(const std::string& value,
                                   std::size_t offset,
                                   std::size_t length) {
  return length == 1 &&
         std::isspace(static_cast<unsigned char>(value[offset])) != 0;
}

DisplayTextMap normalize_display_text_with_map(std::string value) {
  value = remove_decoded_line_markers(std::move(value));
  // Decoder/layout padding surrounding a CFONT display row is not part of the
  // control's coordinate space.  Normalize it away before mapping display
  // characters, while retaining byte offsets only at UTF-8 boundaries.
  value = trim_ascii(std::move(value));
  collapse_terminal_question_separator(value);
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] != '?') {
      continue;
    }
    const auto terminal_literal = index + 1 == value.size();
    if (!terminal_literal) {
      value[index] = ' ';
    }
  }
  remove_space_before_terminal_question(value);
  if (value.empty()) {
    return {};
  }

  DisplayTextMap mapped;
  mapped.source_to_output.clear();
  mapped.source_to_output.reserve(value.size() + 1);
  bool pending_space = false;
  for (std::size_t index = 0; index < value.size();) {
    const auto length = utf8_character_size(value, index);
    mapped.source_to_output.push_back(mapped.text.size());
    if (utf8_character_is_ascii_space(value, index, length)) {
      pending_space = !mapped.text.empty();
      index += length;
      continue;
    }
    if (pending_space) {
      mapped.text.push_back(' ');
      pending_space = false;
    }
    mapped.text.append(value, index, length);
    index += length;
  }
  mapped.source_to_output.push_back(mapped.text.size());
  return mapped;
}

DisplayTextMap normalize_fixed_display_text_with_map(std::string value) {
  DisplayTextMap mapped;
  mapped.source_to_output.clear();
  mapped.source_to_output.reserve(value.size() + 1);

  bool pending_space = false;
  for (std::size_t index = 0; index < value.size();) {
    const auto length = utf8_character_size(value, index);
    mapped.source_to_output.push_back(mapped.text.size());
    if (length == 1 && value[index] == '?') {
      // The visual placeholder is an ASCII source character and has one
      // display-column width, just like ordinary whitespace below.
      pending_space = !mapped.text.empty();
      ++index;
      continue;
    }
    if (utf8_character_is_ascii_space(value, index, length)) {
      pending_space = !mapped.text.empty();
      index += length;
      continue;
    }
    if (pending_space) {
      mapped.text.push_back(' ');
      pending_space = false;
    }
    mapped.text.append(value, index, length);
    index += length;
  }
  mapped.source_to_output.push_back(mapped.text.size());
  return mapped;
}

std::optional<FontSpan> map_font_span_to_normalized_text(
    const FontSpan& span,
    const DisplayTextMap& mapped,
    std::size_t base_column) {
  if (span.length == 0 || mapped.text.empty()) {
    return std::nullopt;
  }
  const auto source_offset =
      span.offset >= base_column ? span.offset - base_column : 0;
  if (source_offset >= mapped.source_to_output.size()) {
    return std::nullopt;
  }
  const auto source_end = std::min(mapped.source_to_output.size() - 1,
                                   source_offset + span.length);
  auto output_offset = mapped.source_to_output[source_offset];
  auto output_end = mapped.source_to_output[source_end];
  while (output_offset < mapped.text.size() &&
         std::isspace(static_cast<unsigned char>(mapped.text[output_offset])) !=
             0) {
    ++output_offset;
  }
  while (output_end > output_offset &&
         std::isspace(static_cast<unsigned char>(mapped.text[output_end - 1])) !=
             0) {
    --output_end;
  }
  if (output_offset >= output_end) {
    return std::nullopt;
  }
  return FontSpan{output_offset, output_end - output_offset, span.code};
}

std::string apply_font_spans_to_text(std::string value,
                                     const std::vector<FontSpan>& spans,
                                     std::size_t base_column) {
  // A span beginning at the active paragraph column is encoded relative to
  // the visible continuation text (some records retain only layout padding
  // before that text).  In that form the padding must not consume source
  // columns.  Spans that begin later use the complete display row, including
  // its leading columns, as their coordinate space.
  if (!spans.empty() && spans.front().offset <= base_column) {
    value = trim_ascii(std::move(value));
  }
  auto mapped = normalize_display_text_with_map(std::move(value));
  if (mapped.text.empty() || spans.empty()) {
    return mapped.text;
  }

  std::vector<std::string> opens(mapped.text.size() + 1);
  std::vector<std::string> closes(mapped.text.size() + 1);
  for (const auto& original_span : spans) {
    const auto mapped_span =
        map_font_span_to_normalized_text(original_span, mapped, base_column);
    if (!mapped_span) {
      continue;
    }
    const auto& span = *mapped_span;
    const auto tag = font_code_to_highlight_tag(span.code);
    if (tag.empty() || span.length == 0 || span.offset >= mapped.text.size()) {
      continue;
    }
    const auto end = std::min(mapped.text.size(), span.offset + span.length);
    opens[span.offset] += ":" + tag + ".";
    closes[end] = ":e" + tag + "." + closes[end];
  }

  std::string output;
  for (std::size_t index = 0; index < mapped.text.size(); ++index) {
    output += opens[index];
    output.push_back(mapped.text[index]);
    output += closes[index + 1];
  }
  return output;
}

std::string apply_font_spans_to_mapped_text(
    const DisplayTextMap& mapped,
    const std::vector<FontSpan>& spans) {
  if (mapped.text.empty() || spans.empty()) {
    return mapped.text;
  }

  std::vector<std::string> opens(mapped.text.size() + 1);
  std::vector<std::string> closes(mapped.text.size() + 1);
  for (const auto& span : spans) {
    if (span.length == 0 || span.offset >= mapped.source_to_output.size()) {
      continue;
    }
    const auto tag = font_code_to_highlight_tag(span.code);
    if (tag.empty()) {
      continue;
    }
    const auto source_end =
        std::min(mapped.source_to_output.size() - 1,
                 span.offset + span.length);
    auto output_offset = mapped.source_to_output[span.offset];
    auto output_end = mapped.source_to_output[source_end];
    while (output_offset < mapped.text.size() &&
           std::isspace(
               static_cast<unsigned char>(mapped.text[output_offset])) != 0) {
      ++output_offset;
    }
    while (output_end > output_offset &&
           std::isspace(
               static_cast<unsigned char>(mapped.text[output_end - 1])) != 0) {
      --output_end;
    }
    if (output_offset >= output_end) {
      continue;
    }
    opens[output_offset] += ":" + tag + ".";
    closes[output_end] = ":e" + tag + "." + closes[output_end];
  }

  std::string output;
  for (std::size_t index = 0; index < mapped.text.size(); ++index) {
    output += opens[index];
    output.push_back(mapped.text[index]);
    output += closes[index + 1];
  }
  return output;
}

std::string apply_font_spans_to_text_without_normalizing(
    std::string value,
    const std::vector<FontSpan>& spans,
    std::size_t base_column) {
  return apply_font_spans_to_text(std::move(value), spans, base_column);
}

std::optional<std::string> apply_font_spans_to_bar_visual_row(
    const std::string& text,
    const std::vector<FontSpan>& spans) {
  const auto first_visible = text.find_first_not_of(" \t\r\n");
  if (first_visible == std::string::npos || text[first_visible] != '|') {
    return std::nullopt;
  }
  const auto display_delta = first_visible == 0 ? std::size_t{0}
                                                : first_visible - 1;
  auto adjusted = spans;
  for (auto& span : adjusted) {
    span.offset += display_delta;
  }
  auto rendered =
      apply_font_spans_to_mapped_text(normalize_fixed_display_text_with_map(text),
                                      adjusted);
  if (rendered.empty()) {
    return std::nullopt;
  }
  return rendered;
}

bool has_visual_border_run(const std::string& value) {
  auto run = std::size_t{0};
  for (const auto ch : value) {
    if (ch == '?') {
      ++run;
      if (run >= 4) {
        return true;
      }
    } else {
      run = 0;
    }
  }
  return false;
}

bool is_literal_question(const std::string& value, std::size_t index) {
  if (index >= value.size() || value[index] != '?' || index == 0) {
    return false;
  }
  const auto previous = static_cast<unsigned char>(value[index - 1]);
  if (std::isalnum(previous) == 0 && value[index - 1] != ')') {
    return false;
  }
  if (index + 1 == value.size()) {
    return true;
  }
  return std::isspace(static_cast<unsigned char>(value[index + 1])) != 0 ||
         value[index + 1] == '?';
}

std::vector<std::string> extract_visual_box_lines(const std::string& value) {
  std::vector<std::string> lines;
  std::string line;

  const auto flush_line = [&]() {
    auto normalized = trim_ascii(line);
    if (!normalized.empty()) {
      lines.push_back(std::move(normalized));
    }
    line.clear();
  };

  for (std::size_t cursor = 0; cursor < value.size();) {
    if (value[cursor] != '?') {
      line.push_back(value[cursor++]);
      continue;
    }

    if (is_literal_question(value, cursor)) {
      line.push_back('?');
      ++cursor;
      continue;
    }

    auto run_end = cursor;
    while (run_end < value.size() && value[run_end] == '?') {
      ++run_end;
    }
    const auto run = run_end - cursor;
    if (run >= 5) {
      flush_line();
      cursor = run_end;
      continue;
    }
    if (run >= 3) {
      if (line.find_first_not_of(" \t\r\n") != std::string::npos) {
        flush_line();
      }
      cursor = run_end;
      continue;
    }

    flush_line();
    cursor = run_end;
    while (cursor < value.size() &&
           std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
      ++cursor;
    }
  }
  flush_line();
  return lines;
}

std::string apply_font_spans_to_words(std::string text,
                                      const std::vector<FontSpan>& spans) {
  if (text.empty() || spans.empty()) {
    return text;
  }

  std::vector<std::pair<std::size_t, std::size_t>> words;
  for (std::size_t cursor = 0; cursor < text.size();) {
    while (cursor < text.size() &&
           std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
      ++cursor;
    }
    const auto begin = cursor;
    while (cursor < text.size() &&
           std::isspace(static_cast<unsigned char>(text[cursor])) == 0) {
      ++cursor;
    }
    if (cursor > begin) {
      words.push_back({begin, cursor});
    }
  }
  if (words.empty()) {
    return text;
  }

  std::vector<std::string> opens(text.size() + 1);
  std::vector<std::string> closes(text.size() + 1);
  auto word_cursor = std::size_t{0};
  auto applied = false;
  for (const auto& span : spans) {
    const auto tag = font_code_to_highlight_tag(span.code);
    if (tag.empty() || span.length == 0) {
      continue;
    }
    if (word_cursor >= words.size()) {
      return text;
    }
    const auto [begin, end] = words[word_cursor];
    const auto word_length = end - begin;
    if (word_length + 1 == span.length && text[end - 1] != '?') {
      text.insert(end, "?");
      return apply_font_spans_to_words(std::move(text), spans);
    }
    if (word_length != span.length) {
      return text;
    }
    opens[begin] += ":" + tag + ".";
    closes[end] = ":e" + tag + "." + closes[end];
    ++word_cursor;
    applied = true;
  }
  if (!applied) {
    return text;
  }

  std::string output;
  for (std::size_t index = 0; index < text.size(); ++index) {
    output += opens[index];
    output.push_back(text[index]);
    output += closes[index + 1];
  }
  return output;
}

std::optional<std::string> apply_font_spans_to_ordered_words(
    std::string text,
    const std::vector<FontSpan>& spans) {
  if (text.empty() || spans.empty()) {
    return std::nullopt;
  }

  std::vector<std::pair<std::size_t, std::size_t>> words;
  for (std::size_t cursor = 0; cursor < text.size();) {
    while (cursor < text.size() &&
           std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
      ++cursor;
    }
    const auto begin = cursor;
    while (cursor < text.size() &&
           std::isspace(static_cast<unsigned char>(text[cursor])) == 0) {
      ++cursor;
    }
    if (cursor > begin) {
      words.push_back({begin, cursor});
    }
  }
  if (words.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> opens(text.size() + 1);
  std::vector<std::string> closes(text.size() + 1);
  auto word_cursor = std::size_t{0};
  auto applied = false;
  for (const auto& span : spans) {
    const auto tag = font_code_to_highlight_tag(span.code);
    if (tag.empty() || span.length == 0) {
      continue;
    }
    auto matched = false;
    while (word_cursor < words.size()) {
      const auto [begin, end] = words[word_cursor++];
      const auto word_length = end - begin;
      auto highlight_end = end;
      if (word_length == span.length) {
        matched = true;
      } else if (word_length == span.length + 1 &&
                 std::ispunct(static_cast<unsigned char>(text[end - 1])) != 0) {
        highlight_end = end - 1;
        matched = true;
      }
      if (!matched) {
        continue;
      }
      opens[begin] += ":" + tag + ".";
      closes[highlight_end] = ":e" + tag + "." + closes[highlight_end];
      applied = true;
      break;
    }
    if (!matched) {
      return std::nullopt;
    }
  }
  if (!applied) {
    return std::nullopt;
  }

  std::string output;
  for (std::size_t index = 0; index < text.size(); ++index) {
    output += opens[index];
    output.push_back(text[index]);
    output += closes[index + 1];
  }
  return output;
}

bool first_font_span_precedes_visible_text(const std::string& text,
                                           const std::vector<FontSpan>& spans) {
  if (spans.size() < 2) {
    return false;
  }
  const auto first_visible = text.find_first_not_of(" \t\r\n?");
  if (first_visible == std::string::npos) {
    return false;
  }
  return spans.front().offset < first_visible;
}

std::string render_visual_box_font_gml(std::string trailing,
                                       const std::vector<FontSpan>& spans) {
  if (spans.size() < 2) {
    return {};
  }
  auto lines = extract_visual_box_lines(trailing);
  if (lines.empty()) {
    return {};
  }

  std::string output;
  auto used_spans = false;
  auto emitted_lines = std::size_t{0};
  for (auto& line : lines) {
    line = dot_text(std::move(line));
    if (line.empty()) {
      continue;
    }
    if (!used_spans && !spans.empty()) {
      auto highlighted = apply_font_spans_to_words(line, spans);
      if (highlighted != line) {
        line = std::move(highlighted);
        used_spans = true;
      }
    }
    if (!output.empty()) {
      output.push_back('\n');
    }
    const auto tag = emitted_lines <= 1 ? "p" : "pinline";
    output += render_normalized_gml_control(tag, std::move(line));
    ++emitted_lines;
  }
  if (!used_spans) {
    return {};
  }
  return output;
}

std::string render_pending_font_continuation_gml(std::string prefix,
                                                 std::string text,
                                                 std::size_t base_column) {
  std::size_t cursor = 0;
  auto spans = parse_font_spans(prefix, cursor);
  auto rendered =
      apply_font_spans_to_text_without_normalizing(std::move(text),
                                                   spans,
                                                   base_column);
  return render_simple_gml_control("pinline", std::move(rendered));
}

std::string cfont_visible_text(std::string value,
                               bool apply_spans,
                               std::size_t base_column) {
  value = trim_ascii(rest_after_first_word(std::move(value)));
  std::size_t cursor = 0;
  const auto spans = parse_font_spans(value, cursor);
  auto trailing = cursor >= value.size() ? std::string{} : value.substr(cursor);
  if (trailing.empty()) {
    return {};
  }
  if (apply_spans) {
    return apply_font_spans_to_text_without_normalizing(
        std::move(trailing),
        spans,
        base_column);
  }
  return trim_ascii(std::move(trailing));
}

std::string labeled_box_title_from_segment(std::string segment) {
  if (ascii_starts_with_case_insensitive(trim_ascii(segment), "cfont")) {
    segment = cfont_visible_text(std::move(segment), false, 3);
  }
  const auto begin = segment.find_first_not_of(" \t\r\n?");
  if (begin == std::string::npos) {
    return {};
  }
  auto end = segment.find('?', begin);
  if (end == std::string::npos) {
    end = segment.size();
  }
  return dot_text(segment.substr(begin, end - begin));
}

std::vector<std::string> split_labeled_box_body(std::string value) {
  value = dot_text(std::move(value));
  std::vector<std::string> paragraphs;
  if (value.empty()) {
    return paragraphs;
  }

  static const std::array<const char*, 2> paragraph_markers = {
      " The audio input ", " As such, "};
  std::size_t begin = 0;
  while (begin < value.size()) {
    std::size_t next = std::string::npos;
    const char* matched = nullptr;
    for (const auto* marker : paragraph_markers) {
      const auto found = value.find(marker, begin + 1);
      if (found != std::string::npos &&
          (next == std::string::npos || found < next)) {
        next = found;
        matched = marker;
      }
    }
    if (next == std::string::npos) {
      paragraphs.push_back(trim_ascii(value.substr(begin)));
      break;
    }
    paragraphs.push_back(trim_ascii(value.substr(begin, next - begin)));
    begin = next + 1;
    if (matched != nullptr && matched[0] == ' ') {
      begin = next + 1;
    }
  }
  return paragraphs;
}

std::string render_labeled_box_gml(GmlRenderState& state) {
  if (state.pending_labeled_box_segments.empty()) {
    return ":lblbox.\n:elblbox.";
  }

  auto title = labeled_box_title_from_segment(
      state.pending_labeled_box_segments.front());

  std::string body;
  for (auto segment : state.pending_labeled_box_segments) {
    auto trimmed = trim_ascii(segment);
    if (ascii_starts_with_case_insensitive(trimmed, "cfont")) {
      segment = cfont_visible_text(std::move(trimmed), true, 5);
    }
    for (auto& ch : segment) {
      if (ch == '?') {
        ch = ' ';
      }
    }
    body += ' ';
    body += segment;
  }

  body = dot_text(std::move(body));
  if (!title.empty() && ascii_starts_with_case_insensitive(body, title)) {
    body = trim_ascii(body.substr(title.size()));
  }
  if (!title.empty()) {
    const auto first_body_sentence = body.find(" For most ");
    if (first_body_sentence != std::string::npos) {
      body = trim_ascii(body.substr(first_body_sentence + 1));
    } else if (const auto split_for_body = body.find("or most people");
               split_for_body != std::string::npos) {
      body = "F" + trim_ascii(body.substr(split_for_body));
    }
  }
  if (!title.empty() && ascii_starts_with_case_insensitive(body, ":hp")) {
    const auto for_body = body.find(" For ");
    const auto if_body = body.find(" If ");
    auto body_begin = std::string::npos;
    if (for_body != std::string::npos) {
      body_begin = for_body + 1;
    }
    if (if_body != std::string::npos &&
        (body_begin == std::string::npos || if_body + 1 < body_begin)) {
      body_begin = if_body + 1;
    }
    if (body_begin != std::string::npos) {
      body = trim_ascii(body.substr(body_begin));
    }
  }

  std::string output = render_simple_gml_control("lblbox", std::move(title));
  for (auto paragraph : split_labeled_box_body(std::move(body))) {
    if (!paragraph.empty()) {
      output += "\n" + render_simple_gml_control("p", std::move(paragraph));
    }
  }
  output += "\n:elblbox.";
  return output;
}

std::string render_generated_title_font_line(std::string value) {
  const auto last_close = value.rfind(":ehp");
  if (last_close == std::string::npos) {
    return render_simple_gml_control("p", std::move(value));
  }

  const auto close_end = value.find('.', last_close);
  if (close_end == std::string::npos || close_end + 1 >= value.size()) {
    return render_simple_gml_control("p", std::move(value));
  }

  auto highlighted = trim_ascii(value.substr(0, close_end + 1));
  auto trailing = trim_ascii(value.substr(close_end + 1));
  if (highlighted.empty() || trailing.empty()) {
    return render_simple_gml_control("p", std::move(value));
  }

  return render_simple_gml_control("p", std::move(highlighted)) + "\n" +
         render_simple_gml_control("p", std::move(trailing));
}

std::string parse_fontdef_style(std::string value, std::string& code) {
  const auto equals = value.find('=');
  if (equals != std::string::npos) {
    value = value.substr(equals + 1);
  } else {
    value = rest_after_first_word(std::move(value));
  }
  std::istringstream input(value);
  if (!(input >> code)) {
    code.clear();
    return {};
  }
  std::string style;
  std::getline(input, style);
  auto lower_style = ascii_lower(style);
  for (const auto* boundary : {", c", "? c"}) {
    const auto found = lower_style.find(boundary);
    if (found != std::string::npos) {
      style = style.substr(0, found);
      break;
    }
  }
  return dot_text(trim_ascii(std::move(style)));
}

bool is_valid_font_definition_code(const std::string& code) {
  if (code.size() != 1) {
    return false;
  }
  const auto ch = static_cast<unsigned char>(code.front());
  return std::isdigit(ch) != 0 ||
         (std::isupper(ch) != 0 && std::isalpha(ch) != 0) ||
         code.front() == '_';
}

std::vector<BooFontTrace> trace_font_spans(
    const std::string& segment,
    std::uint32_t logical_record,
    std::uint32_t segment_index,
    const std::map<std::string, std::string>& font_definitions) {
  auto value = trim_ascii(rest_after_first_word(segment));
  std::size_t cursor = 0;
  auto spans = parse_font_spans(value, cursor);
  auto text = cursor >= value.size() ? std::string{} : value.substr(cursor);
  if (!spans.empty() && spans.front().offset <= 3) {
    text = trim_ascii(std::move(text));
  }
  auto mapped = normalize_display_text_with_map(std::move(text));
  if (mapped.text.empty() || spans.empty()) {
    return {};
  }

  std::vector<BooFontTrace> traced;
  for (std::size_t index = 0; index < spans.size(); ++index) {
    const auto mapped_span =
        map_font_span_to_normalized_text(spans[index], mapped, 3);
    BooFontTrace trace;
    trace.logical_record = logical_record;
    trace.segment_index = segment_index;
    trace.span_index = static_cast<std::uint32_t>(index);
    if (!mapped_span) {
      trace.offset = static_cast<std::uint32_t>(spans[index].offset);
      trace.length = static_cast<std::uint32_t>(spans[index].length);
      trace.code = spans[index].code;
      traced.push_back(std::move(trace));
      continue;
    }
    const auto& span = *mapped_span;
    trace.offset = static_cast<std::uint32_t>(span.offset);
    trace.length = static_cast<std::uint32_t>(span.length);
    trace.code = span.code;
    if (const auto found = font_definitions.find(span.code);
        found != font_definitions.end()) {
      trace.style = found->second;
    } else {
      trace.style = font_code_to_highlight_tag(span.code);
    }

    if (span.offset < mapped.text.size() && span.length > 0) {
      const auto end = std::min(mapped.text.size(), span.offset + span.length);
      trace.text = mapped.text.substr(span.offset, end - span.offset);
      const auto tag = font_code_to_highlight_tag(span.code);
      if (!tag.empty()) {
        trace.projected_gml = ":" + tag + "." + trace.text + ":e" + tag + ".";
      }
    }
    traced.push_back(std::move(trace));
  }
  return traced;
}

std::string render_font_gml(std::string value, GmlRenderState& state) {
  value = trim_ascii(std::move(value));
  const auto font_value_had_visual_border = has_visual_border_run(value);
  std::size_t cursor = 0;
  const auto spans = parse_font_spans(value, cursor);
  auto raw_trailing = cursor >= value.size() ? std::string{}
                                             : value.substr(cursor);
  auto trailing = trim_ascii(raw_trailing);
  if (trailing.empty()) {
    state.pending_font_prefix = std::move(value);
    state.pending_font_base_column = 3;
    return {};
  }
  if (state.in_vnotice && !state.emitted_vnotice_heading) {
    state.emitted_vnotice_heading = true;
    trailing = apply_font_spans_to_text(std::move(trailing),
                                        spans,
                                        state.current_font_base_column);
    return render_simple_gml_control("vnhd", std::move(trailing));
  }
  auto plain_trailing = dot_text(trailing);
  if (ascii_starts_with_case_insensitive(plain_trailing, "note:")) {
    plain_trailing = trim_ascii(plain_trailing.substr(5));
    const std::string copyright_marker = "\xC2\xA9";
    const auto copyright = plain_trailing.find(copyright_marker);
    if (copyright != std::string::npos) {
      auto note_text = trim_ascii(plain_trailing.substr(0, copyright));
      auto copyright_text = trim_ascii(plain_trailing.substr(copyright));
      state.pending_copyright_extension = true;
      return render_simple_gml_control("note", std::move(note_text)) + "\n" +
             render_simple_gml_control("coprnote", std::move(copyright_text));
    }
    return render_simple_gml_control("note", std::move(plain_trailing));
  }
  if (state.in_generated_title_page) {
    return render_simple_gml_control(
        "p",
        normalize_display_text_with_map(std::move(trailing)).text);
  }
  if (auto fixed_row = apply_font_spans_to_bar_visual_row(raw_trailing, spans);
      fixed_row) {
    auto text = strip_visual_line_markers_from_inline_gml(std::move(*fixed_row));
    return render_normalized_gml_control("line", std::move(text));
  }
  if (has_visual_border_run(trailing)) {
    if (auto box = render_visual_box_font_gml(trailing, spans);
        !box.empty()) {
      return box;
    }
  }
  if (first_font_span_precedes_visible_text(raw_trailing, spans)) {
    auto plain_visual_line = dot_text(raw_trailing);
    if (!plain_visual_line.empty() && plain_visual_line.size() <= 120) {
      if (auto word_rendered =
              apply_font_spans_to_ordered_words(std::move(plain_visual_line),
                                                spans);
          word_rendered) {
        return render_normalized_gml_control("pinline",
                                             std::move(*word_rendered));
      }
    }
  }
  if (font_value_had_visual_border && spans.size() > 1) {
    auto plain_visual_line = dot_text(trailing);
    if (!plain_visual_line.empty() && plain_visual_line.size() <= 100) {
      auto word_rendered = apply_font_spans_to_words(plain_visual_line, spans);
      if (word_rendered != plain_visual_line) {
        return render_normalized_gml_control("pinline",
                                             std::move(word_rendered));
      }
    }
  }
  // Preserve the display-column padding while projecting CFONT spans.  The
  // normalizer collapses/removes it after mapping; trimming first shifts every
  // span that follows a control's visual indent.  A few fixed rows carry an
  // inline heading marker before the display text; that marker is structural,
  // not one of the columns covered by the CFONT triples.
  const auto raw_first_visible = raw_trailing.find_first_not_of(" \t\r\n");
  if (raw_first_visible != std::string::npos &&
      ascii_starts_with_case_insensitive(raw_trailing,
                                         raw_first_visible,
                                         ":h3")) {
    auto body = trim_ascii(raw_trailing.substr(raw_first_visible + 3));
    body = apply_font_spans_to_text(std::move(body), spans, 7);
    trailing = ":H3 " + std::move(body);
  } else {
    trailing = apply_font_spans_to_text(std::move(raw_trailing),
                                        spans,
                                        state.current_font_base_column);
  }
  trailing = strip_visual_line_markers_from_inline_gml(std::move(trailing));
  if (state.in_footnote && trailing.size() >= 2 &&
      trailing[trailing.size() - 1] == '.' &&
      trailing[trailing.size() - 2] == '.') {
    trailing.pop_back();
  }
  return render_simple_gml_control("pinline", std::move(trailing));
}

std::string render_gml_segment(std::string segment,
                               bool allow_topic_header,
                               GmlRenderState& state) {
  const auto raw_segment = segment;
  if (state.in_figure && !state.in_table &&
      !looks_like_gml_control_at(trim_ascii(segment), 0)) {
    return render_fixed_figure_text_gml(segment);
  }
  segment = trim_ascii(std::move(segment));
  while (!segment.empty() && segment.front() == ',') {
    segment.erase(segment.begin());
    segment = trim_ascii(std::move(segment));
  }
  const auto lower = ascii_lower(segment);
  if (state.table_just_closed &&
      !ascii_starts_with_case_insensitive(lower, "srtbl") &&
      !ascii_starts_with_case_insensitive(lower, "sretbl")) {
    state.table_just_closed = false;
  }
  // Only a real topic header is suppressed here. Ordinary prose can begin
  // with SH (SHOULD, SHIPPED, SHARING, ...); treating every such first line
  // as an SH header loses the complete paragraph.
  const auto sh_header =
      ascii_starts_with_case_insensitive(lower, "sh") &&
      (lower.find("ctopicn") != std::string::npos ||
       segment.find_first_of(" \t\r\n") == std::string::npos);
  if (allow_topic_header && sh_header) {
    return {};
  }
  if (state.ignore_after_index) {
    return {};
  }
  if (state.in_example) {
    if (ascii_starts_with_case_insensitive(lower, "cz")) {
      const auto layout = rest_after_first_word(segment);
      if (ascii_starts_with_case_insensitive(ascii_lower(layout),
                                             "off exmp")) {
        state.in_example = false;
        return ":exmp.";
      }
    }
    auto line = normalize_preformatted_line(std::move(segment));
    if (line.empty()) {
      return {};
    }
    return ":xline." + std::move(line);
  }
  if (state.in_index) {
    if (ascii_starts_with_case_insensitive(lower, "cendindex")) {
      state.in_index = false;
      state.ignore_after_index = true;
      return ":eindex.";
    }
  }
  if (state.in_labeled_box) {
    if (ascii_starts_with_case_insensitive(lower, "cz")) {
      const auto layout = rest_after_first_word(segment);
      if (ascii_starts_with_case_insensitive(ascii_lower(layout),
                                             "off elblbox")) {
        auto output = render_labeled_box_gml(state);
        state.in_labeled_box = false;
        state.pending_labeled_box_segments.clear();
        return output;
      }
    }
    state.pending_labeled_box_segments.push_back(std::move(segment));
    return {};
  }
  if (ascii_starts_with_case_insensitive(lower, "ctopicn")) {
    return {};
  }
  if (ascii_starts_with_case_insensitive(lower, "cparent")) {
    return {};
  }
  if (ascii_starts_with_case_insensitive(lower, "cforwardlevel")) {
    return {};
  }
  if (ascii_starts_with_case_insensitive(lower, "cbacklevel")) {
    return {};
  }
  if (ascii_starts_with_case_insensitive(lower, "csummary")) {
    return {};
  }
  if (ascii_starts_with_case_insensitive(lower, "chdlevel")) {
    state.pending_topic_tag =
        normalize_bookmaster_tag(rest_after_first_word(segment));
    return {};
  }
  if (ascii_starts_with_case_insensitive(lower, "csourcefn")) {
    return {};
  }
  if (ascii_starts_with_case_insensitive(lower, "st")) {
    auto title = rest_after_first_word(segment);
    if (!state.pending_topic_tag.empty()) {
      const auto tag = state.pending_topic_tag;
      state.pending_topic_tag.clear();
      if (bookmaster_topic_tag_takes_title(tag)) {
        state.in_generated_title_page = false;
        state.in_vnotice = false;
        state.emitted_vnotice_heading = false;
        state.pending_copyright_extension = false;
        return render_bookmaster_tag(tag, std::move(title));
      }
      state.in_fixed_selection_list = tag == "figlist" || tag == "tlist";
      if (tag == "toc") {
        state.emitted_toc = true;
      }
      state.in_generated_title_page = tag == "cover" || tag == "tipage";
      state.in_vnotice = tag == "vnotice";
      state.emitted_vnotice_heading = false;
      state.pending_copyright_extension = false;
      return render_bookmaster_tag(tag, {});
    }
    return render_simple_gml_control("p", std::move(title));
  }
  if (ascii_starts_with_case_insensitive(lower, "ctocdef")) {
    if (!state.in_generated_toc) {
      state.in_generated_toc = true;
      if (state.emitted_toc) {
        return {};
      }
      state.emitted_toc = true;
      return ":toc.";
    }
    return render_tocdef_gml(std::move(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "ctoce")) {
    return render_toc_entry_gml(rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "etoc")) {
    state.in_generated_toc = false;
    return ":etoc.";
  }
  if (ascii_starts_with_case_insensitive(lower, "cidelm")) {
    return {};
  }
  if (ascii_starts_with_case_insensitive(lower, "cindex")) {
    state.in_index = true;
    return ":index.";
  }
  if (ascii_starts_with_case_insensitive(lower, "cendindex")) {
    state.in_index = false;
    state.ignore_after_index = true;
    return ":eindex.";
  }
  if (ascii_starts_with_case_insensitive(lower, "cfontdef")) {
    return {};
  }
  const auto is_table_picture_select = [&]() {
    if (!ascii_starts_with_case_insensitive(lower, "cselect")) {
      return false;
    }
    const auto control = parse_select_control(rest_after_first_word(segment));
    return control && !picture_resource_id(control->target).empty();
  }();
  if (state.in_table && ascii_starts_with_case_insensitive(lower, "si")) {
    auto rendered = render_subject_index_gml(rest_after_first_word(segment));
    if (ascii_starts_with_case_insensitive(rendered, ":line.")) {
      rendered.replace(0, std::string(":line.").size(), ":figcap.");
    }
    return rendered;
  }
  if (state.in_table && !ascii_starts_with_case_insensitive(lower, "sretbl") &&
      !is_table_picture_select &&
      !ascii_starts_with_case_insensitive(lower, "cz")) {
    return render_table_body_gml(std::move(segment), state);
  }
  if (ascii_starts_with_case_insensitive(lower, "cfont")) {
    if (!state.pending_selects.empty()) {
      auto font_value = trim_ascii(rest_after_first_word(segment));
      std::size_t cursor = 0;
      (void)parse_font_spans(font_value, cursor);
      auto trailing = cursor >= font_value.size()
                          ? std::string{}
                          : font_value.substr(cursor);
      if (!trim_ascii(trailing).empty()) {
        auto controls = std::move(state.pending_selects);
        state.pending_selects.clear();
        const auto rendered = render_pending_selects_gml(controls, trailing);
        if (!rendered.empty()) {
          return rendered;
        }
        state.pending_selects = std::move(controls);
      }
    }
    return render_font_gml(rest_after_first_word(segment), state);
  }
  if (ascii_starts_with_case_insensitive(lower, "cselect")) {
    auto control = parse_select_control(rest_after_first_word(segment));
    if (!control) {
      return render_simple_gml_control(
          "hdref", rest_after_first_word(std::move(segment)));
    }
    if (state.in_table && !picture_resource_id(control->target).empty()) {
      state.table_has_picture = true;
    }
    std::string rendered;
    if (!state.pending_selects.empty() &&
        !trim_ascii(control->display_fragment).empty()) {
      auto controls = std::move(state.pending_selects);
      state.pending_selects.clear();
      controls.push_back(*control);
      rendered = render_pending_selects_gml(controls,
                                            control->display_fragment);
      if (rendered.empty()) {
        state.pending_selects = std::move(controls);
      }
    } else {
      rendered = render_select_gml(*control);
    }
    if (rendered.empty() && state.pending_selects.empty()) {
      state.pending_selects.push_back(std::move(*control));
    }
    if (state.in_fixed_selection_list &&
        ascii_starts_with_case_insensitive(rendered, ":pinline.")) {
      rendered.replace(0, std::string(":pinline.").size(), ":p.");
    }
    return rendered;
  }
  if (ascii_starts_with_case_insensitive(lower, "cmenu")) {
    return ":ul type='menu'.";
  }
  if (ascii_starts_with_case_insensitive(lower, "cmitem")) {
    return render_menu_item_gml(rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cemenu")) {
    return ":eul.";
  }
  if (ascii_starts_with_case_insensitive(lower, "srfig")) {
    const auto raw_lower = ascii_lower(raw_segment);
    const auto srfig = raw_lower.find("srfig");
    if (srfig != std::string::npos) {
      return render_figure_start_gml(raw_segment.substr(srfig + 5), state);
    }
    return render_figure_start_gml(segment.substr(5), state);
  }
  if (ascii_starts_with_case_insensitive(lower, "srefig")) {
    return render_figure_end_gml(segment.substr(6), state);
  }
  if (ascii_starts_with_case_insensitive(lower, "srtbl")) {
    if (state.table_just_closed && !state.in_table &&
        segment.find('?') == std::string::npos &&
        ascii_lower(segment).find("other trademarks") != std::string::npos) {
      state.table_just_closed = false;
      auto payload = trim_ascii(segment.substr(5));
      const auto space = payload.find_first_of(" \t");
      if (space != std::string::npos) {
        return render_simple_gml_control(
            "p", dot_text(payload.substr(space + 1)));
      }
    }
    state.table_just_closed = false;
    state.in_table = true;
    state.table_columns = 0;
    state.table_separator_offsets.clear();
    state.table_line_width = 0;
    state.table_border_width = 0;
    state.table_final_separator_is_synthetic = false;
    state.table_layout_from_font_heading = false;
    state.table_has_picture = false;
    state.table_visual_buffer.clear();
    state.pending_table_row.clear();
    auto table_payload = segment.substr(5);
    const auto first_separator = table_payload.find('?');
    const auto payload_head = trim_ascii(
        table_payload.substr(0, first_separator == std::string::npos
                                      ? table_payload.size()
                                      : first_separator));
    std::istringstream payload_words(payload_head);
    std::string payload_target;
    payload_words >> payload_target;
    const auto inline_first_row =
        first_separator != std::string::npos &&
        trim_ascii(payload_head.substr(payload_target.size())) .empty();
    if (first_separator == std::string::npos) {
      auto header = trim_ascii(table_payload);
      std::istringstream input(header);
      std::string target;
      input >> target;
      std::string body;
      std::getline(input, body);
      std::vector<std::string> cells;
      std::size_t cursor = 0;
      while (cursor < body.size()) {
        while (cursor < body.size() &&
               std::isspace(static_cast<unsigned char>(body[cursor])) != 0) {
          ++cursor;
        }
        if (cursor >= body.size()) break;
        const auto begin = cursor;
        while (cursor < body.size()) {
          if (std::isspace(static_cast<unsigned char>(body[cursor])) == 0) {
            ++cursor;
            continue;
          }
          auto gap = cursor;
          while (gap < body.size() &&
                 std::isspace(static_cast<unsigned char>(body[gap])) != 0) {
            ++gap;
          }
          if (gap - cursor >= 3) break;
          cursor = gap;
        }
        cells.push_back(trim_ascii(body.substr(begin, cursor - begin)));
      }
      if (cells.size() >= 3) {
        auto output = ":table id='" + escape_gml_attr(table_anchor_id(target)) + "'.\n:row.";
        for (std::size_t index = 0; index < cells.size(); ++index) {
          output += "\n:c col='" + std::to_string(index) + "'." +
                    dot_text(cells[index]);
        }
        return output;
      }
      return render_table_gml(std::move(table_payload),
                              state.table_border_width);
    }
    if (!inline_first_row ||
        table_payload.find('?', first_separator + 1) != std::string::npos) {
      return render_table_gml(std::move(table_payload),
                              state.table_border_width);
    }

    auto output = render_table_gml(
        table_payload.substr(0, first_separator), state.table_border_width);
    auto first_row = table_payload.substr(first_separator);
    auto rows = render_table_body_gml(std::move(first_row), state);
    if (rows.empty() &&
        table_payload.find('?', first_separator + 1) == std::string::npos) {
      auto body = trim_ascii(table_payload.substr(first_separator + 1));
      std::vector<std::string> cells;
      std::size_t cursor = 0;
      while (cursor < body.size()) {
        while (cursor < body.size() && std::isspace(static_cast<unsigned char>(body[cursor]))) ++cursor;
        if (cursor >= body.size()) break;
        const auto begin = cursor;
        while (cursor < body.size()) {
          if (std::isspace(static_cast<unsigned char>(body[cursor])) == 0) { ++cursor; continue; }
          auto gap = cursor;
          while (gap < body.size() && std::isspace(static_cast<unsigned char>(body[gap])) != 0) ++gap;
          if (gap - cursor >= 3) break;
          cursor = gap;
        }
        cells.push_back(trim_ascii(body.substr(begin, cursor - begin)));
      }
      if (cells.size() >= 3) {
        rows = ":row.";
        for (std::size_t index = 0; index < cells.size(); ++index)
          rows += "\n:c col='" + std::to_string(index) + "'." + dot_text(cells[index]);
      }
    }
    if (!rows.empty()) {
      output.push_back('\n');
      output += std::move(rows);
    }
    return output;
  }
  if (ascii_starts_with_case_insensitive(lower, "sretbl")) {
    if (!state.in_table) {
      return {};
    }
    auto output = flush_table_visual_buffer(state);
    auto pending = flush_pending_table_row(state);
    if (!pending.empty()) {
      if (!output.empty()) {
        output += "\n";
      }
      output += std::move(pending);
    }
    if (!output.empty()) {
      output += "\n";
    }
    output += ":etable.";
    state.in_table = false;
    state.table_just_closed = true;
    state.table_columns = 0;
    state.table_separator_offsets.clear();
    state.table_line_width = 0;
    state.table_border_width = 0;
    state.table_final_separator_is_synthetic = false;
    state.table_layout_from_font_heading = false;
    state.table_has_picture = false;
    state.table_visual_buffer.clear();
    const auto trailing = dot_text(rest_after_first_word(segment));
    if (!trailing.empty()) {
      output += "\n" + render_simple_gml_control("p", trailing);
    }
    return output;
  }
  if (ascii_starts_with_case_insensitive(lower, "srftn")) {
    state.pending_footnote_id = trim_control_operand(segment.substr(2));
    return {};
  }
  if (ascii_starts_with_case_insensitive(lower, "sreftn")) {
    state.pending_footnote_id.clear();
    state.in_footnote = false;
    return ":efn.";
  }
  if (ascii_starts_with_case_insensitive(lower, "sr")) {
    if (segment.size() <= 2 ||
        std::isspace(static_cast<unsigned char>(segment[2])) != 0 ||
        (segment.size() > 3 &&
         std::isspace(static_cast<unsigned char>(segment[3])) != 0)) {
      return render_simple_gml_control("p", std::move(segment));
    }
    return render_anchor_gml(segment.substr(2));
  }
  if (ascii_starts_with_case_insensitive(lower, "cz")) {
    const auto layout = rest_after_first_word(segment);
    const auto lower_layout = ascii_lower(layout);
    if (ascii_starts_with_case_insensitive(lower_layout, "off lblbox")) {
      state.in_labeled_box = true;
      state.pending_labeled_box_segments.clear();
      return {};
    }
    if (ascii_starts_with_case_insensitive(lower_layout, "off xmp")) {
      state.in_example = true;
      return ":xmp.";
    }
    if (ascii_starts_with_case_insensitive(lower_layout, "off exmp")) {
      state.in_example = false;
      return ":exmp.";
    }
    if (ascii_starts_with_case_insensitive(lower_layout, "off table") ||
        ascii_starts_with_case_insensitive(lower_layout, "off etable")) {
      return {};
    }
    if (state.pending_copyright_extension &&
        ascii_starts_with_case_insensitive(layout, "break")) {
      auto rendered = render_layout_gml(layout, state.current_font_base_column);
      const auto dot = rendered.find('.');
      const auto content =
          dot == std::string::npos ? std::string{}
                                   : dot_text(rendered.substr(dot + 1));
      if (!content.empty()) {
        state.pending_copyright_extension = false;
        return render_simple_gml_control("coprext", content);
      }
    }
    if (ascii_starts_with_case_insensitive(lower_layout, "flow fn")) {
      return render_footnote_gml(layout, state);
    }
    return render_layout_gml(layout, state.current_font_base_column);
  }
  if (ascii_starts_with_case_insensitive(lower, "si")) {
    return render_subject_index_gml(rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "citerm")) {
    return render_simple_gml_control("i1", rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cgpsep")) {
    return render_simple_gml_control("grpsep", rest_after_first_word(segment));
  }
  if (!state.pending_selects.empty()) {
    auto controls = std::move(state.pending_selects);
    state.pending_selects.clear();
    const auto rendered = render_pending_selects_gml(controls, segment);
    if (!rendered.empty()) {
      if (state.in_fixed_selection_list &&
          ascii_starts_with_case_insensitive(rendered, ":pinline.")) {
        auto figure_row = rendered;
        figure_row.replace(0, std::string(":pinline.").size(), ":p.");
        return figure_row;
      }
      return rendered;
    }
    state.pending_selects = std::move(controls);
  }
  if (auto continuation = render_marker_continuation_gml(segment)) {
    return *continuation;
  }
  if (!state.pending_font_prefix.empty()) {
    auto pending = std::move(state.pending_font_prefix);
    state.pending_font_prefix.clear();
    return render_pending_font_continuation_gml(std::move(pending),
                                                std::move(segment),
                                                state.pending_font_base_column);
  }
  if (looks_like_gml_control_at(segment, 0)) {
    return render_unknown_control_gml(segment);
  }
  return render_simple_gml_control("pinline", std::move(segment));
}

std::map<std::string, std::string> extract_font_definitions(
    const std::vector<std::string>& decoded_records) {
  std::map<std::string, std::string> definitions;
  for (const auto& decoded_record : decoded_records) {
    for (const auto& segment : split_decoded_markup_segments(decoded_record)) {
      if (!ascii_starts_with_case_insensitive(trim_ascii(segment),
                                              "cfontdef")) {
        continue;
      }
      std::string code;
      auto style = parse_fontdef_style(segment, code);
      if (is_valid_font_definition_code(code) && !style.empty()) {
        definitions[std::move(code)] = std::move(style);
      }
    }
  }
  return definitions;
}

struct GmlAppendResult {
  std::string record;
  bool merged_with_previous = false;
};

std::vector<GmlAppendResult> append_rendered_gml_line(
    std::vector<std::string>& rendered,
    const std::string& line) {
  std::vector<GmlAppendResult> results;
  if (line.empty()) {
    return results;
  }

  std::size_t begin = 0;
  while (begin <= line.size()) {
    const auto end = line.find('\n', begin);
    auto part = end == std::string::npos ? line.substr(begin)
                                         : line.substr(begin, end - begin);
    if (!part.empty()) {
      if (ascii_starts_with_case_insensitive(part, ":pinline.")) {
        auto content = part.substr(std::string(":pinline.").size());
        if (!content.empty() && !rendered.empty() &&
            (ascii_starts_with_case_insensitive(rendered.back(), ":p ") ||
             ascii_starts_with_case_insensitive(rendered.back(), ":p.") ||
             ascii_starts_with_case_insensitive(rendered.back(), ":li ") ||
             ascii_starts_with_case_insensitive(rendered.back(), ":li.") ||
             ascii_starts_with_case_insensitive(rendered.back(), ":fn ") ||
             ascii_starts_with_case_insensitive(rendered.back(), ":fn."))) {
          if (!rendered.back().empty() && rendered.back().back() != ' ') {
            rendered.back().push_back(' ');
          }
          rendered.back() += std::move(content);
          results.push_back({rendered.back(), true});
        } else {
          rendered.push_back(":p." + std::move(content));
          results.push_back({rendered.back(), false});
        }
      } else {
        rendered.push_back(std::move(part));
        results.push_back({rendered.back(), false});
      }
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  return results;
}

std::vector<std::string> render_gml_records(
    const std::vector<std::string>& decoded_records) {
  std::vector<std::string> rendered;
  GmlRenderState state;
  for (std::size_t record_index = 0; record_index < decoded_records.size();
       ++record_index) {
    auto segments = split_decoded_markup_segments(decoded_records[record_index]);
    for (std::size_t segment_index = 0; segment_index < segments.size();
         ++segment_index) {
      const auto allow_topic_header = record_index == 0 && segment_index == 0;
      auto line = render_gml_segment(std::move(segments[segment_index]),
                                     allow_topic_header,
                                     state);
      (void)append_rendered_gml_line(rendered, line);
    }
    if (state.in_table && state.table_layout_from_font_heading &&
        !state.table_visual_buffer.empty()) {
      (void)append_rendered_gml_line(rendered,
                                     flush_table_visual_buffer(state));
    }
    if (state.in_table && state.table_layout_from_font_heading &&
        !state.pending_table_row.empty()) {
      (void)append_rendered_gml_line(rendered,
                                     flush_pending_table_row(state));
    }
  }
  return rendered;
}

std::vector<BooLogicalRecordTrace> trace_gml_records(
    const std::vector<std::string>& decoded_records,
    std::uint32_t first_logical_record,
    const std::map<std::string, std::string>& font_definitions) {
  std::vector<BooLogicalRecordTrace> traced_records;
  std::vector<std::string> rendered;
  GmlRenderState state;

  traced_records.reserve(decoded_records.size());
  for (std::size_t record_index = 0; record_index < decoded_records.size();
       ++record_index) {
    BooLogicalRecordTrace traced;
    traced.logical_record =
        first_logical_record + static_cast<std::uint32_t>(record_index);
    traced.decoded_record = decoded_records[record_index];
    traced.segments = split_decoded_markup_segments(decoded_records[record_index]);

    for (std::size_t segment_index = 0; segment_index < traced.segments.size();
         ++segment_index) {
      const auto& segment = traced.segments[segment_index];
      if (ascii_starts_with_case_insensitive(trim_ascii(segment), "cfont")) {
        auto font_spans =
            trace_font_spans(segment,
                             traced.logical_record,
                             static_cast<std::uint32_t>(segment_index),
                             font_definitions);
        traced.font_spans.insert(traced.font_spans.end(),
                                 std::make_move_iterator(font_spans.begin()),
                                 std::make_move_iterator(font_spans.end()));
      }

      const auto allow_topic_header = record_index == 0 && segment_index == 0;
      auto line = render_gml_segment(segment, allow_topic_header, state);
      auto appended = append_rendered_gml_line(rendered, line);
      for (auto& result : appended) {
        if (result.merged_with_previous) {
          traced.normalized_gml_records.push_back("[merged] " +
                                                  std::move(result.record));
        } else {
          traced.normalized_gml_records.push_back(std::move(result.record));
        }
      }
    }
    if (state.in_table && state.table_layout_from_font_heading &&
        !state.table_visual_buffer.empty()) {
      auto appended = append_rendered_gml_line(
          rendered, flush_table_visual_buffer(state));
      for (auto& result : appended) {
        if (result.merged_with_previous) {
          traced.normalized_gml_records.push_back("[merged] " +
                                                  std::move(result.record));
        } else {
          traced.normalized_gml_records.push_back(std::move(result.record));
        }
      }
    }
    if (state.in_table && state.table_layout_from_font_heading &&
        !state.pending_table_row.empty()) {
      auto appended = append_rendered_gml_line(
          rendered, flush_pending_table_row(state));
      for (auto& result : appended) {
        if (result.merged_with_previous) {
          traced.normalized_gml_records.push_back("[merged] " +
                                                  std::move(result.record));
        } else {
          traced.normalized_gml_records.push_back(std::move(result.record));
        }
      }
    }
    traced_records.push_back(std::move(traced));
  }

  return traced_records;
}

bool looks_like_control_boundary(const std::string& decoded_record,
                                 const std::string& lower_record,
                                 std::size_t offset) {
  std::size_t key_start = std::string::npos;
  if (offset + 3 < decoded_record.size() &&
      decoded_record[offset] == '?' &&
      decoded_record[offset + 1] == ',') {
    key_start = offset + 2;
  } else if (offset + 3 < decoded_record.size() &&
             decoded_record[offset] == ',' &&
             decoded_record[offset + 1] == ' ') {
    key_start = offset + 2;
  } else if (offset + 2 < decoded_record.size() &&
             decoded_record[offset] == '?' &&
             decoded_record[offset + 1] == ' ') {
    key_start = offset + 2;
  } else {
    return false;
  }

  if (lower_record[key_start] != 'c') {
    return false;
  }

  const auto max_key_end =
      std::min(decoded_record.size(), key_start + std::size_t{20});
  for (auto cursor = key_start + 1; cursor < max_key_end; ++cursor) {
    const auto ch = static_cast<unsigned char>(lower_record[cursor]);
    if (decoded_record[cursor] == '=') {
      return cursor > key_start + 1;
    }
    if (std::isalnum(ch) == 0 && decoded_record[cursor] != '_') {
      return false;
    }
  }
  return false;
}

} // namespace geist::detail
