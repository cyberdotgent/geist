#include "geist/detail/book_topic_catalog_ir.hpp"
#include "test_failures.hpp"
#include "geist/detail/document_markdown_renderer.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/menu_document_lowering.hpp"
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
    geist_test::record_failure();
    return;
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

// SC31-711 4.1 "LNM OS/2 Agent Application Traps": its first CMITEM label is
// `Generic Traps >` (logical record 98, segment 4), where `>` is a width-1
// compact display token that BookServer does not show.  Source extraction
// records that token as compact_terminal without consulting any catalog;
// validation types it as a marker only because the remaining label cells
// agree with the target's catalog title.
void generic_traps_marker_contract(const MenuIR &menu,
                                   const BookTopicCatalogIR &catalog,
                                   const BooDocument &document) {
  require(menu.items.size() == 3 && menu.items.front().target == "4.1.1" &&
              menu.items.front().text == "Generic Traps >" &&
              menu.items.front().compact_terminal.has_value() &&
              menu.items.front().compact_terminal->display_cells == 1 &&
              menu.items.front().compact_terminal->label_cell_begin ==
                  menu.items.front().label_cells.size() - 1 &&
              !menu.items[1].compact_terminal && !menu.items[2].compact_terminal,
          "SC31-711 4.1 source-only compact terminal evidence changed");
  const auto &terminal = *menu.items.front().compact_terminal;
  require(menu.items.front().label_cells.back().token_index ==
                  terminal.token_index &&
              menu.items.front().label_cells.back().word == '>',
          "SC31-711 4.1 compact terminal token does not own the `>` cell");

  // The book's topic-header titles for 4.1.1-4.1.3 are the whole ST payload
  // (title plus introduction), so header-preferred validation still declines
  // this menu; the marker rule is not what blocks it.
  std::string error;
  require(!validate_source_menu_targets(menu, catalog, &error) &&
              error.find("beyond its compact terminal token: 4.1.1") !=
                  std::string::npos,
          "SC31-711 4.1 validation outcome changed: " + error);

  // Against TOC evidence alone the marker-stripped label agrees exactly.
  const auto toc_only =
      build_book_topic_catalog_ir({}, document.table_of_contents());
  require(toc_only.has_value(), "SC31-711 TOC-only catalog was rejected");
  const auto validation =
      validate_source_menu_targets(menu, *toc_only, &error);
  require(validation.has_value(),
          "SC31-711 4.1 `Generic Traps >` was not admitted by marker-stripped "
          "validation: " + error);
  if (!validation)
    return;
  require(validation->items.size() == 3 &&
              validation->items.front().label == "Generic Traps" &&
              validation->items.front().terminal_marker_token ==
                  terminal.token_index &&
              validation->items.front().label_evidence ==
                  MenuTargetValidationEntryIR::LabelEvidence::toc_title &&
              !validation->items[1].terminal_marker_token &&
              validation->items[1].label ==
                  "LNM OS/2 Agent Application-Generated Traps" &&
              !validation->items[2].terminal_marker_token,
          "SC31-711 4.1 marker-stripped validation evidence changed");

  // The marker is source evidence: without it the same label is rejected,
  // and it cannot excuse a difference beyond the terminal token.
  auto unproven = menu;
  unproven.items.front().compact_terminal.reset();
  require(!validate_source_menu_targets(unproven, *toc_only),
          "validation stripped a marker without source evidence");
  auto widened = menu;
  widened.items.front().compact_terminal->label_cell_begin -= 2;
  require(!validate_source_menu_targets(widened, *toc_only),
          "validation admitted a label differing beyond its terminal token");
}

