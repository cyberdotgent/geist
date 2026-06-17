#include "geist/detail/internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace geist {

namespace {

std::string trim_trailing_blank_lines(std::string value) {
  while (value.size() >= 2 &&
         value.compare(value.size() - 2, 2, "\n\n") == 0) {
    value.resize(value.size() - 1);
  }
  return value;
}

} // namespace

std::string TocEntry::markdown() const {
  auto records = raw_records;
  if (!records.empty() && !records.front().empty() && !id.empty() &&
      !title.empty()) {
    const auto dot = records.front().find('.');
    if (dot != std::string::npos && records.front().front() == ':') {
      auto tag_end = std::size_t{1};
      while (tag_end < dot &&
             std::isalnum(static_cast<unsigned char>(records.front()[tag_end])) !=
                 0) {
        ++tag_end;
      }
      const auto tag =
          detail::ascii_lower(records.front().substr(1, tag_end - 1));
      if (tag == "h1" || tag == "h2" || tag == "h3" || tag == "h4" ||
          tag == "h5" || tag == "ih2" || tag == "preface" ||
          tag == "appendix") {
        records.front().resize(dot + 1);
        records.front() += id + " " + title;
      }
    }
  }
  return detail::render_markdown_records(records);
}

std::string BooDocument::markdown() const {
  return detail::render_markdown_records(raw_gml_records_);
}

