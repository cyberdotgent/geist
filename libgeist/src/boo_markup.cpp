#include "geist/detail/boo_detail.hpp"

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

std::string dot_text(std::string value) {
  for (auto& ch : value) {
    if (ch == '?') {
      ch = ' ';
    }
  }
  value = collapse_ascii_whitespace(std::move(value));
  if (value.empty()) {
    return {};
  }
  return value;
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

  static const std::array<const char*, 47> prefixes = {
      "sh",          "ctopicn",    "cparent",    "cforwardlevel",
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
      "cindex",      "cpicture"};
  for (const auto* prefix : prefixes) {
    if (!ascii_starts_with_case_insensitive(value, offset, prefix)) {
      continue;
    }
    const auto prefix_text = std::string(prefix);
    const auto end = offset + prefix_text.size();
    if (end == value.size()) {
      return true;
    }
    const auto next = value[end];
    if (std::isspace(static_cast<unsigned char>(next)) != 0 || next == '=' ||
        next == ',' || next == '.') {
      return true;
    }
    if ((prefix_text == "sh" || prefix_text == "srfig" ||
         prefix_text == "srtbl" || prefix_text == "sr") &&
        is_topic_id_char(next)) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> split_decoded_markup_segments(
    const std::string& decoded_record) {
  std::vector<std::string> segments;
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

    const auto end = split_before ? cursor : cursor;
    auto segment = trim_ascii(decoded_record.substr(begin, end - begin));
    if (!segment.empty()) {
      segments.push_back(std::move(segment));
    }
    begin = split_before ? cursor : cursor + 1;
  }

  auto segment = trim_ascii(decoded_record.substr(begin));
  if (!segment.empty()) {
    segments.push_back(std::move(segment));
  }
  return segments;
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

std::string render_keyed_gml_control(const std::string& tag,
                                     const std::string& attr,
                                     std::string value) {
  value = trim_control_operand(std::move(value));
  if (value.empty()) {
    return ":" + tag + ".";
  }
  return ":" + tag + " " + attr + "='" + escape_gml_attr(value) + "'.";
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
  return render_simple_gml_control("li", std::move(title));
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

std::string render_link_gml(std::string value) {
  std::istringstream input(value);
  std::string column;
  std::string length;
  std::string target;
  if (!(input >> column >> length >> target)) {
    return render_simple_gml_control("hdref", std::move(value));
  }
  std::string text;
  std::getline(input, text);
  auto output = ":hdref refid='" + escape_gml_attr(target) + "'.";
  text = dot_text(text);
  if (!text.empty()) {
    output += text;
  }
  return output;
}

std::string render_menu_item_gml(std::string value) {
  std::istringstream input(value);
  std::string target;
  if (!(input >> target)) {
    return render_simple_gml_control("li", std::move(value));
  }
  std::string text;
  std::getline(input, text);
  return render_simple_gml_control("li", std::move(text));
}

std::string render_layout_gml(std::string value) {
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
    std::string text;
    std::getline(rest_input, text);
    if (trim_ascii(text).empty()) {
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
      return ":efig.";
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
  bool in_generated_toc = false;
  bool emitted_toc = false;
};

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

std::string trailing_text_after_font_spans(std::string value) {
  value = trim_ascii(std::move(value));
  std::size_t cursor = 0;
  std::size_t last_complete = 0;
  while (cursor < value.size()) {
    const auto before = cursor;
    if (!parse_unsigned_word(value, cursor) ||
        !parse_unsigned_word(value, cursor) ||
        !parse_nonspace_word(value, cursor)) {
      cursor = before;
      break;
    }
    last_complete = cursor;
  }
  if (last_complete >= value.size()) {
    return {};
  }
  return trim_ascii(value.substr(last_complete));
}

std::string render_font_gml(std::string value) {
  auto trailing = trailing_text_after_font_spans(std::move(value));
  if (trailing.empty()) {
    return {};
  }
  if (ascii_starts_with_case_insensitive(trailing, "note:")) {
    trailing = trim_ascii(trailing.substr(5));
    return render_simple_gml_control("note", std::move(trailing));
  }
  return render_simple_gml_control("p", std::move(trailing));
}

std::string render_gml_segment(std::string segment,
                               bool allow_topic_header,
                               GmlRenderState& state) {
  segment = trim_ascii(std::move(segment));
  while (!segment.empty() && segment.front() == ',') {
    segment.erase(segment.begin());
    segment = trim_ascii(std::move(segment));
  }
  const auto lower = ascii_lower(segment);
  if (allow_topic_header && ascii_starts_with_case_insensitive(lower, "sh")) {
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
        return render_bookmaster_tag(tag, std::move(title));
      }
      if (tag == "toc") {
        state.emitted_toc = true;
      }
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
  if (ascii_starts_with_case_insensitive(lower, "cfontdef")) {
    return {};
  }
  if (ascii_starts_with_case_insensitive(lower, "cfont")) {
    return render_font_gml(rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cselect")) {
    return render_link_gml(rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cmenu")) {
    return ":ul.";
  }
  if (ascii_starts_with_case_insensitive(lower, "cmitem")) {
    return render_menu_item_gml(rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cemenu")) {
    return ":eul.";
  }
  if (ascii_starts_with_case_insensitive(lower, "srfig")) {
    return render_keyed_gml_control("fig", "id", segment.substr(5));
  }
  if (ascii_starts_with_case_insensitive(lower, "srefig")) {
    return ":efig.";
  }
  if (ascii_starts_with_case_insensitive(lower, "srtbl")) {
    return render_keyed_gml_control("table", "id", segment.substr(5));
  }
  if (ascii_starts_with_case_insensitive(lower, "sretbl")) {
    return ":etable.";
  }
  if (ascii_starts_with_case_insensitive(lower, "sr")) {
    return render_anchor_gml(segment.substr(2));
  }
  if (ascii_starts_with_case_insensitive(lower, "cz")) {
    return render_layout_gml(rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "si")) {
    return render_simple_gml_control("i1", rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "citerm")) {
    return render_simple_gml_control("i1", rest_after_first_word(segment));
  }
  if (ascii_starts_with_case_insensitive(lower, "cgpsep")) {
    return render_simple_gml_control("grpsep", rest_after_first_word(segment));
  }
  return render_simple_gml_control("p", std::move(segment));
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
      if (!line.empty() && line != ":p.") {
        rendered.push_back(std::move(line));
      }
    }
  }
  return rendered;
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