void inventory_complete_menu_topics() {
  const auto directory = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  std::vector<std::string> admitted;
  std::vector<std::string> lowered;
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
    require(catalog.has_value(),
            "book topic catalog extraction failed: " + catalog_error);
    require(verify_book_topic_catalog_ir(document.topics(),
                                         document.table_of_contents(), *catalog,
                                         &catalog_error),
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
      if (entry.path().filename() == "SC31-711.boo" && topic.id == "4.1")
        generic_traps_marker_contract(*menu, *catalog, document);
      const auto layout = extract_layout_ir(sources);
      // An unverifiable ledger declines every family, exactly as the menu
      // extractor's own verification did.
      const auto ownership = build_verified_ownership_ir(sources, layout);
      if (!ownership)
        continue;
      // Census structure separately with deliberately unvalidated identity
      // evidence.  Such evidence must never be used for production lowering:
      // source alone cannot establish that a CMITEM operand names a real topic.
      MenuTargetValidationIR identity;
      for (const auto &item : menu->items)
        identity.items.push_back({item.target, item.text});
      if (!extract_menu_topic_ir(sources, identity, layout, *ownership))
        continue;
      ++structurally_complete;

      const auto target_validation =
          validate_source_menu_targets(*menu, *catalog);
      if (!target_validation)
        continue;
      std::string error;
      const auto semantic = extract_menu_topic_ir(sources, *target_validation,
                                                  layout, *ownership, &error);
      if (!semantic)
        continue;
      require(verify_menu_topic_ir(sources, *target_validation, layout,
                                   *ownership, *semantic, &error),
              "canonical menu topic failed verification: " + error);
      if (entry.path().filename() == "SC33-033.boo" && topic.id == "5.3") {
        require(semantic->title == "Data sets and file processing" &&
                    semantic->introductions.size() == 1 &&
                    semantic->introductions.front().text ==
                        "The PGF file names, file types, and record types used "
                        "(according to the subsystem environment) for symbol "
                        "sets, chart formats, and chart data are shown in the "
                        "following tables." &&
                    !semantic->introductions.front().cells.empty(),
                "SC33-033 menu title/intro column ownership changed");
      }
      if (entry.path().filename() == "SH12-565.boo" &&
          topic.id == "APPENDIX1.9.5") {
        require(semantic->title ==
                        "Events Issued by NetView FTP V2.2.1 MVS" &&
                    semantic->introductions.size() == 1 &&
                    semantic->introductions.front().text ==
                        "The following describes the events issued by NetView "
                        "FTP V2.2.1 MVS." &&
                    !semantic->introductions.front().cells.empty(),
                "SH12-565 menu title/intro column ownership changed");
      }

      TopicIdentityIR topic_identity;
      topic_identity.id = topic.id;
      topic_identity.title = topic.title;
      topic_identity.heading_level = topic.heading_level;
      topic_identity.topic_number = topic.topic_number;
      topic_identity.start_logical_record = topic.start_logical_record;
      topic_identity.end_logical_record = topic.end_logical_record;
      const auto document_ir =
          lower_menu_topic_to_document_ir(topic_identity, *semantic, &error);
      require(document_ir.has_value(),
              "canonical menu DocumentIR lowering failed: " + error);
      require(verify_menu_topic_document_ir(*semantic, *document_ir, &error),
              "canonical menu DocumentIR failed verification: " + error);
      require(document_ir->topic.heading_level == semantic->heading_level,
              "source-proven menu heading level was not authoritative");
      const auto heading_index = semantic->anchor ? 1U : 0U;
      const auto list_index = heading_index + 1U + semantic->introductions.size();
      require(document_ir->blocks.size() == list_index + 1,
              "menu DocumentIR did not preserve title/anchor/list shape");
      if (semantic->anchor) {
        require(std::get_if<AnchorBlockIR>(&document_ir->blocks[0].node) !=
                        nullptr &&
                    std::get_if<HeadingBlockIR>(&document_ir->blocks[1].node) !=
                        nullptr,
                "menu DocumentIR did not preserve anchor-before-title source "
                "order");
        auto reordered = *document_ir;
        std::swap(reordered.blocks[0], reordered.blocks[1]);
        require(!verify_menu_topic_document_ir(*semantic, reordered),
                "menu DocumentIR verifier admitted title-before-anchor order");
      } else {
        require(std::get_if<HeadingBlockIR>(&document_ir->blocks[0].node) !=
                    nullptr,
                "unanchored menu DocumentIR does not begin with its title");
      }
      for (std::size_t paragraph = 0;
           paragraph < semantic->introductions.size(); ++paragraph) {
        const auto *prose = std::get_if<ParagraphBlockIR>(
            &document_ir->blocks[heading_index + 1 + paragraph].node);
        require(prose != nullptr && prose->content.size() == 1 &&
                    std::get<TextInlineIR>(prose->content.front().node).text ==
                        semantic->introductions[paragraph].text,
                "menu introduction did not lower as an independent paragraph");
      }
      const auto *menu_block =
          std::get_if<MenuBlockIR>(&document_ir->blocks[list_index].node);
      require(menu_block != nullptr &&
                  menu_block->items.size() == semantic->items.size(),
              "menu DocumentIR did not lower to one typed menu block");
      for (std::size_t item_index = 0; item_index < menu_block->items.size();
           ++item_index) {
        const auto &item = menu_block->items[item_index];
        require(item.target.kind == CrossReferenceTargetKindIR::topic &&
                    item.target.value ==
                        semantic->items[item_index].target.value &&
                    item.label == semantic->items[item_index].label,
                "menu item label or raw topic target changed in lowering");
        const auto &source = semantic->items[item_index].source;
        const auto owns_item_slice = std::any_of(
            item.origin.slices.begin(), item.origin.slices.end(),
            [&](const auto &slice) {
              return slice.logical_record == source.logical_record &&
                     slice.token_begin == source.token_begin &&
                     slice.token_end == source.token_end &&
                     slice.byte_begin == source.byte_begin &&
                     slice.byte_end == source.byte_end;
            });
        const auto owns_cell_slice = std::any_of(
            item.origin.slices.begin(), item.origin.slices.end(),
            [&](const auto &slice) {
              return slice.token_end == slice.token_begin + 1 &&
                     slice.token_begin >= source.token_begin &&
                     slice.token_end <= source.token_end;
            });
        require(owns_item_slice && owns_cell_slice,
                "menu item lost item or exact cell provenance");
      }
      // `Subtopics:` and the `<id> ` label prefix are BookServer render-time
      // output; the typed block must not carry them as text.
      const auto formatted_ir = format_document_ir(*document_ir);
      require(formatted_ir.find("Subtopics") == std::string::npos,
              "menu DocumentIR materialized reader-generated text");
      const auto markdown = render_document_markdown(*document_ir);
      require(markdown.find("\n\nSubtopics:\n\n- [") != std::string::npos,
              "typed menu Markdown lost its Subtopics lead line");
      for (const auto &item : semantic->items) {
        std::string prefixed;
        for (const auto ch : item.target.value + ' ' + item.label) {
          if (std::string("\\`*_{}[]<>()#+-.!|~").find(ch) !=
              std::string::npos)
            prefixed.push_back('\\');
          prefixed.push_back(ch);
        }
        require(markdown.find("- [" + prefixed + "](<#" + item.target.value +
                              ">)") != std::string::npos,
                "typed menu Markdown lost the `<id> <title>` label form for " +
                    item.target.value);
      }
      auto mutated_document = *document_ir;
      auto *mutated_block =
          std::get_if<MenuBlockIR>(&mutated_document.blocks[list_index].node);
      mutated_block->items.front().target.value += "-changed";
      require(!verify_menu_topic_document_ir(*semantic, mutated_document),
              "menu DocumentIR verifier admitted a mutated target");
      mutated_document = *document_ir;
      mutated_block =
          std::get_if<MenuBlockIR>(&mutated_document.blocks[list_index].node);
      mutated_block->items.front().label += "-changed";
      require(!verify_menu_topic_document_ir(*semantic, mutated_document),
              "menu DocumentIR verifier admitted a mutated label");
      mutated_document = *document_ir;
      mutated_block =
          std::get_if<MenuBlockIR>(&mutated_document.blocks[list_index].node);
      ++mutated_block->items.front().origin.slices.front().byte_begin;
      require(!verify_menu_topic_document_ir(*semantic, mutated_document),
              "menu DocumentIR verifier admitted mutated cell provenance");
      mutated_document = *document_ir;
      mutated_block =
          std::get_if<MenuBlockIR>(&mutated_document.blocks[list_index].node);
      mutated_block->items.front().label =
          mutated_block->items.front().target.value + ' ' +
          mutated_block->items.front().label;
      require(!verify_menu_topic_document_ir(*semantic, mutated_document),
              "menu DocumentIR verifier admitted a label with a decoder-"
              "injected id prefix");
      lowered.push_back(entry.path().filename().string() + ':' + topic.id +
                        ':' + std::to_string(menu_block->items.size()));
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
                          target_validation->items.end(),
                          [](const auto &item) {
                            return !item.target.empty() && !item.label.empty();
                          }),
              "catalog validation lost target identity or label evidence");
      require(verify_source_menu_ir(sources, *menu, &error),
              "canonical source-only menu failed verification: " + error);
      auto mutated_menu = *menu;
      ++mutated_menu.items.front().target_cells.front().word;
      require(
          !verify_source_menu_ir(sources, mutated_menu),
          "source-only menu verifier admitted mutated target-cell evidence");
      mutated_menu = *menu;
      mutated_menu.items.front().text += "-changed";
      require(!verify_source_menu_ir(sources, mutated_menu),
              "source-only menu verifier admitted mutated label text");
      mutated_menu = *menu;
      mutated_menu.items.front().terminal_marker_token = 0;
      require(!verify_source_menu_ir(sources, mutated_menu),
              "source-only menu verifier admitted unproven marker repair");
      mutated_menu = *menu;
      mutated_menu.items.front().compact_terminal =
          MenuCompactTerminalTokenIR{};
      require(!verify_source_menu_ir(sources, mutated_menu),
              "source-only menu verifier admitted mutated compact terminal "
              "evidence");
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
                                     *ownership),
              "menu topic extractor admitted mutated catalog evidence");
      admitted.push_back(entry.path().filename().string() + ':' + topic.id +
                         ':' + std::to_string(semantic->items.size()));

      auto mutated = *semantic;
      mutated.items.front().target.value += "-changed";
      require(!verify_menu_topic_ir(sources, *target_validation, layout,
                                    *ownership, mutated),
              "menu topic verifier admitted a mutated raw target identity");
      mutated = *semantic;
      ++mutated.items.front().label_cells.front().word;
      require(!verify_menu_topic_ir(sources, *target_validation, layout,
                                    *ownership, mutated),
              "menu topic verifier admitted mutated cell content");
      mutated = *semantic;
      ++mutated.title_source.byte_begin;
      require(!verify_menu_topic_ir(sources, *target_validation, layout,
                                    *ownership, mutated),
              "menu topic verifier admitted mutated title provenance");
      if (!semantic->introductions.empty()) {
        mutated = *semantic;
        mutated.introductions.front().text += "-changed";
        require(!verify_menu_topic_ir(sources, *target_validation, layout,
                                      *ownership, mutated),
                "menu topic verifier admitted mutated introduction text");
        mutated = *semantic;
        ++mutated.introductions.front().cells.front().word;
        require(!verify_menu_topic_ir(sources, *target_validation, layout,
                                      *ownership, mutated),
                "menu topic verifier admitted mutated introduction cells");
        mutated = *semantic;
        mutated.introductions.push_back(mutated.introductions.front());
        require(!verify_menu_topic_ir(sources, *target_validation, layout,
                                      *ownership, mutated),
                "menu topic verifier admitted a second title/intro split");
        mutated = *semantic;
        mutated.title_cells.push_back(
            mutated.introductions.front().cells.front());
        require(!verify_menu_topic_ir(sources, *target_validation, layout,
                                      *ownership, mutated),
                "menu topic verifier admitted an intervening visible cell "
                "owned by both title and intro");
      }
      mutated = *semantic;
      ++mutated.segments.front().source.token_begin;
      require(!verify_menu_topic_ir(sources, *target_validation, layout,
                                    *ownership, mutated),
              "menu topic verifier admitted mutated envelope provenance");
    }
  }
  std::sort(admitted.begin(), admitted.end());
  std::sort(lowered.begin(), lowered.end());
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
                                "changed; admitted:" +
                                    inventory);
  require(lowered == expected,
          "catalog-validated menu lowering inventory changed");
  // 160 = the 153 envelopes admitted before ee31d26 plus seven topics whose
  // empty-operand "cforwardlevel." metadata control was previously typed as an
  // opaque structural segment: GG24-4302-00 6.7.1, PRG1SORT 1.2.1 and 1.3.1,
  // SC24-5520-00 5.10, 6.11 and C.1, and SC34-425 FRONT_3.2.1.
  require(structurally_complete == 160,
          "raw structural menu envelope inventory changed: " +
              std::to_string(structurally_complete));
}