namespace detail {

namespace {

bool is_heading_tag(const std::string& tag) {
  static const std::array<const char*, 8> heading_tags = {
      "h1", "h2", "h3", "h4", "h5", "ih2", "preface", "appendix"};
  for (const auto* heading_tag : heading_tags) {
    if (tag == heading_tag) {
      return true;
    }
  }
  return false;
}

std::string heading_prefix(const std::string& tag) {
  if (tag == "h1" || tag == "preface" || tag == "appendix") {
    return "# ";
  }
  if (tag == "h2" || tag == "ih2") {
    return "## ";
  }
  if (tag == "h3") {
    return "### ";
  }
  if (tag == "h4") {
    return "#### ";
  }
  return "##### ";
}

std::string gml_tag(const std::string& record) {
  if (record.empty() || record.front() != ':') {
    return {};
  }

  std::size_t cursor = 1;
  while (cursor < record.size()) {
    const auto ch = static_cast<unsigned char>(record[cursor]);
    if (std::isalnum(ch) == 0) {
      break;
    }
    ++cursor;
  }
  return ascii_lower(record.substr(1, cursor - 1));
}

std::size_t gml_content_offset(const std::string& record) {
  auto quote = char{0};
  for (std::size_t offset = 0; offset < record.size(); ++offset) {
    const auto ch = record[offset];
    if (quote != 0) {
      if (ch == quote) {
        quote = 0;
      }
      continue;
    }
    if (ch == '\'' || ch == '"') {
      quote = ch;
      continue;
    }
    if (ch == '.') {
      return offset + 1;
    }
  }
  return record.size();
}

std::string gml_content(const std::string& record) {
  const auto offset = gml_content_offset(record);
  if (offset >= record.size()) {
    return {};
  }
  return trim_ascii(record.substr(offset));
}

std::string markdown_marker_for_highlight(const std::string& tag) {
  if (tag == "hp1") {
    return "*";
  }
  if (tag == "hp2") {
    return "**";
  }
  if (tag == "hp3") {
    return "***";
  }
  return {};
}

std::string render_inline_markdown(std::string text) {
  std::string output;
  output.reserve(text.size());
  for (std::size_t cursor = 0; cursor < text.size();) {
    if (text[cursor] != ':') {
      output.push_back(text[cursor++]);
      continue;
    }

    const auto dot = text.find('.', cursor + 1);
    if (dot == std::string::npos) {
      output.push_back(text[cursor++]);
      continue;
    }

    auto tag = ascii_lower(text.substr(cursor + 1, dot - cursor - 1));
    const auto closing = ascii_starts_with_case_insensitive(tag, "e");
    if (closing) {
      tag.erase(tag.begin());
    }
    const auto marker = markdown_marker_for_highlight(tag);
    if (marker.empty()) {
      output.push_back(text[cursor++]);
      continue;
    }

    output += marker;
    cursor = dot + 1;
  }
  return output;
}

bool has_inline_highlight_markup(const std::string& text) {
  for (const auto* tag : {":hp1.", ":hp2.", ":hp3."}) {
    if (text.find(tag) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string gml_markdown_content(const std::string& record) {
  return render_inline_markdown(gml_content(record));
}

std::string gml_attr(const std::string& record, const std::string& attr) {
  const auto pattern = attr + "='";
  const auto begin = record.find(pattern);
  if (begin == std::string::npos) {
    return {};
  }
  const auto value_begin = begin + pattern.size();
  const auto value_end = record.find('\'', value_begin);
  if (value_end == std::string::npos) {
    return {};
  }
  return record.substr(value_begin, value_end - value_begin);
}

void append_block(std::string& output, const std::string& block) {
  if (block.empty()) {
    return;
  }
  if (!output.empty() && output.back() != '\n') {
    output.push_back('\n');
  }
  if (!output.empty() && output.size() >= 2 &&
      output.compare(output.size() - 2, 2, "\n\n") != 0) {
    output.push_back('\n');
  }
  output += block;
  output += "\n\n";
}

void append_title_page_line_part(std::vector<std::string>& lines,
                                 const std::string& line,
                                 std::size_t begin,
                                 std::size_t end) {
  auto part = trim_ascii(line.substr(begin, end - begin));
  if (part.empty()) {
    return;
  }

  if (ascii_starts_with_case_insensitive(part, "Document Number ")) {
    static const std::array<const char*, 12> months = {
        "January ",   "February ", "March ",    "April ",
        "May ",       "June ",     "July ",     "August ",
        "September ", "October ",  "November ", "December "};
    auto date_begin = std::string::npos;
    for (const auto* month : months) {
      const auto found = part.find(month);
      if (found != std::string::npos) {
        date_begin = std::min(date_begin, found);
      }
    }
    if (date_begin != std::string::npos && date_begin > 0) {
      auto document_number = trim_ascii(part.substr(0, date_begin));
      if (!document_number.empty()) {
        lines.push_back(std::move(document_number));
      }

      auto date_end = part.size();
      const auto comma = part.find(',', date_begin);
      if (comma != std::string::npos) {
        auto cursor = comma + 1;
        while (cursor < part.size() &&
               std::isspace(static_cast<unsigned char>(part[cursor])) != 0) {
          ++cursor;
        }
        auto year_end = cursor;
        while (year_end < part.size() &&
               std::isdigit(static_cast<unsigned char>(part[year_end])) != 0) {
          ++year_end;
        }
        if (year_end > cursor) {
          date_end = year_end;
        }
      }

      auto date = trim_ascii(part.substr(date_begin, date_end - date_begin));
      auto trailing = trim_ascii(part.substr(date_end));
      if (!date.empty()) {
        lines.push_back(std::move(date));
      }
      if (!trailing.empty()) {
        lines.push_back(std::move(trailing));
      }
      return;
    }
  }

  lines.push_back(std::move(part));
}

bool is_title_page_metadata_line(const std::string& line) {
  static const std::array<const char*, 3> labels = {
      "Document Number ", "Part Number ", "File Number "};
  for (const auto* label : labels) {
    if (ascii_starts_with_case_insensitive(line, label)) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> split_title_page_lines(const std::string& text) {
  static const std::array<const char*, 3> labels = {
      "Document Number ", "Part Number ", "File Number "};

  std::vector<std::string> lines;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    auto next = std::string::npos;
    for (const auto* label : labels) {
      const auto found = text.find(label, cursor);
      if (found != std::string::npos) {
        next = std::min(next, found);
      }
    }
    if (next == std::string::npos) {
      append_title_page_line_part(lines, text, cursor, text.size());
      return lines;
    }
    if (next > cursor) {
      append_title_page_line_part(lines, text, cursor, next);
      cursor = next;
      continue;
    }

    auto following = std::string::npos;
    for (const auto* label : labels) {
      const auto found = text.find(label, cursor + 1);
      if (found != std::string::npos) {
        following = std::min(following, found);
      }
    }
    append_title_page_line_part(lines,
                                text,
                                cursor,
                                following == std::string::npos ? text.size()
                                                               : following);
    if (following == std::string::npos) {
      return lines;
    }
    cursor = following;
  }
  return lines;
}

void flush_pending_title_page_lines(
    std::string& output,
    std::vector<std::string>& pending_bold_lines);

void append_title_page_markdown(std::string& output,
                                const std::string& text,
                                bool is_cover,
                                std::size_t& line_count,
                                bool& title_block_complete,
                                std::vector<std::string>& pending_bold_lines) {
  for (auto raw_line : split_title_page_lines(text)) {
    const auto title_block_line =
        is_cover ? line_count < 2
                 : !title_block_complete &&
                       !is_title_page_metadata_line(raw_line);
    if (!title_block_line) {
      title_block_complete = true;
      flush_pending_title_page_lines(output, pending_bold_lines);
    }

    auto line = render_inline_markdown(raw_line);
    if (title_block_line) {
      if (!has_inline_highlight_markup(raw_line)) {
        line = "**" + line + "**";
      }
      if (!is_cover) {
        pending_bold_lines.push_back(std::move(line));
        ++line_count;
        continue;
      }
    }
    append_block(output, line);
    ++line_count;
  }
}

void flush_pending_title_page_lines(
    std::string& output,
    std::vector<std::string>& pending_bold_lines) {
  if (pending_bold_lines.empty()) {
    return;
  }

  std::string block;
  for (std::size_t index = 0; index < pending_bold_lines.size(); ++index) {
    if (index != 0) {
      block += "<br>\n";
    }
    block += pending_bold_lines[index];
  }
  append_block(output, block);
  pending_bold_lines.clear();
}

void append_list_item(std::string& output, std::string text) {
  text = gml_markdown_content(std::move(text));
  if (text.empty()) {
    return;
  }
  if (!output.empty() && output.back() != '\n') {
    output.push_back('\n');
  }
  output += "- " + text + "\n";
}

void append_toc_item(std::string& output, const std::string& record) {
  auto text = gml_markdown_content(record);
  if (text.empty()) {
    return;
  }

  const auto id = gml_attr(record, "id");
  if (!id.empty()) {
    text = "`" + id + "` [" + text + "](#" + id + ")";
  }

  const auto level_attr = gml_attr(record, "level");
  char* level_end = nullptr;
  const auto parsed_level =
      std::strtol(level_attr.c_str(), &level_end, 10);
  const auto level =
      level_end != level_attr.c_str() && *level_end == '\0'
          ? std::max(parsed_level, long{0})
          : long{0};
  if (!output.empty() && output.back() != '\n') {
    output.push_back('\n');
  }
  output.append(static_cast<std::size_t>(level) * 2, ' ');
  output += "- " + text + "\n";
}

std::string render_link_markdown(const std::string& record) {
  auto text = gml_markdown_content(record);
  const auto target = gml_attr(record, "refid");
  if (text.empty()) {
    text = target;
  }
  if (target.empty()) {
    return text;
  }
  return "[" + text + "](#" + target + ")";
}

std::string render_image_markdown(const std::string& record) {
  auto text = gml_markdown_content(record);
  const auto resource = gml_attr(record, "resource");
  if (text.empty()) {
    text = resource.empty() ? "Image" : "Resource " + resource;
  }
  if (resource.empty()) {
    return text;
  }
  return "![" + text + "](resource:" + resource + ")";
}

std::string render_anchor_markdown(const std::string& record) {
  const auto text = gml_markdown_content(record);
  if (!text.empty()) {
    return text;
  }
  const auto id = gml_attr(record, "id");
  if (id.empty()) {
    return {};
  }
  return "<a id=\"" + id + "\"></a>";
}

std::optional<int> gml_int_attr(const std::string& record,
                                const std::string& attr) {
  const auto value = gml_attr(record, attr);
  if (value.empty()) {
    return std::nullopt;
  }

  char* end = nullptr;
  const auto parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0') {
    return std::nullopt;
  }
  return static_cast<int>(parsed);
}

std::string escape_markdown_table_cell(std::string value) {
  value = collapse_ascii_whitespace(std::move(value));
  std::string output;
  output.reserve(value.size());
  for (const auto ch : value) {
    if (ch == '|') {
      output += "\\|";
    } else {
      output.push_back(ch);
    }
  }
  return output;
}

bool looks_like_table_command(const std::string& value) {
  const auto text = ascii_lower(collapse_ascii_whitespace(value));
  if (text.empty()) {
    return false;
  }
  if (text == "none" || ascii_starts_with_case_insensitive(text, "go ")) {
    return true;
  }
  if (text.find(' ') != std::string::npos) {
    return false;
  }
  static const std::array<const char*, 20> command_prefixes = {
      "chg",    "clr",    "dlt",  "dsp", "end", "hld", "infomsg",
      "msg",    "off",    "prt",  "pwr", "rcv", "rls", "sbm",
      "snd",    "vry",    "wrk",  "wrt", "str", "sav"};
  for (const auto* prefix : command_prefixes) {
    if (ascii_starts_with_case_insensitive(text, prefix)) {
      return true;
    }
  }
  return false;
}

bool looks_like_table_row_label(const std::string& value) {
  const auto text = collapse_ascii_whitespace(value);
  if (text.empty()) {
    return false;
  }
  if (text.find('(') != std::string::npos ||
      text.find(')') != std::string::npos) {
    return true;
  }
  if (looks_like_table_command(text)) {
    return false;
  }
  return true;
}

struct TableCell {
  int column = -1;
  std::string text;
};

std::vector<int> infer_table_columns(const std::vector<TableCell>& cells) {
  std::vector<int> columns;
  for (const auto& cell : cells) {
    if (cell.column < 0) {
      continue;
    }
    if (!columns.empty() && cell.column == columns.front()) {
      break;
    }
    if (std::find(columns.begin(), columns.end(), cell.column) ==
        columns.end()) {
      columns.push_back(cell.column);
    }
  }

  if (columns.empty()) {
    for (const auto& cell : cells) {
      if (cell.column >= 0 &&
          std::find(columns.begin(), columns.end(), cell.column) ==
              columns.end()) {
        columns.push_back(cell.column);
      }
    }
    std::sort(columns.begin(), columns.end());
  }
  return columns;
}

std::size_t nearest_table_column(const std::vector<int>& columns, int column) {
  if (columns.empty() || column < 0) {
    return 0;
  }
  auto best = std::size_t{0};
  auto best_distance = std::abs(column - columns[0]);
  for (std::size_t index = 1; index < columns.size(); ++index) {
    const auto distance = std::abs(column - columns[index]);
    if (distance < best_distance) {
      best = index;
      best_distance = distance;
    }
  }
  return best;
}

std::string render_rows_as_markdown_table(
    const std::string& id,
    const std::vector<std::vector<std::string>>& rows) {
  if (rows.empty()) {
    return id.empty() ? "[Table]" : "[Table: " + id + "]";
  }

  std::ostringstream output;
  if (!id.empty()) {
    output << "<a id=\"" << id << "\"></a>\n\n";
  }
  const auto& header = rows.front();
  output << "|";
  for (const auto& cell : header) {
    output << " " << escape_markdown_table_cell(cell) << " |";
  }
  output << "\n|";
  for (std::size_t index = 0; index < header.size(); ++index) {
    output << " --- |";
  }
  output << "\n";
  for (std::size_t row_index = 1; row_index < rows.size(); ++row_index) {
    output << "|";
    for (const auto& cell : rows[row_index]) {
      output << " " << escape_markdown_table_cell(cell) << " |";
    }
    output << "\n";
  }
  return output.str();
}

std::optional<std::string> render_flat_three_column_table(
    const std::string& id,
    const std::vector<TableCell>& cells) {
  if (cells.size() < 6) {
    return std::nullopt;
  }

  std::vector<std::string> text_cells;
  text_cells.reserve(cells.size());
  for (const auto& cell : cells) {
    auto text = collapse_ascii_whitespace(cell.text);
    if (!text.empty()) {
      text_cells.push_back(std::move(text));
    }
  }
  if (text_cells.size() < 6) {
    return std::nullopt;
  }

  std::vector<std::vector<std::string>> rows;
  rows.push_back({text_cells[0], text_cells[1], text_cells[2]});

  auto cursor = std::size_t{3};
  while (cursor < text_cells.size()) {
    std::vector<std::string> row(3);
    auto body_begin = cursor;
    if (cursor + 2 < text_cells.size() &&
        looks_like_table_row_label(text_cells[cursor]) &&
        looks_like_table_command(text_cells[cursor + 1])) {
      row[0] = text_cells[cursor];
      row[1] = text_cells[cursor + 1];
      body_begin = cursor + 2;
    } else if (cursor + 1 < text_cells.size() &&
               looks_like_table_command(text_cells[cursor])) {
      row[1] = text_cells[cursor];
      body_begin = cursor + 1;
    } else {
      if (!rows.empty()) {
        if (!rows.back()[2].empty()) {
          rows.back()[2] += "<br>";
        }
        rows.back()[2] += text_cells[cursor++];
      } else {
        ++cursor;
      }
      continue;
    }

    auto next = body_begin;
    while (next < text_cells.size()) {
      const auto starts_labeled_row =
          next + 2 < text_cells.size() &&
          looks_like_table_row_label(text_cells[next]) &&
          looks_like_table_command(text_cells[next + 1]);
      const auto starts_blank_label_row =
          next + 1 < text_cells.size() &&
          looks_like_table_command(text_cells[next]);
      if (next > body_begin && (starts_labeled_row || starts_blank_label_row)) {
        break;
      }
      if (!row[2].empty()) {
        row[2] += "<br>";
      }
      row[2] += text_cells[next++];
    }

    rows.push_back(std::move(row));
    cursor = std::max(next, body_begin + 1);
  }

  return render_rows_as_markdown_table(id, rows);
}

std::string render_table_markdown(const std::string& id,
                                  const std::vector<TableCell>& cells) {
  if (cells.empty()) {
    return id.empty() ? "[Table]" : "[Table: " + id + "]";
  }

  auto columns = infer_table_columns(cells);
  if (columns.size() < 2) {
    if (auto table = render_flat_three_column_table(id, cells)) {
      return *table;
    }

    std::string fallback = id.empty() ? "[Table]" : "[Table: " + id + "]";
    for (const auto& cell : cells) {
      if (!cell.text.empty()) {
        fallback += "\n\n" + cell.text;
      }
    }
    return fallback;
  }

  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> current(columns.size());
  auto has_current = false;
  for (const auto& cell : cells) {
    const auto column_index = nearest_table_column(columns, cell.column);
    if (column_index == 0 && has_current) {
      rows.push_back(std::move(current));
      current = std::vector<std::string>(columns.size());
      has_current = false;
    }
    auto text = collapse_ascii_whitespace(cell.text);
    if (text.empty()) {
      continue;
    }
    if (!current[column_index].empty()) {
      current[column_index] += "<br>";
    }
    current[column_index] += std::move(text);
    has_current = true;
  }
  if (has_current) {
    rows.push_back(std::move(current));
  }

  if (rows.empty()) {
    return id.empty() ? "[Table]" : "[Table: " + id + "]";
  }

  return render_rows_as_markdown_table(id, rows);
}

} // namespace

std::string render_markdown_records(const std::vector<std::string>& records) {
  std::string output;
  bool in_list = false;
  bool in_table = false;
  bool in_title_page = false;
  bool title_page_is_cover = false;
  bool title_block_complete = false;
  std::size_t title_page_line_count = 0;
  std::string table_id;
  std::string pending_copyright_note;
  std::vector<TableCell> table_cells;
  std::vector<std::string> pending_title_page_bold_lines;

  for (const auto& record : records) {
    const auto tag = gml_tag(record);
    if (in_title_page) {
      if (tag == "p") {
        append_title_page_markdown(output,
                                   gml_content(record),
                                   title_page_is_cover,
                                   title_page_line_count,
                                   title_block_complete,
                                   pending_title_page_bold_lines);
        continue;
      }
      flush_pending_title_page_lines(output, pending_title_page_bold_lines);
      in_title_page = false;
    }

    if (in_table) {
      if (tag == "etable") {
        append_block(output, render_table_markdown(table_id, table_cells));
        in_table = false;
        table_id.clear();
        table_cells.clear();
        continue;
      }
      if (tag == "p" || tag == "hdref" || tag == "lblbox") {
        const auto column = gml_int_attr(record, "col").value_or(-1);
        auto text = tag == "hdref" ? render_link_markdown(record)
                                   : gml_markdown_content(record);
        if (!text.empty()) {
          table_cells.push_back({column, std::move(text)});
        }
      }
      continue;
    }

    if (tag == "cover" || tag == "tipage") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      in_title_page = true;
      title_page_is_cover = tag == "cover";
      title_block_complete = false;
      title_page_line_count = 0;
      pending_title_page_bold_lines.clear();
      in_list = false;
      continue;
    }

    if (tag.empty()) {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      append_block(output, collapse_ascii_whitespace(record));
      in_list = false;
      continue;
    }

    if (tag == "ul" || tag == "ol" || tag == "dl" || tag == "toc") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      in_list = true;
      continue;
    }
    if (tag == "eul" || tag == "eol" || tag == "edl" || tag == "etoc") {
      if (in_list && !output.empty() && output.back() == '\n') {
        output.push_back('\n');
      }
      in_list = false;
      continue;
    }
    if (tag == "li") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      append_list_item(output, record);
      in_list = true;
      continue;
    }
    if (tag == "tocentry") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      append_toc_item(output, record);
      in_list = true;
      continue;
    }

