// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

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
         << "  render-diagnostics.tsv  per-topic render route, severity and reason\n"
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

// Every `<a id="...">` a topic's Markdown really emits, in order.
std::vector<std::string> emitted_anchor_ids(const std::string& markdown) {
  const std::string opening = "<a id=\"";
  std::vector<std::string> ids;
  std::size_t offset = 0;
  while ((offset = markdown.find(opening, offset)) != std::string::npos) {
    const auto begin = offset + opening.size();
    const auto end = markdown.find('"', begin);
    if (end == std::string::npos) {
      break;
    }
    ids.push_back(markdown.substr(begin, end - begin));
    offset = end + 1;
  }
  return ids;
}

// A figure or table object id is spelled without the prefix its source
// control carries: the XWEBDEMO record 11 control is `SRFIGMONET1`, so its
// reference id is `MONET1` and a cross reference resolves to
// `1-4-1.md#MONET1`
// while both renderers write the anchor hosted BookServer writes,
// `<a name="FIGMONET1">` (DT 19970423182524).  A destination whose anchor its
// own file does not contain is repaired against the anchors that file really
// emits, and only when exactly one of them ends with the destination's id.
// A destination that already resolves is never touched.
void repair_anchor_destinations(
    const std::vector<geist::TocEntry>& toc,
    const std::map<std::string, std::string>& topic_files,
    std::map<std::string, std::string>& links) {
  std::map<std::string, std::vector<std::string>> file_anchors;
  for (const auto& entry : toc) {
    const auto file = topic_files.find(lowercase(entry.id));
    if (file == topic_files.end()) {
      continue;
    }
    file_anchors[file->second] = emitted_anchor_ids(entry.markdown());
  }
  for (auto& link : links) {
    const auto hash = link.second.find('#');
    if (hash == std::string::npos) {
      continue;
    }
    const auto file = link.second.substr(0, hash);
    const auto anchor = lowercase(link.second.substr(hash + 1));
    const auto anchors = file_anchors.find(file);
    if (anchors == file_anchors.end() || anchor.empty()) {
      continue;
    }
    const std::string* repair = nullptr;
    auto matches = std::size_t{0};
    for (const auto& id : anchors->second) {
      const auto candidate = lowercase(id);
      if (candidate == anchor) {
        matches = 0;
        break;
      }
      if (candidate.size() > anchor.size() &&
          candidate.compare(candidate.size() - anchor.size(), anchor.size(),
                            anchor) == 0) {
        repair = &id;
        ++matches;
      }
    }
    if (matches != 1) {
      continue;
    }
    link.second = file + "#" + *repair;
  }
}