void catalog_contract() {
  std::vector<TopicInfo> topics{{"TOPIC", "Header title", "h2", 7, 10, 20}};
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

  // A label carrying one compact terminal token is admitted only when the
  // raw item proves that token from source and the cells before it agree
  // with the canonical title.
  MenuIR marked;
  marked.items.push_back({});
  auto &item = marked.items.front();
  item.target = "TOPIC";
  item.text = "header TITLE >";
  const std::string visible = "header TITLE >";
  for (std::size_t index = 0; index < visible.size(); ++index) {
    MenuSourceCellIR cell;
    cell.logical_record = 10;
    cell.output_word_index = 20 + index;
    cell.token_index = index < 13 ? 5 : 6;
    cell.word_index = index < 13 ? index : 0;
    cell.word = static_cast<std::uint16_t>(visible[index]);
    item.label_cells.push_back(cell);
  }
  require(!validate_source_menu_targets(marked, *catalog),
          "validation stripped a terminal token without source evidence");
  MenuCompactTerminalTokenIR terminal;
  terminal.token_index = 6;
  terminal.display_cells = 1;
  terminal.label_cell_begin = 13;
  item.compact_terminal = terminal;
  const auto marked_validation = validate_source_menu_targets(marked, *catalog);
  require(marked_validation && marked_validation->items.size() == 1 &&
              marked_validation->items.front().label == "header TITLE" &&
              marked_validation->items.front().terminal_marker_token == 6 &&
              marked_validation->items.front().label_evidence ==
                  MenuTargetValidationEntryIR::LabelEvidence::topic_title,
          "source-proven compact terminal token was not typed as a marker");
  item.text = "header TITL >";
  item.label_cells.erase(item.label_cells.begin() + 11);
  item.compact_terminal->label_cell_begin = 12;
  require(!validate_source_menu_targets(marked, *catalog),
          "validation admitted a label differing beyond its terminal token");

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