    if (in_list && !output.empty() && output.back() == '\n') {
      output.push_back('\n');
    }
    in_list = false;

    if (is_heading_tag(tag)) {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      append_block(output, heading_prefix(tag) + gml_markdown_content(record));
    } else if (tag == "vnhd") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      append_block(output, "**" + gml_markdown_content(record) + "**");
    } else if (tag == "p" || tag == "lblbox") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      append_block(output, gml_markdown_content(record));
    } else if (tag == "note") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      append_block(output, "**Note:** " + gml_markdown_content(record));
    } else if (tag == "coprnote") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
      }
      pending_copyright_note = gml_markdown_content(record);
    } else if (tag == "coprext") {
      auto text = gml_markdown_content(record);
      if (!pending_copyright_note.empty()) {
        text = pending_copyright_note + "<br>\n" + text;
        pending_copyright_note.clear();
      }
      append_block(output, text);
    } else if (tag == "hdref") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      append_block(output, render_link_markdown(record));
    } else if (tag == "image") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      append_block(output, render_image_markdown(record));
    } else if (tag == "anchor") {
      append_block(output, render_anchor_markdown(record));
    } else if (tag == "fig") {
      const auto id = gml_attr(record, "id");
      if (!id.empty()) {
        append_block(output, "<a id=\"" + id + "\"></a>");
      }
    } else if (tag == "table") {
      in_table = true;
      table_id = gml_attr(record, "id");
      table_cells.clear();
    } else if (tag == "i1" || tag == "grpsep" || tag == "efig" ||
               tag == "etable" || tag == "fontdef") {
      continue;
    } else {
      append_block(output, gml_markdown_content(record));
    }
  }

  flush_pending_title_page_lines(output, pending_title_page_bold_lines);
  if (!pending_copyright_note.empty()) {
    append_block(output, pending_copyright_note);
  }
  return trim_trailing_blank_lines(std::move(output));
}

} // namespace detail

} // namespace geist
