// Fixed-table block IR: table envelopes recovered from the typed
// Layout/Ownership IR of real topics, checked against the hosted BookServer
// rendering (packet DT 20260614112503):
//   packet 2.4.4  declared `cz OFF TABLE`, caption, header, wrapped cells
//   packet 2.4.5  two declared tables in one topic
//   packet 3.9    52 body rows over several records, four-line cells
//
// Only packet.boo may be redistributed, and every table it carries is a
// *declared* `cz OFF TABLE` grid.  The undeclared SRTBL box-art half of this
// family -- column recovery from drawn rules, gap-column envelopes with no
// rules at all, verbatim `<pre>` lowering, pictures placed inside a region,
// index lines inside an envelope, and the record-cut and false-row-boundary
// rejoins -- was pinned entirely on books that cannot be published, and is
// gone with them (issue #59).

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
#include <set>
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
    const auto path = std::filesystem::path(GEIST_FIXTURE_DIR) / name;
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

// A `cz OFF TABLE` envelope: the source declared a `:table` whose column
// structure survived the build, so hosted BookServer emits an HTML `<table>`
// and the family lowers to a real table rather than to the drawn box.
void test_declared_address_classes() {
  const auto topic = extract("packet.boo", "2.4.4");
  require(topic.blocks.blocks.size() == 1 && topic.blocks.declined.empty(),
          "2.4.4 must admit exactly one table");
  if (topic.blocks.blocks.size() != 1)
    return;
  const auto &block = topic.blocks.blocks.front();
  require(block.object_id == "TBLUNIQ17", "2.4.4 object id");
  require(block.source_declared_table,
          "2.4.4 is a `cz OFF TABLE` envelope and must be marked declared");
  require(block.separator_columns.size() == 2,
          "2.4.4 has two interior separator columns");
  require(block.caption.has_value() && block.header_rows == 1 &&
              block.body.size() == 6,
          "2.4.4 is a captioned one-header six-row table");

  const std::vector<std::vector<std::string>> expected{
      {"Class", "Range", "Default Netmask"},
      {"A", "0.0.0.0 -\n127.255.255.255", "255.0.0.0"},
      {"B", "128.0.0.0 -\n191.255.255.255", "255.255.0.0"},
      {"C", "192.0.0.0 -\n223.255.255.255", "255.255.255.0"},
      {"D", "224.0.0.0 -\n239.255.255.255", "none, used for\nmulticast"},
      {"E", "240.0.0.0 -\n255.255.255.255", "none, experimental"},
  };
  for (std::size_t index = 0;
       index < expected.size() && index < block.body.size(); ++index) {
    const auto &row = block.body[index];
    require(row.cells.size() == 3, "2.4.4 row " + std::to_string(index) +
                                       " does not have three cells");
    for (std::size_t column = 0;
         column < expected[index].size() && column < row.cells.size();
         ++column) {
      require(cell_text(row, column) == expected[index][column],
              "2.4.4 row " + std::to_string(index) + " column " +
                  std::to_string(column) + " text: '" +
                  cell_text(row, column) + "'");
    }
  }
  // A cell whose text wraps onto a second display line keeps both lines; the
  // lowering turns the break into a hard break, not a lost line.
  const auto *class_d = find_row(block, "D");
  require(class_d != nullptr && cell_lines(*class_d, 2).size() == 2,
          "2.4.4 multi-line cell lost a display line");

  // Because the source declared the table, the lowering is a table -- not the
  // drawn box verbatim, which is what every undeclared SRTBL envelope gets.
  const auto lowered = lower_fixed_table_block_to_document_ir(block);
  const auto *table = lowered_table(lowered);
  require(table != nullptr && lowered_verbatim(lowered) == nullptr,
          "2.4.4 must lower to a table, not to a verbatim block");
  if (table != nullptr) {
    require(table->rows.size() == 6, "2.4.4 lowered row count");
    require(!table->rows.empty() && table->rows.front().cells.size() == 3 &&
                inline_text(table->rows.front().cells[2].content) ==
                    "Default Netmask",
            "2.4.4 lowered header cell text");
    require(table->rows.size() == 6 &&
                inline_text(table->rows[4].cells[2].content) ==
                    "none, used for\nmulticast",
            "2.4.4 lowered multi-line cell lost its hard break");
  }
  require(std::holds_alternative<AnchorBlockIR>(lowered.front().node),
          "2.4.4 lowers behind its own anchor");

  // Row-range API: the exact span admits, a disjoint span ignores, and a
  // partial span declines without admitting anything.
  const auto exact = extract_fixed_table_blocks_ir(
      topic.sources, topic.layout, topic.ownership, block.rows);
  require(exact.blocks.size() == 1 && exact.declined.empty(),
          "2.4.4 exact row range admits the table");
  const auto disjoint = extract_fixed_table_blocks_ir(
      topic.sources, topic.layout, topic.ownership,
      {0, block.rows.begin});
  require(disjoint.blocks.empty() && disjoint.declined.empty(),
          "2.4.4 disjoint row range is silent");
  const auto partial = extract_fixed_table_blocks_ir(
      topic.sources, topic.layout, topic.ownership,
      {0, block.rows.begin + 1});
  require(partial.blocks.empty() && partial.declined.size() == 1 &&
              partial.declined.front().reason ==
                  "table envelope crosses the requested row range",
          "2.4.4 partial row range declines");

  // Mutation rejection for the block verifier.
  std::string error;
  auto mutated = topic.blocks;
  mutated.blocks.front().body[1].cells[0].lines[0].text = "Z";
  require(!verify_fixed_table_blocks_ir(topic.sources, topic.layout,
                                        topic.ownership, topic.range, mutated,
                                        &error),
          "mutated cell text must be rejected");
  mutated = topic.blocks;
  mutated.blocks.front().header_rows = 2;
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
  // NOTE: `source_declared_table` is not re-derived by
  // verify_fixed_table_blocks_ir -- clearing it on an admitted block is
  // accepted.  That is a gap in the verifier, not something this test may
  // paper over, so it is recorded here rather than asserted (issue #59).

  // Mutation rejection for the document lowering verifier.
  auto lowered_mutated = lowered;
  if (auto *body = std::get_if<TableBlockIR>(&lowered_mutated.back().node)) {
    body->rows.pop_back();
    require(!verify_fixed_table_document_ir(block, lowered_mutated, &error),
            "dropped lowered row must be rejected");
  }
  lowered_mutated = lowered;
  lowered_mutated.erase(lowered_mutated.begin());
  require(!verify_fixed_table_document_ir(block, lowered_mutated, &error),
          "dropped anchor block must be rejected");
}

