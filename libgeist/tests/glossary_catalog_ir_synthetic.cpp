#include "geist/detail/glossary_catalog_ir.hpp"
#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

void open_context(const std::filesystem::path& path,
                  geist::detail::LogicalDecodeContext& context) {
  context.bytes = geist::detail::read_file(path);
  const auto directory_page = geist::detail::read_be16(context.bytes, 0);
  const auto base = static_cast<std::size_t>(directory_page) *
                    geist::boo_page_size;
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

std::optional<geist::detail::GlossaryCatalogIR> extract(
    const geist::detail::LogicalDecodeContext& context, std::uint32_t first,
    std::uint32_t end, std::string* error = nullptr) {
  const auto sources =
      geist::detail::decode_logical_record_sources(context, first, end);
  const auto layout = geist::detail::extract_layout_ir(sources);
  const auto ownership = geist::detail::build_ownership_ir(sources, layout);
  return geist::detail::extract_glossary_catalog_ir(
      sources, layout, ownership, error);
}

} // namespace

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  geist::detail::LogicalDecodeContext context;
  open_context(root / "SC31-711.boo", context);
  const auto sources =
      geist::detail::decode_logical_record_sources(context, 435, 518);
  const auto layout = geist::detail::extract_layout_ir(sources);
  const auto ownership = geist::detail::build_ownership_ir(sources, layout);
  std::string error;
  const auto catalog = geist::detail::extract_glossary_catalog_ir(
      sources, layout, ownership, &error);
  require(catalog.has_value(),
          error.empty() ? "complete glossary did not enter catalog IR"
                        : error.c_str());
  require(catalog->first_logical_record == 435 &&
              catalog->end_logical_record == 518 &&
              catalog->heading_level == "GLOSSARY",
          "glossary whole-topic envelope is incorrect");
  require(catalog->entries.size() == 281,
          "glossary catalog did not conserve all 281 observed terms");
  require(catalog->sections.size() == 21 &&
              catalog->sections.front().label == "A" &&
              catalog->sections.back().label == "X",
          "glossary alphabet section boundaries are incomplete");
  require(catalog->introduction.title == "Glossary" &&
              catalog->introduction.sources.size() == 5 &&
              catalog->introduction.cross_references.size() == 6,
          "glossary catalog lost its typed introduction");

  const auto find_entry = [&](const std::string& term) {
    return std::find_if(catalog->entries.begin(), catalog->entries.end(),
                        [&](const auto& entry) { return entry.term == term; });
  };
  for (const auto* term : {"accelerator", "managed node",
                           "wildcard character", "X.25 interface",
                           "ASCII (American National Standard Code for "
                           "Information Interchange)",
                           "end-user interface (EUI)"}) {
    const auto entry = find_entry(term);
    require(entry != catalog->entries.end(),
            "glossary catalog lost a representative term");
    require(!entry->definition.rows.empty() &&
                !entry->definition.rows.front().cells.empty(),
            "glossary definition lost row/cell provenance");
  }
  const auto ascii = find_entry(
      "ASCII (American National Standard Code for Information Interchange)");
  require(ascii != catalog->entries.end() && ascii->source_suffix == " a",
          "glossary term boundary did not conserve trailing source carry");
  const auto dlci = std::find_if(
      catalog->entries.begin(), catalog->entries.end(), [](const auto& entry) {
        return entry.term.find("data link connection identifier") == 0;
      });
  require(dlci != catalog->entries.end() &&
              dlci->definition.structural_sources.size() == 4,
          "glossary embedded table/figure envelope was not conserved");
  require(dlci->definition.embedded_table.has_value(),
          "glossary DLCI table did not enter semantic embedded-object IR");
  const auto& table = *dlci->definition.embedded_table;
  require(table.controls.size() == 4 && table.physical_rows.size() == 5 &&
              table.header_rows == 1 && table.rows.size() == 7,
          "glossary DLCI table envelope or grid shape is incorrect");
  require(table.controls[0].kind ==
                  geist::detail::GlossaryEmbeddedControlKindIR::figure_start &&
              table.controls[0].identifier == "TBLUNIQ7" &&
              table.controls[1].kind ==
                  geist::detail::GlossaryEmbeddedControlKindIR::table_start &&
              table.controls[1].identifier == "TBLUNIQ7" &&
              table.controls[2].kind ==
                  geist::detail::GlossaryEmbeddedControlKindIR::table_end &&
              table.controls[3].kind ==
                  geist::detail::GlossaryEmbeddedControlKindIR::figure_end,
          "glossary DLCI table controls or identifiers are incorrect");
  const std::vector<std::vector<std::string>> expected_table = {
      {"DLCI Values", "Function"},
      {"0", "in-channel signaling"},
      {"1-15", "reserved"},
      {"16-991", "assigned using frame-relay connection procedures"},
      {"992-1007", "layer 2 management of frame-relay bearer service"},
      {"1008-1022", "reserved"},
      {"1023", "in-channel layer management"},
  };
  for (std::size_t row = 0; row < expected_table.size(); ++row) {
    require(table.rows[row].cells.size() == expected_table[row].size(),
            "glossary DLCI table column count is incorrect");
    for (std::size_t cell = 0; cell < expected_table[row].size(); ++cell)
      require(table.rows[row].cells[cell].text == expected_table[row][cell] &&
                  !table.rows[row].cells[cell].source_cells.empty(),
              "glossary DLCI semantic cell or provenance is incorrect");
  }
  require(table.rows[2].cells[0].source_cells.front().logical_record == 454 &&
              table.rows[2].cells[0].source_cells.front().token_index == 110 &&
              table.rows[2].cells[0].source_cells.back().token_index == 114,
          "glossary DLCI split 1-15 source carry was not conserved");
  require(std::any_of(catalog->entries.begin(), catalog->entries.end(),
                      [](const auto& entry) {
                        return !entry.source_suffix.empty();
                      }),
          "glossary term boundary evidence did not retain source carry");

  require(geist::detail::verify_glossary_catalog_ir(
              sources, layout, ownership, *catalog, &error),
          error.empty() ? "canonical glossary catalog failed verification"
                        : error.c_str());
  auto changed = *catalog;
  changed.entries.front().definition.rows.front().visible_text += " changed";
  require(!geist::detail::verify_glossary_catalog_ir(
              sources, layout, ownership, changed),
          "glossary verifier admitted changed definition content");
  changed = *catalog;
  changed.entries.front().definition.rows.front().cells.pop_back();
  require(!geist::detail::verify_glossary_catalog_ir(
              sources, layout, ownership, changed),
          "glossary verifier admitted missing owned source cells");
  changed = *catalog;
  changed.entries.front().definition.rows.front().cells.front().run += 1;
  require(!geist::detail::verify_glossary_catalog_ir(
              sources, layout, ownership, changed),
          "glossary verifier admitted changed cell run provenance");
  changed = *catalog;
  const auto changed_dlci = std::find_if(
      changed.entries.begin(), changed.entries.end(), [](const auto& entry) {
        return entry.term == "data link connection identifier (DLCI)";
      });
  require(changed_dlci != changed.entries.end() &&
              changed_dlci->definition.embedded_table.has_value(),
          "glossary embedded table mutation fixture is absent");
  changed_dlci->definition.embedded_table->rows[2].cells[0].text = "1-16";
  require(!geist::detail::verify_glossary_catalog_ir(
              sources, layout, ownership, changed),
          "glossary verifier admitted changed embedded semantic cell");
  changed = *catalog;
  const auto changed_table_entry = std::find_if(
      changed.entries.begin(), changed.entries.end(), [](const auto& entry) {
        return entry.term == "data link connection identifier (DLCI)";
      });
  changed_table_entry->definition.embedded_table->physical_rows.pop_back();
  require(!geist::detail::verify_glossary_catalog_ir(
              sources, layout, ownership, changed),
          "glossary verifier admitted a missing embedded physical row");
  changed = *catalog;
  require(changed.entries.front().definition.rows.front().marker.has_value(),
          "glossary marker mutation fixture has no marker");
  changed.entries.front()
      .definition.rows.front()
      .marker->encoded_value += 1;
  require(!geist::detail::verify_glossary_catalog_ir(
              sources, layout, ownership, changed),
          "glossary verifier admitted changed marker encoding provenance");
  changed = *catalog;
  changed.segments.pop_back();
  require(!geist::detail::verify_glossary_catalog_ir(
              sources, layout, ownership, changed),
          "glossary verifier admitted an incomplete segment ledger");
  changed = *catalog;
  changed.introduction.lead.source_rows.clear();
  require(!geist::detail::verify_glossary_catalog_ir(
              sources, layout, ownership, changed),
          "glossary verifier admitted missing introduction provenance");

  require(!extract(context, 435, 438),
          "truncated glossary topic entered whole-topic IR");
  require(!extract(context, 435, 519),
          "glossary topic with trailing content entered whole-topic IR");
  require(!extract(context, 172, 435),
          "message catalog entered glossary catalog IR");

  std::cout << "glossary catalog IR synthetic tests passed\n";
  return 0;
}
