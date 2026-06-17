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
  const auto resource_id = picture_resource_id(target);
  if (!resource_id.empty()) {
    auto output = ":image resource='" + escape_gml_attr(resource_id) + "'.";
    if (!text.empty()) {
      output += text;
    }
    return output;
  }

  auto output = ":hdref refid='" + escape_gml_attr(target) + "'.";
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
  bool in_generated_title_page = false;
  bool emitted_toc = false;
  bool in_vnotice = false;
  bool emitted_vnotice_heading = false;
  bool pending_copyright_extension = false;
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
  return {};
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
  if (first_offset > 0) {
    for (auto& span : normalized_spans) {
      span.offset -= first_offset;
    }
    if (normalized_spans.size() == 1 && normalized_spans.front().offset == 0) {
      auto styled_length = value.size();
      while (styled_length > 0) {
        const auto ch = value[styled_length - 1];
        if (ch != '.' && ch != ',' && ch != ';' && ch != ':' && ch != '!' &&
            ch != '?') {
          break;
        }
        --styled_length;
      }
      normalized_spans.front().length =
          std::max(normalized_spans.front().length, styled_length);
    }
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
    return {};
  }
  trailing = apply_font_spans_to_text(std::move(trailing), spans);
  if (state.in_vnotice && !state.emitted_vnotice_heading) {
    state.emitted_vnotice_heading = true;
    return render_simple_gml_control("vnhd", std::move(trailing));
  }
  if (ascii_starts_with_case_insensitive(trailing, "note:")) {
    trailing = trim_ascii(trailing.substr(5));
    const std::string copyright_marker = "\xC2\xA9";
    const auto copyright = trailing.find(copyright_marker);
    if (copyright != std::string::npos) {
      auto note_text = trim_ascii(trailing.substr(0, copyright));
      auto copyright_text = trim_ascii(trailing.substr(copyright));
      state.pending_copyright_extension = true;
      return render_simple_gml_control("note", std::move(note_text)) + "\n" +
             render_simple_gml_control("coprnote", std::move(copyright_text));
    }
    return render_simple_gml_control("note", std::move(trailing));
  }
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
  if (ascii_starts_with_case_insensitive(lower, "cfont")) {
    return render_font_gml(rest_after_first_word(segment), state);
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
    return render_table_gml(segment.substr(5));
  }
  if (ascii_starts_with_case_insensitive(lower, "sretbl")) {
    return ":etable.";
  }
  if (ascii_starts_with_case_insensitive(lower, "sr")) {
    return render_anchor_gml(segment.substr(2));
  }
  if (ascii_starts_with_case_insensitive(lower, "cz")) {
    if (state.pending_copyright_extension &&
        ascii_starts_with_case_insensitive(rest_after_first_word(segment),
                                           "break")) {
      auto rendered = render_layout_gml(rest_after_first_word(segment));
      const auto dot = rendered.find('.');
      const auto content =
          dot == std::string::npos ? std::string{}
                                   : dot_text(rendered.substr(dot + 1));
      if (!content.empty()) {
        state.pending_copyright_extension = false;
        return render_simple_gml_control("coprext", content);
      }
    }
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
             ascii_starts_with_case_insensitive(rendered.back(), ":p."))) {
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
