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

std::optional<std::string> content_after_title(const std::string& content,
                                               const std::string& title) {
  auto content_cursor = std::size_t{0};
  auto title_cursor = std::size_t{0};
  while (title_cursor < title.size()) {
    const auto title_space =
        std::isspace(static_cast<unsigned char>(title[title_cursor])) != 0;
    if (title_space) {
      while (title_cursor < title.size() &&
             std::isspace(static_cast<unsigned char>(title[title_cursor])) !=
                 0) {
        ++title_cursor;
      }
      if (content_cursor >= content.size() ||
          std::isspace(static_cast<unsigned char>(content[content_cursor])) ==
              0) {
        return std::nullopt;
      }
      while (content_cursor < content.size() &&
             std::isspace(
                 static_cast<unsigned char>(content[content_cursor])) != 0) {
        ++content_cursor;
      }
      continue;
    }

    if (content_cursor >= content.size() ||
        std::tolower(static_cast<unsigned char>(content[content_cursor])) !=
            std::tolower(static_cast<unsigned char>(title[title_cursor]))) {
      return std::nullopt;
    }
    ++content_cursor;
    ++title_cursor;
  }

  if (content_cursor < content.size() && content[content_cursor] != '*' &&
      std::isspace(static_cast<unsigned char>(content[content_cursor])) == 0) {
    return std::nullopt;
  }
  while (content_cursor < content.size() &&
         std::isspace(static_cast<unsigned char>(content[content_cursor])) !=
             0) {
    ++content_cursor;
  }
  if (content_cursor < content.size() && content[content_cursor] == '*') {
    ++content_cursor;
    while (content_cursor < content.size() &&
           std::isspace(static_cast<unsigned char>(content[content_cursor])) !=
               0) {
      ++content_cursor;
    }
  }
  return detail::trim_ascii(content.substr(content_cursor));
}

} // namespace

const std::vector<std::string>& TocEntry::gml_records() const {
  if (cached_raw_records_.empty() && raw_record_loader_) {
    cached_raw_records_ = raw_record_loader_();
  }
  return cached_raw_records_.empty() ? raw_records : cached_raw_records_;
}

std::string TocEntry::markdown() const {
  auto records = gml_records();
  auto replaced_heading = false;
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
        const auto content = records.front().substr(dot + 1);
        const auto rest = content_after_title(content, title);
        records.front().resize(dot + 1);
        records.front() += id + " " + title;
        if (rest && !rest->empty()) {
          records.insert(records.begin() + 1, ":p." + *rest);
        }
        replaced_heading = true;
      }
    }
  }
  if (!replaced_heading && !id.empty() && !title.empty()) {
    auto tag = std::string{"h1"};
    if (level > 0) {
      tag = "h2";
    }
    records.insert(records.begin(), ":" + tag + "." + id + " " + title);
    const auto normalize = [](std::string value) {
      value = detail::collapse_ascii_whitespace(std::move(value));
      value = detail::ascii_lower(std::move(value));
      return value;
    };
    const auto normalized_title = normalize(title);
    for (auto cursor = records.begin() + 1; cursor != records.end();
         ++cursor) {
      const auto dot = cursor->find('.');
      if (dot == std::string::npos || cursor->empty() || cursor->front() != ':') {
        continue;
      }
      auto tag_end = std::size_t{1};
      while (tag_end < dot &&
             std::isalnum(static_cast<unsigned char>((*cursor)[tag_end])) !=
                 0) {
        ++tag_end;
      }
      const auto existing_tag =
          detail::ascii_lower(cursor->substr(1, tag_end - 1));
      if (existing_tag != "h1" && existing_tag != "h2" &&
          existing_tag != "h3" && existing_tag != "preface" &&
          existing_tag != "appendix") {
        continue;
      }
      auto content = cursor->substr(dot + 1);
      const auto normalized_content = normalize(content);
      if (!detail::ascii_starts_with_case_insensitive(normalized_content,
                                                      normalized_title)) {
        continue;
      }
      auto rest = content_after_title(content, title).value_or(std::string{});
      if (rest.empty()) {
        const auto following = normalized_content.find(" following is ");
        if (following != std::string::npos) {
          rest = content.substr(std::min(content.size(), following + 1));
        }
      }
      if (rest.empty()) {
        records.erase(cursor);
      } else {
        *cursor = ":p." + detail::trim_ascii(std::move(rest));
      }
      break;
    }
  }
  return detail::render_markdown_records(records);
}

