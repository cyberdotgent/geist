// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

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
#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/ir/fixed_table_block_ir.hpp"
#include "geist/detail/lowering/fixed_table_document_lowering.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/layout_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
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

TokenWords words(const std::string &text) {
  return TokenWords(text.begin(), text.end());
}

struct RecordBuilder {
  DecodedLogicalRecordSource record;
  std::uint16_t next_encoded = 0x100;

  void line(std::vector<TokenWords> content) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(content.size() * 2), 1});
    record.tokens.push_back({0x25ba});
    for (auto &token : content) {
      record.encoded_tokens.push_back({next_encoded++, 2});
      record.tokens.push_back(std::move(token));
    }
  }

  // One display line of (word, width) tokens; `\x01` is the attach token.
  // `spelling` is the length byte's dictionary word.
  void sized_line(std::vector<std::pair<std::string, std::uint8_t>> content,
                  TokenWords spelling = {0x25ba}) {
    std::uint16_t length = 0;
    for (const auto &token : content) length += token.second;
    record.encoded_tokens.push_back({length, 1});
    record.tokens.push_back(std::move(spelling));
    for (const auto &[text, width] : content) {
      record.encoded_tokens.push_back(
          {static_cast<std::uint16_t>(width == 1 ? 0xC0 : next_encoded++),
           width});
      if (text == "\x01")
        record.tokens.push_back({1});
      else
        record.tokens.push_back(words(text));
    }
  }

  // A display line of one-byte tokens, the shape a drawn box row is stored
  // in: every glyph run, origin and cell word is its own one-byte token.
  void narrow_line(std::vector<TokenWords> content) {
    record.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(content.size()), 1});
    record.tokens.push_back({0x25ba});
    for (auto &token : content) {
      const auto joiner = !token.empty() && token.front() < 4;
      record.encoded_tokens.push_back(
          {static_cast<std::uint16_t>(joiner ? token.front() : 0x60), 1});
      record.tokens.push_back(std::move(token));
    }
  }

  DecodedLogicalRecordSource finish(std::uint32_t logical_record) {
    record.logical_record = logical_record;
    record.assembled = assemble_logical_record_with_sources(record.tokens);
    record.ir.logical_record = logical_record;
    std::uint32_t byte = 1;
    for (std::size_t token = 0; token < record.tokens.size(); ++token) {
      const auto encoded = record.encoded_tokens[token];
      const auto spacing = !record.tokens[token].empty() &&
                           record.tokens[token].front() < 4;
      record.ir.tokens.push_back(
          {token, encoded, record.tokens[token],
           {byte, static_cast<std::uint32_t>(byte + encoded.width)}, spacing,
           spacing ? record.tokens[token].front() : std::uint16_t{3}, {},
           TokenFramingRole::unframed});
      byte += encoded.width;
    }
    record.ir.payload_range = {1, byte};
    assign_display_line_framing(record.ir);
    record.control_segments = decode_control_segments(
        logical_record, record.assembled, record.encoded_tokens,
        record.ir.display_lines);
    demote_display_line_owned_controls(record);
    return std::move(record);
  }
};

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
  // The dictionary text the directory version and parameter select.
  context.directory.version_text =
      geist::detail::EbcdicCodec::cp037().decode_ascii(
          context.bytes, base + 0x10, 4, "short directory");
  context.directory.dictionary_text_parameter = static_cast<std::uint16_t>(
      (context.bytes[base + 0x4c] << 8) | context.bytes[base + 0x4d]);
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

