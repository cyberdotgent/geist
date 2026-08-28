// Fixed-table block IR: box-drawn SRTBL tables recovered from the typed
// Layout/Ownership IR of real topics, checked against the hosted BookServer
// rendering (AnalysisNotes URL mapping, fetched 2026-08-28):
//   SC31-711 FRONT_1.1  headerless two-column trademark box
//   SC31-711 4.0        bold two-line header, CSELECT cells, record cut
//   GG24-4302-00 10.2   caption row, bold header, multi-line bullet cells
//   SC31-605 2.1        caption row, two-line header, 89 rows over 10 records
//   SC31-605 3.5        four-column event table with wrapped cells
// plus negatives (no SRTBL, CFONT-only directory grid, prose) and mutation
// rejection for both verifiers.

#include "geist/boo.hpp"
#include "geist/detail/document_ir.hpp"
#include "geist/detail/fixed_table_block_ir.hpp"
#include "geist/detail/fixed_table_document_lowering.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/layout_ir.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "geist/document.hpp"
#include "test_failures.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace geist::detail;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "fixed_table_block_ir_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

void open_context(const std::filesystem::path &path,
                  LogicalDecodeContext &context) {
  context.bytes = read_file(path);
  const auto directory_page = read_be16(context.bytes, 0);
  const auto base =
      static_cast<std::size_t>(directory_page) * geist::boo_page_size;
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

struct Book {
  std::unique_ptr<geist::BooDocument> document;
  LogicalDecodeContext context;
};

// LogicalDecodeContext owns a mutex, so books are held by pointer.
std::map<std::string, std::unique_ptr<Book>> books;

const Book &book(const std::string &name) {
  auto found = books.find(name);
  if (found == books.end()) {
    const auto path = std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / name;
    auto opened = std::make_unique<Book>();
    opened->document =
        std::make_unique<geist::BooDocument>(geist::BooDocument::open(path));
    open_context(path, opened->context);
    found = books.emplace(name, std::move(opened)).first;
  }
  return *found->second;
}

struct Topic {
  std::vector<DecodedLogicalRecordSource> sources;
  LayoutIR layout;
  OwnershipIR ownership;
  LayoutRowRangeIR range;
  FixedTableBlocksIR blocks;
};

Topic extract(const std::string &name, const std::string &topic_id) {
  const auto &opened = book(name);
  const auto *entry = opened.document->find_toc_entry(topic_id);
  require(entry != nullptr, name + " " + topic_id + " is not in the TOC");
  Topic topic;
  if (entry == nullptr)
    return topic;
  topic.sources = decode_logical_record_sources(
      opened.context, entry->start_logical_record, entry->end_logical_record);
  topic.layout = extract_layout_ir(topic.sources);
  topic.ownership = build_ownership_ir(topic.sources, topic.layout);
  topic.range = {0, count_layout_rows(topic.layout)};
  topic.blocks = extract_fixed_table_blocks_ir(topic.sources, topic.layout,
                                               topic.ownership, topic.range);
  std::string error;
  require(verify_fixed_table_blocks_ir(topic.sources, topic.layout,
                                       topic.ownership, topic.range,
                                       topic.blocks, &error),
          name + " " + topic_id + " verification failed: " + error);
  for (const auto &block : topic.blocks.blocks) {
    const auto lowered = lower_fixed_table_block_to_document_ir(block);
    require(verify_fixed_table_document_ir(block, lowered, &error),
            name + " " + topic_id + " lowering verification failed: " + error);
  }
  return topic;
}

std::vector<std::string> cell_lines(const FixedTableRowIR &row,
                                    std::size_t column) {
  std::vector<std::string> lines;
  if (column < row.cells.size())
    for (const auto &line : row.cells[column].lines)
      lines.push_back(line.text);
  return lines;
}

std::string cell_text(const FixedTableRowIR &row, std::size_t column) {
  std::string text;
  for (const auto &line : cell_lines(row, column)) {
    if (!text.empty())
      text += '\n';
    text += line;
  }
  return text;
}

const FixedTableRowIR *find_row(const FixedTableBlockIR &block,
                                const std::string &first_cell) {
  for (const auto &row : block.body)
    if (cell_text(row, 0) == first_cell)
      return &row;
  return nullptr;
}

const TableBlockIR *lowered_table(const std::vector<BlockIR> &blocks) {
  for (const auto &block : blocks)
    if (const auto *table = std::get_if<TableBlockIR>(&block.node))
      return table;
  return nullptr;
}

std::string inline_text(const InlineSequenceIR &content) {
  std::string text;
  for (const auto &in : content) {
    if (const auto *node = std::get_if<TextInlineIR>(&in.node))
      text += node->text;
    else if (std::holds_alternative<HardBreakInlineIR>(in.node))
      text += '\n';
  }
  return text;
}

void test_sc31_711_trademark_box() {
  const auto topic = extract("SC31-711.boo", "FRONT_1.1");
  require(topic.blocks.blocks.size() == 1 && topic.blocks.declined.empty(),
          "FRONT_1.1 must admit exactly one table");
  if (topic.blocks.blocks.size() != 1)
    return;
  const auto &block = topic.blocks.blocks.front();
  require(block.object_id == "TBLUNIQ1", "FRONT_1.1 object id");
  require(block.left_column == 3 && block.width == 74 &&
              block.separator_columns == std::vector<std::size_t>{40},
          "FRONT_1.1 box geometry (hosted: borders at columns 3, 40, 76)");
  require(!block.caption && block.header_rows == 0 && block.body.size() == 5,
          "FRONT_1.1 is a headerless five-row box");
  const std::vector<std::pair<std::string, std::string>> expected{
      {"IBM", "NetView"},
      {"AIX", "SystemView"},
      {"PS/2", "OS/2"},
      {"RISC System/6000", "RS/6000"},
      {"NETCENTER", "RT"},
  };
  for (std::size_t index = 0; index < expected.size() && index < block.body.size();
       ++index) {
    const auto &row = block.body[index];
    require(row.cells.size() == 2 && cell_text(row, 0) == expected[index].first &&
                cell_text(row, 1) == expected[index].second,
            "FRONT_1.1 row " + std::to_string(index) + " text: '" +
                cell_text(row, 0) + "' / '" + cell_text(row, 1) + "'");
  }
  // `RISC System/6000` spans a false Layout IR row boundary (`6000` was a
  // marker-slot candidate); the table rejoins it from the display geometry.
  const auto *risc = find_row(block, "RISC System/6000");
  require(risc != nullptr && risc->source_rows.size() == 3,
          "FRONT_1.1 RISC row must rejoin three physical rows");
  // The whole SRTBL envelope is claimed: rows 2..7 of the flattened layout.
  require(block.rows.begin == 2 && block.rows.end == 8,
          "FRONT_1.1 row span");

  const auto lowered = lower_fixed_table_block_to_document_ir(block);
  const auto *table = lowered_table(lowered);
  require(lowered.size() == 2 &&
              std::holds_alternative<AnchorBlockIR>(lowered.front().node) &&
              table != nullptr && table->header_rows == 0 &&
              table->rows.size() == 5,
          "FRONT_1.1 lowers to an anchor and a headerless table");
  if (table != nullptr && table->rows.size() == 5)
    require(inline_text(table->rows[3].cells[0].content) ==
                "RISC System/6000",
            "FRONT_1.1 lowered cell text");

  // Row-range API: the exact span admits, a disjoint span ignores, and a
  // partial span declines without admitting anything.
  const auto exact = extract_fixed_table_blocks_ir(
      topic.sources, topic.layout, topic.ownership, block.rows);
  require(exact.blocks.size() == 1 && exact.declined.empty(),
          "FRONT_1.1 exact row range admits the table");
  const auto disjoint = extract_fixed_table_blocks_ir(
      topic.sources, topic.layout, topic.ownership, {0, 2});
  require(disjoint.blocks.empty() && disjoint.declined.empty(),
          "FRONT_1.1 disjoint row range is silent");
  const auto partial = extract_fixed_table_blocks_ir(
      topic.sources, topic.layout, topic.ownership, {0, 5});
  require(partial.blocks.empty() && partial.declined.size() == 1 &&
              partial.declined.front().reason ==
                  "table envelope crosses the requested row range",
          "FRONT_1.1 partial row range declines");

  // Mutation rejection for the block verifier.
  std::string error;
  auto mutated = topic.blocks;
  mutated.blocks.front().body[0].cells[0].lines[0].text = "IBN";
  require(!verify_fixed_table_blocks_ir(topic.sources, topic.layout,
                                        topic.ownership, topic.range, mutated,
                                        &error),
          "mutated cell text must be rejected");
  mutated = topic.blocks;
  mutated.blocks.front().header_rows = 1;
  require(!verify_fixed_table_blocks_ir(topic.sources, topic.layout,
                                        topic.ownership, topic.range, mutated,
                                        &error),
          "mutated header count must be rejected");
  mutated = topic.blocks;
  mutated.blocks.front().body[1].structural_cells.pop_back();
  require(!verify_fixed_table_blocks_ir(topic.sources, topic.layout,
                                        topic.ownership, topic.range, mutated,
                                        &error),
          "dropped structural claim must be rejected");
  mutated = topic.blocks;
  mutated.blocks.front().body[2].cells[1].lines[0].source_cells[0].word = 'X';
  require(!verify_fixed_table_blocks_ir(topic.sources, topic.layout,
                                        topic.ownership, topic.range, mutated,
                                        &error),
          "altered source cell must be rejected");
  // Mutation rejection for the document lowering verifier.
  auto lowered_mutated = lowered;
  auto *mutated_table = std::get_if<TableBlockIR>(&lowered_mutated.back().node);
  if (mutated_table != nullptr) {
    std::get<TextInlineIR>(mutated_table->rows[0].cells[1].content[0].node)
        .text = "NetVieW";
    require(!verify_fixed_table_document_ir(block, lowered_mutated, &error),
            "mutated lowered text must be rejected");
    lowered_mutated = lowered;
    std::get_if<TableBlockIR>(&lowered_mutated.back().node)->header_rows = 1;
    require(!verify_fixed_table_document_ir(block, lowered_mutated, &error),
            "mutated lowered header count must be rejected");
    lowered_mutated = lowered;
    lowered_mutated.erase(lowered_mutated.begin());
    require(!verify_fixed_table_document_ir(block, lowered_mutated, &error),
            "dropped anchor block must be rejected");
  }
}

void test_sc31_711_trap_directory() {
  const auto topic = extract("SC31-711.boo", "4.0");
  require(topic.blocks.blocks.size() == 1 && topic.blocks.declined.empty(),
          "4.0 must admit exactly one table");
  if (topic.blocks.blocks.size() != 1)
    return;
  const auto &block = topic.blocks.blocks.front();
  require(block.object_id == "TBLUNIQ6" && block.width == 74 &&
              block.separator_columns == std::vector<std::size_t>{27},
          "4.0 box geometry (hosted: borders at columns 3, 27, 76)");
  // CFONT `5 3 2 9 11 2 29 5 2` and `5 6 2` highlight every word of the
  // two-line first row: it is the header.
  require(block.header_rows == 1 && block.body.size() == 5 && !block.caption,
          "4.0 has a bold two-line header and four body rows");
  if (block.body.size() != 5)
    return;
  require(cell_lines(block.body[0], 0) ==
                  std::vector<std::string>{"For information", "about:"} &&
              cell_lines(block.body[0], 1) == std::vector<std::string>{"Read:"},
          "4.0 header cells");
  require(cell_lines(block.body[1], 1) ==
              std::vector<std::string>{
                  "\"LNM OS/2 Agent Application Traps\" in", "topic 4.1"},
          "4.0 first body row wraps its CSELECT cell over two lines");
  require(cell_text(block.body[2], 0) == "SNMP token-ring traps" &&
              cell_text(block.body[2], 1) ==
                  "\"SNMP Token-Ring Traps\" in topic 4.2",
          "4.0 second body row");
  // Logical record 95 ends inside this row: the right border is implied and
  // record 96 opens with the next rule. The `3` of `topic 4.3` was dropped
  // by the Layout IR (its row payload was trimmed to a placeholder run) and
  // is claimed from the opaque ledger instead.
  const auto &bridge = block.body[3];
  require(cell_text(bridge, 0) == "SNMP bridge traps" &&
              cell_text(bridge, 1) == "\"SNMP Bridge Traps\" in topic 4.3",
          "4.0 record-cut row text");
  require(bridge.cells.size() == 2 && bridge.cells[1].lines.size() == 1 &&
              bridge.cells[1].lines[0].unpositioned_cells.size() == 1 &&
              bridge.cells[1].lines[0].unpositioned_cells[0].word == '3',
          "4.0 record-cut row claims the unpositioned `3` from the ledger");
  require(cell_text(block.body[4], 0) == "FDDI traps" &&
              cell_text(block.body[4], 1) ==
                  "\"FDDI SNMP Proxy Agent Traps\" in topic 4.4",
          "4.0 last body row");
  const auto lowered = lower_fixed_table_block_to_document_ir(block);
  const auto *table = lowered_table(lowered);
  require(table != nullptr && table->header_rows == 1 &&
              table->rows.size() == 5 &&
              inline_text(table->rows[0].cells[0].content) ==
                  "For information\nabout:",
          "4.0 lowered header keeps the display line break");
}

void test_gg24_dbctl_overview() {
  const auto topic = extract("GG24-4302-00.boo", "10.2");
  require(topic.blocks.blocks.size() == 1 && topic.blocks.declined.empty(),
          "10.2 must admit exactly one table");
  if (topic.blocks.blocks.size() != 1)
    return;
  const auto &block = topic.blocks.blocks.front();
  require(block.object_id == "DBCTL51" && block.width == 120 &&
              block.separator_columns == std::vector<std::size_t>{83, 103},
          "10.2 box geometry (hosted: borders at columns 3, 83, 103, 122)");
  require(block.caption && cell_text(*block.caption, 0) ==
                               "Table 15. DBCTL 5.1 Overview",
          "10.2 caption row spans the junction-free top rule");
  require(block.header_rows == 1 && block.body.size() == 31,
          "10.2 header and 31 body rows (hosted table 15)");
  if (block.body.size() != 31)
    return;
  require(cell_text(block.body[0], 0).empty() &&
              cell_text(block.body[0], 1) == "TM" &&
              cell_text(block.body[0], 2) == "DBCTL",
          "10.2 header cells");
  const auto *fast_path = find_row(block, "Fast Path enhancements\nVSO\nDEDB "
                                          "enhancements\nHigh speed "
                                          "reorganization\nEnhanced HSSP\nHSSP "
                                          "ASIC");
  require(fast_path != nullptr && cell_text(*fast_path, 1) == "**" &&
              cell_text(*fast_path, 2) == "*",
          "10.2 multi-line bullet row keeps its display lines and stars");
  require(cell_text(block.body[1], 0) == "Processing Cost Enhancements" &&
              cell_text(block.body[1], 1).empty(),
          "10.2 section row has empty star cells");
  require(cell_text(block.body[30], 0) == "Dropped LU6.1 adapter support" &&
              cell_text(block.body[30], 1) == "*" &&
              cell_text(block.body[30], 2).empty(),
          "10.2 last row");
  const auto lowered = lower_fixed_table_block_to_document_ir(block);
  require(lowered.size() == 3 &&
              std::holds_alternative<ParagraphBlockIR>(lowered[1].node) &&
              inline_text(std::get<ParagraphBlockIR>(lowered[1].node).content) ==
                  "Table 15. DBCTL 5.1 Overview",
          "10.2 caption lowers to a paragraph before the table");
}

void test_sc31_605_block_005() {
  const auto topic = extract("SC31-605.boo", "2.1");
  require(topic.blocks.blocks.size() == 1 && topic.blocks.declined.empty(),
          "2.1 must admit exactly one table");
  if (topic.blocks.blocks.size() != 1)
    return;
  const auto &block = topic.blocks.blocks.front();
  require(block.object_id == "005" && block.width == 74 &&
              block.separator_columns == std::vector<std::size_t>{13, 24},
          "2.1 box geometry (hosted: borders at columns 3, 13, 24, 76)");
  require(block.caption && cell_text(*block.caption, 0) == "Table 3. Block ID 005",
          "2.1 caption");
  require(block.header_rows == 1 && block.body.size() == 89,
          "2.1 header and 89 body rows across logical records 48-57");
  if (block.body.size() != 89)
    return;
  require(cell_lines(block.body[0], 0) == std::vector<std::string>{"Action", "Code"} &&
              cell_lines(block.body[0], 1) == std::vector<std::string>{"Event", "Type"} &&
              cell_lines(block.body[0], 2) ==
                  std::vector<std::string>{"Event or Alert Text"},
          "2.1 two-line header (first line cut before its blank third cell)");
  const auto *first = find_row(block, "01");
  require(first != nullptr && cell_text(*first, 1) == "4" &&
              cell_text(*first, 2) == "TRANSFER MICROCODE DUMP",
          "2.1 first data row");
  // Record 50 opens directly with the left border of this row.
  const auto *cut = find_row(block, "0F");
  require(cut != nullptr && cell_text(*cut, 1) == "1" &&
              cell_text(*cut, 2) == "DATA LOST:STORE CONTROLLER",
          "2.1 row opened by a logical-record boundary");
  const auto *last = find_row(block, "7B");
  require(last != nullptr && cell_text(*last, 1) == "5" &&
              cell_text(*last, 2) == "USER APPLICATION GENERATED",
          "2.1 last row");
}

void test_sc31_605_series1_events() {
  const auto topic = extract("SC31-605.boo", "3.5");
  require(topic.blocks.blocks.size() == 1 && topic.blocks.declined.empty(),
          "3.5 must admit exactly one table");
  if (topic.blocks.blocks.size() != 1)
    return;
  const auto &block = topic.blocks.blocks.front();
  require(block.object_id == "S1" &&
              block.separator_columns == std::vector<std::size_t>{13, 34, 55} &&
              block.header_rows == 1 && block.body.size() == 107,
          "3.5 is a four-column table with a bold header and 106 event rows");
  require(!block.body.empty() &&
              cell_lines(block.body[0], 0) ==
                  std::vector<std::string>{"Event", "Code"} &&
              cell_text(block.body[0], 1) == "Qualifier 1" &&
              cell_text(block.body[0], 3) == "Qualifier 3",
          "3.5 header cells");
  const auto *wrapped = find_row(block, "02136");
  require(wrapped != nullptr &&
              cell_lines(*wrapped, 1) ==
                  std::vector<std::string>{"Node ID,device", "type"} &&
              cell_text(*wrapped, 2) == "Device address" &&
              cell_text(*wrapped, 3) == "Log record ID",
          "3.5 wrapped qualifier cell keeps both display lines");
  const auto *malfunction = find_row(block, "02142");
  require(malfunction != nullptr &&
              cell_lines(*malfunction, 2) ==
                  std::vector<std::string>{"Device address,", "malfunction code"},
          "3.5 second wrapped cell");
}

void test_negatives() {
  // A CFONT-headed directory grid without box rules is not a fixed table.
  const auto directory = extract("SC31-711.boo", "1.1");
  require(directory.blocks.blocks.empty() && directory.blocks.declined.empty(),
          "1.1 directory grid has no SRTBL envelope");
  // Plain prose with aligned spaces.
  const auto prose = extract("SC31-711.boo", "4.1.3");
  require(prose.blocks.blocks.empty() && prose.blocks.declined.empty(),
          "4.1.3 prose has no SRTBL envelope");
  // The comments questionnaire: its two satisfaction boxes are genuine
  // three-column SRTBL boxes (hosted draws them), while the `Name ______`
  // response form below them has no envelope and produces neither a block
  // nor a decline.
  const auto comments = extract("SC31-711.boo", "COMMENTS");
  require(comments.blocks.blocks.size() == 2 && comments.blocks.declined.empty(),
          "COMMENTS admits exactly the two questionnaire boxes");
  if (comments.blocks.blocks.size() == 2) {
    const auto &satisfaction = comments.blocks.blocks[1];
    require(satisfaction.separator_columns.size() == 2 &&
                satisfaction.header_rows == 0 && satisfaction.body.size() == 7 &&
                cell_text(satisfaction.body[1], 0) == "Accurate" &&
                cell_text(satisfaction.body[1], 1) == "__",
            "COMMENTS satisfaction box rows and response cells");
    for (const auto &block : comments.blocks.blocks)
      for (const auto &row : block.body)
        for (const auto &cell : row.cells)
          for (const auto &line : cell.lines)
            require(line.text.find("Name") == std::string::npos &&
                        line.text.find("Company") == std::string::npos,
                    "COMMENTS response form lines must stay outside the boxes");
  }
}

} // namespace

int main() {
  test_sc31_711_trademark_box();
  test_sc31_711_trap_directory();
  test_gg24_dbctl_overview();
  test_sc31_605_block_005();
  test_sc31_605_series1_events();
  test_negatives();
  std::cout << "fixed_table_block_ir_synthetic: done\n";
  return 0;
}
