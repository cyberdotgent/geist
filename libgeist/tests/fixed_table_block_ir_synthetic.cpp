// Fixed-table block IR: box-drawn SRTBL tables recovered from the typed
// Layout/Ownership IR of real topics, checked against the hosted BookServer
// rendering (AnalysisNotes URL mapping, fetched 2026-08-28):
//   SC31-711 FRONT_1.1  headerless two-column trademark box
//   SC31-711 4.0        bold two-line header, CSELECT cells, record cut
//   GG24-4302-00 10.2   caption row, bold header, multi-line bullet cells
//   SC31-605 2.1        caption row, two-line header, 89 rows over 10 records
//   SC31-605 3.5        four-column event table with wrapped cells
// and rule-less gap-column SRTBL tables (hosted pages fetched 2026-08-28,
// SC33-033 DT=19930422134757, QSYSINFO DT=19910524120827, SC24-5527-02
// Service Guide DT=19921218151459, SC31-711 DT=19941010174546):
//   QSYSINFO APPENDIX1.4.1.1  HP2 header over an empty first cell, `___`
//                             order form cell, `,` terminal slot before SRETBL
//   QSYSINFO APPENDIX1.4      ten rows, one-byte `Guide` kept before CFONT
//   SC33-033 PREFACE.6        two-column bibliography: 18-line cell, wrapped
//                             first cell, `GDDM-` `PGF` one-byte word in-line
//   SC33-033 4.6              bold signature caption + paragraph break, two
//                             tables, `)` kept before a glyph marker
//   SC24-5527-02 3.8.4.2      vertically centred command/explanation row,
//                             `built..` duplicate period, code listing declined
//   SC24-5527-02 2.2          revision-bar lines, empty second cell row
//   SC31-711 GLOSSARY         bold header, rows separated by blank lines
// SC24-5527-02 1.4 is checked alongside them as a control: its underscore
// rules make it a `box` table, so the gap model must not claim it.
// SC24-5520-00 3.8.1.10.2/3.8.1.11 pin the header rule itself: their first
// rows are italic (`<I>` on the hosted page), not bold, so they are body
// rows and not headers.
// plus negatives (no SRTBL, CFONT-only directory grid, prose) and mutation
// rejection for both verifiers.
// Envelopes with no provable column structure -- aligned code listings,
// single-line commands, ragged address blocks -- are admitted as
// preformatted regions instead: the display lines of their records
// reproduced exactly as the hosted reader prints them
// (SC24-5527-02 3.8.4.2 `TBLUNIQ99`/`TBLUNIQ100` at DT=19921218151459,
// 3.8.4.6 `TBLUNIQ114`, SG24-204 BACK_1.2 `TBLUNIQ18`/`TBLUNIQ20`).
// A region may also carry a `PIC<n>` picture selector, which hosted serves
// as an `<img>` over the columns the selector names: GG24-395 3.3.8
// `TBLUNIQ14` (one picture, DT=19941215160749) and GX27-3999-00 1.3
// `NOSENVI` (four, DT=19950730184057) check that the columns are blanked out
// of the reproduced art and the picture kept.

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