// Two declared tables in one topic, each with its own object id, caption and
// column count.
void test_two_declared_tables_in_one_topic() {
  const auto topic = extract("packet.boo", "2.4.5");
  require(topic.blocks.blocks.size() == 2 && topic.blocks.declined.empty(),
          "2.4.5 must admit exactly two tables");
  if (topic.blocks.blocks.size() != 2)
    return;
  const auto &ports = topic.blocks.blocks[0];
  const auto &protocols = topic.blocks.blocks[1];
  require(ports.object_id == "TBLUNIQ18" &&
              protocols.object_id == "TBLUNIQ19",
          "2.4.5 object ids or their order changed");
  require(ports.separator_columns.size() == 2 && ports.header_rows == 1 &&
              ports.body.size() == 14,
          "2.4.5 first table shape changed");
  require(protocols.separator_columns.size() == 1 &&
              protocols.header_rows == 1 && protocols.body.size() == 10,
          "2.4.5 second table shape changed");
  const auto *https = find_row(ports, "TCP");
  require(https != nullptr, "2.4.5 first table lost its TCP rows");
  require(cell_text(ports.body[13], 1) == "HTTPS" &&
              cell_text(ports.body[13], 2) == "443",
          "2.4.5 last port row changed");
  require(cell_text(protocols.body[8], 0) == "93" &&
              cell_text(protocols.body[8], 1) == "AX.25-in-IP",
          "2.4.5 protocol number row changed");
  // Both blocks are declared, and both lower to real tables.
  for (const auto *block : {&ports, &protocols}) {
    require(block->source_declared_table,
            "2.4.5 block " + block->object_id + " is not marked declared");
    const auto lowered = lower_fixed_table_block_to_document_ir(*block);
    require(lowered_table(lowered) != nullptr,
            "2.4.5 block " + block->object_id + " did not lower to a table");
  }
}