// A spot destination inside a character-art table is structure, not a reason
// to reject the table's faithful preformatted representation.  The control
// opens the following row, so lowering must keep it between the two verbatim
// blocks instead of moving it ahead of the drawing.
void test_spot_anchor_inside_preformatted_table() {
  RecordBuilder builder;
  builder.line({words("cz"), words("OFF"), words("TABLE")});
  builder.line({words("SRTBLDRAWING")});
  builder.line({words("   first art row")});
  builder.line({words("SRSPTCHART")});
  builder.line({words("   second art row")});
  builder.line({words("SRETBL")});
  builder.line({words("cz"), words("OFF"), words("ETABLE")});
  std::vector<DecodedLogicalRecordSource> sources;
  sources.push_back(builder.finish(17));
  LayoutIR layout;
  DisplayRunIR run;
  run.id = 1;
  run.control_kind = BookControlKind::text;
  for (const auto line_index : {std::size_t{2}, std::size_t{4}}) {
    const auto &line = sources.front().ir.display_lines[line_index];
    PhysicalRowIR row;
    row.run = run.id;
    row.logical_record = 17;
    row.token_begin = line.prefix_token + 1;
    row.token_end = line.token_end;
    row.start = PhysicalRowStartKind::control_payload;
    row.visible_text = token_words_to_ascii(
        sources.front().tokens[row.token_begin]);
    run.rows.push_back(std::move(row));
  }
  layout.runs.push_back(std::move(run));
  const auto ownership = build_ownership_ir(sources, layout);
  const auto blocks = extract_fixed_table_blocks_ir(
      sources, layout, ownership, {0, count_layout_rows(layout)});
  require(blocks.declined.empty() && blocks.blocks.size() == 1,
          "SRSPT character-art table was declined" +
              (blocks.declined.empty()
                   ? std::string{}
                   : ": " + blocks.declined.front().reason));
  if (blocks.blocks.size() != 1)
    return;
  const auto &block = blocks.blocks.front();
  require(block.geometry == FixedTableGeometryIR::preformatted,
          "single-column character art was asserted to be a semantic table");
  require(block.preformatted_lines.size() == 2 &&
              block.preformatted_lines[0].text == "   first art row" &&
              block.preformatted_lines[1].text == "   second art row",
          "character-art rows changed");
  require(block.spot_anchors.size() == 1 &&
              block.spot_anchors.front().id == "SPTCHART" &&
              block.spot_anchors.front().line == 1 &&
              block.spot_anchors.front().source.token_end ==
                  block.spot_anchors.front().source.token_begin + 1,
          "SRSPT identity or row position changed");
  std::string error;
  require(verify_fixed_table_blocks_ir(
              sources, layout, ownership, {0, count_layout_rows(layout)},
              blocks, &error),
          "SRSPT character-art table did not verify: " + error);
  auto mutated_blocks = blocks;
  mutated_blocks.blocks.front().spot_anchors.front().id = "SPTWRONG";
  require(!verify_fixed_table_blocks_ir(
              sources, layout, ownership, {0, count_layout_rows(layout)},
              mutated_blocks, &error),
          "mutated SRSPT identity passed block verification");
  mutated_blocks = blocks;
  ++mutated_blocks.blocks.front().spot_anchors.front().source.token_begin;
  require(!verify_fixed_table_blocks_ir(
              sources, layout, ownership, {0, count_layout_rows(layout)},
              mutated_blocks, &error),
          "mutated SRSPT source token passed block verification");

  const auto lowered = lower_fixed_table_block_to_document_ir(block);
  require(lowered.size() == 4,
          "positional SRSPT lowering did not split the drawing");
  if (lowered.size() == 4) {
    const auto *before = std::get_if<PreformattedBlockIR>(&lowered[1].node);
    const auto *anchor = std::get_if<AnchorBlockIR>(&lowered[2].node);
    const auto *after = std::get_if<PreformattedBlockIR>(&lowered[3].node);
    require(before != nullptr && before->lines ==
                                     std::vector<std::string>{"   first art row"},
            "rows before SRSPT moved or changed");
    require(anchor != nullptr && anchor->id == "SPTCHART" &&
                anchor->role == AnchorRoleIR::cross_reference,
            "SRSPT lowered with the wrong identity or role");
    require(after != nullptr && after->lines ==
                                   std::vector<std::string>{"   second art row"},
            "rows after SRSPT moved or changed");
  }
  require(verify_fixed_table_document_ir(block, lowered, &error),
          "SRSPT character-art lowering did not verify: " + error);
  auto mutated_lowering = lowered;
  if (mutated_lowering.size() == 4) {
    auto *anchor = std::get_if<AnchorBlockIR>(&mutated_lowering[2].node);
    if (anchor != nullptr)
      anchor->role = AnchorRoleIR::local;
  }
  require(!verify_fixed_table_document_ir(block, mutated_lowering, &error),
          "mutated SRSPT anchor role passed lowering verification");
}

