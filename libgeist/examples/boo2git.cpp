#include "geist/boo.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
  std::filesystem::path input;
  std::filesystem::path output;
  bool force = false;
  bool verbose = false;
};

struct TopicOutput {
  const geist::TocEntry* entry = nullptr;
  std::string file;
};

void print_usage(std::ostream& output) {
  output << "usage: boo2git [options] <book.boo> <destination-folder>\n"
         << "\n"
         << "Render a BOO book as Git-hosted Markdown.\n"
         << "\n"
         << "Output:\n"
         << "  README.md             generated table of contents\n"
         << "  <topic>.md            one Markdown file per TOC topic\n"
         << "  <resource>.png        rendered PNG resources in the same folder\n"
         << "\n"
         << "Options:\n"
         << "  -f, --force           write into a non-empty destination without prompting\n"
         << "  -v, --verbose         log each written file\n"
         << "  -h, --help            show this help text\n";
}

Options parse_options(int argc, char** argv) {
  Options options;
  std::vector<std::string> positional;
  for (int index = 1; index < argc; ++index) {
    const std::string arg(argv[index]);
    if (arg == "-h" || arg == "--help") {
      print_usage(std::cout);
      std::exit(0);
    }
    if (arg == "-f" || arg == "--force") {
      options.force = true;
      continue;
    }
    if (arg == "-v" || arg == "--verbose") {
      options.verbose = true;
      continue;
    }
    if (!arg.empty() && arg.front() == '-') {
      throw std::runtime_error("unknown option: " + arg);
    }
    positional.push_back(arg);
  }

  if (positional.size() != 2) {
    throw std::runtime_error("expected <book.boo> and <destination-folder>");
  }
  options.input = positional[0];
  options.output = positional[1];
  return options;
}

std::string lowercase(std::string value) {
  for (auto& ch : value) {
    ch = static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

bool equals_case_insensitive(const std::string& left,
                             const std::string& right) {
  return lowercase(left) == lowercase(right);
}

std::string sanitize_stem(std::string value, const std::string& fallback) {
  if (value.empty()) {
    value = fallback;
  }
  for (auto& ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte) != 0) {
      ch = static_cast<char>(std::tolower(byte));
    } else if (ch == '-' || ch == '_') {
      ch = ch;
    } else {
      ch = '-';
    }
  }

  std::string compact;
  bool last_dash = false;
  for (const auto ch : value) {
    if (ch == '-') {
      if (!last_dash) {
        compact.push_back(ch);
      }
      last_dash = true;
    } else {
      compact.push_back(ch);
      last_dash = false;
    }
  }
  while (!compact.empty() && compact.front() == '-') {
    compact.erase(compact.begin());
  }
  while (!compact.empty() && compact.back() == '-') {
    compact.pop_back();
  }
  if (compact.empty()) {
    return fallback;
  }
  return compact;
}

std::string markdown_escape_link_text(std::string value) {
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '[' || value[index] == ']') {
      value.insert(value.begin() + static_cast<std::ptrdiff_t>(index), '\\');
      ++index;
    }
  }
  return value;
}

std::string markdown_escape_url(std::string value) {
  for (auto& ch : value) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  return value;
}

void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& bytes) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to open output file: " + path.string());
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("failed to write output file: " + path.string());
  }
}

void write_text(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to open output file: " + path.string());
  }
  output << text;
  if (!output) {
    throw std::runtime_error("failed to write output file: " + path.string());
  }
}

bool directory_has_entries(const std::filesystem::path& path) {
  return std::filesystem::exists(path) &&
         std::filesystem::is_directory(path) &&
         std::filesystem::directory_iterator(path) !=
             std::filesystem::directory_iterator();
}

void confirm_output_directory(const std::filesystem::path& output,
                              bool force) {
  if (std::filesystem::exists(output) &&
      !std::filesystem::is_directory(output)) {
    throw std::runtime_error("destination exists but is not a folder: " +
                             output.string());
  }
  if (!directory_has_entries(output) || force) {
    return;
  }

  std::cerr << "boo2git: destination folder is not empty: "
            << output.string() << "\n"
            << "Continue and overwrite generated files? [y/N] ";
  std::string answer;
  std::getline(std::cin, answer);
  if (answer != "y" && answer != "Y" && answer != "yes" &&
      answer != "YES") {
    throw std::runtime_error("destination confirmation was not accepted");
  }
}

