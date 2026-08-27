#include "geist/boo.hpp"
#include "test_failures.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/message_section_blocks_ir.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <tuple>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "message_section_blocks_ir_synthetic: " << message << '\n';
    geist_test::record_failure();
    return;
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

const geist::detail::MessageSectionBlockIR *
find_block(const geist::detail::MessageSectionBlocksIR &blocks,
           const geist::detail::MessageCatalogIR &catalog,
           const std::string &id) {
  for (const auto &block : blocks.blocks)
    if (block.entry_index < catalog.entries.size() &&
        catalog.entries[block.entry_index].id == id)
      return &block;
  return nullptr;
}

geist::detail::MessageSectionBlockIR *
find_block(geist::detail::MessageSectionBlocksIR &blocks,
           const geist::detail::MessageCatalogIR &catalog,
           const std::string &id) {
  for (auto &block : blocks.blocks)
    if (block.entry_index < catalog.entries.size() &&
        catalog.entries[block.entry_index].id == id)
      return &block;
  return nullptr;
}

} // namespace

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  geist::detail::LogicalDecodeContext context;
  open_context(root / "SC31-711.boo", context);
  const auto sources =
      geist::detail::decode_logical_record_sources(context, 172, 435);
  const auto layout = geist::detail::extract_layout_ir(sources);
  const auto ownership = geist::detail::build_ownership_ir(sources, layout);
  std::string error;
  const auto catalog = geist::detail::extract_message_catalog_ir(
      sources, layout, ownership, &error);
  require(catalog.has_value(),
          error.empty() ? "message catalog was rejected" : error.c_str());

  const auto blocks = geist::detail::extract_message_section_blocks_ir(
      layout, ownership, *catalog);
  require(geist::detail::verify_message_section_blocks_ir(
              layout, ownership, *catalog, blocks, &error),
          error.empty() ? "structured message block verification failed"
                        : error.c_str());

  const auto *message_807 = find_block(blocks, *catalog, "807");
  require(
      message_807 != nullptr &&
          std::holds_alternative<geist::detail::MessageStructuredTableBlockIR>(
              message_807->node),
      "MSG807 command table was not admitted");
  const auto &command_table =
      std::get<geist::detail::MessageStructuredTableBlockIR>(message_807->node);
  require(command_table.header.cells.size() == 2 &&
              command_table.header.cells[0].text == "Command type" &&
              command_table.header.cells[1].text == "Command" &&
              command_table.header.cells[0].column == 0 &&
              command_table.header.cells[1].column == 15 &&
              command_table.rows.size() == 25,
          "MSG807 command table schema changed");
  std::set<std::string> command_types;
  for (const auto &row : command_table.rows) {
    require(row.cells.size() == 2 && row.cells[0].column == 0 &&
                row.cells[1].column == 15 &&
                !row.cells[0].source_cells.empty() &&
                !row.cells[1].source_cells.empty(),
            "MSG807 table geometry or exact source provenance changed");
    require(std::all_of(row.cells[0].source_cells.begin(),
                        row.cells[0].source_cells.end(),
                        [](const auto &cell) {
                          return cell.role ==
                                 geist::detail::RowCellRole::content;
                        }),
            "MSG807 primary key claimed a structural marker");
    command_types.insert(row.cells[0].text);
  }
  require(command_types.size() == 25 && command_types.count("23006") == 1 &&
              command_types.count("103000") == 1,
          "MSG807 command keys are incomplete or ambiguous");
  // Cell text follows the semantic row projection: the marker spellings
  // `action`/`an` that message semantics carried into three rows are claimed
  // as structural cells, and the terminal `>` delimiter restored by message
  // semantics is kept.
  for (const auto &row : command_table.rows) {
    require(row.cells[0].text.find("action") == std::string::npos &&
                row.cells[1].text.find("action ") == std::string::npos &&
                row.cells[1].text.rfind("an ", 0) != 0,
            "MSG807 table cell leaked a structural marker spelling");
    if (row.cells[0].text == "31161")
      require(row.cells[1].text ==
                  "LAN CAU QUERY UNIT=<unit id> MOD=<module number> ATTR=LOBE",
              "MSG807 row 31161 lost its restored delimiter or continuation");
    if (row.cells[0].text == "31096")
      require(row.cells[1].text == "LAN CAU QUERY UNIT=<unit id> ATTR=WRAP",
              "MSG807 row 31096 text changed");
  }

  const auto *message_739 = find_block(blocks, *catalog, "739");
  require(
      message_739 != nullptr &&
          std::holds_alternative<geist::detail::MessageStructuredListBlockIR>(
              message_739->node),
      "MSG739 hanging checklist was not admitted");
  const auto &checklist =
      std::get<geist::detail::MessageStructuredListBlockIR>(message_739->node);
  require(
      checklist.items.size() == 3 &&
          checklist.lead_in.text ==
              "Verify that the following conditions are true:" &&
          checklist.items[0].text.find("lnmlnmemgr.pdf") != std::string::npos &&
          checklist.items[1].text.find("lnmeapp.cat") != std::string::npos &&
          checklist.items[2].text.find("lnmlnmemgr_dfi.cat") !=
              std::string::npos &&
          checklist.items[2].text.find("If everything") == std::string::npos,
      "MSG739 checklist boundaries or item attachment changed");
  for (const auto &item : checklist.items)
    require(!item.source_cells.empty() && !item.structural_cells.empty(),
            "MSG739 item did not conserve content and boundary cells");

  const auto *message_508 = find_block(blocks, *catalog, "508");
  require(message_508 != nullptr &&
              std::holds_alternative<
                  geist::detail::MessageStructuredPreformattedBlockIR>(
                  message_508->node),
          "MSG508 provenance gap did not select the explicit fallback");
  const auto &fallback =
      std::get<geist::detail::MessageStructuredPreformattedBlockIR>(
          message_508->node);
  require(!fallback.provenance_complete && !fallback.lines.empty() &&
              fallback.fallback_reason ==
                  "table candidate has an unpositioned source continuation",
          "MSG508 fallback did not retain its fail-closed reason");

  auto changed = blocks;
  auto *changed_807 = find_block(changed, *catalog, "807");
  require(changed_807 != nullptr, "MSG807 mutation fixture is absent");
  auto &changed_table =
      std::get<geist::detail::MessageStructuredTableBlockIR>(changed_807->node);
  ++changed_table.rows.front().cells.front().source_cells.front().word;
  require(!geist::detail::verify_message_section_blocks_ir(layout, ownership,
                                                           *catalog, changed),
          "structured verifier admitted a mutated source cell");

  changed = blocks;
  changed_807 = find_block(changed, *catalog, "807");
  auto &duplicated_table =
      std::get<geist::detail::MessageStructuredTableBlockIR>(changed_807->node);
  duplicated_table.rows.front().structural_cells.push_back(
      duplicated_table.rows.front().cells.front().source_cells.front());
  require(!geist::detail::verify_message_section_blocks_ir(layout, ownership,
                                                           *catalog, changed),
          "structured verifier admitted a multiply claimed source cell");

  auto shifted_ownership = ownership;
  const auto claimed =
      command_table.rows.front().cells.front().source_cells.front();
  const auto shifted =
      std::find_if(shifted_ownership.row_cells.begin(),
                   shifted_ownership.row_cells.end(), [&](const auto &cell) {
                     return cell.logical_record == claimed.logical_record &&
                            cell.token_index == claimed.token_index &&
                            cell.word_index == claimed.word_index;
                   });
  require(shifted != shifted_ownership.row_cells.end() &&
              shifted->display_column.has_value(),
          "positioned-column mutation fixture is absent");
  ++*shifted->display_column;
  require(!geist::detail::verify_message_section_blocks_ir(
              layout, shifted_ownership, *catalog, blocks),
          "structured verifier admitted shifted positioned-cell geometry");

  auto conflicted_ownership = ownership;
  conflicted_ownership.conflicts.push_back("synthetic conflict");
  require(!geist::detail::verify_message_section_blocks_ir(
              layout, conflicted_ownership, *catalog, blocks),
          "structured verifier admitted conflicted ownership");

  changed = blocks;
  changed_807 = find_block(changed, *catalog, "807");
  auto &rerouted_table =
      std::get<geist::detail::MessageStructuredTableBlockIR>(changed_807->node);
  ++rerouted_table.rows.front().cells.front().source_rows.front().second;
  require(!geist::detail::verify_message_section_blocks_ir(layout, ownership,
                                                           *catalog, changed),
          "structured verifier admitted rerouted row provenance");

  const auto trace = geist::detail::format_message_section_blocks_ir(blocks);
  require(trace.find("kind=table rows=25") != std::string::npos &&
              trace.find("kind=list items=3") != std::string::npos &&
              trace.find("kind=preformatted complete=no") != std::string::npos,
          "structured message trace omitted a canonical block kind");
  std::cout << "message section block IR checks passed\n";
}

