// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/boo.hpp"

#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
  std::cerr << "usage:\n"
            << "  boorsrc --list <book.boo>\n"
            << "  boorsrc -l <book.boo>\n"
            << "  boorsrc --extract <book.boo> <asset-id> [output-file]\n"
            << "  boorsrc -e <book.boo> <asset-id> [output-file]\n"
            << "  boorsrc --png <book.boo> <asset-id> [output-file]\n";
}

std::string fallback_extension(const geist::ResourceEntry& resource) {
  const auto& format = resource.stored_format;
  if (format == "image/gif") {
    return ".gif";
  }
  if (format == "image/jpeg" || format == "image/jpg") {
    return ".jpg";
  }
  if (format == "image/png") {
    return ".png";
  }
  if (format == "image/tiff" || format == "image/tif") {
    return ".tif";
  }
  if (format == "image/cgm") {
    return ".cgm";
  }
  if (format == "legacy-gdf") {
    return ".gdf";
  }
  if (format == "legacy-mmr") {
    return ".mmr";
  }
  if (format == "legacy-met") {
    return ".met";
  }
  return ".bin";
}

std::filesystem::path output_path_for(const geist::ResourceEntry& resource,
                                      const char* requested_path) {
  if (requested_path != nullptr) {
    return requested_path;
  }
  if (!resource.name.empty()) {
    return resource.name;
  }
  return resource.id + fallback_extension(resource);
}

std::filesystem::path png_output_path_for(const geist::ResourceEntry& resource,
                                          const char* requested_path) {
  if (requested_path != nullptr) {
    return requested_path;
  }
  if (!resource.name.empty()) {
    auto path = std::filesystem::path(resource.name);
    path.replace_extension(".png");
    return path;
  }
  return resource.id + ".png";
}

const geist::ResourceEntry* find_resource(
    const std::vector<geist::ResourceEntry>& resources,
    const std::string& id) {
  for (const auto& resource : resources) {
    if (resource.id.size() != id.size()) {
      continue;
    }
    bool matches = true;
    for (std::size_t i = 0; i < id.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(resource.id[i])) !=
          std::tolower(static_cast<unsigned char>(id[i]))) {
        matches = false;
        break;
      }
    }
    if (matches) {
      return &resource;
    }
  }
  return nullptr;
}

void list_resources(const geist::BooDocument& document) {
  const auto& resources = document.resources();
  if (resources.empty()) {
    std::cout << "No assets found.\n";
    return;
  }

  std::cout << "ID\tLayout\tKind\tFormat\tOffset\tSize\tDescription\n";
  for (const auto& resource : resources) {
    std::cout << resource.id << '\t'
              << geist::to_string(resource.layout) << '\t'
              << resource.kind << '\t'
              << resource.stored_format << '\t'
              << resource.offset << '\t'
              << resource.size << '\t'
              << resource.description << '\n';
  }
}

void extract_resource(const geist::BooDocument& document,
                      const std::string& id,
                      const char* requested_path) {
  const auto* resource = find_resource(document.resources(), id);
  if (resource == nullptr) {
    throw std::runtime_error("asset id was not found: " + id);
  }

  const auto output_path = output_path_for(*resource, requested_path);
  const auto bytes = document.read_resource_data(resource->id);

  std::ofstream output(output_path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to open output file: " +
                             output_path.string());
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("failed to write output file: " +
                             output_path.string());
  }

  std::cout << "Extracted " << resource->id << " to "
            << output_path.string() << " (" << bytes.size() << " bytes)\n";
}

void write_png_resource(const geist::BooDocument& document,
                        const std::string& id,
                        const char* requested_path) {
  const auto* resource = find_resource(document.resources(), id);
  if (resource == nullptr) {
    throw std::runtime_error("asset id was not found: " + id);
  }

  const auto output_path = png_output_path_for(*resource, requested_path);
  const auto bytes = document.read_resource_png(resource->id);

  std::ofstream output(output_path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to open output file: " +
                             output_path.string());
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("failed to write output file: " +
                             output_path.string());
  }

  std::cout << "Rendered " << resource->id << " to "
            << output_path.string() << " (" << bytes.size() << " bytes)\n";
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    print_usage();
    return 2;
  }

  const std::string mode(argv[1]);
  try {
    if (mode == "--list" || mode == "-l") {
      if (argc != 3) {
        print_usage();
        return 2;
      }
      const auto document = geist::BooDocument::open(argv[2]);
      list_resources(document);
      return 0;
    }

    if (mode == "--extract" || mode == "-e") {
      if (argc != 4 && argc != 5) {
        print_usage();
        return 2;
      }
      const auto document = geist::BooDocument::open(argv[2]);
      extract_resource(document, argv[3], argc == 5 ? argv[4] : nullptr);
      return 0;
    }

    if (mode == "--png") {
      if (argc != 4 && argc != 5) {
        print_usage();
        return 2;
      }
      const auto document = geist::BooDocument::open(argv[2]);
      write_png_resource(document, argv[3], argc == 5 ? argv[4] : nullptr);
      return 0;
    }

    print_usage();
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "boorsrc: " << error.what() << "\n";
    return 1;
  }
}
