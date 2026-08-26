#include "geist/detail/internal.hpp"
#include "geist/detail/menu_topic_ir.hpp"
#include "geist/document.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace geist;
using namespace geist::detail;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "menu_topic_ir_synthetic: " << message << '\n';
    std::exit(1);
  }
}

void load_context(const std::filesystem::path &path,
                  LogicalDecodeContext *context_ptr) {
  auto &context = *context_ptr;
  context.bytes = read_file(path);
  const auto directory_page = read_be16(context.bytes, 0);
  const auto base = static_cast<std::size_t>(directory_page) * boo_page_size;
  context.directory.page_number = directory_page;
  context.directory.token_threshold = context.bytes[base + 0x14];
  context.directory.token_map_offset = read_be16(context.bytes, base + 0x22);
  context.directory.dictionary_start_page =
      read_be16(context.bytes, base + 0x28);
  context.directory.dictionary_page_count =
      read_be16(context.bytes, base + 0x2e);
  context.directory.logical_record_count =
      read_be16(context.bytes, base + 0x36);
  context.directory.content_page_count = read_be16(context.bytes, base + 0x38);
  context.directory.content_start_page = read_be16(context.bytes, base + 0x3a);
  context.decoded_records = decode_experimental_logical_records(
      context.bytes, context.directory, &context.record_payload_ranges);
}

void inventory_complete_menu_topics() {
  const auto directory = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  std::vector<std::string> admitted;
  std::size_t terminal_repairs = 0;
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file())
      continue;
    auto extension = entry.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char ch) { return std::tolower(ch); });
    if (extension != ".boo")
      continue;

    const auto document = BooDocument::open(entry.path());
    std::map<std::string, std::string> titles;
    for (const auto &topic : document.topics())
      titles[topic.id] = topic.title;
    LogicalDecodeContext context;
    load_context(entry.path(), &context);
    for (const auto &topic : document.topics()) {
      const auto sources = decode_logical_record_sources(
          context, topic.start_logical_record, topic.end_logical_record);
      const auto has_menu =
          std::any_of(sources.begin(), sources.end(), [](const auto &record) {
            return std::any_of(
                record.control_segments.begin(), record.control_segments.end(),
                [](const auto &segment) {
                  return segment.kind == BookControlKind::menu_start;
                });
          });
      if (!has_menu)
        continue;
      const auto menu = extract_menu_ir(sources, titles);
      if (!menu)
        continue;
      const auto layout = extract_layout_ir(sources);
      const auto ownership = build_ownership_ir(sources, layout);
      std::string error;
      const auto semantic =
          extract_menu_topic_ir(sources, *menu, layout, ownership, &error);
      if (!semantic)
        continue;
      require(verify_menu_topic_ir(sources, *menu, layout, ownership, *semantic,
                                   &error),
              "canonical menu topic failed verification: " + error);
      require(std::all_of(semantic->items.begin(), semantic->items.end(),
                          [](const auto &item) {
                            return item.target.kind ==
                                       CrossReferenceTargetKindIR::topic &&
                                   !item.target.value.empty() &&
                                   !item.target_cells.empty() &&
                                   !item.label_cells.empty();
                          }),
              "menu target or exact visible-cell provenance was lost");
      auto mutated_menu = *menu;
      ++mutated_menu.items.front().target_cells.front().word;
      require(!verify_menu_ir(sources, titles, mutated_menu),
              "inner menu verifier admitted mutated target-cell evidence");
      admitted.push_back(entry.path().filename().string() + ':' + topic.id +
                         ':' + std::to_string(semantic->items.size()));
      terminal_repairs += static_cast<std::size_t>(std::count_if(
          menu->items.begin(), menu->items.end(), [](const auto &item) {
            return item.terminal_marker_token.has_value();
          }));

      auto mutated = *semantic;
      mutated.items.front().target.value += "-changed";
      require(!verify_menu_topic_ir(sources, *menu, layout, ownership, mutated),
              "menu topic verifier admitted a mutated raw target identity");
      mutated = *semantic;
      ++mutated.items.front().label_cells.front().word;
      require(!verify_menu_topic_ir(sources, *menu, layout, ownership, mutated),
              "menu topic verifier admitted mutated cell content");
      mutated = *semantic;
      ++mutated.title_source.byte_begin;
      require(!verify_menu_topic_ir(sources, *menu, layout, ownership, mutated),
              "menu topic verifier admitted mutated title provenance");
      mutated = *semantic;
      ++mutated.segments.front().source.token_begin;
      require(!verify_menu_topic_ir(sources, *menu, layout, ownership, mutated),
              "menu topic verifier admitted mutated envelope provenance");
    }
  }
  std::sort(admitted.begin(), admitted.end());
  auto expected = std::vector<std::string>{
      "FA1PLMM0.boo:5.6:1",      "SC33-033.boo:5.3:4",
      "SC34-425.boo:1.8.15.5:1", "SC34-425.boo:1.8.18.5:1",
      "SC34-425.boo:1.8.5.5:1",  "SH12-565.boo:APPENDIX1.9.5:3",
  };
  std::sort(expected.begin(), expected.end());
  require(admitted == expected,
          "strict whole-topic menu admission inventory changed");
  require(terminal_repairs == 0,
          "proven whole-menu topics unexpectedly require title-map-based "
          "terminal marker repair");
}

} // namespace

int main() {
  inventory_complete_menu_topics();
  std::cout << "menu whole-topic inventory: 6 accepted; all other CMENU "
               "topics fail closed\n";
}
