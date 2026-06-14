#include "geist/detail/boo_detail.hpp"

#include <array>
#include <cctype>
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
  return detail::render_markdown_records(raw_records);
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
  const auto dot = record.find('.');
  if (dot == std::string::npos) {
    return record.size();
  }
  return dot + 1;
}

std::string gml_content(const std::string& record) {
  const auto offset = gml_content_offset(record);
  if (offset >= record.size()) {
    return {};
  }
  return trim_ascii(record.substr(offset));
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

void append_list_item(std::string& output, std::string text) {
  text = gml_content(std::move(text));
  if (text.empty()) {
    return;
  }
  if (!output.empty() && output.back() != '\n') {
    output.push_back('\n');
  }
  output += "- " + text + "\n";
}

std::string render_link_markdown(const std::string& record) {
  auto text = gml_content(record);
  const auto target = gml_attr(record, "refid");
  if (text.empty()) {
    text = target;
  }
  if (target.empty()) {
    return text;
  }
  return "[" + text + "](#" + target + ")";
}

std::string render_anchor_markdown(const std::string& record) {
  const auto text = gml_content(record);
  if (!text.empty()) {
    return text;
  }
  const auto id = gml_attr(record, "id");
  if (id.empty()) {
    return {};
  }
  return "<a id=\"" + id + "\"></a>";
}

} // namespace

std::string render_markdown_records(const std::vector<std::string>& records) {
  std::string output;
  bool in_list = false;

  for (const auto& record : records) {
    const auto tag = gml_tag(record);
    if (tag.empty()) {
      append_block(output, collapse_ascii_whitespace(record));
      in_list = false;
      continue;
    }

    if (tag == "ul" || tag == "ol" || tag == "dl" || tag == "toc") {
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
      append_list_item(output, record);
      in_list = true;
      continue;
    }

    if (in_list && !output.empty() && output.back() == '\n') {
      output.push_back('\n');
    }
    in_list = false;

    if (is_heading_tag(tag)) {
      append_block(output, heading_prefix(tag) + gml_content(record));
    } else if (tag == "p" || tag == "tipage" || tag == "lblbox") {
      append_block(output, gml_content(record));
    } else if (tag == "hdref") {
      append_block(output, render_link_markdown(record));
    } else if (tag == "anchor") {
      append_block(output, render_anchor_markdown(record));
    } else if (tag == "fig") {
      const auto id = gml_attr(record, "id");
      append_block(output, id.empty() ? "[Figure]" : "[Figure: " + id + "]");
    } else if (tag == "table") {
      const auto id = gml_attr(record, "id");
      append_block(output, id.empty() ? "[Table]" : "[Table: " + id + "]");
    } else if (tag == "i1" || tag == "grpsep" || tag == "efig" ||
               tag == "etable" || tag == "fontdef") {
      continue;
    } else {
      append_block(output, gml_content(record));
    }
  }

  return trim_trailing_blank_lines(std::move(output));
}

} // namespace detail

} // namespace geist