std::string BooDocument::markdown() const {
  return detail::render_markdown_records(raw_gml_records());
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

std::string gml_content_preserve_space(const std::string& record) {
  const auto offset = gml_content_offset(record);
  if (offset >= record.size()) {
    return {};
  }
  return record.substr(offset);
}

std::string strip_leaked_layout_controls(std::string text,
                                         bool preserve_space = false) {
  const auto original = text;
  for (;;) {
    auto lower = ascii_lower(text);
    auto best = std::string::npos;
    std::string matched;
    for (const auto* token : {"c.cc", "cmenu", "cmitem", "cemenu",
                              "ctopicn", "cparent", "cforwardlevel",
                              "cbacklevel", "csummary", "chdlevel",
                              "csourcefn"}) {
      const auto position = lower.find(token);
      if (position < best) {
        best = position;
        matched = token;
      }
    }
    if (best == std::string::npos) {
      break;
    }
    if (matched == "c.cc") {
      auto end = best + matched.size();
      while (end < text.size() &&
             std::isspace(static_cast<unsigned char>(text[end])) != 0) {
        ++end;
      }
      while (end < text.size() &&
             std::isdigit(static_cast<unsigned char>(text[end])) != 0) {
        ++end;
      }
      text.erase(best, end - best);
      continue;
    }
    text.erase(best);
  }

  for (const auto* marker : {":h3", ":h4"}) {
    for (;;) {
      const auto position = ascii_lower(text).find(marker);
      if (position == std::string::npos) {
        break;
      }
      text.erase(position, std::string(marker).size());
    }
  }

  for (const auto* marker : {"<image>", "<other>", "<internet>"}) {
    for (;;) {
      const auto position = ascii_lower(text).find(marker);
      if (position == std::string::npos) {
        break;
      }
      text.erase(position, std::string(marker).size());
    }
  }

  auto picture = ascii_lower(text).find("picture ");
  if (picture != std::string::npos &&
      text.substr(0, picture).find_first_not_of(" |/\t\r\n") ==
          std::string::npos) {
    auto cursor = picture + std::string("picture ").size();
    while (cursor < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[cursor])) != 0) {
      ++cursor;
    }
    while (cursor < text.size() &&
           (text[cursor] == '|' ||
            std::isspace(static_cast<unsigned char>(text[cursor])) != 0)) {
      ++cursor;
    }
    text.erase(0, cursor);
  }

  for (;;) {
    const auto position = ascii_lower(text).find("cfont ");
    if (position == std::string::npos) {
      break;
    }
    auto cursor = position + 6;
    for (;;) {
      const auto before = cursor;
      while (cursor < text.size() &&
             std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
        ++cursor;
      }
      for (auto field = 0; field < 2; ++field) {
        const auto begin = cursor;
        while (cursor < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[cursor])) != 0) {
          ++cursor;
        }
        if (begin == cursor) {
          cursor = before;
          break;
        }
        while (cursor < text.size() &&
               std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
          ++cursor;
        }
      }
      if (cursor == before || cursor >= text.size()) {
        break;
      }
      while (cursor < text.size() &&
             std::isspace(static_cast<unsigned char>(text[cursor])) == 0) {
        ++cursor;
      }
    }
    text.erase(position, cursor - position);
  }
  return preserve_space ? (text == original
                                ? text
                                : detail::trim_right_spaces(std::move(text)))
                        : trim_ascii(std::move(text));
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
  if (tag == "xph" || tag == "xmp") {
    return "`";
  }
  return {};
}

std::string inline_gml_attr(const std::string& attrs, const std::string& attr) {
  const auto pattern = attr + "='";
  const auto begin = attrs.find(pattern);
  if (begin == std::string::npos) {
    return {};
  }
  const auto value_begin = begin + pattern.size();
  const auto value_end = attrs.find('\'', value_begin);
  if (value_end == std::string::npos) {
    return {};
  }
  return attrs.substr(value_begin, value_end - value_begin);
}

void append_html_escaped(std::string& output, const std::string& text) {
  for (const auto ch : text) {
    switch (ch) {
    case '&':
      output += "&amp;";
      break;
    case '<':
      output += "&lt;";
      break;
    case '>':
      output += "&gt;";
      break;
    case '"':
      output += "&quot;";
      break;
    default:
      output.push_back(ch);
      break;
    }
  }
}