// A long declared table: 52 body rows over many logical records, with cells
// that wrap onto up to four display lines.  Every row keeps its column count
// and every wrapped line is kept.
void test_long_declared_table() {
  const auto topic = extract("packet.boo", "3.9");
  require(topic.blocks.blocks.size() == 1 && topic.blocks.declined.empty(),
          "3.9 must admit exactly one table");
  if (topic.blocks.blocks.size() != 1)
    return;
  const auto &block = topic.blocks.blocks.front();
  require(block.object_id == "TBLUNIQ40" && block.header_rows == 1 &&
              block.body.size() == 52,
          "3.9 table shape changed: " + std::to_string(block.body.size()) +
              " body rows");
  for (const auto &row : block.body)
    require(row.cells.size() == 3,
            "3.9 row '" + cell_text(row, 0) + "' does not have three cells");
  const auto *axspawn = find_row(block, "axspawn");
  require(axspawn != nullptr && cell_lines(*axspawn, 2).size() == 4,
          "3.9 four-line cell lost a display line");
  // The body's cells come from more than one logical record.
  std::set<std::uint32_t> cell_records;
  for (const auto &row : block.body)
    for (const auto &cell : row.cells)
      for (const auto &line : cell.lines)
        for (const auto &source : line.source_cells)
          cell_records.insert(source.logical_record);
  require(cell_records.size() > 1,
          "3.9 table body does not span a record boundary");
}

// Every declared table the book's generated TABLES list names is admitted,
// nothing is declined anywhere in the book, and no other topic invents one.
void test_book_inventory() {
  const auto &opened = book("packet.boo");
  std::size_t admitted = 0;
  for (const auto &entry : opened.document->table_of_contents()) {
    if (entry.id == "TABLES" || entry.id == "FIGURES" ||
        entry.id == "CONTENTS" || entry.id == "INDEX")
      continue;
    const auto topic = extract("packet.boo", entry.id);
    admitted += topic.blocks.blocks.size();
    for (const auto &block : topic.blocks.blocks)
      require(block.source_declared_table,
              "packet " + entry.id + " admitted an undeclared table envelope");
    for (const auto &declined : topic.blocks.declined)
      require(false, "packet " + entry.id + " declined a table envelope: " +
                         declined.reason);
  }
  require(admitted == 7,
          "packet admitted " + std::to_string(admitted) +
              " tables, not the seven its TABLES list names");
}

// Negatives: prose with aligned spaces, a drawn box that is not an SRTBL
// envelope, and a preformatted listing are not fixed tables.
void test_negatives() {
  // 2.2.1 draws a `___ So, what are all these layers? ___` box in prose.  It
  // is not an SRTBL envelope, so the family must neither admit nor decline it.
  const auto box = extract("packet.boo", "2.2.1");
  require(box.blocks.blocks.empty() && box.blocks.declined.empty(),
          "a drawn prose box was seen as a table envelope");
  // A.0 is a run of `cz OFF XMP` listings.
  const auto listing = extract("packet.boo", "A.0");
  require(listing.blocks.blocks.empty() && listing.blocks.declined.empty(),
          "a preformatted listing was seen as a table envelope");
  // Ordinary prose.
  const auto prose = extract("packet.boo", "1.1");
  require(prose.blocks.blocks.empty() && prose.blocks.declined.empty(),
          "ordinary prose was seen as a table envelope");
}

} // namespace

int main() {
  test_declared_address_classes();
  test_two_declared_tables_in_one_topic();
  test_long_declared_table();
  test_book_inventory();
  test_negatives();
  geist_test::exit_with_failures();
  std::cout << "fixed table block IR checks passed\n";
  return 0;
}