// A one-line SRTBL command can cross a logical-record boundary without
// producing any compatibility LayoutIR row: the opening/control line is in
// the first record and the native display row plus SRETBL are in the next.
// The source display-line framing still proves the complete verbatim region.
void test_native_row_without_layout_projection() {
  RecordBuilder first;
  first.line({words("SRTBLCOMMAND")});
  first.line({words("cfont"), words("3"), words("6"), words("2")});
  RecordBuilder second;
  second.line({words("   vmfbld ppf esa rexx")});
  second.line({words("SRETBL")});

  std::vector<DecodedLogicalRecordSource> sources;
  sources.push_back(first.finish(41));
  sources.push_back(second.finish(42));
  LayoutIR layout;
  const auto ownership = build_ownership_ir(sources, layout);
  const LayoutRowRangeIR range{0, 0};
  const auto blocks =
      extract_fixed_table_blocks_ir(sources, layout, ownership, range);
  require(blocks.declined.empty() && blocks.blocks.size() == 1,
          "native-only SRTBL row was declined" +
              (blocks.declined.empty()
                   ? std::string{}
                   : ": " + blocks.declined.front().reason));
  if (blocks.blocks.size() != 1)
    return;
  const auto &block = blocks.blocks.front();
  require(block.geometry == FixedTableGeometryIR::preformatted &&
              block.rows.begin == block.rows.end &&
              block.source_rows.empty() &&
              block.preformatted_lines.size() == 1 &&
              block.preformatted_lines.front().text ==
                  "   vmfbld ppf esa rexx" &&
              block.preformatted_lines.front().rows.empty(),
          "native-only SRTBL row lost its text or invented a layout row");
  std::string error;
  require(verify_fixed_table_blocks_ir(sources, layout, ownership, range,
                                       blocks, &error),
          "native-only SRTBL block did not verify: " + error);
  const auto lowered = lower_fixed_table_block_to_document_ir(block);
  const auto *verbatim = lowered_verbatim(lowered);
  require(verbatim != nullptr &&
              verbatim->lines ==
                  std::vector<std::string>{"   vmfbld ppf esa rexx"} &&
              verbatim->line_origins.size() == 1 &&
              verbatim->line_origins.front().rows.empty() &&
              verbatim->line_origins.front().slices.size() == 1,
          "native-only SRTBL lowering lost source-only provenance");
  require(verify_fixed_table_document_ir(block, lowered, &error),
          "native-only SRTBL lowering did not verify: " + error);
}

