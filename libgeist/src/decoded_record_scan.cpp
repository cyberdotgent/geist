// Scanning of the decoded logical record: splitting it into markup segments,
// reading control values out of it, tracing its font spans, and annotating
// its decoder placeholders. This is string-level work on the decoded record
// and is used by the record trace and by the passes that build the typed IR.
#include "geist/detail/internal.hpp"
#include "geist/detail/implicit_grid.hpp"
#include "geist/detail/procedure_rows.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
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
      ascii_lower_char(record[start]) != 's' ||
      ascii_lower_char(record[start + 1]) != 'h') {
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
    case '\'':
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
    static constexpr std::array<std::string_view, 4>
        kGeneratedSelectorMarkers = {"<IMAGE>", "<INTERNET>", "<OTHER>",
                                     "<>"};
    const auto generated_selector_marker = std::find_if(
        kGeneratedSelectorMarkers.begin(),
        kGeneratedSelectorMarkers.end(),
        [&](const auto marker) {
          return ascii_starts_with_case_insensitive(value, index, marker);
        });
    if (generated_selector_marker != kGeneratedSelectorMarkers.end()) {
      index += generated_selector_marker->size() - 1;
      if (!output.empty() && output.back() != ' ') {
        output.push_back(' ');
      }
      continue;
    }
    const auto generated_heading_marker =
        index + 3 < value.size() && value[index] == ':' &&
        (value[index + 1] == 'h' || value[index + 1] == 'H') &&
        value[index + 2] >= '1' && value[index + 2] <= '6' &&
        std::isspace(static_cast<unsigned char>(value[index + 3])) != 0;
    if (generated_heading_marker) {
      index += 2;
      if (!output.empty() && output.back() != ' ') {
        output.push_back(' ');
      }
      continue;
    }
    if (value[index] == '/') {
      auto run_end = index;
      while (run_end < value.size() && value[run_end] == '/') {
        ++run_end;
      }
      auto padding_end = run_end;
      while (padding_end < value.size() &&
             std::isspace(static_cast<unsigned char>(value[padding_end])) !=
                 0) {
        ++padding_end;
      }
      if (padding_end - run_end >= 2) {
        index = padding_end - 1;
        if (!output.empty() && output.back() != ' ') {
          output.push_back(' ');
        }
        continue;
      }
    }
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

bool looks_like_gml_control_at(const std::string& value, std::size_t offset) {
  while (offset < value.size() &&
         std::isspace(static_cast<unsigned char>(value[offset])) != 0) {
    ++offset;
  }
  if (offset >= value.size()) {
    return false;
  }
  const auto initial = ascii_lower_char(value[offset]);
  if (initial != 'c' && initial != 'e' && initial != 's') {
    return false;
  }

  static constexpr std::array<std::string_view, 50> prefixes = {
      "ctopicn",     "cparent",    "cforwardlevel",
      "cbacklevel",  "csummary",   "chdlevel",   "csourcefn",
      "st",          "c.sp",       "c.cp",       "ctocdef",    "ctoce",
      "etoc",
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
    // Same answer, one character earlier: a prefix whose own initial differs
    // from the folded initial can never match, so it never reaches the full
    // case-insensitive comparison.
    if (prefix.front() != initial ||
        !ascii_starts_with_case_insensitive(value, offset, prefix)) {
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

std::vector<DecodedMarkupSegmentSpan> split_decoded_markup_segment_spans(
    const std::string& decoded_record) {
  std::vector<DecodedMarkupSegmentSpan> segments;
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
  const auto make_segment = [&](std::size_t raw_begin, std::size_t raw_end) {
    const auto fixed = has_fixed_visual_payload(
        decoded_record.substr(raw_begin, raw_end - raw_begin));
    const auto trimmable = [&](const char ch) {
      const auto byte = static_cast<unsigned char>(ch);
      return std::isspace(byte) != 0 ||
             (!fixed && (byte < 0x20 || ch == '?'));
    };
    while (raw_begin < raw_end &&
           trimmable(decoded_record[raw_begin])) {
      ++raw_begin;
    }
    while (raw_end > raw_begin &&
           trimmable(decoded_record[raw_end - 1])) {
      --raw_end;
    }
    auto text = decoded_record.substr(raw_begin, raw_end - raw_begin);
    collapse_terminal_question_separator(text);
    return DecodedMarkupSegmentSpan{raw_begin, raw_end, std::move(text)};
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
    auto segment = make_segment(begin, end);
    if (!segment.text.empty()) {
      segments.push_back(std::move(segment));
    }
    begin = split_before ? cursor : cursor + 1;
  }

  auto segment = make_segment(begin, decoded_record.size());
  if (!segment.text.empty()) {
    segments.push_back(std::move(segment));
  }
  return segments;
}

std::vector<std::string> split_decoded_markup_segments(
    const std::string& decoded_record) {
  std::vector<std::string> result;
  for (auto& segment : split_decoded_markup_segment_spans(decoded_record)) {
    result.push_back(std::move(segment.text));
  }
  return result;
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

std::string rest_after_first_word(std::string value) {
  value = trim_ascii(std::move(value));
  const auto end = value.find_first_of(" \t\r\n,");
  if (end == std::string::npos) {
    return {};
  }
  return trim_ascii(value.substr(end + 1));
}

struct SelectControl {
  std::size_t column = 0;
  std::size_t length = 0;
  std::string target;
  std::string display_fragment;
  std::string external_kind;
  std::string external_target;
};

struct SelectedDisplayText {
  std::string prefix;
  std::string selected;
  std::string suffix;
};

struct SelectDisplayLineCandidate {
  std::string line;
  bool may_have_suppressed_prefix = false;
};

std::string render_select_gml(const SelectControl& control);

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
  bool fixed_e_display_active = false;
  bool fixed_e_display_after_divider = false;
  bool next_segment_is_all_e = false;
  bool current_segment_qualifies_e_display = false;
  bool pending_note_continuation = false;
  bool in_table = false;
  bool table_just_closed = false;
  bool in_labeled_box = false;
  bool in_figure = false;
  bool in_example = false;
  bool in_index = false;
  bool in_footnote = false;
  bool ignore_after_index = false;
  bool current_record_has_message_catalog = false;
  std::size_t next_selector_column = std::numeric_limits<std::size_t>::max();
  bool fixed_catalog_requested = false;
  bool in_fixed_catalog = false;
  bool in_semantic_message_catalog = false;
  bool in_glossary_catalog = false;
  std::string glossary_term;
  std::size_t table_columns = 0;
  std::vector<std::size_t> table_separator_offsets;
  std::size_t table_line_width = 0;
  std::size_t table_border_width = 0;
  bool table_final_separator_is_synthetic = false;
  bool table_layout_from_font_heading = false;
  bool table_font_heading_continuation = false;
  bool table_font_heading_can_continue = false;
  bool table_cfont_continuation = false;
  bool table_form_continuations = false;
  bool table_action_code_layout = false;
  bool table_hex_code_layout = false;
  bool table_event_qualifier_layout = false;
  bool table_has_picture = false;
  std::string table_visual_buffer;
  std::vector<std::string> pending_table_row;
  std::vector<SelectControl> table_physical_row_selects;
};

constexpr char literal_table_question = '\x7f';

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

std::vector<BooLogicalRecordTrace> trace_decoded_records(
    const std::vector<std::string>& decoded_records,
    std::uint32_t first_logical_record,
    const std::map<std::string, std::string>& font_definitions) {
  std::vector<BooLogicalRecordTrace> traced_records;
  traced_records.reserve(decoded_records.size());
  for (std::size_t record_index = 0; record_index < decoded_records.size();
       ++record_index) {
    BooLogicalRecordTrace traced;
    traced.logical_record =
        first_logical_record + static_cast<std::uint32_t>(record_index);
    traced.decoded_record = decoded_records[record_index];
    traced.segments =
        split_decoded_markup_segments(decoded_records[record_index]);
    for (std::size_t segment_index = 0; segment_index < traced.segments.size();
         ++segment_index) {
      const auto& segment = traced.segments[segment_index];
      if (!ascii_starts_with_case_insensitive(trim_ascii(segment), "cfont"))
        continue;
      auto font_spans =
          trace_font_spans(segment, traced.logical_record,
                           static_cast<std::uint32_t>(segment_index),
                           font_definitions);
      traced.font_spans.insert(traced.font_spans.end(),
                               std::make_move_iterator(font_spans.begin()),
                               std::make_move_iterator(font_spans.end()));
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