// Each topic states what it names; the library decides whether that answer
// comes from the topic's typed Document IR or, for the topics that still
// render through it, from the legacy GML projection.  This loop only spells
// the answers as Markdown destinations.
std::map<std::string, std::string> build_markdown_link_map(
    const std::vector<geist::TocEntry>& toc,
    const std::map<std::string, std::string>& topic_files) {
  auto links = topic_files;
  for (const auto& entry : toc) {
    const auto file = topic_files.find(lowercase(entry.id));
    if (file == topic_files.end()) {
      continue;
    }
    for (const auto& target : entry.link_targets()) {
      if (target.id.empty()) {
        continue;
      }
      switch (target.kind) {
      case geist::LinkTargetKind::anchor:
        links[lowercase(target.id)] = file->second;
        break;
      case geist::LinkTargetKind::figure: {
        // A figure whose body is a stored object resolves to the object; one
        // drawn in the topic resolves to its anchor inside the file.  Source
        // spells a reference to it with and without the `FIG` prefix.
        //
        // The fragment is the anchor the file really emits, not the stripped
        // reference id: FA1PLMM0 `5.1.1` writes `<a id="FIGVMSUM">` while a
        // reference to it spells `VMSUM`, so a destination built from the id
        // alone named a fragment no file carried.
        const auto uri = target.resource.empty()
                             ? file->second + "#" +
                                   (target.fragment.empty() ? target.id
                                                            : target.fragment)
                             : target.resource;
        links[lowercase(target.id)] = uri;
        links[lowercase("fig" + target.id)] = uri;
        if (!target.fragment.empty())
          links[lowercase(target.fragment)] = uri;
        break;
      }
      case geist::LinkTargetKind::table: {
        // Same for a table, and it is spelled both ways too: SC09-138
        // `8.5.7.1` references `SRVS` where the anchor is `TBLSRVS`, and
        // GX27-3999-00 `B.0` references `TBLMPROKEY` where `A.0` writes that
        // same anchor.  Registering only the stripped id left the prefixed
        // spelling unresolved and the export unlinked it.
        const auto uri =
            file->second + "#" +
            (target.fragment.empty() ? target.id : target.fragment);
        links[lowercase(target.id)] = uri;
        links[lowercase("tbl" + target.id)] = uri;
        if (!target.fragment.empty())
          links[lowercase(target.fragment)] = uri;
        break;
      }
      }
    }
  }
  repair_anchor_destinations(toc, topic_files, links);
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
  while ((offset = markdown.find("](", offset)) != std::string::npos) {
    const auto destination_begin = offset + 2;
    const auto angled = destination_begin < markdown.size() &&
                        markdown[destination_begin] == '<';
    const auto marker = destination_begin + static_cast<std::size_t>(angled);
    if (marker >= markdown.size() || (!angled && markdown[marker] != '#')) {
      offset = destination_begin;
      continue;
    }
    const auto target_begin =
        marker + static_cast<std::size_t>(markdown[marker] == '#');
    const auto target_end = markdown.find(angled ? '>' : ')', target_begin);
    if (target_end == std::string::npos ||
        (angled && (target_end + 1 >= markdown.size() ||
                    markdown[target_end + 1] != ')')))
      break;
    const auto target =
        markdown.substr(target_begin, target_end - target_begin);
    const auto found = links.find(lowercase(target));
    if (found == links.end()) {
      offset = target_end + 1 + static_cast<std::size_t>(angled);
      continue;
    }
    const auto replacement = "](" + markdown_escape_url(found->second) + ")";
    const auto destination_end = target_end + 1 + static_cast<std::size_t>(angled);
    markdown.replace(offset, destination_end - offset, replacement);
    offset += replacement.size();
  }
}

void rewrite_html_anchor_links(std::string& markdown,
                               const std::map<std::string, std::string>& links) {
  std::size_t offset = 0;
  while ((offset = markdown.find("<a href=\"#", offset)) != std::string::npos) {
    const auto target_begin = offset + std::string("<a href=\"#").size();
    const auto target_end = markdown.find('"', target_begin);
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
    const auto replacement =
        "<a href=\"" + markdown_escape_url(found->second) + "\"";
    markdown.replace(offset, (target_end + 1) - offset, replacement);
    offset += replacement.size();
  }
}