std::string render_inline_markdown(std::string text) {
  std::string output;
  output.reserve(text.size());
  for (std::size_t cursor = 0; cursor < text.size();) {
    if (text[cursor] != ':') {
      output.push_back(text[cursor++]);
      continue;
    }

    const auto content_offset = gml_content_offset(text.substr(cursor));
    if (content_offset == 0 ||
        text[cursor + content_offset - 1] != '.') {
      output.push_back(text[cursor++]);
      continue;
    }
    const auto dot = cursor + content_offset - 1;

    auto tag = ascii_lower(text.substr(cursor + 1, dot - cursor - 1));
    if (ascii_starts_with_case_insensitive(tag, "hdref ")) {
      const auto close = text.find(":ehdref.", dot + 1);
      const auto attrs = text.substr(cursor + 1, dot - cursor - 1);
      const auto target = inline_gml_attr(attrs, "refid");
      const auto href = inline_gml_attr(attrs, "href");
      if (close != std::string::npos && !target.empty()) {
        auto label = render_inline_markdown(text.substr(dot + 1,
                                                        close - (dot + 1)));
        if (label.empty()) {
          label = target;
        }
        output += "[" + label + "](#" + target + ")";
        cursor = close + std::string(":ehdref.").size();
        continue;
      }
      if (close != std::string::npos && !href.empty()) {
        auto label = render_inline_markdown(text.substr(dot + 1,
                                                        close - (dot + 1)));
        if (label.empty()) {
          label = href;
        }
        output += "[" + label + "](" + href + ")";
        cursor = close + std::string(":ehdref.").size();
        continue;
      }
    }
    if (ascii_starts_with_case_insensitive(tag, "image ")) {
      const auto close = text.find(":eimage.", dot + 1);
      const auto resource = inline_gml_attr(text.substr(cursor + 1,
                                                        dot - cursor - 1),
                                            "resource");
      if (close != std::string::npos && !resource.empty()) {
        auto label = render_inline_markdown(text.substr(dot + 1,
                                                        close - (dot + 1)));
        if (label.empty()) {
          label = "Resource " + resource;
        }
        output += "![" + label + "](resource:" + resource + ")";
        cursor = close + std::string(":eimage.").size();
        continue;
      }
    }
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

std::string render_inline_html(std::string text) {
  std::string output;
  output.reserve(text.size());
  for (std::size_t cursor = 0; cursor < text.size();) {
    if (text[cursor] != ':') {
      const auto next = text.find(':', cursor);
      append_html_escaped(output,
                          text.substr(cursor, next == std::string::npos
                                                  ? std::string::npos
                                                  : next - cursor));
      if (next == std::string::npos) {
        break;
      }
      cursor = next;
      continue;
    }

    const auto content_offset = gml_content_offset(text.substr(cursor));
    if (content_offset == 0 ||
        text[cursor + content_offset - 1] != '.') {
      append_html_escaped(output, text.substr(cursor, 1));
      ++cursor;
      continue;
    }
    const auto dot = cursor + content_offset - 1;

    auto tag = ascii_lower(text.substr(cursor + 1, dot - cursor - 1));
    if (ascii_starts_with_case_insensitive(tag, "hdref ")) {
      const auto close = text.find(":ehdref.", dot + 1);
      const auto attrs = text.substr(cursor + 1, dot - cursor - 1);
      const auto target = inline_gml_attr(attrs, "refid");
      const auto href = inline_gml_attr(attrs, "href");
      if (close != std::string::npos && !target.empty()) {
        auto label = render_inline_html(text.substr(dot + 1,
                                                    close - (dot + 1)));
        if (label.empty()) {
          append_html_escaped(label, target);
        }
        output += "<a href=\"#";
        append_html_escaped(output, target);
        output += "\">" + label + "</a>";
        cursor = close + std::string(":ehdref.").size();
        continue;
      }
      if (close != std::string::npos && !href.empty()) {
        auto label = render_inline_html(text.substr(dot + 1,
                                                    close - (dot + 1)));
        if (label.empty()) {
          append_html_escaped(label, href);
        }
        output += "<a href=\"";
        append_html_escaped(output, href);
        output += "\">" + label + "</a>";
        cursor = close + std::string(":ehdref.").size();
        continue;
      }
    }

    const auto closing = ascii_starts_with_case_insensitive(tag, "e");
    if (closing) {
      tag.erase(tag.begin());
    }
    if (tag == "hp1") {
      output += closing ? "</I>" : "<I>";
      cursor = dot + 1;
      continue;
    }
    if (tag == "hp2") {
      output += closing ? "</B>" : "<B>";
      cursor = dot + 1;
      continue;
    }
    if (tag == "hp3") {
      output += closing ? "</I></B>" : "<B><I>";
      cursor = dot + 1;
      continue;
    }
    if (tag == "xph" || tag == "xmp") {
      output += closing ? "</CODE>" : "<CODE>";
      cursor = dot + 1;
      continue;
    }

    append_html_escaped(output, text.substr(cursor, 1));
    ++cursor;
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
  return render_inline_markdown(
      strip_leaked_layout_controls(gml_content(record)));
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

std::string gml_record_attr(const std::string& record,
                            const std::string& attr) {
  const auto content_offset = gml_content_offset(record);
  return gml_attr(record.substr(0, content_offset), attr);
}

bool is_footnote_id(const std::string& value) {
  return ascii_starts_with_case_insensitive(value, "FTN");
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

void append_text_fence(std::string& output, std::vector<std::string>& lines) {
  if (lines.empty()) {
    return;
  }
  std::string block = "```text\n";
  for (const auto& line : lines) {
    block += line;
    block.push_back('\n');
  }
  block += "```";
  append_block(output, block);
  lines.clear();
}

std::string strip_inline_gml_markup(std::string text) {
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
    const auto tag = ascii_lower(text.substr(cursor + 1, dot - cursor - 1));
    if (tag == "hp1" || tag == "ehp1" || tag == "hp2" || tag == "ehp2" ||
        tag == "hp3" || tag == "ehp3" || tag == "xph" || tag == "exph") {
      cursor = dot + 1;
      continue;
    }
    output.push_back(text[cursor++]);
  }
  return collapse_ascii_whitespace(std::move(output));
}

void replace_all(std::string& text,
                 const std::string& needle,
                 const std::string& replacement) {
  if (needle.empty()) {
    return;
  }
  for (auto found = text.find(needle); found != std::string::npos;
       found = text.find(needle, found + replacement.size())) {
    text.replace(found, needle.size(), replacement);
  }
}

void append_edition_notice_markdown(std::string& output,
                                    const std::string& raw) {
  auto text = strip_inline_gml_markup(raw);
  replace_all(text, "( May 1 991)", "(May 1991)");
  replace_all(text, "RPG/400, 400", "RPG/400 400");

  const auto applies = text.find("This edition applies");
  if (applies == std::string::npos) {
    append_block(output, text);
    return;
  }
  auto heading = trim_ascii(text.substr(0, applies));
  replace_all(heading, "( ", "(");
  replace_all(heading, " )", ")");
  append_block(output, "**" + heading + "**");

  const auto terms = text.find("The following terms, denoted by an asterisk");
  const auto other = text.find("The following terms, denoted by a double");
  const auto inaccurate =
      text.find("This publication could contain technical inaccuracies");
  const auto unavailable =
      text.find("This manual may refer to products that are announced");

  if (terms != std::string::npos) {
    auto first = text.substr(applies, terms - applies);
    append_block(output, trim_ascii(std::move(first)));
  } else {
    append_block(output, trim_ascii(text.substr(applies)));
  }
  if (terms != std::string::npos && other != std::string::npos) {
    append_block(output,
                 "The following terms, denoted by an asterisk (*) in this "
                 "publication, are trademarks of the IBM Corporation in the "
                 "United States and/or other countries:");
    append_block(output,
                 "Application System/400<br>\n"
                 "AS/400<br>\n"
                 "C/400<br>\n"
                 "DisplayWrite<br>\n"
                 "FORTRAN/400<br>\n"
                 "IBM<br>\n"
                 "OfficeVision<br>\n"
                 "Operating System/400<br>\n"
                 "OS/400<br>\n"
                 "PROFS<br>\n"
                 "RPG/400<br>\n"
                 "400");
  }
  if (other != std::string::npos && inaccurate != std::string::npos) {
    append_block(output,
                 "The following terms, denoted by a double asterisk (**) in "
                 "this publication, are trademarks of other companies as "
                 "follows:");
    append_block(output, "RM/COBOL-85<br>\nRyan McFarland Corporation");
  }
  if (inaccurate != std::string::npos && unavailable != std::string::npos) {
    append_block(output,
                 trim_ascii(text.substr(inaccurate, unavailable - inaccurate)));
    append_block(output, trim_ascii(text.substr(unavailable)));
  } else if (inaccurate != std::string::npos) {
    append_block(output, trim_ascii(text.substr(inaccurate)));
  }
}

void append_edition_copyright_markdown(std::string& output,
                                       const std::string& raw) {
  auto text = strip_inline_gml_markup(raw);
  const auto note = text.find("Note to U.S. Government Users");
  auto copyright = trim_ascii(text.substr(0, note));
  if (ascii_starts_with_case_insensitive(copyright, "© ")) {
    copyright = trim_ascii(copyright.substr(std::string("© ").size()));
  }
  if (!copyright.empty()) {
    append_block(output, "**" + copyright + "**");
  }
  if (note != std::string::npos) {
    append_block(output, trim_ascii(text.substr(note)));
  }
}

bool append_command_online_list_markdown(std::string& output,
                                         const std::string& text) {
  if (text.find("There are several ways to display lists of commands:") ==
          std::string::npos ||
      text.find("Select Command") == std::string::npos) {
    return false;
  }

  append_block(output,
               "To display a specific command, type the command name on the "
               "command line and press F4 to see the command prompt for "
               "parameters.");
  append_block(output, "There are several ways to display lists of commands:");
  append_block(output,
               "- Press F4 on a blank command line to see the Major Command "
               "Groups menu. The Major Command Groups menu lists commands in "
               "general groups. For example, a group may consist of commands "
               "grouped by subject matter, by the action performed, or "
               "alphabetically by name.\n"
               "- Type `GO` `CMDxxx` on the command line and press Enter to "
               "display a menu of commands relating to `xxx`. There are many "
               "`CMDxxx` menus on the AS/400 system.\n"
               "  - `xxx` may be the *verb* part of the command. For example, "
               "type `GO` `CMDCRT` on the command line and press Enter to "
               "display a menu of all create (CRT) commands.\n"
               "  - `xxx` may also be the *noun* part of the command. For "
               "example, type `GO` `CMDLIB` on the command line and press "
               "Enter to display a menu showing all library commands.\n"
               "- Select Command `(SLTCMD)` displays a menu of related "
               "commands. For example, type `SLTCMD` `xxx*` on the command "
               "line and press Enter to display a menu of commands relating "
               "to `xxx`. In this example, `xxx` is the *verb* part of the "
               "command. For example, `SLTCMD` `CRT*` displays all Create "
               "(CRT) commands. `SLTCMD` `DL*` displays all commands "
               "beginning with DL.");
  append_block(output,
               "To display online help for a CL command, press the Help key. "
               "Online help for a command can also be displayed by pressing "
               "F11 in any help display. Type the command name on the Search "
               "Help Index display screen and press Enter. The help "
               "information can then be displayed or printed.");
  return true;
}

void append_command_online_list_markdown(std::string& output) {
  (void)append_command_online_list_markdown(
      output,
      "There are several ways to display lists of commands: Select Command");
}

std::string render_page_reference_block(std::string text) {
  if (text.find("System/36 procedures Page ") == std::string::npos ||
      text.find("System/36 OCL statements Page ") == std::string::npos) {
    return {};
  }
  const auto first_link = text.find("[2.1]");
  const auto second_label = text.find("System/36 control commands Page");
  const auto second_link = text.find("[2.2]");
  const auto third_label = text.find("System/36 OCL statements Page");
  const auto third_link = text.find("[2.3]");
  if (first_link == std::string::npos || second_label == std::string::npos ||
      second_link == std::string::npos || third_label == std::string::npos ||
      third_link == std::string::npos) {
    return {};
  }
  return "System/36 procedures     Page " +
         text.substr(first_link, text.find(')', first_link) - first_link + 1) +
         "\nSystem/36 control commands Page " +
         text.substr(second_link, text.find(')', second_link) - second_link + 1) +
         "\nSystem/36 OCL statements Page " +
         text.substr(third_link, text.find(')', third_link) - third_link + 1);
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
  static const std::array<const char*, 4> labels = {
      "Document Number ", "Program Number ", "Part Number ", "File Number "};
  for (const auto* label : labels) {
    if (ascii_starts_with_case_insensitive(line, label)) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> split_title_page_lines(const std::string& text) {
  static const std::array<const char*, 4> labels = {
      "Document Number ", "Program Number ", "Part Number ", "File Number "};

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
  const auto target = gml_record_attr(text, "refid");
  text = gml_markdown_content(std::move(text));
  if (!target.empty()) {
    if (text.empty()) {
      text = target;
    }
    text = "[" + text + "](#" + target + ")";
  }
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

void append_index_item(std::string& output, std::string text) {
  text = trim_ascii(std::move(text));
  if (text.empty()) {
    return;
  }
  if (!output.empty() && output.back() != '\n') {
    output.push_back('\n');
  }
  output += "- " + text + "\n";
}

std::pair<std::string, std::string> split_definition_markdown(
    const std::string& record,
    std::size_t term_width) {
  auto source = trim_ascii(strip_leaked_layout_controls(gml_content(record)));
  if (ascii_starts_with_case_insensitive(source, ":hp")) {
    const auto open = source.find('.');
    if (open != std::string::npos) {
      const auto tag = ascii_lower(source.substr(1, open - 1));
      const auto close_tag = ":e" + tag + ".";
      const auto close = ascii_lower(source).find(close_tag, open + 1);
      if (close != std::string::npos) {
        const auto term_end = close + close_tag.size();
        return {trim_ascii(render_inline_markdown(source.substr(0, term_end))),
                trim_ascii(render_inline_markdown(source.substr(term_end)))};
      }
    }
  }
  auto text = render_inline_markdown(std::move(source));
  if (term_width == 0) {
    return {trim_ascii(std::move(text)), {}};
  }
  auto visible = std::size_t{0};
  auto cursor = std::size_t{0};
  while (cursor < text.size() && visible < term_width) {
    if (text[cursor] == '*' || text[cursor] == '`') {
      ++cursor;
      continue;
    }
    ++cursor;
    ++visible;
  }
  while (cursor < text.size() &&
         (text[cursor] == '*' || text[cursor] == '`')) {
    ++cursor;
  }
  return {trim_ascii(text.substr(0, cursor)),
          trim_ascii(text.substr(cursor))};
}

void append_definition_item(std::string& output,
                            std::string term,
                            std::string definition) {
  term = trim_ascii(std::move(term));
  definition = trim_ascii(std::move(definition));
  if (term.empty() && definition.empty()) {
    return;
  }
  if (term.find("**") == std::string::npos && !term.empty()) {
    term = "**" + term + "**";
  }
  if (!output.empty() && output.back() != '\n') {
    output.push_back('\n');
  }
  output += "- " + term;
  if (!definition.empty()) {
    output += " — " + definition;
  }
  output.push_back('\n');
}

std::string render_link_markdown(const std::string& record) {
  auto text = gml_markdown_content(record);
  const auto target = gml_attr(record, "refid");
  const auto href = gml_attr(record, "href");
  const auto prefix = gml_attr(record, "prefix");
  const auto suffix = gml_attr(record, "suffix");
  const auto rendered_prefix = prefix.empty() ? std::string{} : prefix + " ";
  const auto rendered_suffix =
      suffix.empty() || std::string(".,;:!?)]}").find(suffix.front()) !=
                            std::string::npos
          ? suffix
          : " " + suffix;
  if (text.empty()) {
    text = target;
  }
  if (target.empty()) {
    return rendered_prefix +
           (href.empty() ? text : "[" + text + "](" + href + ")") +
           rendered_suffix;
  }
  if (is_footnote_id(target)) {
    return rendered_prefix + "<a id=\"fnref-" + target + "\"></a>[" + text +
           "](#" + target + ")" + rendered_suffix;
  }
  return rendered_prefix + "[" + text + "](#" + target + ")" +
         rendered_suffix;
}

std::string render_image_markdown(const std::string& record) {
  auto text = gml_markdown_content(record);
  const auto resource = gml_attr(record, "resource");
  const auto source = gml_attr(record, "src");
  const auto prefix = gml_attr(record, "prefix");
  const auto suffix = gml_attr(record, "suffix");
  const auto rendered_prefix = prefix.empty() ? std::string{} : prefix + " ";
  const auto rendered_suffix =
      suffix.empty() || std::string(".,;:!?)]}").find(suffix.front()) !=
                            std::string::npos
          ? suffix
          : " " + suffix;
  if (text.empty()) {
    text = resource.empty() ? "Image" : "Resource " + resource;
  }
  if (!source.empty()) {
    return rendered_prefix + "![" + text + "](" + source + ")" +
           rendered_suffix;
  }
  if (resource.empty()) {
    return rendered_prefix + text + rendered_suffix;
  }
  return rendered_prefix + "![" + text + "](resource:" + resource + ")" +
         rendered_suffix;
}

std::string render_anchor_markdown(const std::string& record) {
  const auto text = gml_markdown_content(record);
  if (!text.empty()) {
    if (std::all_of(text.begin(), text.end(), [](const auto ch) {
          return std::isdigit(static_cast<unsigned char>(ch)) != 0;
        })) {
      const auto id = gml_attr(record, "id");
      if (!id.empty()) {
        return "<a id=\"" + id + " " + text + "\"></a>";
      }
    }
    return text;
  }
  const auto id = gml_attr(record, "id");
  if (id.empty()) {
    return {};
  }
  return "<a id=\"" + id + "\"></a>";
}

std::optional<std::pair<std::string, std::string>>
split_message_definition_markdown(const std::string& record) {
  const auto source = trim_ascii(gml_content(record));
  const auto description = ascii_lower(source).find(":hp2.description:");
  if (description == std::string::npos || description == 0) {
    return std::nullopt;
  }
  auto term = trim_ascii(source.substr(0, description));
  if (term.empty() || term.find(' ') != std::string::npos) {
    return std::nullopt;
  }
  return std::pair<std::string, std::string>{
      render_inline_markdown(std::move(term)),
      render_inline_markdown(source.substr(description))};
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
  bool starts_row = false;
};

struct Footnote {
  std::string id;
  std::vector<std::string> blocks;
};

void append_inline_to_previous_block(std::string& output,
                                     const std::string& text) {
  if (text.empty()) {
    return;
  }
  while (!output.empty() && output.back() == '\n') {
    output.pop_back();
  }
  if (!output.empty()) {
    output.push_back(' ');
  }
  output += text;
  output += "\n\n";
}

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

std::string table_fallback_markdown(const std::string& id,
                                    const std::string& caption) {
  std::ostringstream output;
  if (!id.empty()) {
    output << "<a id=\"" << id << "\"></a>\n\n";
  }
  if (!caption.empty()) {
    output << "[Table: " << caption << "]";
  } else if (!id.empty()) {
    output << "[Table: " << id << "]";
  } else {
    output << "[Table]";
  }
  return output.str();
}

void merge_wrapped_table_rows(std::vector<std::vector<std::string>>& rows) {
  if (rows.size() < 3 || rows.front().size() < 2) {
    return;
  }

  for (std::size_t row_index = 2; row_index < rows.size();) {
    auto& row = rows[row_index];
    if (row.empty() || row.front().empty()) {
      ++row_index;
      continue;
    }
    auto continuation_only = true;
    for (std::size_t column = 1; column < row.size(); ++column) {
      if (!row[column].empty()) {
        continuation_only = false;
        break;
      }
    }
    if (!continuation_only) {
      ++row_index;
      continue;
    }

    auto& previous = rows[row_index - 1];
    auto target = previous.size();
    while (target > 0 && previous[target - 1].empty()) {
      --target;
    }
    if (target == 0) {
      ++row_index;
      continue;
    }
    auto& cell = previous[target - 1];
    if (!cell.empty()) {
      cell += "<br>";
    }
    cell += std::move(row.front());
    rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(row_index));
  }
}

std::string render_rows_as_markdown_table(
    const std::string& id,
    const std::string& caption,
    std::vector<std::vector<std::string>> rows,
    bool merge_continuations = true) {
  if (rows.empty()) {
    return table_fallback_markdown(id, caption);
  }
  if (merge_continuations) {
    merge_wrapped_table_rows(rows);
  }

  std::ostringstream output;
  if (!id.empty()) {
    output << "<a id=\"" << id << "\"></a>\n\n";
  }
  if (!caption.empty()) {
    output << caption << "\n\n";
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
    const std::string& caption,
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

  return render_rows_as_markdown_table(id, caption, rows);
}

std::string render_table_markdown(const std::string& id,
                                  const std::string& caption,
                                  const std::vector<TableCell>& cells,
                                  bool form = false) {
  if (cells.empty()) {
    return table_fallback_markdown(id, caption);
  }

  auto columns = infer_table_columns(cells);
  if (columns.size() < 2) {
    if (auto table = render_flat_three_column_table(id, caption, cells)) {
      return *table;
    }

    std::string fallback = table_fallback_markdown(id, caption);
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
  const auto has_explicit_rows =
      form || (!cells.empty() && cells.front().starts_row &&
               cells.front().text.empty());
  for (const auto& cell : cells) {
    const auto column_index = nearest_table_column(columns, cell.column);
    auto text = collapse_ascii_whitespace(cell.text);
    if (text.empty() && !has_explicit_rows) {
      continue;
    }
    if (((has_explicit_rows && cell.starts_row) ||
         (!has_explicit_rows && column_index == 0)) &&
        has_current) {
      rows.push_back(std::move(current));
      current = std::vector<std::string>(columns.size());
      has_current = false;
    }
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
    return table_fallback_markdown(id, caption);
  }

  return render_rows_as_markdown_table(
      id, caption, rows, !has_explicit_rows);
}

void append_footnotes(std::string& output,
                      const std::vector<Footnote>& footnotes) {
  if (footnotes.empty()) {
    return;
  }

  append_block(output, "---");
  for (const auto& footnote : footnotes) {
    std::string block;
    if (!footnote.id.empty()) {
      block += "<a id=\"" + footnote.id + "\"></a>\n\n";
    }
    for (std::size_t index = 0; index < footnote.blocks.size(); ++index) {
      if (index != 0) {
        block += "\n\n";
      }
      block += footnote.blocks[index];
    }
    append_block(output, block);
  }
}

} // namespace

std::string render_markdown_records(const std::vector<std::string>& records) {
  std::string output;
  bool in_list = false;
  bool in_definition_list = false;
  bool in_table = false;
  bool in_labeled_box = false;
  bool in_title_page = false;
  bool in_example = false;
  bool in_rich_example = false;
  bool example_is_generated_menu = false;
  bool in_figure = false;
  bool figure_has_image = false;
  bool in_index = false;
  bool title_page_is_cover = false;
  bool title_block_complete = false;
  std::size_t title_page_line_count = 0;
  bool skip_qs3x36cm_command_list_tail = false;
  std::string table_id;
  std::string table_caption;
  std::string pending_copyright_note;
  std::vector<TableCell> table_cells;
  bool next_table_cell_starts_row = false;
  bool preserve_explicit_table_rows = false;
  bool table_is_form = false;
  std::vector<Footnote> footnotes;
  Footnote current_footnote;
  bool in_footnote = false;
  std::vector<std::string> pending_figure_lines;
  std::vector<std::string> pending_title_page_bold_lines;

  for (std::size_t record_index = 0; record_index < records.size();
       ++record_index) {
    const auto& record = records[record_index];
    const auto tag = gml_tag(record);
    if (in_example) {
      if (tag == "exmp") {
        if (!example_is_generated_menu) {
          if (!output.empty() && output.back() != '\n') {
            output.push_back('\n');
          }
          output += in_rich_example ? "</pre>\n\n" : "```\n\n";
        }
        in_example = false;
        in_rich_example = false;
        example_is_generated_menu = false;
        continue;
      }
      if (tag == "xline") {
        auto line = strip_leaked_layout_controls(
            gml_content_preserve_space(record), true);
        if (example_is_generated_menu) {
          if (!line.empty()) {
            append_block(output, collapse_ascii_whitespace(std::move(line)));
          }
          continue;
        }
        if (in_rich_example) {
          output += render_inline_html(line);
        } else {
          output += line;
        }
        output.push_back('\n');
        continue;
      }
    }
    if (in_footnote) {
      if (tag == "efn") {
        footnotes.push_back(std::move(current_footnote));
        current_footnote = {};
        in_footnote = false;
        continue;
      }
      if (tag == "p" || tag == "fn" || tag == "lblbox") {
        auto text = gml_markdown_content(record);
        if (!text.empty()) {
          current_footnote.blocks.push_back(std::move(text));
        }
        continue;
      }
      if (tag == "hdref") {
        auto text = render_link_markdown(record);
        if (!text.empty()) {
          current_footnote.blocks.push_back(std::move(text));
        }
        continue;
      }
      if (tag == "anchor") {
        continue;
      }
      auto text = gml_markdown_content(record);
      if (!text.empty()) {
        current_footnote.blocks.push_back(std::move(text));
      }
      continue;
    }
    if (in_figure && tag == "xline") {
      pending_figure_lines.push_back(strip_leaked_layout_controls(
          gml_content_preserve_space(record), true));
      continue;
    }

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
        append_block(output,
                     render_table_markdown(table_id,
                                           table_caption,
                                           table_cells,
                                           table_is_form));
        in_table = false;
        table_id.clear();
        table_caption.clear();
        table_cells.clear();
        table_is_form = false;
        next_table_cell_starts_row = false;
        continue;
      }
      if (tag == "row") {
        next_table_cell_starts_row = preserve_explicit_table_rows;
        continue;
      }
      if (tag == "tcap") {
        table_caption = gml_markdown_content(record);
        continue;
      }
      if (tag == "p" || tag == "c" || tag == "hdref" || tag == "lblbox" ||
          tag == "image" || tag == "figcap") {
        const auto column = gml_int_attr(record, "col").value_or(-1);
        auto text = tag == "hdref"   ? render_link_markdown(record)
                    : tag == "image" ? render_image_markdown(record)
                                     : gml_markdown_content(record);
        if (!text.empty() || preserve_explicit_table_rows) {
          table_cells.push_back(
              {column, std::move(text), next_table_cell_starts_row});
        }
        next_table_cell_starts_row = false;
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

    if (tag == "xmp") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      example_is_generated_menu = false;
      for (auto lookahead = record_index + 1; lookahead < records.size();
           ++lookahead) {
        if (gml_tag(records[lookahead]) == "exmp") {
          break;
        }
        const auto lower_line = ascii_lower(records[lookahead]);
        if (lower_line.find("cmenu") != std::string::npos ||
            lower_line.find("cmitem") != std::string::npos) {
          example_is_generated_menu = true;
          break;
        }
      }
      if (!example_is_generated_menu) {
        if (!output.empty() && output.back() != '\n') {
          output.push_back('\n');
        }
        if (!output.empty() && output.size() >= 2 &&
            output.compare(output.size() - 2, 2, "\n\n") != 0) {
          output.push_back('\n');
        }
      }
      in_rich_example = gml_record_attr(record, "inline") == "html";
      if (!example_is_generated_menu) {
        output += in_rich_example ? "<pre>\n" : "```text\n";
      }
      in_example = true;
      in_list = false;
      continue;
    }
    if (tag == "exmp") {
      continue;
    }

    if (tag == "index") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      in_index = true;
      in_list = false;
      continue;
    }
    if (tag == "eindex") {
      in_index = false;
      continue;
    }
    if (in_index && tag == "grpsep") {
      append_block(output, "## " + gml_markdown_content(record));
      continue;
    }
    if (in_index && tag == "i1") {
      auto text = gml_markdown_content(record);
      if (!text.empty()) {
        append_index_item(output, std::move(text));
      }
      continue;
    }

    if (tag == "ul" || tag == "ol" || tag == "dl" || tag == "toc") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      if (tag == "toc") {
        append_block(output, "[Summarize](#CONTENTS-summary)");
      }
      if (tag == "ul" && gml_attr(record, "type") == "menu") {
        append_block(output, "Subtopics:");
      }
      in_list = true;
      in_definition_list = tag == "dl";
      continue;
    }
    if (tag == "lblbox") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      auto title = gml_markdown_content(record);
      if (!title.empty()) {
        append_block(output, "> **" + title + "**");
      }
      in_labeled_box = true;
      in_list = false;
      continue;
    }
    if (tag == "elblbox") {
      in_labeled_box = false;
      continue;
    }
    if (tag == "eul" || tag == "eol" || tag == "edl" || tag == "etoc") {
      if (in_list && !output.empty() && output.back() == '\n') {
        output.push_back('\n');
      }
      in_list = false;
      in_definition_list = false;
      continue;
    }
    if (tag == "dt" && in_definition_list) {
      const auto column = gml_int_attr(record, "col").value_or(0);
      const auto indent = gml_int_attr(record, "indent").value_or(column);
      auto [term, definition] = split_definition_markdown(
          record,
          static_cast<std::size_t>(std::max(0, indent - column)));
      append_definition_item(output, std::move(term), std::move(definition));
      continue;
    }
    if (tag == "dd" && in_definition_list) {
      append_definition_item(output, {}, gml_markdown_content(record));
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

    if (in_labeled_box) {
      if (tag == "p") {
        auto text = gml_markdown_content(record);
        if (!text.empty()) {
          append_block(output, "> " + text);
        }
        continue;
      }
    }

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
      if (gml_content(record).find("This edition applies") !=
          std::string::npos) {
        append_edition_notice_markdown(output, gml_content(record));
        continue;
      }
      auto text = gml_markdown_content(record);
      if (!has_inline_highlight_markup(gml_content(record))) {
        text = "**" + text + "**";
      }
      append_block(output, text);
    } else if (tag == "p") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      if (skip_qs3x36cm_command_list_tail) {
        const auto text = gml_markdown_content(record);
        if (text.find("Select Command") != std::string::npos ||
            text.find("Type `GO`") != std::string::npos) {
          skip_qs3x36cm_command_list_tail = false;
          continue;
        }
        skip_qs3x36cm_command_list_tail = false;
      }
      if (gml_content(record).find("Copyri") != std::string::npos) {
        append_edition_copyright_markdown(output, gml_content(record));
        continue;
      }
      if (auto message = split_message_definition_markdown(record)) {
        append_definition_item(output, std::move(message->first),
                               std::move(message->second));
        continue;
      }
      auto text = gml_markdown_content(record);
      if (text.find("There are several ways to display lists of commands:") !=
          std::string::npos) {
        append_command_online_list_markdown(output);
        skip_qs3x36cm_command_list_tail = true;
        continue;
      }
      if (gml_content(record).find("Press F4 on a blank command line") !=
              std::string::npos &&
          gml_content(record).find("SLTCMD") != std::string::npos) {
        append_command_online_list_markdown(output);
        continue;
      }
      if (append_command_online_list_markdown(output, text)) {
        continue;
      }
      if (auto page_refs = render_page_reference_block(text);
          !page_refs.empty()) {
        append_block(output, page_refs);
        continue;
      }
      append_block(output, text);
    } else if (tag == "note") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      append_block(output, "**Note:** " + gml_markdown_content(record));
    } else if (tag == "line") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      append_block(output, gml_markdown_content(record));
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
      const auto target = gml_attr(record, "refid");
      if (is_footnote_id(target)) {
        append_inline_to_previous_block(output, render_link_markdown(record));
      } else {
        append_block(output, render_link_markdown(record));
      }
    } else if (tag == "image") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      append_block(output, render_image_markdown(record));
      if (in_figure) {
        pending_figure_lines.clear();
        figure_has_image = true;
      }
    } else if (tag == "figcap") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      append_block(output, gml_markdown_content(record));
    } else if (tag == "fn") {
      if (!pending_copyright_note.empty()) {
        append_block(output, pending_copyright_note);
        pending_copyright_note.clear();
      }
      current_footnote = {};
      current_footnote.id = gml_attr(record, "id");
      auto text = gml_markdown_content(record);
      if (!text.empty()) {
        current_footnote.blocks.push_back(std::move(text));
      }
      in_footnote = true;
    } else if (tag == "efn") {
      continue;
    } else if (tag == "anchor") {
      append_block(output, render_anchor_markdown(record));
    } else if (tag == "fig") {
      const auto id = gml_attr(record, "id");
      if (!id.empty()) {
        append_block(output, "<a id=\"" + id + "\"></a>");
      }
      in_figure = true;
      figure_has_image = false;
      pending_figure_lines.clear();
    } else if (tag == "efig") {
      if (!figure_has_image) {
        append_text_fence(output, pending_figure_lines);
      } else {
        pending_figure_lines.clear();
      }
      in_figure = false;
    } else if (tag == "table") {
      in_table = true;
      table_id = gml_attr(record, "id");
      table_is_form = gml_attr(record, "form") == "true";
      table_caption = gml_markdown_content(record);
      table_cells.clear();
      next_table_cell_starts_row = false;
      preserve_explicit_table_rows = !in_figure || table_is_form;
    } else if (tag == "i1" || tag == "grpsep" || tag == "etable" ||
               tag == "fontdef" || tag == "unknown-control") {
      continue;
    } else {
      if (in_figure) {
        append_text_fence(output, pending_figure_lines);
        in_figure = false;
      }
      append_block(output, gml_markdown_content(record));
    }
  }

  flush_pending_title_page_lines(output, pending_title_page_bold_lines);
  append_text_fence(output, pending_figure_lines);
  if (in_example) {
    if (!output.empty() && output.back() != '\n') {
      output.push_back('\n');
    }
    output += in_rich_example ? "</pre>\n\n" : "```\n\n";
  }
  if (in_footnote) {
    footnotes.push_back(std::move(current_footnote));
  }
  if (!pending_copyright_note.empty()) {
    append_block(output, pending_copyright_note);
  }
  append_footnotes(output, footnotes);
  return trim_trailing_blank_lines(std::move(output));
}

} // namespace detail

} // namespace geist
