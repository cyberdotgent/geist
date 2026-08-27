#include "geist/detail/document_markdown_renderer.hpp"
#include "geist/detail/glossary_catalog_ir.hpp"
#include "geist/detail/glossary_document_lowering.hpp"
#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "glossary_document_lowering_synthetic: " << message << '\n';
    std::exit(1);
  }
}

void open_context(const std::filesystem::path &path,
                  geist::detail::LogicalDecodeContext &context) {
  context.bytes = geist::detail::read_file(path);
  const auto directory_page = geist::detail::read_be16(context.bytes, 0);
  const auto base =
      static_cast<std::size_t>(directory_page) * geist::boo_page_size;
  context.directory.page_number = directory_page;
  context.directory.token_threshold = context.bytes[base + 0x14];
  context.directory.token_map_offset =
      geist::detail::read_be16(context.bytes, base + 0x22);
  context.directory.dictionary_start_page =
      geist::detail::read_be16(context.bytes, base + 0x28);
  context.directory.dictionary_page_count =
      geist::detail::read_be16(context.bytes, base + 0x2e);
  context.directory.logical_record_count =
      geist::detail::read_be16(context.bytes, base + 0x36);
  context.directory.content_page_count =
      geist::detail::read_be16(context.bytes, base + 0x38);
  context.directory.content_start_page =
      geist::detail::read_be16(context.bytes, base + 0x3a);
  context.decoded_records = geist::detail::decode_experimental_logical_records(
      context.bytes, context.directory, &context.record_payload_ranges);
}

std::string visible_markdown(const std::string &markdown) {
  std::string visible;
  visible.reserve(markdown.size());
  for (std::size_t index = 0; index < markdown.size(); ++index) {
    if (markdown[index] == '\\' && index + 1 < markdown.size()) {
      visible.push_back(markdown[++index]);
    } else if (markdown.compare(index, 5, "&amp;") == 0) {
      visible.push_back('&');
      index += 4;
    } else {
      visible.push_back(markdown[index]);
    }
  }
  return geist::detail::collapse_ascii_whitespace(std::move(visible));
}

} // namespace