std::map<std::string, std::string> build_topic_file_map(
    const std::vector<geist::TocEntry>& toc) {
  std::map<std::string, std::string> files;
  std::map<std::string, int> used;
  used["readme"] = 1;
  for (const auto& entry : toc) {
    auto stem = sanitize_stem(entry.id, "topic");
    if (equals_case_insensitive(stem, "readme")) {
      stem = "readme-topic";
    }
    const auto base_stem = stem;
    auto& count = used[stem];
    ++count;
    if (count > 1) {
      stem = base_stem + "-" + std::to_string(count);
    }
    files[lowercase(entry.id)] = stem + ".md";
  }
  return files;
}

std::vector<TopicOutput> build_topic_outputs(
    const std::vector<geist::TocEntry>& toc,
    const std::map<std::string, std::string>& topic_files) {
  std::vector<TopicOutput> outputs;
  outputs.reserve(toc.size());
  for (const auto& entry : toc) {
    const auto found = topic_files.find(lowercase(entry.id));
    if (found != topic_files.end()) {
      outputs.push_back({&entry, found->second});
    }
  }
  return outputs;
}

std::string raw_attr(const std::string& record, const std::string& attr) {
  const auto pattern = attr + "='";
  const auto begin = record.find(pattern);
  if (begin == std::string::npos) {
    return {};
  }
  const auto value_begin = begin + pattern.size();
  const auto value_end = record.find('\'', value_begin);
  if (value_end == std::string::npos || value_end <= value_begin) {
    return {};
  }
  return record.substr(value_begin, value_end - value_begin);
}

std::string picture_resource_id(const std::string& target) {
  if (target.size() <= 3 || lowercase(target.substr(0, 3)) != "pic") {
    return {};
  }
  for (std::size_t index = 3; index < target.size(); ++index) {
    if (std::isdigit(static_cast<unsigned char>(target[index])) == 0) {
      return {};
    }
  }
  return target.substr(3);
}

std::map<std::string, std::string> build_markdown_link_map(
    const std::vector<geist::TocEntry>& toc,
    const std::map<std::string, std::string>& topic_files) {
  auto links = topic_files;
  for (const auto& entry : toc) {
    const auto file = topic_files.find(lowercase(entry.id));
    if (file == topic_files.end()) {
      continue;
    }
    std::string pending_figure_id;
    for (const auto& record : entry.raw_records) {
      if (record.rfind(":anchor ", 0) == 0) {
        const auto id = raw_attr(record, "id");
        if (!id.empty()) {
          links[lowercase(id)] = file->second;
        }
        continue;
      }

      if (record.rfind(":fig ", 0) == 0) {
        pending_figure_id = raw_attr(record, "id");
        if (!pending_figure_id.empty()) {
          links[lowercase(pending_figure_id)] = file->second;
          links[lowercase("fig" + pending_figure_id)] = file->second;
        }
        continue;
      }

      if (record.rfind(":image ", 0) == 0) {
        const auto resource = raw_attr(record, "resource");
        if (!pending_figure_id.empty() && !resource.empty()) {
          const auto uri = "resource:" + resource;
          links[lowercase(pending_figure_id)] = uri;
          links[lowercase("fig" + pending_figure_id)] = uri;
          pending_figure_id.clear();
        }
        continue;
      }

      if (record.rfind(":hdref ", 0) == 0) {
        const auto resource = picture_resource_id(raw_attr(record, "refid"));
        if (!pending_figure_id.empty() && !resource.empty()) {
          const auto uri = "resource:" + resource;
          links[lowercase(pending_figure_id)] = uri;
          links[lowercase("fig" + pending_figure_id)] = uri;
          pending_figure_id.clear();
        }
      }
    }
  }
  return links;
}

std::map<std::string, std::string> extract_png_resources(
    const geist::BooDocument& document,
    const std::filesystem::path& output,
    bool verbose) {
  std::map<std::string, std::string> png_files;
  std::map<std::string, int> used;
  for (const auto& resource : document.resources()) {
    auto stem = sanitize_stem(resource.id, "resource");
    const auto base_stem = stem;
    auto& count = used[stem];
    ++count;
    if (count > 1) {
      stem = base_stem + "-" + std::to_string(count);
    }

    const auto filename = stem + ".png";
    try {
      const auto bytes = document.read_resource_png(resource.id);
      write_bytes(output / filename, bytes);
      png_files[lowercase(resource.id)] = filename;
      std::cerr << "boo2git: rendered resource " << resource.id << " -> "
                << filename << " (" << bytes.size() << " bytes)\n";
    } catch (const std::exception& error) {
      std::cerr << "boo2git: warning: resource " << resource.id
                << " could not be rendered as PNG: " << error.what()
                << "\n";
    }
    if (verbose && png_files.find(lowercase(resource.id)) != png_files.end()) {
      std::cerr << "boo2git: wrote " << (output / filename).string() << "\n";
    }
  }
  return png_files;
}

