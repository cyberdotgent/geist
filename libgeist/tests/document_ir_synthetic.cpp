#include "geist/detail/document_ir.hpp"
#include "geist/detail/document_lowering.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace geist::detail;

bool require(bool condition, const std::string& message) {
  if (!condition) std::cerr << "document_ir_synthetic: " << message << '\n';
  return condition;
}

InlineIR text(std::string value) {
  InlineIR result;
  result.node = TextInlineIR{std::move(value)};
  result.origin.derivation = DocumentDerivationIR::semantic_lowering;
  return result;
}

BlockIR paragraph(std::string value) {
  BlockIR result;
  result.node = ParagraphBlockIR{{text(std::move(value))}};
  result.origin.derivation = DocumentDerivationIR::semantic_lowering;
  return result;
}

} // namespace

int main() {
  TopicIdentityIR identity;
  identity.id = "INTRO";
  identity.title = "Introduction";
  identity.topic_number = 7;
  identity.start_logical_record = 100;
  identity.end_logical_record = 102;

  const std::vector<std::string> records = {
      ":h1.Introduction", ":p.First paragraph", ":p.Second paragraph"};
  auto legacy = lower_legacy_topic_to_document_ir(identity, records);
  std::string error;
  if (!require(verify_document_ir(legacy, &error), error) ||
      !require(legacy.blocks.size() == 1,
               "legacy adapter must create exactly one stateful region"))
    return 1;
  const auto* region =
      std::get_if<LegacyGmlRegionIR>(&legacy.blocks.front().node);
  if (!require(region != nullptr, "adapter block is not legacy GML") ||
      !require(region->normalized_records == records,
               "adapter changed record text or boundaries") ||
      !require(region->state_scope == LegacyRendererStateScopeIR::whole_topic,
               "adapter did not preserve whole-topic renderer state"))
    return 1;

  const auto formatted = format_document_ir(legacy);
  if (!require(formatted.find("legacy_gml scope=whole_topic records=3") !=
                   std::string::npos,
               "formatter omitted compatibility boundary") ||
      !require(formatted.find("record=\":p.First paragraph\"") !=
                   std::string::npos,
               "formatter omitted a normalized record"))
    return 1;

  DocumentIR typed;
  typed.topic = identity;
  HeadingBlockIR heading;
  heading.level = 1;
  heading.content.push_back(text("Typed introduction"));
  BlockIR heading_block;
  heading_block.node = std::move(heading);
  typed.blocks.push_back(std::move(heading_block));
  typed.blocks.push_back(paragraph("Body"));

  TableBlockIR table;
  table.header_rows = 1;
  TableRowIR header;
  header.cells.push_back(TableCellIR{{text("Name")}, {}});
  header.cells.push_back(TableCellIR{{text("Value")}, {}});
  TableRowIR body;
  body.cells.push_back(TableCellIR{{text("Mode")}, {}});
  body.cells.push_back(TableCellIR{{text("Safe")}, {}});
  table.rows = {std::move(header), std::move(body)};
  BlockIR table_block;
  table_block.node = std::move(table);
  typed.blocks.push_back(std::move(table_block));

  DocumentSourceSliceIR slice;
  slice.logical_record = 101;
  slice.segment_index = 2;
  slice.token_begin = 4;
  slice.token_end = 8;
  slice.byte_begin = 12;
  slice.byte_end = 20;
  typed.blocks[1].origin.slices.push_back(slice);
  typed.blocks[1].origin.rows.push_back(DocumentSourceRowIR{9, 3});

  error.clear();
  if (!require(verify_document_ir(typed, &error), error)) return 1;
  const auto typed_format = format_document_ir(typed);
  if (!require(typed_format.find("block 0 heading level=1") !=
                   std::string::npos,
               "formatter omitted typed heading") ||
      !require(typed_format.find("source=(lr=101 seg=2 tok=4:8 bytes=12:20)") !=
                   std::string::npos,
               "formatter omitted source slice") ||
      !require(typed_format.find("row=9:3") != std::string::npos,
               "formatter omitted row evidence"))
    return 1;

  auto invalid_heading = typed;
  std::get<HeadingBlockIR>(invalid_heading.blocks[0].node).level = 0;
  error.clear();
  if (!require(!verify_document_ir(invalid_heading, &error) &&
                   error == "heading level is outside 1..6",
               "verifier admitted invalid heading geometry"))
    return 1;

  auto invalid_table = typed;
  auto& invalid_rows =
      std::get<TableBlockIR>(invalid_table.blocks[2].node).rows;
  invalid_rows[1].cells.pop_back();
  error.clear();
  if (!require(!verify_document_ir(invalid_table, &error) &&
                   error == "table row geometry is inconsistent",
               "verifier admitted inconsistent table geometry"))
    return 1;

  auto invalid_source = typed;
  invalid_source.blocks[1].origin.slices[0].token_begin = 9;
  error.clear();
  if (!require(!verify_document_ir(invalid_source, &error) &&
                   error == "source slice has reversed token range",
               "verifier admitted reversed provenance"))
    return 1;

  auto unordered_source = typed;
  auto earlier = slice;
  earlier.token_begin = 1;
  earlier.token_end = 2;
  unordered_source.blocks[1].origin.slices.push_back(earlier);
  error.clear();
  if (!require(!verify_document_ir(unordered_source, &error) &&
                   error == "source slices are duplicated or out of order",
               "verifier admitted out-of-order provenance"))
    return 1;

  auto mixed_legacy = legacy;
  mixed_legacy.blocks.push_back(paragraph("must not split renderer state"));
  error.clear();
  if (!require(!verify_document_ir(mixed_legacy, &error) &&
                   error ==
                       "whole-topic legacy region is mixed or duplicated",
               "verifier admitted a partial legacy state scope"))
    return 1;

  auto empty_identity = typed;
  empty_identity.topic.id.clear();
  error.clear();
  if (!require(!verify_document_ir(empty_identity, &error) &&
                   error == "document topic identity is incomplete",
               "verifier admitted an empty topic identity"))
    return 1;

  std::cout << "document IR synthetic checks passed\n";
  return 0;
}