int main() {
  geist::detail::LogicalDecodeContext context;
  open_context(std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "SC31-711.boo",
               context);
  const auto sources =
      geist::detail::decode_logical_record_sources(context, 435, 518);
  const auto layout = geist::detail::extract_layout_ir(sources);
  const auto ownership = geist::detail::build_ownership_ir(sources, layout);
  std::string error;
  const auto catalog = geist::detail::extract_glossary_catalog_ir(
      sources, layout, ownership, &error);
  require(catalog.has_value(), error);

  geist::detail::TopicIdentityIR topic;
  topic.id = "GLOSSARY";
  topic.title = "Glossary";
  topic.topic_number = 37;
  topic.start_logical_record = 435;
  topic.end_logical_record = 518;
  const auto document = geist::detail::lower_glossary_catalog_to_document_ir(
      topic, *catalog, &error);
  require(document.has_value(), error);
  require(geist::detail::verify_glossary_catalog_document_ir(*catalog,
                                                             *document, &error),
          error);

  auto section_headings = std::size_t{0};
  auto definitions = std::size_t{0};
  auto anchors = std::size_t{0};
  auto tables = std::size_t{0};
  const geist::detail::BlockIR *dlci_definition = nullptr;
  const geist::detail::BlockIR *dlci_table = nullptr;
  auto after_dlci_anchor = false;
  for (const auto &block : document->blocks) {
    if (const auto *heading =
            std::get_if<geist::detail::HeadingBlockIR>(&block.node)) {
      if (heading->level == 2)
        ++section_headings;
    } else if (const auto *anchor =
                   std::get_if<geist::detail::AnchorBlockIR>(&block.node)) {
      ++anchors;
      after_dlci_anchor =
          anchor->id == "GLS data link connection identifier (DLCI)";
    } else if (std::holds_alternative<geist::detail::DefinitionListBlockIR>(
                   block.node)) {
      ++definitions;
      if (after_dlci_anchor)
        dlci_definition = &block;
      after_dlci_anchor = false;
    } else if (std::holds_alternative<geist::detail::TableBlockIR>(
                   block.node)) {
      ++tables;
      if (dlci_definition != nullptr && dlci_table == nullptr)
        dlci_table = &block;
    }
  }
  require(section_headings == 21 && definitions == 281 && anchors == 281,
          "source-ordered 21-section/281-entry structure changed");
  require(tables == 1 && dlci_definition != nullptr && dlci_table != nullptr,
          "DLCI table was omitted, duplicated, or detached from its entry");
  require(dlci_table->origin.rows.size() == 5,
          "DLCI table did not own its five physical source rows exactly once");
  for (const auto &row : dlci_table->origin.rows)
    require(std::none_of(dlci_definition->origin.rows.begin(),
                         dlci_definition->origin.rows.end(),
                         [&](const auto &prose_row) {
                           return prose_row.display_run == row.display_run &&
                                  prose_row.row_index == row.row_index;
                         }),
            "DLCI physical table row was duplicated in definition prose");

  const auto markdown = geist::detail::render_document_markdown(*document);
  const auto visible = visible_markdown(markdown);
  for (const auto &entry : catalog->entries) {
    require(visible.find(geist::detail::collapse_ascii_whitespace(
                entry.term)) != std::string::npos,
            "Markdown lost glossary term: " + entry.term);
    require(visible.find(entry.definition.prose) != std::string::npos,
            "Markdown lost glossary definition: " + entry.term);
  }
  for (const auto *expected :
       {"| DLCI Values | Function |", "| 1-15 | reserved |",
        "| 1023 | in-channel layer management |"})
    require(visible.find(expected) != std::string::npos,
            "Markdown lost typed DLCI table structure");
  require(visible.find("information interchange among data processing") !=
              std::string::npos,
          "lexical information carry was lost");
  require(visible.find("causes the graphical interface") != std::string::npos,
          "lexical article carry was lost");
  require(visible.find("processor. (T) (2) A buffer") != std::string::npos,
          "balanced closing punctuation carry was lost");
  require(visible.find("encoded, multiplexed, and transmitted") !=
              std::string::npos,
          "comma punctuation carry was lost");
  require(visible.find("keys on the keyboard") != std::string::npos &&
              visible.find("input speed and greater") != std::string::npos &&
              visible.find("UNIX operating system. The RISC System/6000") !=
                  std::string::npos &&
              visible.find("keys on the:") == std::string::npos &&
              visible.find("speed and:") == std::string::npos &&
              visible.find("operating system.:") == std::string::npos,
          "structural marker punctuation leaked through canonical lowering");
  require(
      visible.find("program can use the AIX NetView Service Point program to "
                   "communicate with the NetView and NETCENTER programs.") !=
          std::string::npos,
      "record-leading glossary continuation was not lowered");
  require(visible.find("Recommendation X.25..") == std::string::npos &&
              visible.find("perform applications..") == std::string::npos,
          "structural terminal delimiters duplicated visible punctuation");
  require(visible.find("a and mouse button") == std::string::npos &&
              visible.find("a adapter frame-relay") == std::string::npos,
          "fixed marker-code projections leaked into Markdown");
  require(visible.find("???????????") == std::string::npos,
          "ownership-classified layout padding leaked into Markdown");

  auto changed = *document;
  std::get<geist::detail::TextInlineIR>(
      std::get<geist::detail::DefinitionListBlockIR>(changed.blocks.back().node)
          .entries.front()
          .definition.front()
          .node)
      .text += " changed";
  require(!geist::detail::verify_glossary_catalog_document_ir(*catalog, changed,
                                                              &error),
          "glossary DocumentIR verifier admitted changed visible content");
  auto changed_catalog = *catalog;
  std::swap(changed_catalog.items[0], changed_catalog.items[1]);
  require(!geist::detail::lower_glossary_catalog_to_document_ir(
              topic, changed_catalog, &error),
          "glossary lowerer inferred order after source sequence mutation");
  changed_catalog = *catalog;
  changed_catalog.entries.front().definition.prose += " changed";
  require(!geist::detail::lower_glossary_catalog_to_document_ir(
              topic, changed_catalog, &error),
          "glossary lowerer admitted prose not derived from source rows");

  std::cout << "glossary document lowering synthetic checks passed\n";
  return 0;
}