// GC30-3820-03 B.1 (DT 19980528120919), record 129: the trademark table's
// line 18 is `   AT   Operating System/2   PS/2`.  Its last word ends with
// the one-byte `2` (0x3F32C), glued to `/` by the attach token; the next
// byte is line 19's length byte (`12` at 0x3F32D, spelled as four blanks)
// and line 19's origin run follows -- the shape of a hidden marker slot, a
// fill and an origin.  The record's framing proves the `2` is no length
// byte, and hosted prints `PS/2`.
void test_glued_one_byte_word_is_content() {
  RecordBuilder builder;
  builder.line({words("cz"), words("OFF"), words("TABLE")});
  builder.line({words("SRTBLTBLUNIQ10")});
  builder.sized_line({{"   ", 1}, {"AIX", 2}, {"          ", 1},
                      {"Personal", 2}});
  builder.sized_line({{"   ", 1}, {"AT", 2}, {"           ", 1},
                      {"System", 2}, {"   ", 1}, {"PS", 2}, {"\x01", 1},
                      {"/", 1}, {"\x01", 1}, {"2", 1}});
  builder.sized_line({{"   ", 1}, {"IBM", 2}, {"          ", 1},
                      {"OS", 2}},
                     words("    "));
  builder.line({words("SRETBL")});
  std::vector<DecodedLogicalRecordSource> sources;
  sources.push_back(builder.finish(129));
  require(sources.front().ir.display_lines_parse,
          "glued-word table: synthetic framing did not parse");
  const auto layout = extract_layout_ir(sources);
  const auto ownership = build_ownership_ir(sources, layout);
  const LayoutRowRangeIR range{0, count_layout_rows(layout)};
  const auto blocks =
      extract_fixed_table_blocks_ir(sources, layout, ownership, range);
  require(blocks.blocks.size() == 1,
          "glued-word table did not become one block" +
              (blocks.declined.empty()
                   ? std::string{}
                   : ": " + blocks.declined.front().reason));
  if (blocks.blocks.size() != 1) return;
  require(blocks.blocks.front().geometry == FixedTableGeometryIR::gap,
          "glued-word table is not a gap table");
  std::string text;
  for (const auto &row : blocks.blocks.front().body)
    for (const auto &cell : row.cells)
      for (const auto &line : cell.lines) text += line.text + "|";
  require(text.find("PS/2|") != std::string::npos,
          "the `2` of `PS/2` was taken for a hidden marker slot: " + text);
}

// A revision bar standing in the margin of a declared box table, one column
// to the left of the table's own left border.  SC33-1369-09 2.1 record 94
// line 23 is ` | |      Y     |      Y     | ...`: a one-space origin, the
// bar (U+2502), then the box's left border at the column every other line of
// the box puts it.  Hosted BookServer (DT 19971205131216) serves the line as
// drawn, bar included, inside the region's `<pre>`; the `Y` cells are table
// content.  The row must survive as a body row with its revision flag, and
// the bar must stay structural.
TokenWords glyphs(std::uint16_t glyph, std::size_t count) {
  return TokenWords(count, glyph);
}

void test_revision_bar_before_box_border() {
  constexpr std::uint16_t vertical = 0x2502;
  constexpr std::uint16_t horizontal = 0x2500;
  const TokenWords join{1};
  const auto rule = [&](std::uint16_t left, std::uint16_t middle,
                        std::uint16_t right) {
    return std::vector<TokenWords>{words("   "), {left},  join,
                                   glyphs(horizontal, 6),  join,
                                   {middle},               join,
                                   glyphs(horizontal, 6),  join,
                                   {right}};
  };
  RecordBuilder builder;
  builder.line({words("cz"), words("OFF"), words("TABLE")});
  builder.line({words("SRTBLREVISED")});
  builder.narrow_line(rule(0x250c, 0x252c, 0x2510));
  builder.narrow_line({words("   "), {vertical}, words(" "), words("Ab"),
                       words(" "), {vertical}, words(" "), words("Cd"),
                       words(" "), {vertical}});
  builder.narrow_line(rule(0x251c, 0x253c, 0x2524));
  builder.narrow_line({words(" "), {vertical}, {vertical}, words(" "),
                       words("Y"), words("  "), {vertical}, words(" "),
                       words("Y"), words("  "), {vertical}});
  builder.narrow_line(rule(0x2514, 0x2534, 0x2518));
  builder.line({words("SRETBL")});
  builder.line({words("cz"), words("OFF"), words("ETABLE")});

  std::vector<DecodedLogicalRecordSource> sources;
  sources.push_back(builder.finish(23));
  require(sources.front().ir.display_lines_parse,
          "revision-bar table: display lines do not parse");
  const auto layout = extract_layout_ir(sources);
  const auto ownership = build_ownership_ir(sources, layout);
  const LayoutRowRangeIR range{0, count_layout_rows(layout)};
  const auto blocks =
      extract_fixed_table_blocks_ir(sources, layout, ownership, range);
  require(blocks.declined.empty() && blocks.blocks.size() == 1,
          "revision-bar table was declined" +
              (blocks.declined.empty()
                   ? std::string{}
                   : ": " + blocks.declined.front().reason));
  if (blocks.blocks.size() != 1)
    return;
  const auto &block = blocks.blocks.front();
  require(block.geometry == FixedTableGeometryIR::box,
          "revision-bar table did not prove its box geometry");
  require(block.body.size() == 2,
          "revision-barred row was not kept as a body row (" +
              std::to_string(block.body.size()) + " body rows)");
  const auto *revised = find_row(block, "Y");
  require(revised != nullptr, "revision-barred row lost its cell words");
  if (revised != nullptr) {
    require(cell_text(*revised, 1) == "Y",
            "revision-barred row lost its second cell: '" +
                cell_text(*revised, 1) + "'");
    require(revised->revised, "revision-barred row lost its revision flag");
  }
  const auto *plain = find_row(block, "Ab");
  require(plain != nullptr && !plain->revised,
          "unrevised row is missing or carries a revision flag");
  std::string error;
  require(verify_fixed_table_blocks_ir(sources, layout, ownership, range,
                                       blocks, &error),
          "revision-bar table did not verify: " + error);
  const auto lowered = lower_fixed_table_block_to_document_ir(block);
  require(verify_fixed_table_document_ir(block, lowered, &error),
          "revision-bar table lowering did not verify: " + error);
  const auto *table = lowered_table(lowered);
  require(table != nullptr, "revision-bar table did not lower to a table");
}

