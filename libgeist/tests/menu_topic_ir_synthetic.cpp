#include "geist/detail/internal.hpp"
#include "geist/detail/book_topic_catalog_ir.hpp"
#include "geist/detail/menu_topic_ir.hpp"
#include "geist/document.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
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
  std::size_t structurally_complete = 0;
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file())
      continue;
    auto extension = entry.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char ch) { return std::tolower(ch); });
    if (extension != ".boo")
      continue;

    const auto document = BooDocument::open(entry.path());
    std::string catalog_error;
    const auto catalog = build_book_topic_catalog_ir(
        document.topics(), document.table_of_contents(), &catalog_error);
    require(catalog.has_value(), "book topic catalog extraction failed: " +
                                     catalog_error);
    require(verify_book_topic_catalog_ir(document.topics(),
                                         document.table_of_contents(),
                                         *catalog, &catalog_error),
            "book topic catalog verification failed: " + catalog_error);
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
      const auto menu = extract_source_menu_ir(sources);
      if (!menu)
        continue;
      const auto layout = extract_layout_ir(sources);
      const auto ownership = build_ownership_ir(sources, layout);
      // Census structure separately with deliberately unvalidated identity
      // evidence.  Such evidence must never be used for production lowering:
      // source alone cannot establish that a CMITEM operand names a real topic.
      MenuTargetValidationIR identity;
      for (const auto &item : menu->items)
        identity.items.push_back({item.target, item.text});
      if (!extract_menu_topic_ir(sources, identity, layout, ownership))
        continue;
      ++structurally_complete;

      const auto target_validation =
          validate_source_menu_targets(*menu, *catalog);
      if (!target_validation)
        continue;
      std::string error;
      const auto semantic =
          extract_menu_topic_ir(sources, *target_validation, layout, ownership,
                                &error);
      if (!semantic)
        continue;
      require(verify_menu_topic_ir(sources, *target_validation, layout,
                                   ownership, *semantic, &error),
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
      require(std::all_of(target_validation->items.begin(),
                          target_validation->items.end(), [](const auto &item) {
                            return !item.target.empty() && !item.label.empty();
                          }),
              "catalog validation lost target identity or label evidence");
      require(verify_source_menu_ir(sources, *menu, &error),
              "canonical source-only menu failed verification: " + error);
      auto mutated_menu = *menu;
      ++mutated_menu.items.front().target_cells.front().word;
      require(!verify_source_menu_ir(sources, mutated_menu),
              "source-only menu verifier admitted mutated target-cell evidence");
      mutated_menu = *menu;
      mutated_menu.items.front().text += "-changed";
      require(!verify_source_menu_ir(sources, mutated_menu),
              "source-only menu verifier admitted mutated label text");
      mutated_menu = *menu;
      mutated_menu.items.front().terminal_marker_token = 0;
      require(!verify_source_menu_ir(sources, mutated_menu),
              "source-only menu verifier admitted unproven marker repair");
      auto mutated_catalog = *catalog;
      BookTopicCatalogEntryIR *mutated_entry = nullptr;
      for (auto &candidate : mutated_catalog.topics)
        if (ascii_equals_case_insensitive(candidate.raw_topic_id,
                                          menu->items.front().target)) {
          mutated_entry = &candidate;
          break;
        }
      require(mutated_entry != nullptr,
              "validated target disappeared from mutated catalog");
      if (mutated_entry->topic_header)
        mutated_entry->topic_header->title += "-changed";
      else
        mutated_entry->toc_entries.back().title += "-changed";
      require(!validate_source_menu_targets(*menu, mutated_catalog),
              "target validation admitted a mismatched catalog label");
      auto mutated_validation = *target_validation;
      mutated_validation.items.front().label += "-changed";
      require(!extract_menu_topic_ir(sources, mutated_validation, layout,
                                     ownership),
              "menu topic extractor admitted mutated catalog evidence");
      admitted.push_back(entry.path().filename().string() + ':' + topic.id +
                         ':' + std::to_string(semantic->items.size()));

      auto mutated = *semantic;
      mutated.items.front().target.value += "-changed";
      require(!verify_menu_topic_ir(sources, *target_validation, layout,
                                    ownership, mutated),
              "menu topic verifier admitted a mutated raw target identity");
      mutated = *semantic;
      ++mutated.items.front().label_cells.front().word;
      require(!verify_menu_topic_ir(sources, *target_validation, layout,
                                    ownership, mutated),
              "menu topic verifier admitted mutated cell content");
      mutated = *semantic;
      ++mutated.title_source.byte_begin;
      require(!verify_menu_topic_ir(sources, *target_validation, layout,
                                    ownership, mutated),
              "menu topic verifier admitted mutated title provenance");
      mutated = *semantic;
      ++mutated.segments.front().source.token_begin;
      require(!verify_menu_topic_ir(sources, *target_validation, layout,
                                    ownership, mutated),
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
  std::string inventory;
  for (const auto &entry : admitted)
    inventory += "\n  " + entry;
  require(admitted == expected, "strict whole-topic menu admission inventory "
                                "changed; admitted:" + inventory);
  require(structurally_complete == 153,
          "raw structural menu envelope inventory changed: " +
              std::to_string(structurally_complete));
}

void catalog_contract() {
  std::vector<TopicInfo> topics{
      {"TOPIC", "Header title", "h2", 7, 10, 20}};
  std::vector<TocEntry> toc(1);
  toc[0].id = "topic";
  toc[0].title = "Contents title";
  toc[0].level = 3;
  toc[0].style = 4;
  toc[0].heading_level = "h2";
  toc[0].topic_number = 7;
  toc[0].start_logical_record = 10;
  toc[0].end_logical_record = 20;
  std::string error;
  const auto catalog = build_book_topic_catalog_ir(topics, toc, &error);
  require(catalog.has_value(), "synthetic catalog rejected: " + error);
  require(catalog->topics.size() == 1 &&
              catalog->topics.front().raw_topic_id == "TOPIC" &&
              catalog->topics.front().topic_header->topic_info_index == 0 &&
              catalog->topics.front().toc_entries.front().raw_id == "topic" &&
              catalog->topics.front().toc_entries.front().toc_index == 0,
          "catalog lost raw identity or boundary provenance");

  MenuIR raw_menu;
  raw_menu.items.push_back({});
  raw_menu.items.front().target = "ToPiC";
  raw_menu.items.front().text = "header TITLE";
  const auto validation = validate_source_menu_targets(raw_menu, *catalog);
  require(validation && validation->items.size() == 1 &&
              validation->items.front().existence ==
                  MenuTargetValidationEntryIR::ExistenceEvidence::
                      topic_header_and_toc &&
              validation->items.front().label_evidence ==
                  MenuTargetValidationEntryIR::LabelEvidence::topic_title,
          "catalog validation misclassified header and TOC evidence");
  raw_menu.items.front().text = "Contents title";
  require(!validate_source_menu_targets(raw_menu, *catalog),
          "TOC display title displaced available topic-header evidence");

  const auto toc_only = build_book_topic_catalog_ir({}, toc);
  require(toc_only.has_value(), "TOC-only catalog evidence was rejected");
  const auto toc_validation = validate_source_menu_targets(raw_menu, *toc_only);
  require(toc_validation &&
              toc_validation->items.front().existence ==
                  MenuTargetValidationEntryIR::ExistenceEvidence::toc_entry &&
              toc_validation->items.front().label_evidence ==
                  MenuTargetValidationEntryIR::LabelEvidence::toc_title,
          "TOC-only existence and label evidence was misclassified");

  auto mutated = *catalog;
  ++mutated.topics.front().topic_header->start_logical_record;
  require(!verify_book_topic_catalog_ir(topics, toc, mutated),
          "catalog verifier admitted mutated logical-record provenance");
  topics.push_back({"topic", "Duplicate", "h1", 8, 20, 30});
  require(!build_book_topic_catalog_ir(topics, toc),
          "catalog admitted a case-insensitive duplicate topic identity");
}

} // namespace

int main() {
  catalog_contract();
  inventory_complete_menu_topics();
  std::cout << "menu whole-topic inventory: 153 source-complete envelopes; "
               "6 catalog-validated without repair\n";
}
