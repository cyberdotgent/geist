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

std::string dot_text(std::string value) {
  value = remove_decoded_line_markers(std::move(value));
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
      if (prefix_text == "sr" && end + 1 < value.size() &&
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

std::optional<std::string> render_marker_continuation_gml(
    const std::string& segment) {
  if (segment.size() < 3 || !is_decoded_line_marker(segment.front())) {
    return std::nullopt;
  }
  if (std::isspace(static_cast<unsigned char>(segment[1])) == 0 ||
      std::isspace(static_cast<unsigned char>(segment[2])) == 0) {
    return std::nullopt;
  }

  auto text = trim_ascii(segment.substr(1));
  if (text.empty()) {
    return std::string{};
  }
  return render_simple_gml_control("pinline", std::move(text));
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

std::string render_table_gml(std::string value) {
  value = trim_ascii(std::move(value));
  std::istringstream input(value);
  std::string target;
  if (!(input >> target)) {
    return ":table.";
  }

  std::string caption;
  std::getline(input, caption);
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
  text = dot_text(text);
  auto selected_text = text;
  auto prefix_text = std::string{};
  auto suffix_text = std::string{};
  char* column_end = nullptr;
  const auto selected_column = std::strtol(column.c_str(), &column_end, 10);
  char* length_end = nullptr;
  const auto selected_length = std::strtol(length.c_str(), &length_end, 10);
  const auto has_column =
      column_end != column.c_str() && *column_end == '\0' && selected_column > 0;
  const auto has_length =
      length_end != length.c_str() && *length_end == '\0' && selected_length > 0;
  if (has_length && static_cast<std::size_t>(selected_length) < text.size()) {
    auto selected_begin = std::string::npos;
    if (has_column) {
      constexpr auto display_left_margin = long{3};
      const auto zero_based_column =
          selected_column > display_left_margin
              ? static_cast<std::size_t>(selected_column - display_left_margin)
              : std::size_t{0};
      if (zero_based_column < text.size()) {
        selected_begin = zero_based_column;
      }
    }
    if (selected_begin == std::string::npos ||
        selected_begin + static_cast<std::size_t>(selected_length) >
            text.size()) {
      selected_begin =
          text.size() - static_cast<std::size_t>(selected_length);
    }
    const auto selected_end =
        std::min(text.size(),
                 selected_begin + static_cast<std::size_t>(selected_length));
    prefix_text = trim_ascii(text.substr(0, selected_begin));
    selected_text =
        trim_ascii(text.substr(selected_begin, selected_end - selected_begin));
    suffix_text = trim_ascii(text.substr(selected_end));
  }

  const auto resource_id = picture_resource_id(target);
  if (!resource_id.empty()) {
    auto inline_text = prefix_text;
    if (!inline_text.empty()) {
      inline_text += " ";
    }
    inline_text += ":image resource='" + escape_gml_attr(resource_id) + "'.";
    inline_text += selected_text;
    inline_text += ":eimage.";
    if (!suffix_text.empty()) {
      inline_text += " " + suffix_text;
    }
    return render_simple_gml_control("pinline", std::move(inline_text));
  }

  auto inline_text = prefix_text;
  if (!inline_text.empty()) {
    inline_text += " ";
  }
  inline_text += ":hdref refid='" + escape_gml_attr(target) + "'.";
  inline_text += selected_text;
  inline_text += ":ehdref.";
  if (!suffix_text.empty()) {
    if (std::ispunct(static_cast<unsigned char>(suffix_text.front())) == 0) {
      inline_text += " ";
    }
    inline_text += suffix_text;
  }
  return render_simple_gml_control("pinline", std::move(inline_text));
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
  std::string pending_footnote_id;
  std::string pending_font_prefix;
  std::vector<std::string> pending_labeled_box_segments;
  bool in_generated_toc = false;
  bool in_generated_title_page = false;
  bool emitted_toc = false;
  bool in_vnotice = false;
  bool emitted_vnotice_heading = false;
  bool pending_copyright_extension = false;
  bool in_table = false;
  bool in_labeled_box = false;
  std::size_t table_columns = 0;
  std::vector<std::size_t> table_separator_offsets;
  std::size_t table_line_width = 0;
  std::string table_visual_buffer;
  std::vector<std::string> pending_table_row;
};

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
  std::string text;
  std::getline(input, text);
  text = dot_text(std::move(text));

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
    std::size_t start) {
  if (start >= value.size() || value[start] != '?' ||
      is_table_border_run(value, start)) {
    return std::nullopt;
  }
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
                              const std::vector<std::size_t>& offsets) {
  if (offsets.size() < 2 || start >= value.size()) {
    return false;
  }
  for (const auto offset : offsets) {
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
  if (ascii_starts_with_case_insensitive(trim_ascii(segment), "cfont")) {
    const auto first_separator = segment.find('?');
    if (first_separator == std::string::npos) {
      return {};
    }
    segment = segment.substr(first_separator);
  }

  if (segment.find('?') == std::string::npos && state.table_columns == 0 &&
      state.pending_table_row.empty()) {
    return render_simple_gml_control("tcap", std::move(segment));
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
      if (auto offsets = infer_table_separator_offsets(state.table_visual_buffer,
                                                       0)) {
        state.table_separator_offsets = std::move(*offsets);
        state.table_columns = state.table_separator_offsets.size() - 1;
        state.table_line_width = state.table_separator_offsets.back() + 1;
      } else {
        if (state.table_visual_buffer.size() < 80) {
          break;
        }
        state.table_visual_buffer.erase(0, 1);
        continue;
      }
    }
    if (state.table_visual_buffer.size() < state.table_line_width) {
      break;
    }
    if (!fixed_table_line_matches(state.table_visual_buffer,
                                  0,
                                  state.table_separator_offsets)) {
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

std::size_t font_span_score(const std::string& text,
                            const FontSpan& span,
                            std::size_t offset) {
  if (offset >= text.size() || span.length == 0) {
    return 1000;
  }
  const auto end = std::min(text.size(), offset + span.length);
  const auto fragment = text.substr(offset, end - offset);
  auto score = std::size_t{0};
  if (fragment.empty()) {
    return 1000;
  }
  if (std::isspace(static_cast<unsigned char>(fragment.front())) != 0) {
    score += 40;
  }
  if (std::isspace(static_cast<unsigned char>(fragment.back())) != 0) {
    score += 40;
  }
  auto nonspace = std::size_t{0};
  auto alpha = std::size_t{0};
  for (const auto ch : fragment) {
    if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
      ++nonspace;
    }
    if (std::isalpha(static_cast<unsigned char>(ch)) != 0) {
      ++alpha;
    }
  }
  if (nonspace == 0) {
    score += 200;
  }
  if (alpha == 0 && !ascii_equals_case_insensitive(span.code, "x")) {
    score += 20;
  }
  const auto before_is_word =
      offset > 0 &&
      std::isalnum(static_cast<unsigned char>(text[offset - 1])) != 0;
  const auto after_is_word =
      end < text.size() &&
      std::isalnum(static_cast<unsigned char>(text[end])) != 0;
  if (before_is_word) {
    score += 12;
  }
  if (after_is_word) {
    score += 12;
  }
  score += fragment.size() - nonspace;
  return score;
}

std::vector<FontSpan> normalize_font_spans_for_text(
    const std::string& value,
    const std::vector<FontSpan>& spans) {
  if (value.empty() || spans.empty()) {
    return spans;
  }

  auto normalized_spans = spans;
  auto first_offset = normalized_spans.front().offset;
  for (const auto& span : normalized_spans) {
    first_offset = std::min(first_offset, span.offset);
  }

  for (auto& span : normalized_spans) {
    std::vector<std::size_t> candidates;
    candidates.push_back(span.offset);
    for (const auto bias : {std::size_t{3}, std::size_t{6}, std::size_t{7},
                            std::size_t{11}, first_offset}) {
      if (span.offset >= bias) {
        candidates.push_back(span.offset - bias);
      }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());

    auto best = span.offset < value.size() ? span.offset : std::size_t{0};
    auto best_score = font_span_score(value, span, best);
    for (const auto candidate : candidates) {
      const auto score = font_span_score(value, span, candidate);
      if (score < best_score ||
          (score == best_score && candidate < best && candidate < value.size())) {
        best = candidate;
        best_score = score;
      }
    }
    span.offset = best;
  }
  return normalized_spans;
}

std::string apply_font_spans_to_text(std::string value,
                                     const std::vector<FontSpan>& spans) {
  value = dot_text(std::move(value));
  if (value.empty() || spans.empty()) {
    return value;
  }

  auto normalized_spans = normalize_font_spans_for_text(value, spans);

  std::vector<std::string> opens(value.size() + 1);
  std::vector<std::string> closes(value.size() + 1);
  for (const auto& span : normalized_spans) {
    const auto tag = font_code_to_highlight_tag(span.code);
    if (tag.empty() || span.length == 0 || span.offset >= value.size()) {
      continue;
    }
    const auto end = std::min(value.size(), span.offset + span.length);
    opens[span.offset] += ":" + tag + ".";
    closes[end] = ":e" + tag + "." + closes[end];
  }

  std::string output;
  for (std::size_t index = 0; index < value.size(); ++index) {
    output += opens[index];
    output.push_back(value[index]);
    output += closes[index + 1];
  }
  return output;
}

std::string apply_font_spans_to_text_without_normalizing(
    std::string value,
    const std::vector<FontSpan>& spans) {
  value = dot_text(std::move(value));
  if (value.empty() || spans.empty()) {
    return value;
  }

  std::vector<std::string> opens(value.size() + 1);
  std::vector<std::string> closes(value.size() + 1);
  for (const auto& span : spans) {
    const auto tag = font_code_to_highlight_tag(span.code);
    if (tag.empty() || span.length == 0 || span.offset >= value.size()) {
      continue;
    }
    const auto end = std::min(value.size(), span.offset + span.length);
    opens[span.offset] += ":" + tag + ".";
    closes[end] = ":e" + tag + "." + closes[end];
  }

  std::string output;
  for (std::size_t index = 0; index < value.size(); ++index) {
    output += opens[index];
    output.push_back(value[index]);
    output += closes[index + 1];
  }
  return output;
}

std::string render_pending_font_continuation_gml(std::string prefix,
                                                 std::string text) {
  std::size_t cursor = 0;
  auto spans = parse_font_spans(prefix, cursor);
  constexpr std::size_t display_left_margin = 6;
  for (auto& span : spans) {
    span.offset = span.offset > display_left_margin
                      ? span.offset - display_left_margin
                      : 0;
  }
  auto rendered =
      apply_font_spans_to_text_without_normalizing(std::move(text), spans);
  return render_simple_gml_control("pinline", std::move(rendered));
}

std::string cfont_visible_text(std::string value, bool apply_spans) {
  value = trim_ascii(rest_after_first_word(std::move(value)));
  std::size_t cursor = 0;
  const auto spans = parse_font_spans(value, cursor);
  auto trailing = cursor >= value.size() ? std::string{}
                                         : trim_ascii(value.substr(cursor));
  if (trailing.empty()) {
    return {};
  }
  if (apply_spans) {
    auto adjusted_spans = spans;
    constexpr std::size_t display_left_margin = 6;
    for (auto& span : adjusted_spans) {
      span.offset = span.offset > display_left_margin
                        ? span.offset - display_left_margin
                        : 0;
    }
    return apply_font_spans_to_text_without_normalizing(std::move(trailing),
                                                        adjusted_spans);
  }
  return trailing;
}

std::string labeled_box_title_from_segment(std::string segment) {
  if (ascii_starts_with_case_insensitive(trim_ascii(segment), "cfont")) {
    segment = cfont_visible_text(std::move(segment), false);
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
      segment = cfont_visible_text(std::move(trimmed), true);
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
  text = dot_text(trim_ascii(std::move(text)));
  if (text.empty() || spans.empty()) {
    return {};
  }

  spans = normalize_font_spans_for_text(text, spans);
  std::vector<BooFontTrace> traced;
  for (std::size_t index = 0; index < spans.size(); ++index) {
    const auto& span = spans[index];
    BooFontTrace trace;
    trace.logical_record = logical_record;
    trace.segment_index = segment_index;
    trace.span_index = static_cast<std::uint32_t>(index);
    trace.offset = static_cast<std::uint32_t>(span.offset);
    trace.length = static_cast<std::uint32_t>(span.length);
    trace.code = span.code;
    if (const auto found = font_definitions.find(span.code);
        found != font_definitions.end()) {
      trace.style = found->second;
    } else {
      trace.style = font_code_to_highlight_tag(span.code);
    }

    if (span.offset < text.size() && span.length > 0) {
      const auto end = std::min(text.size(), span.offset + span.length);
      trace.text = text.substr(span.offset, end - span.offset);
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
  std::size_t cursor = 0;
  const auto spans = parse_font_spans(value, cursor);
  auto trailing = cursor >= value.size() ? std::string{}
                                         : trim_ascii(value.substr(cursor));
  if (trailing.empty()) {
    state.pending_font_prefix = std::move(value);
    return {};
  }
  if (state.in_vnotice && !state.emitted_vnotice_heading) {
    state.emitted_vnotice_heading = true;
    trailing = apply_font_spans_to_text(std::move(trailing), spans);
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
  trailing = apply_font_spans_to_text(std::move(trailing), spans);
  if (state.in_generated_title_page) {
    return render_generated_title_font_line(std::move(trailing));
  }
  return render_simple_gml_control("pinline", std::move(trailing));
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
  if (ascii_starts_with_case_insensitive(lower, "cfontdef")) {
    return {};
  }
  if (state.in_table && !ascii_starts_with_case_insensitive(lower, "sretbl") &&
      !ascii_starts_with_case_insensitive(lower, "cz")) {
    return render_table_body_gml(std::move(segment), state);
  }
  if (ascii_starts_with_case_insensitive(lower, "cfont")) {
    return render_font_gml(rest_after_first_word(segment), state);
  }
  if (ascii_starts_with_case_insensitive(lower, "cselect")) {
    return render_link_gml(rest_after_first_word(segment));
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
    return render_keyed_gml_control("fig", "id", segment.substr(5));
  }
  if (ascii_starts_with_case_insensitive(lower, "srefig")) {
    return ":efig.";
  }
  if (ascii_starts_with_case_insensitive(lower, "srtbl")) {
    state.in_table = true;
    state.table_columns = 0;
    state.table_separator_offsets.clear();
    state.table_line_width = 0;
    state.table_visual_buffer.clear();
    state.pending_table_row.clear();
    return render_table_gml(segment.substr(5));
  }
  if (ascii_starts_with_case_insensitive(lower, "sretbl")) {
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
    state.table_columns = 0;
    state.table_separator_offsets.clear();
    state.table_line_width = 0;
    state.table_visual_buffer.clear();
    return output;
  }
  if (ascii_starts_with_case_insensitive(lower, "srftn")) {
    state.pending_footnote_id = trim_control_operand(segment.substr(2));
    return {};
  }
  if (ascii_starts_with_case_insensitive(lower, "sreftn")) {
    state.pending_footnote_id.clear();
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
    if (ascii_starts_with_case_insensitive(lower_layout, "off table") ||
        ascii_starts_with_case_insensitive(lower_layout, "off etable")) {
      return {};
    }
    if (state.pending_copyright_extension &&
        ascii_starts_with_case_insensitive(layout, "break")) {
      auto rendered = render_layout_gml(layout);
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
    return render_layout_gml(layout);
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
  if (auto continuation = render_marker_continuation_gml(segment)) {
    return *continuation;
  }
  if (!state.pending_font_prefix.empty()) {
    auto pending = std::move(state.pending_font_prefix);
    state.pending_font_prefix.clear();
    return render_pending_font_continuation_gml(std::move(pending),
                                                std::move(segment));
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
  if (line.empty() || line == ":p.") {
    return results;
  }

  std::size_t begin = 0;
  while (begin <= line.size()) {
    const auto end = line.find('\n', begin);
    auto part = end == std::string::npos ? line.substr(begin)
                                         : line.substr(begin, end - begin);
    if (!part.empty() && part != ":p.") {
      if (ascii_starts_with_case_insensitive(part, ":pinline.")) {
        auto content = part.substr(std::string(":pinline.").size());
        if (!content.empty() && !rendered.empty() &&
            (ascii_starts_with_case_insensitive(rendered.back(), ":p ") ||
             ascii_starts_with_case_insensitive(rendered.back(), ":p.") ||
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