// A verbatim topic proves what its own source says: the `cselect` names an
// anchor, and the row is marked for it.  Only the whole-book export knows
// whether that anchor exists anywhere -- ten ids in four books are referenced
// and never defined (SC34-425 `appendix1.5.3` points at a topic `2.1.6.3`
// the book does not contain).  Inside a preformatted block a link resolving
// to nothing is worse than none, because the row reads as text either way,
// so the anchor markup comes back off and the row is left exactly as drawn.
//
// Runs after `rewrite_html_anchor_links`, so anything still spelled
// `href="#<id>"` is an id no *other* file defines.  Two spellings survive
// that deliberately: a destination of exactly `#`, which is the cross-book
// reference hosted serves and a single-book export cannot address; and an id
// this very file defines, which is a working same-page fragment.  The second
// is how a footnote resolves -- `document_link_targets` publishes no `FTN`
// destination book-wide, precisely because a footnote is reachable only from
// the page that prints it, so the link map cannot know it and the file must
// answer for it.
void unlink_unresolved_html_anchors(
    std::string& markdown, const std::vector<std::string>& local_anchors) {
  std::size_t offset = 0;
  while ((offset = markdown.find("<a href=\"#", offset)) != std::string::npos) {
    const auto target_begin = offset + std::string("<a href=\"#").size();
    const auto target_end = markdown.find('"', target_begin);
    if (target_end == std::string::npos) {
      break;
    }
    const auto target =
        markdown.substr(target_begin, target_end - target_begin);
    if (target.empty() ||
        std::find_if(local_anchors.begin(), local_anchors.end(),
                     [&](const std::string& id) {
                       return equals_case_insensitive(id, target);
                     }) != local_anchors.end()) {
      offset = target_end;
      continue;
    }
    const auto open_end = markdown.find('>', target_end);
    if (open_end == std::string::npos) {
      break;
    }
    const auto close = markdown.find("</a>", open_end);
    if (close == std::string::npos) {
      break;
    }
    markdown.erase(close, 4);
    markdown.erase(offset, (open_end + 1) - offset);
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

// A `resource:<id>` destination is written either bare, by the legacy
// renderer, or wrapped in angle brackets, by the Document IR renderer's
// `markdown_destination`.  Both spellings name the same book resource, so
// both are resolved to the extracted PNG; before this, every typed figure
// block kept an unresolvable `](<resource:1>)` destination.
void rewrite_resource_uris(std::string& markdown,
                           const std::map<std::string, std::string>& links) {
  const std::string scheme = "resource:";
  std::size_t offset = 0;
  while ((offset = markdown.find(scheme, offset)) != std::string::npos) {
    auto open = offset;
    auto angled = false;
    if (open >= 2 && markdown[open - 1] == '<' && markdown[open - 2] == '(') {
      angled = true;
      open -= 2;
    } else if (open >= 1 && markdown[open - 1] == '(') {
      open -= 1;
    } else {
      offset += scheme.size();
      continue;
    }
    const auto target_end = markdown.find(angled ? '>' : ')', offset);
    if (target_end == std::string::npos) {
      break;
    }
    if (angled && (target_end + 1 >= markdown.size() ||
                   markdown[target_end + 1] != ')')) {
      offset += scheme.size();
      continue;
    }
    const auto target = markdown.substr(offset, target_end - offset);
    const auto found = links.find(lowercase(target));
    if (found == links.end()) {
      offset += scheme.size();
      continue;
    }
    const auto destination_end =
        target_end + 1 + static_cast<std::size_t>(angled);
    const auto replacement = "(" + markdown_escape_url(found->second) + ")";
    markdown.replace(open, destination_end - open, replacement);
    offset = open + replacement.size();
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

bool has_leading_markdown_heading(const std::string& markdown) {
  auto offset = std::size_t{};
  // A topic that did not render cleanly opens with the one-line render
  // diagnostic comment; it is a marker, not content, so it must not stop the
  // heading from being found.
  while (offset < markdown.size() && markdown.compare(offset, 4, "<!--") == 0) {
    const auto comment_end = markdown.find("-->", offset + 4);
    if (comment_end == std::string::npos)
      return false;
    offset = comment_end + 3;
    while (offset < markdown.size() &&
           (markdown[offset] == '\r' || markdown[offset] == '\n'))
      ++offset;
  }
  while (offset < markdown.size() && markdown.compare(offset, 7, "<a id=\"") == 0) {
    const auto anchor_end = markdown.find("</a>", offset + 7);
    if (anchor_end == std::string::npos)
      return false;
    offset = anchor_end + 4;
    while (offset < markdown.size() &&
           (markdown[offset] == '\r' || markdown[offset] == '\n'))
      ++offset;
  }
  return offset < markdown.size() && markdown[offset] == '#';
}

std::string render_topic_markdown(
    const geist::TocEntry& entry,
    const std::map<std::string, std::string>& markdown_links,
    const std::map<std::string, std::string>& resource_links,
    const std::map<std::string, std::string>& png_files,
    const TopicOutput* previous,
    const TopicOutput* next) {
  auto markdown = entry.markdown();
  if (!has_leading_markdown_heading(markdown)) {
    markdown = "# " + entry.title + "\n\n" + markdown;
  }
  const auto local_anchors = emitted_anchor_ids(markdown);
  rewrite_topic_links(markdown, markdown_links);
  rewrite_html_anchor_links(markdown, markdown_links);
  unlink_unresolved_html_anchors(markdown, local_anchors);
  rewrite_resource_links(markdown, resource_links);
  rewrite_resource_uris(markdown, resource_links);
  rewrite_resource_placeholders(markdown, png_files);
  if (markdown.empty() || markdown.back() != '\n') {
    markdown.push_back('\n');
  }
  return wrap_topic_navigation(std::move(markdown), previous, next);
}

std::string tsv_field(std::string value) {
  std::string output;
  output.reserve(value.size());
  for (const auto ch : value) {
    switch (ch) {
    case '\t':
      output += "\\t";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\\':
      output += "\\\\";
      break;
    default:
      output.push_back(ch);
    }
  }
  return output;
}

// The machine-readable triage channel.  It lives beside the Markdown instead
// of inside it, so a book can be triaged as a whole without any topic file
// changing: the 94.5% of topics that render cleanly stay byte-identical to a
// pipeline with no diagnostics at all.  Topics that did *not* render cleanly
// additionally carry the one-line HTML comment the library emits, so a single
// file is self-describing too.
std::string render_diagnostics_manifest(
    const std::vector<TopicOutput>& topics,
    const std::vector<geist::RenderDiagnostic>& diagnostics) {
  std::string output =
      "file\tid\tseverity\troute\tfamily\treason\trecords\tdegraded\tdetail\n";
  std::map<std::string, std::size_t> counts;
  for (std::size_t index = 0; index < topics.size(); ++index) {
    const auto& diagnostic = diagnostics[index];
    const auto* severity = geist::to_string(diagnostic.severity);
    ++counts[severity];
    std::string degraded;
    std::string detail = diagnostic.detail;
    for (const auto& degradation : diagnostic.degradations) {
      if (!degraded.empty()) degraded += ",";
      degraded += degradation.reason;
      // A typed-degraded topic carries no whole-topic detail; the block's own
      // reason is what makes the row triageable.
      if (detail.empty()) detail = degradation.detail;
    }
    output += tsv_field(topics[index].file);
    output += "\t" + tsv_field(topics[index].entry->id);
    output += "\t";
    output += severity;
    output += "\t" + tsv_field(diagnostic.route);
    output += "\t" + tsv_field(diagnostic.family);
    output += "\t" + tsv_field(diagnostic.reason);
    output += "\t" + std::to_string(diagnostic.source.start_logical_record) +
              "-" + std::to_string(diagnostic.source.end_logical_record);
    output += "\t" + tsv_field(degraded);
    output += "\t" + tsv_field(detail);
    output += "\n";
  }
  output += "# summary";
  for (const auto& [severity, count] : counts) {
    output += "\t" + severity + "=" + std::to_string(count);
  }
  output += "\ttotal=" + std::to_string(topics.size()) + "\n";

  // Every degraded block in the book, grouped by reason code, so a consumer
  // can see at a glance which fallbacks are costing fidelity.
  std::map<std::string, std::size_t> blocks;
  for (const auto& diagnostic : diagnostics)
    for (const auto& degradation : diagnostic.degradations)
      ++blocks[degradation.reason];
  if (!blocks.empty()) {
    output += "# degraded-blocks";
    for (const auto& [reason, count] : blocks)
      output += "\t" + reason + "=" + std::to_string(count);
    output += "\n";
  }
  return output;
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

  std::vector<geist::RenderDiagnostic> diagnostics;
  diagnostics.reserve(topic_outputs.size());
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
    diagnostics.push_back(topic.entry->render_diagnostic());
    ++topic_count;
    if (options.verbose) {
      std::cerr << "boo2git: wrote " << path.string() << "\n";
    }
  }

  const auto manifest_path = options.output / "render-diagnostics.tsv";
  write_text(manifest_path,
             render_diagnostics_manifest(topic_outputs, diagnostics));
  std::cerr << "boo2git: wrote " << manifest_path.string() << "\n";

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