// A rule-less (gap) table whose rows end on a one-byte word, followed by the
// next row's length byte spelled as blanks.  dfhcae43 1.9.3.1.3 record 654
// line 36 ends `... the issuing task is not`, dfhlm207 2.7.28 record 516
// line 8 ends `8-character data-area`: the last word and the next line's
// length byte look exactly like an old hidden marker slot and its origin,
// and the slot-shape reading dropped `not` and `area`.  The record's
// display-line framing says which token is the slot.
void test_line_end_word_before_blank_length_byte() {
  RecordBuilder builder;
  builder.line({words("cz"), words("OFF"), words("TABLE")});
  builder.line({words("SRTBLWORDS")});
  builder.narrow_line({words("   "), words("ALPHA"), words("          "),
                       words("is"), words("not")});
  // The next two length bytes are spelled as two blanks.
  builder.narrow_line({words("   "), words("BETA"), words("           "),
                       words("can"), words("area")});
  builder.record.tokens[builder.record.tokens.size() - 6] = words("  ");
  builder.narrow_line({words("   "), words("GAMMA"), words("          "),
                       words("will"), words("run")});
  builder.record.tokens[builder.record.tokens.size() - 6] = words("  ");
  builder.line({words("SRETBL")});
  builder.line({words("cz"), words("OFF"), words("ETABLE")});

  std::vector<DecodedLogicalRecordSource> sources;
  sources.push_back(builder.finish(654));
  require(sources.front().ir.display_lines_parse,
          "line-end word table: display lines do not parse");
  const auto layout = extract_layout_ir(sources);
  const auto ownership = build_ownership_ir(sources, layout);
  const LayoutRowRangeIR range{0, count_layout_rows(layout)};
  const auto blocks =
      extract_fixed_table_blocks_ir(sources, layout, ownership, range);
  require(blocks.declined.empty() && blocks.blocks.size() == 1,
          "line-end word table was declined" +
              (blocks.declined.empty()
                   ? std::string{}
                   : ": " + blocks.declined.front().reason));
  if (blocks.blocks.size() != 1)
    return;
  const auto &block = blocks.blocks.front();
  std::string text;
  for (const auto &row : block.body)
    for (std::size_t column = 0; column < row.cells.size(); ++column)
      text += cell_text(row, column) + (column + 1 < row.cells.size() ? " " : "|");
  // A declared table renders its cells; only a block without a column
  // model renders its display lines.
  if (block.body.empty())
    for (const auto &line : block.preformatted_lines)
      text += line.text + "|";
  for (const auto *word : {"is not", "can area", "will run"})
    require(text.find(word) != std::string::npos,
            std::string("a row's last word was read as a hidden slot (") +
                word + "): " + text);
  std::string error;
  require(verify_fixed_table_blocks_ir(sources, layout, ownership, range,
                                       blocks, &error),
          "line-end word table did not verify: " + error);
}

