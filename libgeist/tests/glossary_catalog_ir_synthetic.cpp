#include "geist/detail/glossary_catalog_ir.hpp"
#include "test_failures.hpp"
#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
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

std::optional<geist::detail::GlossaryCatalogIR>
extract(const geist::detail::LogicalDecodeContext &context, std::uint32_t first,
        std::uint32_t end, std::string *error = nullptr) {
  const auto sources =
      geist::detail::decode_logical_record_sources(context, first, end);
  const auto layout = geist::detail::extract_layout_ir(sources);
  std::string inner;
  const auto ownership =
      geist::detail::build_verified_ownership_ir(sources, layout, &inner);
  if (!ownership) {
    if (error != nullptr)
      *error = "source layout/ownership is not canonical: " + inner;
    return std::nullopt;
  }
  return geist::detail::extract_glossary_catalog_ir(sources, layout, *ownership,
                                                    error);
}

} // namespace

int main() {
  {
    geist::detail::DecodedLogicalRecordSource record;
    record.logical_record = 9;
    record.tokens = {{'x'},
                     {' ', ' ', ' '},
                     {3, 'W', 'h', 'a', 't', '?'},
                     {'?', '?', '?'},
                     {3, 'N', 'e', 'x', 't'}};
    record.assembled =
        geist::detail::assemble_logical_record_with_sources(record.tokens);
    geist::detail::PhysicalRowIR row;
    row.logical_record = 9;
    row.token_begin = 0;
    row.token_end = record.tokens.size();
    std::vector<geist::detail::GlossaryCatalogCellIR> cells;
    const auto add_token = [&](std::size_t token,
                               geist::detail::SourceDisposition disposition) {
      const auto first = record.tokens[token].front() < 4 ? 1u : 0u;
      for (auto word = first; word < record.tokens[token].size(); ++word)
        cells.push_back(
            {9, token, word, record.tokens[token][word], disposition, 1, 0});
    };
    add_token(0, geist::detail::SourceDisposition::marker_slot);
    add_token(1, geist::detail::SourceDisposition::layout_origin);
    add_token(2, geist::detail::SourceDisposition::visible_content);
    add_token(3, geist::detail::SourceDisposition::layout_padding);
    add_token(4, geist::detail::SourceDisposition::visible_content);
    std::string projection_error;
    const auto projected = geist::detail::project_glossary_semantic_row_text(
        record, row, cells, &projection_error);
    require(projected && *projected == "What? Next",
            "exact row projection removed a lexical question mark or retained "
            "question padding");

    auto relabeled = cells;
    for (auto &cell : relabeled)
      if (cell.token_index == 3)
        cell.disposition = geist::detail::SourceDisposition::visible_content;
    const auto padding_made_visible =
        geist::detail::project_glossary_semantic_row_text(record, row,
                                                          relabeled);
    require(padding_made_visible &&
                padding_made_visible->find("???") != std::string::npos,
            "row projection ignored exact padding-token disposition");

    relabeled = cells;
    const auto lexical_question =
        std::find_if(relabeled.begin(), relabeled.end(), [](const auto &cell) {
          return cell.token_index == 2 && cell.word == '?';
        });
    require(lexical_question != relabeled.end(),
            "synthetic lexical question cell is absent");
    lexical_question->disposition =
        geist::detail::SourceDisposition::layout_padding;
    require(!geist::detail::project_glossary_semantic_row_text(record, row,
                                                               relabeled),
            "row projection admitted mixed token dispositions");
  }

  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  geist::detail::LogicalDecodeContext context;
  open_context(root / "SC31-711.boo", context);
  const auto sources =
      geist::detail::decode_logical_record_sources(context, 435, 518);
  const auto layout = geist::detail::extract_layout_ir(sources);
  const auto ownership =
      geist::detail::build_verified_ownership_ir(sources, layout);
  require(ownership.has_value(), "glossary ownership ledger is not verifiable");
  std::string error;
  const auto catalog = geist::detail::extract_glossary_catalog_ir(
      sources, layout, *ownership, &error);
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
  require(catalog->items.size() == 302 &&
              catalog->items.front().kind ==
                  geist::detail::GlossaryCatalogItemKindIR::section &&
              catalog->items.front().index == 0 &&
              catalog->items[1].kind ==
                  geist::detail::GlossaryCatalogItemKindIR::entry &&
              catalog->items[1].index == 0,
          "glossary source-ordered section/entry sequence is incomplete");
  require(catalog->introduction.title == "Glossary" &&
              catalog->introduction.sources.size() == 5 &&
              catalog->introduction.cross_references.size() == 6,
          "glossary catalog lost its typed introduction");

  const auto find_entry = [&](const std::string &term) {
    return std::find_if(catalog->entries.begin(), catalog->entries.end(),
                        [&](const auto &entry) { return entry.term == term; });
  };
  for (const auto *term :
       {"accelerator", "managed node", "wildcard character", "X.25 interface",
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
  require(ascii != catalog->entries.end() &&
              ascii->definition.prose.find(
                  "for information interchange among data processing") !=
                  std::string::npos,
          "glossary definition lost a source-proven lexical marker carry");
  const auto accelerator = find_entry("accelerator");
  require(accelerator != catalog->entries.end() &&
              accelerator->definition.prose.find("a and mouse button") ==
                  std::string::npos &&
              accelerator->definition.rows[2].marker_disposition ==
                  geist::detail::GlossaryMarkerDispositionIR::layout_artifact,
          "glossary definition promoted a fixed marker code into prose");
  require(accelerator != catalog->entries.end() &&
              accelerator->definition.prose.find("keys on the keyboard") !=
                  std::string::npos &&
              accelerator->definition.prose.find("input speed and greater") !=
                  std::string::npos &&
              accelerator->definition.prose.find("keys on the:") ==
                  std::string::npos &&
              accelerator->definition.prose.find("speed and:") ==
                  std::string::npos,
          "ordinary origin-3 row controls became accelerator punctuation");
  const auto aix_operating = find_entry("AIX operating system");
  require(aix_operating != catalog->entries.end() &&
              aix_operating->definition.prose.find(
                  "UNIX operating system. The RISC System/6000") !=
                  std::string::npos &&
              aix_operating->definition.prose.find("system.:") ==
                  std::string::npos,
          "ordinary origin-3 row control followed terminal punctuation");
  const auto aix_system = find_entry("AIX SystemView NetView/6000");
  require(aix_system != catalog->entries.end(),
          "AIX SystemView NetView/6000 fixture is absent");
  require(aix_system->definition.prose.find(
              "program can use the AIX NetView Service Point program to "
              "communicate with the NetView and NETCENTER programs.") !=
              std::string::npos,
          "record-leading continuation prose was lost");
  const auto continuation = std::find_if(
      aix_system->definition.rows.begin(), aix_system->definition.rows.end(),
      [](const auto &row) { return row.continuation_prefix.has_value(); });
  require(continuation != aix_system->definition.rows.end() &&
              continuation->continuation_prefix->semantic_text ==
                  "program can use the AIX NetView Service Point program to "
                  "communicate" &&
              continuation->continuation_prefix->source.logical_record == 440 &&
              continuation->continuation_prefix->source.segment_index == 0 &&
              continuation->continuation_prefix->source.token_begin == 0 &&
              continuation->continuation_prefix->source.token_end == 13,
          "continuation prefix lost its exact source envelope");
  const auto source_440 =
      std::find_if(sources.begin(), sources.end(), [](const auto &source) {
        return source.logical_record == 440;
      });
  require(source_440 != sources.end(),
          "continuation source logical record is absent");
  auto expected_prefix_cells = std::size_t{};
  for (auto token = std::size_t{0}; token < 13; ++token)
    expected_prefix_cells += source_440->tokens[token].size();
  require(continuation->continuation_prefix->cells.size() ==
                  expected_prefix_cells &&
              std::all_of(continuation->continuation_prefix->cells.begin(),
                          continuation->continuation_prefix->cells.end(),
                          [](const auto &cell) {
                            return cell.token_index < 2
                                       ? cell.disposition ==
                                             geist::detail::SourceDisposition::
                                                 layout_padding
                                       : cell.disposition ==
                                             geist::detail::SourceDisposition::
                                                 visible_content;
                          }),
          "continuation prefix did not classify every source cell exactly");

  std::set<std::tuple<std::uint32_t, std::size_t, std::size_t>> claimed_cells;
  auto prefix_count = std::size_t{};
  auto terminal_delimiter_count = std::size_t{};
  for (const auto &entry : catalog->entries) {
    for (const auto &row : entry.definition.rows) {
      if (row.marker_disposition ==
          geist::detail::GlossaryMarkerDispositionIR::prose_punctuation)
        require(row.marker &&
                    (row.native_origin != 3 || row.marker->encoded_value >= 40),
                "structural marker geometry entered lexical punctuation");
      for (const auto &cell : row.cells)
        require(
            claimed_cells
                .emplace(cell.logical_record, cell.token_index, cell.word_index)
                .second,
            "glossary source cell was claimed by multiple definition rows");
      if (row.terminal_delimiter) {
        ++terminal_delimiter_count;
        require(row.terminal_delimiter->disposition ==
                    geist::detail::SourceDisposition::layout_padding,
                "terminal catalog delimiter remained visible prose");
      }
      if (!row.continuation_prefix)
        continue;
      ++prefix_count;
      for (const auto &cell : row.continuation_prefix->cells)
        require(
            claimed_cells
                .emplace(cell.logical_record, cell.token_index, cell.word_index)
                .second,
            "continuation prefix duplicated an owned glossary cell");
    }
  }
  require(prefix_count == 3, "glossary continuation-prefix inventory changed");
  require(terminal_delimiter_count == 19,
          "glossary terminal-delimiter inventory changed");
  const auto gateway = find_entry("gateway");
  const auto osi = find_entry("Open Systems Interconnection (OSI)");
  require(gateway != catalog->entries.end() &&
              gateway->definition.prose.find(
                  "with different network architectures") !=
                  std::string::npos &&
              osi != catalog->entries.end() &&
              osi->definition.prose.find(
                  "development of current and future standards for the "
                  "interconnection of computer systems") != std::string::npos,
          "additional record-leading glossary continuations were lost");
  const auto dlci = std::find_if(
      catalog->entries.begin(), catalog->entries.end(), [](const auto &entry) {
        return entry.term.find("data link connection identifier") == 0;
      });
  require(dlci != catalog->entries.end() &&
              dlci->definition.structural_sources.size() == 4,
          "glossary embedded table/figure envelope was not conserved");
  require(dlci->definition.embedded_table.has_value(),
          "glossary DLCI table did not enter semantic embedded-object IR");
  const auto &table = *dlci->definition.embedded_table;
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
  require(
      std::count_if(
          dlci->definition.rows.begin(), dlci->definition.rows.end(),
          [](const auto &row) {
            return row.role ==
                   geist::detail::GlossaryDefinitionRowRoleIR::embedded_table;
          }) == 5 &&
          dlci->definition.prose.find("DLCI Values") == std::string::npos,
      "glossary definition did not replace its five table rows exactly");
  require(std::any_of(
              catalog->entries.begin(), catalog->entries.end(),
              [](const auto &entry) { return !entry.source_suffix.empty(); }),
          "glossary term boundary evidence did not retain source carry");

  require(geist::detail::verify_glossary_catalog_ir(sources, layout, *ownership,
                                                    *catalog, &error),
          error.empty() ? "canonical glossary catalog failed verification"
                        : error.c_str());
  auto changed = *catalog;
  changed.entries.front().definition.rows.front().visible_text += " changed";
  require(!geist::detail::verify_glossary_catalog_ir(sources, layout, *ownership,
                                                     changed),
          "glossary verifier admitted changed definition content");
  changed = *catalog;
  changed.entries.front().definition.rows.front().cells.pop_back();
  require(!geist::detail::verify_glossary_catalog_ir(sources, layout, *ownership,
                                                     changed),
          "glossary verifier admitted missing owned source cells");
  changed = *catalog;
  changed.entries.front().definition.rows.front().cells.front().run += 1;
  require(!geist::detail::verify_glossary_catalog_ir(sources, layout, *ownership,
                                                     changed),
          "glossary verifier admitted changed cell run provenance");
  changed = *catalog;
  const auto changed_dlci = std::find_if(
      changed.entries.begin(), changed.entries.end(), [](const auto &entry) {
        return entry.term == "data link connection identifier (DLCI)";
      });
  require(changed_dlci != changed.entries.end() &&
              changed_dlci->definition.embedded_table.has_value(),
          "glossary embedded table mutation fixture is absent");
  changed_dlci->definition.embedded_table->rows[2].cells[0].text = "1-16";
  require(!geist::detail::verify_glossary_catalog_ir(sources, layout, *ownership,
                                                     changed),
          "glossary verifier admitted changed embedded semantic cell");
  changed = *catalog;
  const auto changed_table_entry = std::find_if(
      changed.entries.begin(), changed.entries.end(), [](const auto &entry) {
        return entry.term == "data link connection identifier (DLCI)";
      });
  changed_table_entry->definition.embedded_table->physical_rows.pop_back();
  require(!geist::detail::verify_glossary_catalog_ir(sources, layout, *ownership,
                                                     changed),
          "glossary verifier admitted a missing embedded physical row");
  changed = *catalog;
  require(changed.entries.front().definition.rows.front().marker.has_value(),
          "glossary marker mutation fixture has no marker");
  changed.entries.front().definition.rows.front().marker->encoded_value += 1;
  require(!geist::detail::verify_glossary_catalog_ir(sources, layout, *ownership,
                                                     changed),
          "glossary verifier admitted changed marker encoding provenance");
  changed = *catalog;
  const auto changed_aix = std::find_if(
      changed.entries.begin(), changed.entries.end(), [](const auto &entry) {
        return entry.term == "AIX SystemView NetView/6000";
      });
  const auto changed_prefix = std::find_if(
      changed_aix->definition.rows.begin(), changed_aix->definition.rows.end(),
      [](const auto &row) { return row.continuation_prefix.has_value(); });
  changed_prefix->continuation_prefix->cells.pop_back();
  require(!geist::detail::verify_glossary_catalog_ir(sources, layout, *ownership,
                                                     changed),
          "glossary verifier admitted missing continuation-prefix cells");
  changed = *catalog;
  const auto changed_terminal = std::find_if(
      changed.entries.rbegin(), changed.entries.rend(), [](const auto &entry) {
        return std::any_of(entry.definition.rows.begin(),
                           entry.definition.rows.end(), [](const auto &row) {
                             return row.terminal_delimiter.has_value();
                           });
      });
  const auto changed_terminal_row = std::find_if(
      changed_terminal->definition.rows.begin(),
      changed_terminal->definition.rows.end(),
      [](const auto &row) { return row.terminal_delimiter.has_value(); });
  changed_terminal_row->terminal_delimiter->disposition =
      geist::detail::SourceDisposition::visible_content;
  require(!geist::detail::verify_glossary_catalog_ir(sources, layout, *ownership,
                                                     changed),
          "glossary verifier admitted a visible terminal delimiter");
  changed = *catalog;
  changed.segments.pop_back();
  require(!geist::detail::verify_glossary_catalog_ir(sources, layout, *ownership,
                                                     changed),
          "glossary verifier admitted an incomplete segment ledger");
  changed = *catalog;
  std::swap(changed.items[0], changed.items[1]);
  require(!geist::detail::verify_glossary_catalog_ir(sources, layout, *ownership,
                                                     changed),
          "glossary verifier admitted a reordered source item sequence");
  changed = *catalog;
  changed.entries.front().definition.prose += " changed";
  require(!geist::detail::verify_glossary_catalog_ir(sources, layout, *ownership,
                                                     changed),
          "glossary verifier admitted changed composed definition prose");
  changed = *catalog;
  changed.introduction.lead.source_rows.clear();
  require(!geist::detail::verify_glossary_catalog_ir(sources, layout, *ownership,
                                                     changed),
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