std::map<std::string, std::string> build_resource_link_map(
    const std::map<std::string, std::string>& png_files) {
  std::map<std::string, std::string> links;
  for (const auto& [id, filename] : png_files) {
    links[id] = filename;
    links["resource:" + id] = filename;
    links["pic" + id] = filename;
    links["picture" + id] = filename;
  }
  return links;
}

void replace_all(std::string& text,
                 const std::string& needle,
                 const std::string& replacement) {
  if (needle.empty()) {
    return;
  }
  std::size_t offset = 0;
  while ((offset = text.find(needle, offset)) != std::string::npos) {
    text.replace(offset, needle.size(), replacement);
    offset += replacement.size();
  }
}

void rewrite_resource_placeholders(
    std::string& markdown,
    const std::map<std::string, std::string>& png_files) {
  for (const auto* prefix : {"[Figure: ", "[Table: "}) {
    std::size_t offset = 0;
    while ((offset = markdown.find(prefix, offset)) != std::string::npos) {
      const auto id_begin = offset + std::string(prefix).size();
      const auto id_end = markdown.find(']', id_begin);
      if (id_end == std::string::npos) {
        break;
      }
      const auto id = markdown.substr(id_begin, id_end - id_begin);
      const auto found = png_files.find(lowercase(id));
      if (found == png_files.end()) {
        offset = id_end + 1;
        continue;
      }
      const std::string kind =
          std::string(prefix).find("Table") != std::string::npos
              ? "Table "
              : "Figure ";
      const auto replacement =
          "![" + kind + id + "](" + markdown_escape_url(found->second) + ")";
      markdown.replace(offset, (id_end + 1) - offset, replacement);
      offset += replacement.size();
    }
  }
}

void rewrite_topic_links(std::string& markdown,
                         const std::map<std::string, std::string>& links) {
  std::size_t offset = 0;
  while ((offset = markdown.find("](#", offset)) != std::string::npos) {
    const auto target_begin = offset + 3;
    const auto target_end = markdown.find(')', target_begin);
    if (target_end == std::string::npos) {
      break;
    }
    const auto target =
        markdown.substr(target_begin, target_end - target_begin);
    const auto found = links.find(lowercase(target));
    if (found == links.end()) {
      offset = target_end + 1;
      continue;
    }
    const auto replacement = "](" + markdown_escape_url(found->second) + ")";
    markdown.replace(offset, (target_end + 1) - offset, replacement);
    offset += replacement.size();
  }
}

void rewrite_resource_links(std::string& markdown,
                            const std::map<std::string, std::string>& links) {
  std::size_t offset = 0;
  while ((offset = markdown.find("](#", offset)) != std::string::npos) {
    const auto target_begin = offset + 3;
    const auto target_end = markdown.find(')', target_begin);
    if (target_end == std::string::npos) {
      break;
    }
    const auto target =
        markdown.substr(target_begin, target_end - target_begin);
    const auto found = links.find(lowercase(target));
    if (found == links.end()) {
      offset = target_end + 1;
      continue;
    }
    const auto replacement = "](" + markdown_escape_url(found->second) + ")";
    markdown.replace(offset, (target_end + 1) - offset, replacement);
    offset += replacement.size();
  }
}

void rewrite_resource_uris(std::string& markdown,
                           const std::map<std::string, std::string>& links) {
  std::size_t offset = 0;
  while ((offset = markdown.find("(resource:", offset)) != std::string::npos) {
    const auto target_begin = offset + 1;
    const auto target_end = markdown.find(')', target_begin);
    if (target_end == std::string::npos) {
      break;
    }
    const auto target =
        markdown.substr(target_begin, target_end - target_begin);
    const auto found = links.find(lowercase(target));
    if (found == links.end()) {
      offset = target_end + 1;
      continue;
    }
    const auto replacement = "(" + markdown_escape_url(found->second) + ")";
    markdown.replace(offset, (target_end + 1) - offset, replacement);
    offset += replacement.size();
  }
}

std::string render_navigation_link(const std::string& label,
                                   const std::string& file) {
  if (file.empty()) {
    return label;
  }
  return "[" + label + "](" + markdown_escape_url(file) + ")";
}

std::string render_navigation_bar(const TopicOutput* previous,
                                  const TopicOutput* next) {
  std::string output;
  output += render_navigation_link("Previous", previous == nullptr
                                                   ? std::string()
                                                   : previous->file);
  output += " | ";
  output += render_navigation_link("Index", "README.md");
  output += " | ";
  output += render_navigation_link("Next", next == nullptr ? std::string()
                                                           : next->file);
  return output;
}