// A cell word that overflows the box's right border (GA19-5486-16 6.3
// `| 128-5|2`, SV40-0093-03 4.4.38.3 `| IBM_Age|t`) is drawn text no cell
// can hold.  The column model must not hide it as a slot: the region falls
// back to its display lines, every word kept.
void test_word_past_right_border_is_not_hidden() {
  constexpr std::uint16_t vertical = 0x2502;
  constexpr std::uint16_t horizontal = 0x2500;
  const TokenWords join{1};
  const auto rule = [&](std::uint16_t left, std::uint16_t middle,
                        std::uint16_t right) {
    return std::vector<TokenWords>{words("   "), {left},  join,
                                   glyphs(horizontal, 6),  join,
                                   {middle},               join,
                                   glyphs(horizontal, 6),  join,
                                   {right}};
  };
  RecordBuilder builder;
  builder.line({words("cz"), words("OFF"), words("TABLE")});
  builder.line({words("SRTBLOVERFLOW")});
  builder.narrow_line(rule(0x250c, 0x252c, 0x2510));
  builder.narrow_line({words("   "), {vertical}, words(" "), words("Ab"),
                       words(" "), {vertical}, words(" "), words("Cd"),
                       words(" "), {vertical}});
  builder.narrow_line(rule(0x251c, 0x253c, 0x2524));
  builder.narrow_line({words("   "), {vertical}, words(" "), words("Ef"),
                       words(" "), {vertical}, words(" "), words("Gh"),
                       words(" "), {vertical}, join, words("t")});
  builder.narrow_line(rule(0x2514, 0x2534, 0x2518));
  builder.line({words("SRETBL")});
  builder.line({words("cz"), words("OFF"), words("ETABLE")});
  std::vector<DecodedLogicalRecordSource> sources;
  sources.push_back(builder.finish(589));
  const auto layout = extract_layout_ir(sources);
  const auto ownership = build_ownership_ir(sources, layout);
  const LayoutRowRangeIR range{0, count_layout_rows(layout)};
  const auto blocks =
      extract_fixed_table_blocks_ir(sources, layout, ownership, range);
  require(blocks.declined.empty() && blocks.blocks.size() == 1,
          "overflowing box was declined");
  if (blocks.blocks.size() != 1)
    return;
  const auto &block = blocks.blocks.front();
  const auto lowered = lower_fixed_table_block_to_document_ir(block);
  std::string text;
  if (lowered_table(lowered) != nullptr) {
    for (const auto &row : block.body)
      for (std::size_t column = 0; column < row.cells.size(); ++column)
        text += cell_text(row, column) + "|";
  } else if (const auto *verbatim = lowered_verbatim(lowered)) {
    for (const auto &line : verbatim->lines)
      text += line + "|";
  }
  require(text.find("Gh") != std::string::npos &&
              text.find("|t") != std::string::npos,
          "the word past the right border vanished: " + text);
}

} // namespace

int main() {
  test_declared_address_classes();
  test_two_declared_tables_in_one_topic();
  test_long_declared_table();
  test_book_inventory();
  test_negatives();
  test_spot_anchor_inside_preformatted_table();
  test_native_row_without_layout_projection();
  test_glued_one_byte_word_is_content();
  test_revision_bar_before_box_border();
  test_line_end_word_before_blank_length_byte();
  test_word_past_right_border_is_not_hidden();
  geist_test::exit_with_failures();
  std::cout << "fixed table block IR checks passed\n";
  return 0;
}