// Every SRTBL envelope the source did not declare a `:table` with
// `cz OFF TABLE` lowers to its display lines, exactly as the hosted
// BookServer serves them inside `<pre>`.
const PreformattedBlockIR *lowered_verbatim(const std::vector<BlockIR> &blocks) {
  for (const auto &block : blocks)
    if (const auto *body = std::get_if<PreformattedBlockIR>(&block.node))
      return body;
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

  // The recovered columns stay in the IR, but the Markdown is the drawn box
  // itself: hosted serves exactly these eleven lines inside `<pre width="80">`
  // (`   | IBM                                | NetView                           |`
  // over `   |____...____|____...____|`) and emits no `<table>` element.
  const auto lowered = lower_fixed_table_block_to_document_ir(block);
  const auto *verbatim = lowered_verbatim(lowered);
  require(lowered.size() == 2 &&
              std::holds_alternative<AnchorBlockIR>(lowered.front().node) &&
              lowered_table(lowered) == nullptr && verbatim != nullptr &&
              verbatim->lines.size() == 11,
          "FRONT_1.1 lowers to an anchor and the drawn box verbatim");
  if (verbatim != nullptr && verbatim->lines.size() == 11) {
    require(verbatim->lines[0] ==
                "    ____________________________________ "
                "___________________________________",
            "FRONT_1.1 verbatim top rule");
    require(verbatim->lines[7] ==
                "   | RISC System/6000                   | RS/6000            "
                "               |",
            "FRONT_1.1 verbatim RISC row");
  }

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
  auto *mutated_body =
      std::get_if<PreformattedBlockIR>(&lowered_mutated.back().node);
  if (mutated_body != nullptr) {
    mutated_body->lines[1] = "   | IBN";
    require(!verify_fixed_table_document_ir(block, lowered_mutated, &error),
            "mutated lowered line must be rejected");
    lowered_mutated = lowered;
    std::get_if<PreformattedBlockIR>(&lowered_mutated.back().node)
        ->lines.pop_back();
    require(!verify_fixed_table_document_ir(block, lowered_mutated, &error),
            "dropped lowered line must be rejected");
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
  // Hosted serves the same envelope verbatim: `   | For information       |
  // Read:                                          |` on its own line, then
  // `   | about:                |` on the next -- the header's display line
  // break is a real line, not a `<br>` inside a cell.
  const auto lowered = lower_fixed_table_block_to_document_ir(block);
  const auto *verbatim = lowered_verbatim(lowered);
  require(lowered_table(lowered) == nullptr && verbatim != nullptr &&
              verbatim->lines.size() > 2 &&
              verbatim->lines[1] ==
                  "   | For information       | Read:                         "
                  "                 |" &&
              verbatim->lines[2] ==
                  "   | about:                |                               "
                  "                 |",
          "4.0 lowers verbatim and keeps the header's two display lines");
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
  // The caption is not lifted out of the art: hosted draws it inside the box,
  // `   | Table 15. DBCTL 5.1 Overview ... |` between two rules, so the
  // verbatim body carries it in place and the lowering emits anchor + body.
  const auto lowered = lower_fixed_table_block_to_document_ir(block);
  const auto *verbatim = lowered_verbatim(lowered);
  require(lowered.size() == 2 && verbatim != nullptr &&
              lowered_table(lowered) == nullptr && verbatim->lines.size() > 1 &&
              verbatim->lines[1].rfind("   | Table 15. DBCTL 5.1 Overview", 0) ==
                  0,
          "10.2 lowers verbatim with the caption drawn inside the box");
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

void test_gap_qsysinfo_order_form() {
  const auto topic = extract("QSYSINFO.BOO", "APPENDIX1.4.1.1");
  require(topic.blocks.blocks.size() == 1 && topic.blocks.declined.empty(),
          "APPENDIX1.4.1.1 must admit exactly one table");
  if (topic.blocks.blocks.size() != 1)
    return;
  const auto &block = topic.blocks.blocks.front();
  require(block.geometry == FixedTableGeometryIR::gap &&
              block.object_id == "TBLUNIQ4" && block.left_column == 3 &&
              block.separator_columns == std::vector<std::size_t>{12, 27},
          "APPENDIX1.4.1.1 gap geometry (hosted: `___` at 3, order number at "
          "12, title at 27)");
  require(block.header_rows == 1 && block.body.size() == 2 && !block.caption,
          "APPENDIX1.4.1.1 HP2 header line and one body row");
  if (block.body.size() != 2)
    return;
  require(cell_text(block.body[0], 0).empty() &&
              cell_text(block.body[0], 1) == "Order No" &&
              cell_text(block.body[0], 2) == "Title",
          "APPENDIX1.4.1.1 header cells (empty first cell)");
  require(cell_text(block.body[1], 0) == "___" &&
              cell_text(block.body[1], 1) == "SA41-9604" &&
              cell_text(block.body[1], 2) ==
                  "Total System Package and Preloaded System Guide",
          "APPENDIX1.4.1.1 body row (`___` is a visible order-form cell, the "
          "`,` after `Guide` a hidden terminal slot)");
  const auto lowered = lower_fixed_table_block_to_document_ir(block);
  require(lowered_table(lowered) == nullptr &&
              lowered_verbatim(lowered) != nullptr,
          "APPENDIX1.4.1.1 lowers to its display lines, as hosted serves them");

  const auto appendix = extract("QSYSINFO.BOO", "APPENDIX1.4");
  require(appendix.blocks.blocks.size() == 1 && appendix.blocks.declined.empty(),
          "APPENDIX1.4 must admit exactly one table");
  if (appendix.blocks.blocks.size() == 1) {
    const auto &planning = appendix.blocks.blocks.front();
    require(planning.header_rows == 1 && planning.body.size() == 11,
            "APPENDIX1.4 header and ten body rows");
    const auto *physical = find_row(planning, "___");
    require(physical != nullptr, "APPENDIX1.4 rows start with `___`");
    if (planning.body.size() == 11) {
      require(cell_text(planning.body[5], 1) == "GA41-0001" &&
                  cell_text(planning.body[5], 2) == "Physical Planning Guide",
              "APPENDIX1.4 one-byte `Guide` before CFONT is content");
      require(cell_lines(planning.body[4], 2) ==
                  std::vector<std::string>{
                      "New Products Planning Information for Version 2",
                      "Release 1"},
              "APPENDIX1.4 wrapped title cell keeps both display lines");
    }
  }
}

void test_gap_sc33_bibliography() {
  const auto topic = extract("SC33-033.boo", "PREFACE.6");
  require(topic.blocks.blocks.size() == 1 && topic.blocks.declined.empty(),
          "PREFACE.6 must admit exactly one table");
  if (topic.blocks.blocks.size() != 1)
    return;
  const auto &block = topic.blocks.blocks.front();
  require(block.geometry == FixedTableGeometryIR::gap &&
              block.left_column == 3 &&
              block.separator_columns == std::vector<std::size_t>{26} &&
              block.header_rows == 0 && !block.caption && block.body.size() == 5,
          "PREFACE.6 two gap columns (hosted: products at 3, titles at 26), "
          "five rows");
  if (block.body.size() != 5)
    return;
  const auto base = cell_lines(block.body[0], 1);
  require(cell_text(block.body[0], 0) == "GDDM Base" && base.size() == 18 &&
              base[0] == "GDDM Base Application Programming Guide, SC33-0867" &&
              base[1] == "GDDM Base Application Programming Reference" &&
              base[2] == "SC33-0868" &&
              base[5] == "GDDM/MVS Installation: Planning, Testing, and" &&
              base[17] == "GDDM Using the Image Symbol Editor, SC33-0920",
          "PREFACE.6 first row: 18 title lines, `SC33-0868` continuation "
          "line, `, and` kept before the CFONT");
  require(cell_lines(block.body[2], 0) ==
                  std::vector<std::string>{"GDDM Interactive", "Map Definition"} &&
              cell_text(block.body[2], 1) ==
                  "GDDM Interactive Map Definition, SC33-0338",
          "PREFACE.6 wrapped first cell joins its row");
  require(cell_text(block.body[4], 0) == "GDDM-PGF" &&
              cell_lines(block.body[4], 1).size() == 5 &&
              cell_lines(block.body[4], 1)[0] ==
                  "GDDM-PGF Application Programming Guide, SC33-0913",
          "PREFACE.6 `GDDM-` `PGF` one-byte word stays in line");
  // Words the Layout IR dropped are claimed from the opaque ledger.
  std::size_t unpositioned = 0;
  for (const auto &row : block.body)
    for (const auto &cell : row.cells)
      for (const auto &line : cell.lines)
        unpositioned += line.unpositioned_cells.size();
  require(unpositioned == 9, "PREFACE.6 claims nine unpositioned words");
}

void test_gap_sc33_function_signature() {
  const auto topic = extract("SC33-033.boo", "4.6");
  require(topic.blocks.blocks.size() == 2 && topic.blocks.declined.empty(),
          "4.6 must admit both tables");
  if (topic.blocks.blocks.size() != 2)
    return;
  const auto &codes = topic.blocks.blocks[0];
  require(codes.object_id == "TBLUNIQ4" && codes.caption &&
              cell_text(*codes.caption, 0) == "CHAATT      (count, array)" &&
              codes.header_rows == 0 && codes.body.size() == 2,
          "4.6 bold signature line cut off by a paragraph break is the caption");
  if (codes.body.size() == 2)
    require(cell_text(codes.body[0], 0) == "APL code" &&
                cell_text(codes.body[0], 1) == "735" &&
                cell_text(codes.body[1], 0) == "PGF RCP code" &&
                cell_text(codes.body[1], 1) == "X'10020701' (268568321)",
            "4.6 code rows (`,` before SRETBL hidden)");
  const auto &defaults = topic.blocks.blocks[1];
  require(defaults.object_id == "TBLUNIQ5" && !defaults.caption &&
              defaults.body.size() == 3 &&
              cell_text(defaults.body[0], 0) == "color" &&
              cell_text(defaults.body[0], 1) ==
                  "green (displays), black (printers)" &&
              cell_text(defaults.body[1], 0) == "line type" &&
              cell_text(defaults.body[2], 1) == "normal",
          "4.6 defaults table: `line type` one-byte word in line, `)` kept "
          "before a glyph marker");
  // Gap-column envelopes render verbatim too: the caption line is one of the
  // region's display lines and hosted prints it in place.
  const auto lowered = lower_fixed_table_block_to_document_ir(codes);
  require(lowered.size() == 2 && lowered_table(lowered) == nullptr &&
              lowered_verbatim(lowered) != nullptr,
          "4.6 lowers to its display lines, caption line included");
}

void test_gap_sc24_command_tables() {
  const auto build = extract("SC24-5527-02.boo", "3.8.4.2");
  require(build.blocks.blocks.size() == 3 && build.blocks.declined.empty(),
          "3.8.4.2 admits the command row and both listings");
  if (build.blocks.blocks.size() == 3) {
    const auto &row = build.blocks.blocks.front();
    require(row.object_id == "TBLUNIQ98" && row.body.size() == 1 &&
                row.separator_columns == std::vector<std::size_t>{50} &&
                cell_text(row.body[0], 0) == "vmfbld ppf esa cms (status setup" &&
                cell_lines(row.body[0], 1) ==
                    std::vector<std::string>{
                        "This command will update the Build Status",
                        "Table with a status of serviced for each",
                        "object that needs to be built."},
            "3.8.4.2 centred row: command on the middle line, one period");
  }
  // The VMFBLD2185R message listing and the one-line `vmfview build` command
  // are not tables: hosted BookServer serves them as the plain <pre> lines
  // reproduced here (DT=19921218151459).
  if (build.blocks.blocks.size() == 3) {
    const auto &listing = build.blocks.blocks[1];
    require(listing.object_id == "TBLUNIQ99" &&
                listing.geometry == FixedTableGeometryIR::preformatted &&
                listing.body.empty() && !listing.caption &&
                listing.preformatted_lines.size() == 26 &&
                listing.preformatted_lines.front().text ==
                    "   VMFBLD2185R The following source product parameter "
                    "files have been serviced:" &&
                listing.preformatted_lines[4].text ==
                    "               before VMFBLD can be run." &&
                listing.preformatted_lines[10].text.empty() &&
                listing.preformatted_lines.back().text ==
                    "                                                  1 to "
                    "continue.",
            "3.8.4.2 VMFBLD2185R listing is preformatted, not a table");
    const auto &command = build.blocks.blocks[2];
    require(command.object_id == "TBLUNIQ100" &&
                command.geometry == FixedTableGeometryIR::preformatted &&
                command.preformatted_lines.size() == 1 &&
                command.preformatted_lines.front().text == "   vmfview build",
            "3.8.4.2 `vmfview build` is one preformatted line");
    const auto lowered = lower_fixed_table_block_to_document_ir(command);
    std::string lowering_error;
    require(lowered.size() == 2 &&
                std::holds_alternative<AnchorBlockIR>(lowered[0].node) &&
                std::holds_alternative<PreformattedBlockIR>(lowered[1].node) &&
                verify_fixed_table_document_ir(command, lowered,
                                               &lowering_error),
            "3.8.4.2 preformatted region lowers to anchor + preformatted: " +
                lowering_error);
    // Mutation rejection for the preformatted path.
    std::string error;
    auto mutated = build.blocks;
    mutated.blocks[2].preformatted_lines.front().text = "   vmfview built";
    require(!verify_fixed_table_blocks_ir(build.sources, build.layout,
                                          build.ownership, build.range, mutated,
                                          &error),
            "mutated preformatted line must be rejected");
    mutated = build.blocks;
    mutated.blocks[2].structural_cells.pop_back();
    require(!verify_fixed_table_blocks_ir(build.sources, build.layout,
                                          build.ownership, build.range, mutated,
                                          &error),
            "dropped preformatted structural claim must be rejected");
    mutated = build.blocks;
    mutated.blocks[1].preformatted_lines.erase(
        mutated.blocks[1].preformatted_lines.begin() + 3);
    require(!verify_fixed_table_blocks_ir(build.sources, build.layout,
                                          build.ownership, build.range, mutated,
                                          &error),
            "dropped preformatted line must be rejected");
    auto lowered_mutated = lowered;
    std::get<PreformattedBlockIR>(lowered_mutated[1].node).lines[0] = "x";
    require(!verify_fixed_table_document_ir(command, lowered_mutated, &error),
            "mutated lowered preformatted text must be rejected");
    lowered_mutated = lowered;
    lowered_mutated.erase(lowered_mutated.begin() + 1);
    require(!verify_fixed_table_document_ir(command, lowered_mutated, &error),
            "dropped lowered preformatted block must be rejected");
  }

  const auto refresh = extract("SC24-5527-02.boo", "2.2");
  require(refresh.blocks.blocks.size() == 3 && refresh.blocks.declined.empty(),
          "2.2 admits the attach/vmfins table and two preformatted regions");
  if (refresh.blocks.blocks.size() == 3) {
    const auto &table = refresh.blocks.blocks.front();
    require(table.object_id == "TBLUNIQ24" && table.body.size() == 2 &&
                cell_text(table.body[0], 0) == "attach rdev * 181" &&
                cell_lines(table.body[0], 1).size() == 3 &&
                cell_text(table.body[1], 0) == "vmfins install info" &&
                cell_text(table.body[1], 1).empty(),
            "2.2 revision-bar lines are structural; second row has an empty "
            "explanation");
  }

  const auto names = extract("SC24-5527-02.boo", "1.4");
  require(names.blocks.blocks.size() == 2 && names.blocks.declined.empty(),
          "1.4 admits the PPF name table and the PPFNOT note region");
  if (names.blocks.blocks.size() == 2) {
    const auto &table = names.blocks.blocks.front();
    require(table.object_id == "INTPPFN" && table.caption &&
                cell_text(*table.caption, 0) ==
                    "Table  1-1. VM/ESA Component PPF File Names and Aliases" &&
                table.header_rows == 1 && table.body.size() == 9 &&
                table.separator_columns.size() == 2 &&
                cell_lines(table.body[0], 2) ==
                    std::vector<std::string>{"Component Name as",
                                             "Defined in ESA $PPF",
                                             "and ESALCL $PPF"} &&
                cell_text(table.body[1], 0) == "6VMVMK20" &&
                cell_text(table.body[8], 2) == "AVS/AVSSFS",
            "1.4 underscore-drawn table stays a box table");
    require(table.geometry == FixedTableGeometryIR::box,
            "1.4 has underscore rules, so the gap model must not claim it");
  }
}

void test_gap_sc31_glossary() {
  const auto topic = extract("SC31-711.boo", "GLOSSARY");
  require(topic.blocks.blocks.size() == 1 && topic.blocks.declined.empty(),
          "GLOSSARY admits the DLCI table");
  if (topic.blocks.blocks.size() != 1)
    return;
  const auto &block = topic.blocks.blocks.front();
  require(block.geometry == FixedTableGeometryIR::gap &&
              block.header_rows == 1 && block.body.size() == 7 &&
              cell_text(block.body[0], 0) == "DLCI Values" &&
              cell_text(block.body[0], 1) == "Function" &&
              cell_text(block.body[3], 0) == "16-991" &&
              cell_text(block.body[3], 1) ==
                  "assigned using frame-relay connection procedures" &&
              cell_text(block.body[6], 0) == "1023",
          "GLOSSARY bold header and six rows separated by blank lines");
}

void test_gap_negatives() {
  // A command listing whose output lines carry no origin pattern is no
  // table; it stays a verbatim region instead.
  const auto listing = extract("SC24-5527-02.boo", "3.8.4.6");
  bool width = false;
  for (const auto &block : listing.blocks.blocks)
    if (block.object_id == "TBLUNIQ114")
      width = block.geometry == FixedTableGeometryIR::preformatted &&
              block.separator_columns.empty() && block.body.empty();
  require(width && listing.blocks.declined.empty(),
          "3.8.4.6 `query rdr * all` listing is not a table");
  for (const auto &block : listing.blocks.blocks)
    require(block.separator_columns.size() <= 2,
            "3.8.4.6 admitted tables have at most three columns");
  // One-line envelopes and ragged address blocks stay out of the table
  // model as well.
  const auto addresses = extract("SG24-204.boo", "BACK_1.2");
  bool single = false;
  bool ragged = false;
  for (const auto &block : addresses.blocks.blocks) {
    if (block.object_id == "TBLUNIQ18")
      single = block.geometry == FixedTableGeometryIR::preformatted &&
               block.preformatted_lines.size() == 4;
    if (block.object_id == "TBLUNIQ20")
      ragged = block.geometry == FixedTableGeometryIR::preformatted;
  }
  require(single && ragged,
          "BACK_1.2 single-line and ragged-gap envelopes are not tables");
}

// Typed CFONT provenance only makes the first row a header when it is set in
// a bold face. SC24-5520-00 renders the first row of its CPED allocate-data
// tables in italic (`<I>VM</I> <I>architected</I> <I>area</I> ...` on the
// hosted page, DT=19911011135123), which is emphasis inside a body row, not a
// column header.
void test_box_italic_first_row_is_not_a_header() {
  for (const char *id : {"3.8.1.10.2", "3.8.1.11"}) {
    const auto topic = extract("SC24-5520-00.boo", id);
    require(!topic.blocks.blocks.empty(),
            std::string(id) + " admits its allocate-data boxes");
    for (const auto &block : topic.blocks.blocks) {
      if (block.body.empty())
        continue;
      const auto first = cell_text(block.body.front(), 1);
      if (first != "VM architected area starts here" &&
          first != "FMH5 starts here")
        continue;
      require(block.geometry == FixedTableGeometryIR::box &&
                  block.header_rows == 0 &&
                  block.body.front().kind == FixedTableRowKindIR::body,
              std::string(id) + " " + block.object_id +
                  ": an italic first row is a body row, not a header");
    }
  }
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

// A `PIC<n>` selector inside a fixed-layout region.  Hosted BookServer
// replaces exactly the selector's columns with the image and leaves the rest
// of the display line in place, so the region keeps its art *and* its
// picture: the columns are blanked in the reproduced text and the picture is
// recorded.  Reproducing the placeholder words instead would spell
// `PICTURE 69` where hosted shows the image.
//   GG24-395 3.3.8 `TBLUNIQ14`  DT=19941215160749, one picture, prose beside
//   GX27-3999-00 1.3 `NOSENVI`  DT=19950730184057, four icons, one per row
void test_picture_regions() {
  const auto overview = extract("GG24-395.boo", "3.3.8");
  require(overview.blocks.blocks.size() == 1 &&
              overview.blocks.declined.empty(),
          "3.3.8 admits its picture-bearing envelope");
  if (overview.blocks.blocks.size() != 1)
    return;
  const auto &region = overview.blocks.blocks.front();
  require(region.object_id == "TBLUNIQ14" && !region.source_declared_table &&
              region.pictures.size() == 1,
          "3.3.8 records exactly one picture");
  if (region.pictures.size() == 1) {
    const auto &picture = region.pictures.front();
    require(picture.resource == "69" && picture.placeholder == "PICTURE 69" &&
                picture.line == 0 && picture.column == 3 &&
                picture.length == 11,
            "3.3.8 picture is PIC69 over columns [3,14) of the first line");
    require(region.preformatted_lines.front().text ==
                "                   SystemView Host Management Facilities/VM "
                "(HMF/VM, 5684-157),",
            "3.3.8 blanks the placeholder columns, as hosted's <pre> does");
  }
  const auto lowered = lower_fixed_table_block_to_document_ir(region);
  const auto *figure =
      lowered.size() == 3 ? std::get_if<FigureBlockIR>(&lowered[1].node)
                          : nullptr;
  require(lowered.size() == 3 && figure != nullptr &&
              figure->resource == "resource:69" &&
              lowered_verbatim(lowered) != nullptr,
          "3.3.8 lowers to anchor + image + verbatim art");

  const auto adapters = extract("GX27-3999-00.boo", "1.3");
  require(adapters.blocks.blocks.size() == 1, "1.3 admits its envelope");
  if (adapters.blocks.blocks.size() == 1) {
    const auto &box = adapters.blocks.blocks.front();
    std::vector<std::string> resources;
    for (const auto &picture : box.pictures)
      resources.push_back(picture.resource);
    require(resources == std::vector<std::string>{"3", "4", "5", "6"},
            "1.3 records one icon per table row, in line order");
    for (const auto &picture : box.pictures)
      require(picture.line < box.preformatted_lines.size() &&
                  box.preformatted_lines[picture.line].text.find(
                      "PICTURE") == std::string::npos,
              "1.3 blanks every placeholder out of the reproduced art");
  }

  // Mutation rejection: an image may not be dropped, moved or left spelling
  // its placeholder words.
  std::string error;
  auto mutated = overview.blocks;
  mutated.blocks[0].pictures.clear();
  require(!verify_fixed_table_blocks_ir(overview.sources, overview.layout,
                                        overview.ownership, overview.range,
                                        mutated, &error),
          "dropped picture must be rejected");
  mutated = overview.blocks;
  mutated.blocks[0].pictures.front().resource = "70";
  require(!verify_fixed_table_blocks_ir(overview.sources, overview.layout,
                                        overview.ownership, overview.range,
                                        mutated, &error),
          "retargeted picture must be rejected");
  mutated = overview.blocks;
  mutated.blocks[0].preformatted_lines.front().text =
      "    PICTURE 69     SystemView Host Management Facilities/VM "
      "(HMF/VM, 5684-157),";
  require(!verify_fixed_table_blocks_ir(overview.sources, overview.layout,
                                        overview.ownership, overview.range,
                                        mutated, &error),
          "placeholder words left in the picture's columns must be rejected");
  auto lowered_mutated = lowered;
  lowered_mutated.erase(lowered_mutated.begin() + 1);
  require(!verify_fixed_table_document_ir(region, lowered_mutated, &error),
          "lowering that loses the image must be rejected");
}

} // namespace

int main() {
  test_sc31_711_trademark_box();
  test_sc31_711_trap_directory();
  test_gg24_dbctl_overview();
  test_sc31_605_block_005();
  test_sc31_605_series1_events();
  test_negatives();
  test_box_italic_first_row_is_not_a_header();
  test_gap_qsysinfo_order_form();
  test_gap_sc33_bibliography();
  test_gap_sc33_function_signature();
  test_gap_sc24_command_tables();
  test_gap_sc31_glossary();
  test_gap_negatives();
  test_picture_regions();
  std::cout << "fixed_table_block_ir_synthetic: done\n";
  return 0;
}