std::string wrap_topic_navigation(std::string markdown,
                                  const TopicOutput* previous,
                                  const TopicOutput* next) {
  const auto navigation = render_navigation_bar(previous, next);
  if (markdown.empty() || markdown.back() != '\n') {
    markdown.push_back('\n');
  }
  return navigation + "\n\n---\n\n" + markdown + "\n---\n\n" + navigation +
         "\n";
}

std::string render_topic_markdown(
    const geist::TocEntry& entry,
    const std::map<std::string, std::string>& markdown_links,
    const std::map<std::string, std::string>& resource_links,
    const std::map<std::string, std::string>& png_files,
    const TopicOutput* previous,
    const TopicOutput* next) {
  auto markdown = entry.markdown();
  if (markdown.empty() || markdown.front() != '#') {
    markdown = "# " + entry.title + "\n\n" + markdown;
  }
  rewrite_topic_links(markdown, markdown_links);
  rewrite_resource_links(markdown, resource_links);
  rewrite_resource_uris(markdown, resource_links);
  rewrite_resource_placeholders(markdown, png_files);
  if (markdown.empty() || markdown.back() != '\n') {
    markdown.push_back('\n');
  }
  return wrap_topic_navigation(std::move(markdown), previous, next);
}

std::string render_index_markdown(
    const geist::BooDocument& document,
    const std::map<std::string, std::string>& topic_files) {
  const auto& props = document.book_properties();
  std::string output = "# ";
  output += props.title.empty() ? document.metadata().path.filename().string()
                                : props.title;
  output += "\n\n";
  if (!props.document_number.empty()) {
    output += "Document number: `" + props.document_number + "`\n\n";
  }
  output += "## Table of Contents\n\n";

  for (const auto& entry : document.table_of_contents()) {
    const auto found = topic_files.find(lowercase(entry.id));
    if (found == topic_files.end()) {
      continue;
    }
    output.append(static_cast<std::size_t>(entry.level) * 2, ' ');
    output += "- ";
    if (!entry.id.empty()) {
      output += "`" + entry.id + "` ";
    }
    output += "[" + markdown_escape_link_text(entry.title) + "](" +
              markdown_escape_url(found->second) + ")";
    output += "\n";
  }
  return output;
}

void render_book(const Options& options) {
  if (!std::filesystem::exists(options.input)) {
    throw std::runtime_error("input BOO file does not exist: " +
                             options.input.string());
  }

  confirm_output_directory(options.output, options.force);
  std::filesystem::create_directories(options.output);

  std::cerr << "boo2git: opening " << options.input.string() << "\n";
  const auto document = geist::BooDocument::open(options.input);
  const auto& toc = document.table_of_contents();
  std::cerr << "boo2git: found " << toc.size() << " TOC entries and "
            << document.resources().size() << " resources\n";

  const auto topic_files = build_topic_file_map(toc);
  const auto topic_outputs = build_topic_outputs(toc, topic_files);
  const auto markdown_links = build_markdown_link_map(toc, topic_files);
  const auto png_files =
      extract_png_resources(document, options.output, options.verbose);
  const auto resource_links = build_resource_link_map(png_files);

  const auto index_path = options.output / "README.md";
  write_text(index_path, render_index_markdown(document, topic_files));
  std::cerr << "boo2git: wrote " << index_path.string() << "\n";

  std::size_t topic_count = 0;
  for (std::size_t index = 0; index < topic_outputs.size(); ++index) {
    const auto& topic = topic_outputs[index];
    const auto* previous = index == 0 ? nullptr : &topic_outputs[index - 1];
    const auto* next =
        index + 1 == topic_outputs.size() ? nullptr : &topic_outputs[index + 1];
    const auto path = options.output / topic.file;
    write_text(path,
               render_topic_markdown(*topic.entry,
                                     markdown_links,
                                     resource_links,
                                     png_files,
                                     previous,
                                     next));
    ++topic_count;
    if (options.verbose) {
      std::cerr << "boo2git: wrote " << path.string() << "\n";
    }
  }

  std::cerr << "boo2git: rendered " << topic_count << " topics and "
            << png_files.size() << " PNG resources to "
            << options.output.string() << "\n";
}

} // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parse_options(argc, argv);
    render_book(options);
  } catch (const std::exception& error) {
    std::cerr << "boo2git: " << error.what() << "\n\n";
    print_usage(std::cerr);
    return 1;
  }
  return 0;
}
